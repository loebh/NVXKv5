#include "../adapters/core_adapter/nvxk_adapter.hpp"
#include "../benchmark/insights/insights_engine.hpp"
#include "../benchmark/reports/report_exporter.hpp"
#include "../benchmark/runner/workbench_engine.hpp"
#include "../diagnostics/cpu/cpu_diagnostics.hpp"
#include "../ui/server/http_server.hpp"
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#define WB_CHECK(cond)                                                     \
    do {                                                                   \
        if (!(cond)) {                                                     \
            std::cerr << "[FAIL] " << __FILE__ << ":" << __LINE__          \
                      << " — Check failed: " << #cond << "\n";             \
            std::exit(EXIT_FAILURE);                                       \
        }                                                                  \
    } while (0)

int main() {
    using namespace nvxk_wb;

    // 1. Test Backend Registry (only real NVXK backends)
    auto backends = list_implemented_backends();
    WB_CHECK(backends.size() >= 6);
    bool saw_auto_asm = false, saw_ikj = false;
    for (const auto& b : backends) {
        if (b.id == "auto_asm") saw_auto_asm = true;
        if (b.id == "ikj") saw_ikj = true;
    }
    WB_CHECK(saw_auto_asm && saw_ikj);

    // 2. Test 5-Stage ISA Matrix & Diagnostics
    auto diag = collect_diagnostics_report();
    WB_CHECK(diag.profile.hw.physical_cores >= 1);
    WB_CHECK(diag.profile.hw.logical_threads >= diag.profile.hw.physical_cores);
    WB_CHECK(diag.isa_matrix.size() >= 5);
    WB_CHECK(!diag.telemetry.hw_counters_available); // Must truthfully report unavailable

    // 3. Test Plan Inspection & Input Validation
    XK_ExecutionPlan plan;
    std::string err;
    WB_CHECK(!inspect_gemm_plan(0, 64, 64, "f32", "auto_asm", 2, 0, plan, err));
    WB_CHECK(inspect_gemm_plan(4, 256, 256, "f32", "auto_asm", 2, 0, plan, err));
    if (XK_has_avx2_fma()) {
        WB_CHECK(plan.kernel == XK_KernelKind::Avx2ThinM);
        WB_CHECK(plan.required_workspace_bytes == 0);
    }

    // 4. Test Matrix Lab (exact small 2x2 custom matrices + validation)
    WorkbenchEngine engine;
    std::vector<double> A = {1, 2, 3, 4};
    std::vector<double> B = {5, 6, 7, 8};
    auto mres = engine.run_matrix_lab("gemm", "f32", "auto_asm", 2, 2, 2, 1, 64 * 1024 * 1024, 42, true, &A, &B);
    WB_CHECK(mres.success);
    WB_CHECK(mres.c_preview.size() == 4);
    WB_CHECK(std::fabs(mres.c_preview[0] - 19.0) < 1e-4);
    WB_CHECK(std::fabs(mres.c_preview[1] - 22.0) < 1e-4);
    WB_CHECK(std::fabs(mres.c_preview[2] - 43.0) < 1e-4);
    WB_CHECK(std::fabs(mres.c_preview[3] - 50.0) < 1e-4);
    WB_CHECK(mres.validation.executed && mres.validation.passed);

    // 5. Test Vector Add & i32->i64 in Matrix Lab
    auto vres = engine.run_matrix_lab("vector_add", "f32", "avx2", 32, 1, 32, 1, 0, 42, true);
    WB_CHECK(vres.success && vres.validation.passed);
    auto ires = engine.run_matrix_lab("gemm", "i32_i64", "ikj", 8, 8, 8, 1, 0, 42, true);
    WB_CHECK(ires.success && ires.validation.passed);

    // 6. Test Async Benchmark Runner + Concurrency Guard + Insights + Reports
    BenchmarkConfig cfg;
    cfg.M = 128; cfg.K = 128; cfg.N = 128;
    cfg.precision = "f32";
    cfg.backends = {"ikj", "avx2", "auto_asm"};
    cfg.runs = 3;
    cfg.measurement_mode = true;
    cfg.run_validation = true;
    WB_CHECK(engine.start_benchmark(cfg, err));
    // Second concurrent launch must be rejected cleanly
    std::string dup_err;
    WB_CHECK(!engine.start_benchmark(cfg, dup_err));
    engine.wait_for_idle();

    auto hist = engine.get_history();
    WB_CHECK(hist.size() >= 2);
    for (const auto& r : hist) {
        WB_CHECK(r.gflops > 0.0);
        WB_CHECK(r.median_ms > 0.0);
        WB_CHECK(r.validation.executed && r.validation.passed);
    }

    auto diag2 = collect_diagnostics_report();
    auto insights = generate_engineering_insights(diag2, hist);
    WB_CHECK(!insights.empty());

    auto md = export_report_markdown(diag2, hist, insights);
    auto csv = export_report_csv(hist);
    WB_CHECK(md.find("NVXK Engineering Intelligence Report") != std::string::npos);
    WB_CHECK(csv.find("gflops") != std::string::npos);

    // 7. Test Reliability 7-Shape Suite
    auto suite = engine.run_reliability_suite("f32", "auto_asm", 42, err);
    WB_CHECK(suite.size() == 7);
    for (const auto& v : suite) {
        WB_CHECK(v.executed && v.passed && v.nan_inf_count == 0);
    }

    // 8. Test REST API Handler directly
    WorkbenchHttpServer srv(engine, "ui/web", "results");
    int code = 0;
    std::string ct;
    std::string state_json = srv.handle_api_request("GET", "/api/state", "", code, ct);
    WB_CHECK(code == 200);
    WB_CHECK(state_json.find("\"isa_matrix\"") != std::string::npos);
    WB_CHECK(state_json.find("\"insights\"") != std::string::npos);

    std::string bad_import = srv.handle_api_request("POST", "/api/benchmark/import_csv",
                                                    "{\"file\":\"../secret.csv\"}", code, ct);
    WB_CHECK(code == 400);


    // 9. NVXK v3.0 generalized BLAS GEMM + /api/logs + version check
    {
        WorkbenchHttpServer server(engine, "ui/web", "results");
        int code = 0;
        std::string ct;
        const std::string st = server.handle_api_request("GET", "/api/state", "", code, ct);
        WB_CHECK(code == 200);
        WB_CHECK(st.find("\"version\":\"3.0.0\"") != std::string::npos);

        const std::string blas_req =
            "{\"precision\":\"f32\",\"layout\":\"col\",\"trans_a\":\"N\",\"trans_b\":\"T\","
            "\"M\":16,\"N\":12,\"K\":10,\"alpha\":1.5,\"beta\":-0.5,\"threads\":1,\"validate\":true}";
        const std::string blas_res = server.handle_api_request("POST", "/api/matrix_lab/blas", blas_req, code, ct);
        WB_CHECK(code == 200);
        WB_CHECK(blas_res.find("\"success\":true") != std::string::npos);
        WB_CHECK(blas_res.find("\"passed\":true") != std::string::npos);

        const std::string logs_res = server.handle_api_request("GET", "/api/logs", "", code, ct);
        WB_CHECK(code == 200);
        WB_CHECK(logs_res.find("Generalized BLAS GEMM") != std::string::npos);
    }

    std::cout << "[PASS] All NVXK Engineering Workbench C++17 integration tests succeeded.\n";
    return 0;
}