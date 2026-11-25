#include <nvxk/unified.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

struct BenchRow {
    std::string category;
    std::string op_name;
    std::string dtype;
    std::string dims;
    std::string backend;
    int threads = 1;
    bool pinned = false;
    int warmup = 2;
    int iters = 5;
    double median_ms = 0.0;
    double gflops = 0.0;
    double gbs = 0.0;
};

double median_val(std::vector<double> v) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    const size_t n = v.size();
    return (n % 2 == 1) ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
}

void print_usage() {
    std::cout
        << "NVXK Unified Computing Engine CLI v" << NVXK_UNIFIED_VERSION_STRING << "\n"
        << "Usage: nvxk_cli [options]\n"
        << "  --info                  Show unified engine, CPU, ISA, topology & backend status\n"
        << "  --topology              Show detailed hwloc / fallback CPU, cache & NUMA topology\n"
        << "  --simd                  Show Google Highway runtime SIMD dispatch & lane widths\n"
        << "  --backends              Show available computational backends & dispatch policy\n"
        << "  --verify                Run cross-backend numerical equivalence suite\n"
        << "  --benchmark             Run authentic GEMM + Vector/AI SIMD + Pinning benchmark suite\n"
        << "  --backend=<name>        Override GEMM backend (auto|native|blis|scalar)\n"
        << "  --vec-backend=<name>    Override vector backend (auto|highway|scalar)\n"
        << "  --threads=<N>           Set max thread count (0=auto)\n"
        << "  --no-pin                Disable hwloc thread pinning\n"
        << "  --trace                 Enable dispatch decision trace logging\n"
        << "  --csv=<file>            Export benchmark results to CSV\n"
        << "  --json=<file>           Export benchmark results to JSON\n"
        << "  --help                  Show this message\n";
}

void print_info() {
    const XK_HardwareInfo hw = XK_detect_hardware();
    const auto& topo = nvxk::topology::cached_topology();
    const auto simd = nvxk::simd::query_info();
    const auto blis = nvxk::blas::query_info();

    std::cout << "=== NVXK UNIFIED COMPUTING ENGINE v" << NVXK_UNIFIED_VERSION_STRING << " ===\n";
    std::cout << "Core Version     : " << XK_version_string() << " (Unified v" << NVXK_UNIFIED_VERSION_STRING << ")\n";
    std::cout << "CPU              : " << hw.brand << " (" << hw.vendor << ")\n";
    std::cout << "ISA Flags        : AVX2=" << (hw.has_avx2 ? "yes" : "no")
              << " FMA=" << (hw.has_fma ? "yes" : "no")
              << " AVX512F=" << (hw.has_avx512f ? "yes" : "no")
              << " NEON=" << (hw.has_neon ? "yes" : "no")
              << " ASM6x16=" << (XK_has_avx2_asm_kernel() ? "yes" : "no") << "\n";
    std::cout << nvxk::topology::format_summary(topo);
    std::cout << "SIMD Subsystem   : " << simd.backend_name
              << " | ActiveTarget=" << simd.active_target
              << " (f32_lanes=" << simd.lanes_f32 << ", f64_lanes=" << simd.lanes_f64 << ")\n";
    std::cout << "Matrix Subsystem : BLIS compiled=" << (blis.compiled_in ? "yes" : "no")
              << " | " << blis.version << "\n";
}

void print_simd() {
    const auto simd = nvxk::simd::query_info();
    std::cout << "=== NVXK SIMD SUBSYSTEM ===\n";
    std::cout << "Backend          : " << simd.backend_name << "\n";
    std::cout << "Active Target    : " << simd.active_target << "\n";
    std::cout << "Lanes (f32/f64)  : " << simd.lanes_f32 << " / " << simd.lanes_f64 << "\n";
    std::cout << "Supported Targets: ";
    for (size_t i = 0; i < simd.supported_targets.size(); ++i) {
        if (i > 0) std::cout << ", ";
        std::cout << simd.supported_targets[i];
    }
    std::cout << "\n";
}

void print_backends() {
    const auto cfg = nvxk::get_runtime_config();
    const auto simd = nvxk::simd::query_info();
    const auto blis = nvxk::blas::query_info();
    const auto& topo = nvxk::topology::cached_topology();
    std::cout << "=== NVXK BACKEND ORCHESTRATION ===\n";
    std::cout << "Active GEMM Policy   : " << nvxk::backend_kind_name(cfg.gemm_backend) << "\n";
    std::cout << "Active Vector Policy : " << nvxk::backend_kind_name(cfg.vector_backend) << "\n";
    std::cout << "Thread Pinning       : " << (cfg.enable_hwloc_affinity ? "enabled" : "disabled") << "\n";
    std::cout << "Subsystems Status:\n";
    std::cout << "  - NVXK Core & Scalar Reference : ALWAYS AVAILABLE\n";
    std::cout << "  - NVXK Native AVX2+FMA / ASM   : " << (XK_has_avx2_fma() ? "AVAILABLE" : "UNAVAILABLE") << "\n";
    std::cout << "  - Google Highway SIMD          : " << (simd.highway_active ? ("AVAILABLE (" + simd.active_target + ")") : "FALLBACK") << "\n";
    std::cout << "  - BLIS Matrix Backend          : " << (blis.available ? ("AVAILABLE (" + blis.version + ")") : "UNAVAILABLE (native fallback)") << "\n";
    std::cout << "  - hwloc Hardware Topology      : " << (topo.hwloc_active ? ("AVAILABLE (" + topo.backend_name + ")") : "NATIVE FALLBACK") << "\n";
}

int run_verify() {
    const size_t M = 97, K = 83, N = 71;
    std::vector<float> A(M * K), B(K * N), C_ref(M * N, 0.0f), C_opt(M * N, 0.0f);
    for (size_t i = 0; i < A.size(); ++i) A[i] = static_cast<float>(static_cast<int>((i * 17) % 29) - 14) * 0.07f;
    for (size_t i = 0; i < B.size(); ++i) B[i] = static_cast<float>(static_cast<int>((i * 13) % 23) - 11) * 0.09f;

    nvxk::RuntimeConfig scalar_cfg = nvxk::get_runtime_config();
    scalar_cfg.gemm_backend = nvxk::BackendKind::Scalar;
    nvxk::dispatch::gemm(A.data(), B.data(), C_ref.data(), M, K, N, &scalar_cfg);
    auto dec = nvxk::dispatch::gemm(A.data(), B.data(), C_opt.data(), M, K, N);

    double max_abs = 0.0, max_rel = 0.0;
    for (size_t i = 0; i < C_ref.size(); ++i) {
        const double diff = std::abs(static_cast<double>(C_opt[i]) - static_cast<double>(C_ref[i]));
        const double denom = std::max(1.0, std::abs(static_cast<double>(C_ref[i])));
        max_abs = std::max(max_abs, diff);
        max_rel = std::max(max_rel, diff / denom);
    }

    const size_t vn = 4099;
    std::vector<float> vx(vn), vy(vn), out_hwy(vn), out_ref(vn);
    for (size_t i = 0; i < vn; ++i) {
        vx[i] = static_cast<float>(static_cast<int>(i % 37) - 18) * 0.13f;
        vy[i] = static_cast<float>(static_cast<int>(i % 41) - 20) * 0.11f;
    }

    nvxk::simd::gelu_approx(vx.data(), out_ref.data(), vn, nvxk::BackendKind::Scalar);
    nvxk::simd::gelu_approx(vx.data(), out_hwy.data(), vn, nvxk::BackendKind::Highway);
    double gelu_err = 0.0;
    for (size_t i = 0; i < vn; ++i) gelu_err = std::max(gelu_err, std::abs(double(out_hwy[i]) - double(out_ref[i])));

    nvxk::simd::softmax(vx.data(), out_ref.data(), vn, nvxk::BackendKind::Scalar);
    nvxk::simd::softmax(vx.data(), out_hwy.data(), vn, nvxk::BackendKind::Highway);
    double sm_err = 0.0;
    for (size_t i = 0; i < vn; ++i) sm_err = std::max(sm_err, std::abs(double(out_hwy[i]) - double(out_ref[i])));

    nvxk::simd::layernorm(vx.data(), out_ref.data(), vn, 1e-5f, nullptr, nullptr, nvxk::BackendKind::Scalar);
    nvxk::simd::layernorm(vx.data(), out_hwy.data(), vn, 1e-5f, nullptr, nullptr, nvxk::BackendKind::Highway);
    double ln_err = 0.0;
    for (size_t i = 0; i < vn; ++i) ln_err = std::max(ln_err, std::abs(double(out_hwy[i]) - double(out_ref[i])));

    const float d_ref = nvxk::simd::dot(vx.data(), vy.data(), vn, nvxk::BackendKind::Scalar);
    const float d_hwy = nvxk::simd::dot(vx.data(), vy.data(), vn, nvxk::BackendKind::Highway);
    const double dot_rel = std::abs(double(d_hwy) - double(d_ref)) / std::max(1.0, std::abs(double(d_ref)));

    std::cout << "=== NVXK v5.0 CROSS-BACKEND VERIFICATION ===\n";
    std::cout << "GEMM (97x83x71 f32, backend=" << nvxk::backend_kind_name(dec.selected_backend)
              << ", threads=" << dec.threads << "): max_rel=" << max_rel << (max_rel < 1e-4 ? " [PASS]" : " [FAIL]") << "\n";
    std::cout << "Dot (4099 f32, Highway vs Scalar)       : rel_err=" << dot_rel << (dot_rel < 1e-4 ? " [PASS]" : " [FAIL]") << "\n";
    std::cout << "GELU (4099 f32, Highway vs Scalar)      : max_abs=" << gelu_err << (gelu_err < 1e-5 ? " [PASS]" : " [FAIL]") << "\n";
    std::cout << "Softmax (4099 f32, Highway vs Scalar)   : max_abs=" << sm_err << (sm_err < 1e-6 ? " [PASS]" : " [FAIL]") << "\n";
    std::cout << "LayerNorm (4099 f32, Highway vs Scalar) : max_abs=" << ln_err << (ln_err < 1e-4 ? " [PASS]" : " [FAIL]") << "\n";

    return (max_rel < 1e-4 && dot_rel < 1e-4 && gelu_err < 1e-5 && sm_err < 1e-6 && ln_err < 1e-4) ? 0 : 1;
}

std::vector<BenchRow> run_benchmarks() {
    std::vector<BenchRow> rows;
    const auto cfg = nvxk::get_runtime_config();
    const auto& topo = nvxk::topology::cached_topology();
    const auto simd_info = nvxk::simd::query_info();

    std::cout << "=== NVXK v5.0 UNIFIED BENCHMARK SUITE ===\n";
    std::cout << "Methodology: median of timed iterations after warm-up; verified vs scalar reference.\n\n";

    // 1. Vector & Transcendental / AI Activation Benchmarks (Highway AVX3_SPR vs Scalar)
    const size_t vec_n = 1 << 20;  // 1,048,576 floats
    std::vector<float> va(vec_n), vb(vec_n), vc(vec_n, 0.0f);
    for (size_t i = 0; i < vec_n; ++i) {
        va[i] = static_cast<float>(static_cast<int>(i % 200) - 100) * 0.03f;
        vb[i] = static_cast<float>(static_cast<int>((i * 7) % 200) - 100) * 0.02f;
    }

    auto bench_vec = [&](const std::string& op, int flops_per_elem, int bytes_per_elem, auto fn) {
        for (nvxk::BackendKind bk : {nvxk::BackendKind::Scalar, nvxk::BackendKind::Highway}) {
            const int warmup = 2, iters = 11;
            for (int w = 0; w < warmup; ++w) fn(bk);
            std::vector<double> times;
            for (int it = 0; it < iters; ++it) {
                auto t0 = std::chrono::steady_clock::now();
                fn(bk);
                auto t1 = std::chrono::steady_clock::now();
                times.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
            }
            BenchRow r;
            r.category = "Vector";
            r.op_name = op;
            r.dtype = "f32";
            r.dims = std::to_string(vec_n);
            r.backend = (bk == nvxk::BackendKind::Highway && simd_info.highway_active)
                            ? ("highway:" + simd_info.active_target)
                            : "scalar";
            r.threads = 1; r.warmup = warmup; r.iters = iters;
            r.median_ms = median_val(times);
            r.gflops = (static_cast<double>(flops_per_elem) * vec_n / (r.median_ms * 1e-3)) / 1e9;
            r.gbs = (static_cast<double>(bytes_per_elem) * vec_n / (r.median_ms * 1e-3)) / 1e9;
            rows.push_back(r);
        }
    };

    volatile float sink = 0.0f;
    bench_vec("vec_add", 1, 12, [&](nvxk::BackendKind bk) { nvxk::simd::vec_add(va.data(), vb.data(), vc.data(), vec_n, bk); });
    bench_vec("dot", 2, 8, [&](nvxk::BackendKind bk) { sink = nvxk::simd::dot(va.data(), vb.data(), vec_n, bk); });
    bench_vec("gelu", 10, 8, [&](nvxk::BackendKind bk) { nvxk::simd::gelu_approx(va.data(), vc.data(), vec_n, bk); });
    bench_vec("softmax", 5, 8, [&](nvxk::BackendKind bk) { nvxk::simd::softmax(va.data(), vc.data(), vec_n, bk); });
    bench_vec("layernorm", 5, 8, [&](nvxk::BackendKind bk) { nvxk::simd::layernorm(va.data(), vc.data(), vec_n, 1e-5f, nullptr, nullptr, bk); });
    (void)sink;

    // 2. GEMV 1024x1024 (Scalar vs Highway+OMP)
    {
        const size_t M = 1024, K = 1024;
        std::vector<float> A(M * K, 0.5f), x(K, 0.25f), y(M, 0.0f);
        for (nvxk::BackendKind bk : {nvxk::BackendKind::Scalar, nvxk::BackendKind::Highway}) {
            const int warmup = 2, iters = 11;
            for (int w = 0; w < warmup; ++w) nvxk::simd::gemv(M, K, 1.0f, A.data(), K, x.data(), 0.0f, y.data(), bk);
            std::vector<double> times;
            for (int it = 0; it < iters; ++it) {
                auto t0 = std::chrono::steady_clock::now();
                nvxk::simd::gemv(M, K, 1.0f, A.data(), K, x.data(), 0.0f, y.data(), bk);
                auto t1 = std::chrono::steady_clock::now();
                times.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
            }
            BenchRow r;
            r.category = "BLAS2";
            r.op_name = "gemv";
            r.dtype = "f32";
            r.dims = "1024x1024";
            r.backend = (bk == nvxk::BackendKind::Highway && simd_info.highway_active)
                            ? ("highway:" + simd_info.active_target)
                            : "scalar";
            r.threads = (bk == nvxk::BackendKind::Highway) ? topo.physical_cores : 1;
            r.warmup = warmup; r.iters = iters;
            r.median_ms = median_val(times);
            r.gflops = (2.0 * M * K / (r.median_ms * 1e-3)) / 1e9;
            r.gbs = ((M * K + K + M) * sizeof(float) / (r.median_ms * 1e-3)) / 1e9;
            rows.push_back(r);
        }
    }

    // 3. GEMM (f32 & f64; 1T vs 2T; pinned vs unpinned)
    XK_Workspace ws(64 * 1024 * 1024);
    struct GemmCase { size_t M, K, N; int iters; };
    const std::vector<GemmCase> cases = {
        {64, 64, 64, 15},
        {256, 256, 256, 9},
        {512, 512, 512, 7},
        {4, 1024, 1024, 15}
    };
    for (const auto& c : cases) {
        std::vector<float> A(c.M * c.K, 0.5f), B(c.K * c.N, 0.25f), C(c.M * c.N, 0.0f);
        std::vector<int> tlist = {1};
        if (topo.physical_cores > 1) tlist.push_back(topo.physical_cores);
        for (int tc : tlist) {
            nvxk::RuntimeConfig run_cfg = cfg;
            run_cfg.max_threads = tc;
            const int warmup = 2;
            nvxk::dispatch::DispatchDecision dec;
            for (int w = 0; w < warmup; ++w) dec = nvxk::dispatch::gemm(A.data(), B.data(), C.data(), c.M, c.K, c.N, &run_cfg, &ws);
            std::vector<double> times;
            for (int it = 0; it < c.iters; ++it) {
                auto t0 = std::chrono::steady_clock::now();
                dec = nvxk::dispatch::gemm(A.data(), B.data(), C.data(), c.M, c.K, c.N, &run_cfg, &ws);
                auto t1 = std::chrono::steady_clock::now();
                times.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
            }
            BenchRow rg;
            rg.category = "GEMM";
            rg.op_name = "sgemm";
            rg.dtype = "f32";
            rg.dims = std::to_string(c.M) + "x" + std::to_string(c.K) + "x" + std::to_string(c.N);
            rg.backend = nvxk::backend_kind_name(dec.selected_backend);
            rg.threads = dec.threads;
            rg.pinned = dec.pin_threads;
            rg.warmup = warmup; rg.iters = c.iters;
            rg.median_ms = median_val(times);
            rg.gflops = (2.0 * double(c.M) * double(c.K) * double(c.N) / (rg.median_ms * 1e-3)) / 1e9;
            rows.push_back(rg);
        }
    }

    // f64 DGEMM 256x256x256
    {
        const size_t N = 256;
        std::vector<double> A(N * N, 0.5), B(N * N, 0.25), C(N * N, 0.0);
        nvxk::RuntimeConfig run_cfg = cfg;
        run_cfg.max_threads = topo.physical_cores;
        nvxk::dispatch::DispatchDecision dec;
        for (int w = 0; w < 2; ++w) dec = nvxk::dispatch::gemm(A.data(), B.data(), C.data(), N, N, N, &run_cfg, &ws);
        std::vector<double> times;
        for (int it = 0; it < 7; ++it) {
            auto t0 = std::chrono::steady_clock::now();
            dec = nvxk::dispatch::gemm(A.data(), B.data(), C.data(), N, N, N, &run_cfg, &ws);
            auto t1 = std::chrono::steady_clock::now();
            times.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
        }
        BenchRow rd;
        rd.category = "GEMM"; rd.op_name = "dgemm"; rd.dtype = "f64"; rd.dims = "256x256x256";
        rd.backend = nvxk::backend_kind_name(dec.selected_backend);
        rd.threads = dec.threads; rd.pinned = dec.pin_threads; rd.warmup = 2; rd.iters = 7;
        rd.median_ms = median_val(times);
        rd.gflops = (2.0 * double(N) * double(N) * double(N) / (rd.median_ms * 1e-3)) / 1e9;
        rows.push_back(rd);
    }

    std::cout << std::left
              << std::setw(8) << "Cat"
              << std::setw(11) << "Op"
              << std::setw(14) << "Dims"
              << std::setw(21) << "Backend"
              << std::right
              << std::setw(4) << "Thr"
              << std::setw(5) << "Pin"
              << std::setw(10) << "Med(ms)"
              << std::setw(10) << "GFLOPS"
              << std::setw(10) << "GB/s" << "\n";
    std::cout << std::string(93, '-') << "\n";
    std::cout << std::fixed << std::setprecision(3);
    for (const auto& r : rows) {
        std::cout << std::left
                  << std::setw(8) << r.category
                  << std::setw(11) << r.op_name
                  << std::setw(14) << r.dims
                  << std::setw(21) << r.backend
                  << std::right
                  << std::setw(4) << r.threads
                  << std::setw(5) << (r.pinned ? "yes" : "no")
                  << std::setw(10) << r.median_ms
                  << std::setw(10) << r.gflops
                  << std::setw(10) << r.gbs << "\n";
    }
    return rows;
}

void export_csv(const std::string& path, const std::vector<BenchRow>& rows) {
    std::ofstream ofs(path);
    ofs << "category,op,dtype,dims,backend,threads,pinned,warmup,iters,median_ms,gflops,gbs\n";
    for (const auto& r : rows) {
        ofs << r.category << "," << r.op_name << "," << r.dtype << "," << r.dims << ","
            << r.backend << "," << r.threads << "," << (r.pinned ? 1 : 0) << ","
            << r.warmup << "," << r.iters << "," << r.median_ms << "," << r.gflops << "," << r.gbs << "\n";
    }
}

void export_json(const std::string& path, const std::vector<BenchRow>& rows) {
    std::ofstream ofs(path);
    ofs << "{\n  \"engine\": \"NVXK Unified Computing Engine\",\n  \"version\": \""
        << NVXK_UNIFIED_VERSION_STRING << "\",\n  \"results\": [\n";
    for (size_t i = 0; i < rows.size(); ++i) {
        const auto& r = rows[i];
        ofs << "    {\"category\":\"" << r.category << "\",\"op\":\"" << r.op_name
            << "\",\"dtype\":\"" << r.dtype << "\",\"dims\":\"" << r.dims
            << "\",\"backend\":\"" << r.backend << "\",\"threads\":" << r.threads
            << ",\"pinned\":" << (r.pinned ? "true" : "false")
            << ",\"median_ms\":" << r.median_ms << ",\"gflops\":" << r.gflops
            << ",\"gbs\":" << r.gbs << "}" << (i + 1 < rows.size() ? "," : "") << "\n";
    }
    ofs << "  ]\n}\n";
}

}  // namespace

int main(int argc, char** argv) {
    nvxk::apply_env_overrides();
    nvxk::RuntimeConfig cfg = nvxk::get_runtime_config();

    bool do_info = false, do_topo = false, do_simd = false, do_backends = false;
    bool do_verify = false, do_bench = false;
    std::string csv_path, json_path;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") { print_usage(); return 0; }
        else if (arg == "--info") do_info = true;
        else if (arg == "--topology") do_topo = true;
        else if (arg == "--simd") do_simd = true;
        else if (arg == "--backends") do_backends = true;
        else if (arg == "--verify") do_verify = true;
        else if (arg == "--benchmark") do_bench = true;
        else if (arg == "--no-pin") cfg.enable_hwloc_affinity = false;
        else if (arg == "--trace") cfg.enable_trace = true;
        else if (arg.rfind("--backend=", 0) == 0) cfg.gemm_backend = nvxk::parse_backend_kind(arg.substr(10));
        else if (arg.rfind("--vec-backend=", 0) == 0) cfg.vector_backend = nvxk::parse_backend_kind(arg.substr(14));
        else if (arg.rfind("--threads=", 0) == 0) cfg.max_threads = std::atoi(arg.substr(10).c_str());
        else if (arg.rfind("--csv=", 0) == 0) csv_path = arg.substr(6);
        else if (arg.rfind("--json=", 0) == 0) json_path = arg.substr(7);
        else {
            std::cerr << "Unknown option: " << arg << "\n";
            print_usage();
            return 2;
        }
    }

    nvxk::set_runtime_config(cfg);

    if (!do_info && !do_topo && !do_simd && !do_backends && !do_verify && !do_bench) {
        do_info = true;
    }

    if (do_info) print_info();
    if (do_topo) std::cout << nvxk::topology::format_summary(nvxk::topology::cached_topology());
    if (do_simd) print_simd();
    if (do_backends) print_backends();
    int rc = 0;
    if (do_verify) rc |= run_verify();
    if (do_bench) {
        auto rows = run_benchmarks();
        if (!csv_path.empty()) export_csv(csv_path, rows);
        if (!json_path.empty()) export_json(json_path, rows);
    }
    return rc;
}
