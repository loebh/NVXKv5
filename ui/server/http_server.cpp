#include <nvxk/unified.hpp>
#include "http_server.hpp"
#include "../../benchmark/insights/insights_engine.hpp"
#include "../../benchmark/reports/report_exporter.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <thread>

#if defined(_WIN32)
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#define NATIVE_SOCK(s) static_cast<SOCKET>(s)
#define CLOSE_SOCK(s) closesocket(static_cast<SOCKET>(s))
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#define NATIVE_SOCK(s) static_cast<int>(s)
#define CLOSE_SOCK(s) close(static_cast<int>(s))
#endif

namespace nvxk_wb {

namespace {

std::string fmt_num(double v) {
    if (!std::isfinite(v)) return "null";
    std::ostringstream oss;
    oss << std::setprecision(8) << v;
    return oss.str();
}

// Minimal defensive JSON field extractors for flat request objects
std::string json_get_string(const std::string& body, const std::string& key, const std::string& def = "") {
    const std::string pat = "\"" + key + "\"";
    size_t pos = body.find(pat);
    if (pos == std::string::npos) return def;
    pos = body.find(':', pos + pat.size());
    if (pos == std::string::npos) return def;
    pos = body.find('"', pos + 1);
    if (pos == std::string::npos) return def;
    std::string out;
    for (size_t i = pos + 1; i < body.size(); ++i) {
        if (body[i] == '\\' && i + 1 < body.size()) {
            out.push_back(body[++i]);
        } else if (body[i] == '"') {
            return out;
        } else {
            out.push_back(body[i]);
        }
    }
    return def;
}

double json_get_number(const std::string& body, const std::string& key, double def = 0.0) {
    const std::string pat = "\"" + key + "\"";
    size_t pos = body.find(pat);
    if (pos == std::string::npos) return def;
    pos = body.find(':', pos + pat.size());
    if (pos == std::string::npos) return def;
    ++pos;
    while (pos < body.size() && std::isspace(static_cast<unsigned char>(body[pos]))) ++pos;
    char* endp = nullptr;
    const double val = std::strtod(body.c_str() + pos, &endp);
    if (endp == body.c_str() + pos || !std::isfinite(val)) return def;
    return val;
}

bool json_get_bool(const std::string& body, const std::string& key, bool def = false) {
    const std::string pat = "\"" + key + "\"";
    size_t pos = body.find(pat);
    if (pos == std::string::npos) return def;
    pos = body.find(':', pos + pat.size());
    if (pos == std::string::npos) return def;
    ++pos;
    while (pos < body.size() && std::isspace(static_cast<unsigned char>(body[pos]))) ++pos;
    if (body.compare(pos, 4, "true") == 0) return true;
    if (body.compare(pos, 5, "false") == 0) return false;
    return def;
}

std::vector<std::string> json_get_string_array(const std::string& body, const std::string& key) {
    std::vector<std::string> res;
    const std::string pat = "\"" + key + "\"";
    size_t pos = body.find(pat);
    if (pos == std::string::npos) return res;
    pos = body.find('[', pos + pat.size());
    if (pos == std::string::npos) return res;
    size_t end = body.find(']', pos + 1);
    if (end == std::string::npos) return res;
    size_t i = pos + 1;
    while (i < end) {
        size_t q1 = body.find('"', i);
        if (q1 == std::string::npos || q1 >= end) break;
        size_t q2 = body.find('"', q1 + 1);
        if (q2 == std::string::npos || q2 >= end) break;
        res.push_back(body.substr(q1 + 1, q2 - q1 - 1));
        i = q2 + 1;
    }
    return res;
}

std::vector<double> json_get_number_array(const std::string& body, const std::string& key) {
    std::vector<double> res;
    const std::string pat = "\"" + key + "\"";
    size_t pos = body.find(pat);
    if (pos == std::string::npos) return res;
    pos = body.find('[', pos + pat.size());
    if (pos == std::string::npos) return res;
    size_t end = body.find(']', pos + 1);
    if (end == std::string::npos) return res;
    const std::string slice = body.substr(pos + 1, end - pos - 1);
    std::stringstream ss(slice);
    std::string tok;
    while (std::getline(ss, tok, ',')) {
        char* endp = nullptr;
        double v = std::strtod(tok.c_str(), &endp);
        if (endp != tok.c_str() && std::isfinite(v)) res.push_back(v);
    }
    return res;
}

std::string serialize_validation(const ValidationMetrics& v) {
    std::ostringstream o;
    o << "{"
      << "\"executed\":" << (v.executed ? "true" : "false") << ","
      << "\"passed\":" << (v.passed ? "true" : "false") << ","
      << "\"max_abs_error\":" << fmt_num(v.max_abs_error) << ","
      << "\"max_rel_error\":" << fmt_num(v.max_rel_error) << ","
      << "\"rmse\":" << fmt_num(v.rmse) << ","
      << "\"nan_inf_count\":" << v.nan_inf_count << ","
      << "\"abs_tol\":" << fmt_num(v.abs_tol) << ","
      << "\"rel_tol\":" << fmt_num(v.rel_tol) << ","
      << "\"reference_backend\":\"" << json_escape(v.reference_backend) << "\","
      << "\"precision\":\"" << json_escape(v.precision) << "\","
      << "\"seed\":" << v.seed << ","
      << "\"M\":" << v.M << ",\"K\":" << v.K << ",\"N\":" << v.N << ","
      << "\"heatmap_rows\":" << v.heatmap_rows << ","
      << "\"heatmap_cols\":" << v.heatmap_cols << ","
      << "\"error_heatmap\":[";
    for (size_t i = 0; i < v.error_heatmap.size(); ++i) {
        if (i) o << ",";
        o << fmt_num(v.error_heatmap[i]);
    }
    o << "],\"failure_detail\":\"" << json_escape(v.failure_detail) << "\"}";
    return o.str();
}

std::string serialize_telemetry(const SystemTelemetrySnapshot& t) {
    std::ostringstream o;
    o << "{"
      << "\"cpu_util_available\":" << (t.cpu_util_available ? "true" : "false") << ","
      << "\"cpu_util_percent\":" << fmt_num(t.cpu_util_percent) << ","
      << "\"sampling_window_ms\":" << fmt_num(t.sampling_window_ms) << ","
      << "\"per_core_util_percent\":[";
    for (size_t i = 0; i < t.per_core_util_percent.size(); ++i) {
        if (i) o << ",";
        o << fmt_num(t.per_core_util_percent[i]);
    }
    o << "],"
      << "\"memory_available\":" << (t.memory_available ? "true" : "false") << ","
      << "\"process_rss_bytes\":" << t.process_rss_bytes << ","
      << "\"process_peak_rss_bytes\":" << t.process_peak_rss_bytes << ","
      << "\"system_total_ram_bytes\":" << t.system_total_ram_bytes << ","
      << "\"system_avail_ram_bytes\":" << t.system_avail_ram_bytes << ","
      << "\"hw_counters_available\":" << (t.hw_counters_available ? "true" : "false") << ","
      << "\"hw_counters_note\":\"" << json_escape(t.hw_counters_note) << "\""
      << "}";
    return o.str();
}

std::string serialize_diagnostics(const DiagnosticsReport& d) {
    std::ostringstream o;
    o << "{"
      << "\"os_name\":\"" << json_escape(d.os_name) << "\","
      << "\"compiler_name\":\"" << json_escape(d.compiler_name) << "\","
      << "\"cpp_standard\":\"" << json_escape(d.cpp_standard) << "\","
      << "\"openmp_enabled\":" << (d.openmp_enabled ? "true" : "false") << ","
      << "\"openmp_max_threads\":" << d.openmp_max_threads << ","
      << "\"hw\":{"
      << "\"vendor\":\"" << json_escape(d.profile.hw.vendor) << "\","
      << "\"brand\":\"" << json_escape(d.profile.hw.brand) << "\","
      << "\"physical_cores\":" << d.profile.hw.physical_cores << ","
      << "\"logical_threads\":" << d.profile.hw.logical_threads << ","
      << "\"l1d_bytes\":" << d.profile.hw.l1d_bytes << ","
      << "\"l2_bytes\":" << d.profile.hw.l2_bytes << ","
      << "\"l3_bytes\":" << d.profile.hw.l3_bytes << ","
      << "\"has_avx2\":" << (d.profile.hw.has_avx2 ? "true" : "false") << ","
      << "\"has_fma\":" << (d.profile.hw.has_fma ? "true" : "false") << ","
      << "\"has_avx512f\":" << (d.profile.hw.has_avx512f ? "true" : "false") << ","
      << "\"has_neon\":" << (d.profile.hw.has_neon ? "true" : "false")
      << "},"
      << "\"profile\":{"
      << "\"calibrated\":" << (d.profile.calibrated ? "true" : "false") << ","
      << "\"peak_gflops_1t\":" << fmt_num(d.profile.peak_gflops_1t) << ","
      << "\"mem_bandwidth_gbs\":" << fmt_num(d.profile.mem_bandwidth_gbs) << ","
      << "\"omp_fork_overhead_us\":" << fmt_num(d.profile.omp_fork_overhead_us) << ","
      << "\"omp_barrier_overhead_us\":" << fmt_num(d.profile.omp_barrier_overhead_us) << ","
      << "\"smt_efficiency\":" << fmt_num(d.profile.smt_efficiency)
      << "},"
      << "\"isa_matrix\":[";
    for (size_t i = 0; i < d.isa_matrix.size(); ++i) {
        const auto& f = d.isa_matrix[i];
        if (i) o << ",";
        o << "{"
          << "\"feature_name\":\"" << json_escape(f.feature_name) << "\","
          << "\"cpu_supports\":" << (f.cpu_supports ? "true" : "false") << ","
          << "\"compiler_supports\":" << (f.compiler_supports ? "true" : "false") << ","
          << "\"binary_contains\":" << (f.binary_contains ? "true" : "false") << ","
          << "\"runtime_dispatch_selected\":" << (f.runtime_dispatch_selected ? "true" : "false") << ","
          << "\"execution_verified\":" << (f.execution_verified ? "true" : "false") << ","
          << "\"notes\":\"" << json_escape(f.notes) << "\""
          << "}";
    }
    o << "],\"telemetry\":" << serialize_telemetry(d.telemetry) << "}";
    return o.str();
}

std::string serialize_row(const BenchmarkResultRow& r) {
    std::ostringstream o;
    o << "{"
      << "\"id\":" << r.id << ","
      << "\"timestamp_iso\":\"" << json_escape(r.timestamp_iso) << "\","
      << "\"session_name\":\"" << json_escape(r.session_name) << "\","
      << "\"function_name\":\"" << json_escape(r.function_name) << "\","
      << "\"backend\":\"" << json_escape(r.backend) << "\","
      << "\"kernel_selected\":\"" << json_escape(r.kernel_selected) << "\","
      << "\"precision\":\"" << json_escape(r.precision) << "\","
      << "\"M\":" << r.M << ",\"K\":" << r.K << ",\"N\":" << r.N << ","
      << "\"threads_requested\":" << r.threads_requested << ","
      << "\"threads_used\":" << r.threads_used << ","
      << "\"sample_count\":" << r.sample_count << ","
      << "\"total_calls\":" << r.total_calls << ","
      << "\"min_ms\":" << fmt_num(r.min_ms) << ","
      << "\"median_ms\":" << fmt_num(r.median_ms) << ","
      << "\"mean_ms\":" << fmt_num(r.mean_ms) << ","
      << "\"total_ms\":" << fmt_num(r.total_ms) << ","
      << "\"stddev_ms\":" << fmt_num(r.stddev_ms) << ","
      << "\"p95_ms\":" << fmt_num(r.p95_ms) << ","
      << "\"p99_ms\":" << fmt_num(r.p99_ms) << ","
      << "\"p99_reliable\":" << (r.p99_reliable ? "true" : "false") << ","
      << "\"gflops\":" << fmt_num(r.gflops) << ","
      << "\"ops_per_sec\":" << fmt_num(r.ops_per_sec) << ","
      << "\"speedup_vs_baseline\":" << fmt_num(r.speedup_vs_baseline) << ","
      << "\"has_baseline\":" << (r.has_baseline ? "true" : "false") << ","
      << "\"workspace_bytes\":" << r.workspace_bytes << ","
      << "\"working_set_bytes\":" << r.working_set_bytes << ","
      << "\"measured_bandwidth_gbs\":" << fmt_num(r.measured_bandwidth_gbs) << ","
      << "\"budget_clamped\":" << (r.budget_clamped ? "true" : "false") << ","
      << "\"smt_l2_shared\":" << (r.smt_l2_shared ? "true" : "false") << ","
      << "\"cold_cache\":" << (r.cold_cache ? "true" : "false") << ","
      << "\"measurement_mode\":" << (r.measurement_mode ? "true" : "false") << ","
      << "\"plan_reason\":\"" << json_escape(r.plan_reason) << "\","
      << "\"mc\":" << r.mc << ",\"kc\":" << r.kc << ",\"nc\":" << r.nc << ","
      << "\"axis\":\"" << json_escape(r.axis) << "\","
      << "\"validation\":" << serialize_validation(r.validation) << ","
      << "\"raw_samples_ms\":[";
    for (size_t i = 0; i < r.raw_samples_ms.size(); ++i) {
        if (i) o << ",";
        o << fmt_num(r.raw_samples_ms[i]);
    }
    o << "]}";
    return o.str();
}

std::string serialize_job_status(const JobStatus& s) {
    std::ostringstream o;
    o << "{"
      << "\"running\":" << (s.running ? "true" : "false") << ","
      << "\"cancelled\":" << (s.cancelled ? "true" : "false") << ","
      << "\"progress\":" << fmt_num(s.progress) << ","
      << "\"current_step\":\"" << json_escape(s.current_step) << "\","
      << "\"last_error\":\"" << json_escape(s.last_error) << "\","
      << "\"session_name\":\"" << json_escape(s.session_name) << "\","
      << "\"measurement_mode\":" << (s.measurement_mode ? "true" : "false")
      << "}";
    return o.str();
}

std::string read_file_bytes(const std::string& path, bool& ok) {
    std::ifstream in(path, std::ios::binary);
    if (!in) { ok = false; return ""; }
    std::ostringstream oss;
    oss << in.rdbuf();
    ok = true;
    return oss.str();
}

}  // namespace

WorkbenchHttpServer::WorkbenchHttpServer(WorkbenchEngine& engine,
                                         std::string ui_root_dir,
                                         std::string results_dir)
    : engine_(engine),
      ui_root_dir_(std::move(ui_root_dir)),
      results_dir_(std::move(results_dir)) {}

WorkbenchHttpServer::~WorkbenchHttpServer() {
    stop();
}

std::string WorkbenchHttpServer::handle_api_request(const std::string& method,
                                                    const std::string& path,
                                                    const std::string& body,
                                                    int& out_status_code,
                                                    std::string& out_content_type) {
    out_status_code = 200;
    out_content_type = "application/json; charset=utf-8";

    if (method == "GET" && (path == "/" || path == "/index.html")) {
        bool ok = false;
        std::string html = read_file_bytes(ui_root_dir_ + "/index.html", ok);
        if (!ok) {
            out_status_code = 404;
            out_content_type = "text/plain";
            return "UI file not found at " + ui_root_dir_ + "/index.html";
        }
        out_content_type = "text/html; charset=utf-8";
        return html;
    }

    if (method == "GET" && path == "/api/v5/ecosystem") {
        const auto& topo = nvxk::topology::cached_topology();
        const auto simd = nvxk::simd::query_info();
        const auto blis = nvxk::blas::query_info();
        const auto cfg = nvxk::get_runtime_config();
        std::ostringstream js;
        js << "{\"version\":\"" << json_escape(NVXK_UNIFIED_VERSION_STRING) << "\","
           << "\"topology\":{\"backend\":\"" << json_escape(topo.backend_name) << "\","
           << "\"hwloc_active\":" << (topo.hwloc_active ? "true" : "false") << ","
           << "\"packages\":" << topo.packages << ","
           << "\"physical_cores\":" << topo.physical_cores << ","
           << "\"logical_pus\":" << topo.logical_pus << ","
           << "\"numa_nodes\":" << topo.numa_nodes << ","
           << "\"l1d_kib\":" << (topo.l1d_bytes / 1024) << ","
           << "\"l2_kib\":" << (topo.l2_bytes / 1024) << ","
           << "\"l3_kib\":" << (topo.l3_bytes / 1024) << ","
           << "\"affinity_supported\":" << (topo.affinity_supported ? "true" : "false") << "},"
           << "\"simd\":{\"backend\":\"" << json_escape(simd.backend_name) << "\","
           << "\"highway_active\":" << (simd.highway_active ? "true" : "false") << ","
           << "\"active_target\":\"" << json_escape(simd.active_target) << "\","
           << "\"lanes_f32\":" << simd.lanes_f32 << ","
           << "\"lanes_f64\":" << simd.lanes_f64 << "},"
           << "\"blis\":{\"compiled_in\":" << (blis.compiled_in ? "true" : "false") << ","
           << "\"available\":" << (blis.available ? "true" : "false") << ","
           << "\"version\":\"" << json_escape(blis.version) << "\"},"
           << "\"policy\":{\"gemm_backend\":\"" << json_escape(nvxk::backend_kind_name(cfg.gemm_backend)) << "\","
           << "\"vector_backend\":\"" << json_escape(nvxk::backend_kind_name(cfg.vector_backend)) << "\","
           << "\"hwloc_affinity\":" << (cfg.enable_hwloc_affinity ? "true" : "false") << "}}";
        out_status_code = 200;
        out_content_type = "application/json";
        return js.str();
    }
    if (method == "GET" && path == "/api/state") {
        const DiagnosticsReport diag = collect_diagnostics_report();
        const auto backends = list_implemented_backends();
        const auto history = engine_.get_history();
        const auto insights = generate_engineering_insights(diag, history);
        const JobStatus status = engine_.get_status();

        std::ostringstream o;
        o << "{\"version\":\"" << XK_version_string() << "\","
          << "\"diagnostics\":" << serialize_diagnostics(diag) << ","
          << "\"status\":" << serialize_job_status(status) << ","
          << "\"backends\":[";
        for (size_t i = 0; i < backends.size(); ++i) {
            if (i) o << ",";
            o << "{"
              << "\"id\":\"" << json_escape(backends[i].id) << "\","
              << "\"display_name\":\"" << json_escape(backends[i].display_name) << "\","
              << "\"category\":\"" << json_escape(backends[i].category) << "\","
              << "\"supported_precisions\":\"" << json_escape(backends[i].supported_precisions) << "\","
              << "\"available\":" << (backends[i].available ? "true" : "false") << ","
              << "\"availability_note\":\"" << json_escape(backends[i].availability_note) << "\""
              << "}";
        }
        o << "],\"history\":[";
        for (size_t i = 0; i < history.size(); ++i) {
            if (i) o << ",";
            o << serialize_row(history[i]);
        }
        o << "],\"insights\":[";
        for (size_t i = 0; i < insights.size(); ++i) {
            const auto& ins = insights[i];
            if (i) o << ",";
            o << "{"
              << "\"id\":\"" << json_escape(ins.id) << "\","
              << "\"category\":\"" << json_escape(ins.category) << "\","
              << "\"title\":\"" << json_escape(ins.title) << "\","
              << "\"severity\":\"" << json_escape(ins.severity) << "\","
              << "\"confidence\":\"" << json_escape(ins.confidence) << "\","
              << "\"supporting_measurement\":\"" << json_escape(ins.supporting_measurement) << "\","
              << "\"evidence\":\"" << json_escape(ins.evidence) << "\","
              << "\"suggested_investigation\":\"" << json_escape(ins.suggested_investigation) << "\""
              << "}";
        }
        o << "]}";
        return o.str();
    }

    if (method == "GET" && path == "/api/telemetry") {
        const JobStatus status = engine_.get_status();
        // In Measurement Mode while running, skip OS sampling to minimize perturbation
        if (status.running && status.measurement_mode) {
            std::ostringstream o;
            o << "{\"status\":" << serialize_job_status(status) << ",\"telemetry_paused\":true}";
            return o.str();
        }
        const SystemTelemetrySnapshot t = sample_system_telemetry();
        std::ostringstream o;
        o << "{\"status\":" << serialize_job_status(status)
          << ",\"telemetry_paused\":false,\"telemetry\":" << serialize_telemetry(t) << "}";
        return o.str();
    }

    if (method == "POST" && path == "/api/calibrate") {
        const double budget_ms = json_get_number(body, "budget_ms", 200.0);
        XK_HardwareProfile prof;
        std::string err;
        if (!engine_.run_calibration(budget_ms, prof, err)) {
            out_status_code = 409;
            return "{\"ok\":false,\"error\":\"" + json_escape(err) + "\"}";
        }
        return "{\"ok\":true,\"peak_gflops_1t\":" + fmt_num(prof.peak_gflops_1t) +
               ",\"mem_bandwidth_gbs\":" + fmt_num(prof.mem_bandwidth_gbs) +
               ",\"omp_fork_overhead_us\":" + fmt_num(prof.omp_fork_overhead_us) + "}";
    }

    if (method == "POST" && path == "/api/plan") {
        const size_t M = static_cast<size_t>(std::max(0.0, json_get_number(body, "M", 256)));
        const size_t K = static_cast<size_t>(std::max(0.0, json_get_number(body, "K", 256)));
        const size_t N = static_cast<size_t>(std::max(0.0, json_get_number(body, "N", 256)));
        const std::string precision = json_get_string(body, "precision", "f32");
        const std::string backend = json_get_string(body, "backend", "auto_asm");
        const int threads = static_cast<int>(json_get_number(body, "threads", 2));
        const size_t budget_kb = static_cast<size_t>(std::max(0.0, json_get_number(body, "budget_kb", 65536)));

        XK_ExecutionPlan p;
        std::string err;
        if (!inspect_gemm_plan(M, K, N, precision, backend, threads, budget_kb * 1024, p, err)) {
            out_status_code = 400;
            return "{\"ok\":false,\"error\":\"" + json_escape(err) + "\"}";
        }
        std::ostringstream o;
        o << "{\"ok\":true,"
          << "\"kernel\":\"" << json_escape(kernel_kind_to_string(p.kernel)) << "\","
          << "\"axis\":\"" << json_escape(parallel_axis_to_string(p.axis)) << "\","
          << "\"threads\":" << p.threads << ","
          << "\"mc\":" << p.mc << ",\"kc\":" << p.kc << ",\"nc\":" << p.nc << ","
          << "\"required_workspace_bytes\":" << p.required_workspace_bytes << ","
          << "\"arithmetic_intensity\":" << fmt_num(p.explanation.arithmetic_intensity) << ","
          << "\"is_compute_bound\":" << (p.explanation.is_compute_bound ? "true" : "false") << ","
          << "\"predicted_time_us\":" << fmt_num(p.explanation.predicted_time_us) << ","
          << "\"predicted_1t_time_us\":" << fmt_num(p.explanation.predicted_1t_time_us) << ","
          << "\"predicted_omp_overhead_us\":" << fmt_num(p.explanation.predicted_omp_overhead_us) << ","
          << "\"smt_l2_shared\":" << (p.explanation.smt_l2_shared ? "true" : "false") << ","
          << "\"budget_clamped\":" << (p.explanation.budget_clamped ? "true" : "false") << ","
          << "\"reason\":\"" << json_escape(p.explanation.reason) << "\""
          << "}";
        return o.str();
    }

    if (method == "POST" && path == "/api/benchmark/start") {
        BenchmarkConfig cfg;
        cfg.M = static_cast<size_t>(std::max(0.0, json_get_number(body, "M", 256)));
        cfg.K = static_cast<size_t>(std::max(0.0, json_get_number(body, "K", 256)));
        cfg.N = static_cast<size_t>(std::max(0.0, json_get_number(body, "N", 256)));
        cfg.precision = json_get_string(body, "precision", "f32");
        auto bks = json_get_string_array(body, "backends");
        if (!bks.empty()) cfg.backends = bks;
        cfg.threads = static_cast<int>(json_get_number(body, "threads", 2));
        cfg.runs = static_cast<int>(json_get_number(body, "runs", 5));
        cfg.cold_cache = json_get_bool(body, "cold_cache", false);
        cfg.measurement_mode = json_get_bool(body, "measurement_mode", true);
        cfg.run_validation = json_get_bool(body, "run_validation", true);
        cfg.memory_budget_bytes = static_cast<size_t>(std::max(0.0, json_get_number(body, "budget_kb", 65536))) * 1024;
        cfg.seed = static_cast<uint32_t>(std::max(0.0, json_get_number(body, "seed", 42)));

        std::string err;
        if (!engine_.start_benchmark(cfg, err)) {
            out_status_code = 409;
            return "{\"ok\":false,\"error\":\"" + json_escape(err) + "\"}";
        }
        return "{\"ok\":true}";
    }

    if (method == "POST" && path == "/api/benchmark/cancel") {
        engine_.cancel_benchmark();
        return "{\"ok\":true}";
    }

    if (method == "POST" && path == "/api/benchmark/clear") {
        engine_.clear_history();
        return "{\"ok\":true}";
    }

    if (method == "POST" && path == "/api/benchmark/import_csv") {
        const std::string file = json_get_string(body, "file", "v2_opt_auto_asm.csv");
        // Security: reject path traversal
        if (file.find("..") != std::string::npos || file.find('/') != std::string::npos || file.find('\\') != std::string::npos) {
            out_status_code = 400;
            return "{\"ok\":false,\"error\":\"Invalid filename\"}";
        }
        const size_t n = engine_.import_csv_results(results_dir_ + "/" + file, "CSV:" + file);
        return "{\"ok\":true,\"imported\":" + std::to_string(n) + "}";
    }

    if (method == "POST" && path == "/api/matrix_lab/run") {
        const std::string op = json_get_string(body, "operation", "gemm");
        const std::string prec = json_get_string(body, "precision", "f32");
        const std::string bk = json_get_string(body, "backend", "auto_asm");
        const size_t M = static_cast<size_t>(std::max(0.0, json_get_number(body, "M", 4)));
        const size_t K = static_cast<size_t>(std::max(0.0, json_get_number(body, "K", 4)));
        const size_t N = static_cast<size_t>(std::max(0.0, json_get_number(body, "N", 4)));
        const int threads = static_cast<int>(json_get_number(body, "threads", 2));
        const size_t budget_kb = static_cast<size_t>(std::max(0.0, json_get_number(body, "budget_kb", 65536)));
        const uint32_t seed = static_cast<uint32_t>(std::max(0.0, json_get_number(body, "seed", 42)));
        const bool validate = json_get_bool(body, "validate", true);
        auto cA = json_get_number_array(body, "custom_A");
        auto cB = json_get_number_array(body, "custom_B");

        MatrixRunSummary r = engine_.run_matrix_lab(
            op, prec, bk, M, K, N, threads, budget_kb * 1024, seed, validate,
            cA.empty() ? nullptr : &cA, cB.empty() ? nullptr : &cB);
        if (!r.success) {
            out_status_code = 400;
            return "{\"ok\":false,\"error\":\"" + json_escape(r.error_message) + "\"}";
        }
        auto arr_json = [](const std::vector<double>& v) {
            std::ostringstream s;
            s << "[";
            for (size_t i = 0; i < v.size(); ++i) {
                if (i) s << ",";
                s << fmt_num(v[i]);
            }
            s << "]";
            return s.str();
        };
        std::ostringstream o;
        o << "{\"ok\":true,"
          << "\"operation\":\"" << json_escape(r.operation) << "\","
          << "\"precision\":\"" << json_escape(r.precision) << "\","
          << "\"backend\":\"" << json_escape(r.backend) << "\","
          << "\"M\":" << r.M << ",\"K\":" << r.K << ",\"N\":" << r.N << ","
          << "\"threads_used\":" << r.threads_used << ","
          << "\"elapsed_ms\":" << fmt_num(r.elapsed_ms) << ","
          << "\"gflops\":" << fmt_num(r.gflops) << ","
          << "\"effective_bandwidth_gbs\":" << fmt_num(r.effective_bandwidth_gbs) << ","
          << "\"workspace_bytes\":" << r.workspace_bytes << ","
          << "\"working_set_bytes\":" << r.working_set_bytes << ","
          << "\"aligned_64b\":" << (r.aligned_64b ? "true" : "false") << ","
          << "\"has_plan\":" << (r.has_plan ? "true" : "false") << ","
          << "\"plan_kernel\":\"" << json_escape(r.has_plan ? kernel_kind_to_string(r.plan.kernel) : r.backend) << "\","
          << "\"plan_reason\":\"" << json_escape(r.has_plan ? r.plan.explanation.reason : "") << "\","
          << "\"mc\":" << (r.has_plan ? r.plan.mc : 0) << ","
          << "\"kc\":" << (r.has_plan ? r.plan.kc : 0) << ","
          << "\"nc\":" << (r.has_plan ? r.plan.nc : 0) << ","
          << "\"a_rows\":" << r.a_preview_rows << ",\"a_cols\":" << r.a_preview_cols << ",\"a_preview\":" << arr_json(r.a_preview) << ","
          << "\"b_rows\":" << r.b_preview_rows << ",\"b_cols\":" << r.b_preview_cols << ",\"b_preview\":" << arr_json(r.b_preview) << ","
          << "\"c_rows\":" << r.c_preview_rows << ",\"c_cols\":" << r.c_preview_cols << ",\"c_preview\":" << arr_json(r.c_preview) << ","
          << "\"validation\":" << serialize_validation(r.validation)
          << "}";
        return o.str();
    }

    if (method == "GET" && path == "/api/logs") {
        const auto logs = engine_.get_logs();
        std::ostringstream oss;
        oss << "{\"logs\":[";
        for (size_t i = 0; i < logs.size(); ++i) {
            if (i > 0) oss << ",";
            oss << "{\"timestamp\":\"" << json_escape(logs[i].timestamp)
                << "\",\"level\":\"" << json_escape(logs[i].level)
                << "\",\"message\":\"" << json_escape(logs[i].message) << "\"}";
        }
        oss << "]}";
        return oss.str();
    }

    if (method == "POST" && path == "/api/matrix_lab/blas") {
        const std::string precision = json_get_string(body, "precision", "f32");
        const std::string layout = json_get_string(body, "layout", "row");
        const std::string trans_a = json_get_string(body, "trans_a", "N");
        const std::string trans_b = json_get_string(body, "trans_b", "N");
        const size_t M = static_cast<size_t>(std::max(1.0, json_get_number(body, "M", 64)));
        const size_t N = static_cast<size_t>(std::max(1.0, json_get_number(body, "N", 64)));
        const size_t K = static_cast<size_t>(std::max(1.0, json_get_number(body, "K", 64)));
        const double alpha = json_get_number(body, "alpha", 1.0);
        const double beta = json_get_number(body, "beta", 0.0);
        const int threads = static_cast<int>(json_get_number(body, "threads", 0));
        const size_t budget_mb = static_cast<size_t>(std::max(0.0, json_get_number(body, "budget_mb", 64)));
        const uint32_t seed = static_cast<uint32_t>(std::max(1.0, json_get_number(body, "seed", 42)));
        const bool validate = json_get_bool(body, "validate", true);

        const MatrixRunSummary r = run_generalized_gemm_lab(
            precision, layout, trans_a, trans_b, M, N, K, alpha, beta,
            threads, budget_mb * 1024 * 1024, seed, validate);
        engine_.add_log(r.success ? "INFO" : "ERROR",
                        "Generalized BLAS GEMM (" + precision + ", " + layout + ", " + trans_a + trans_b +
                        ", " + std::to_string(M) + "x" + std::to_string(N) + "x" + std::to_string(K) +
                        "): " + (r.success ? "OK" : r.error_message));

        std::ostringstream oss;
        oss << "{\"success\":" << (r.success ? "true" : "false")
            << ",\"error\":\"" << json_escape(r.error_message) << "\""
            << ",\"precision\":\"" << json_escape(r.precision) << "\""
            << ",\"backend\":\"" << json_escape(r.backend) << "\""
            << ",\"M\":" << r.M << ",\"K\":" << r.K << ",\"N\":" << r.N
            << ",\"threads_used\":" << r.threads_used
            << ",\"elapsed_ms\":" << fmt_num(r.elapsed_ms)
            << ",\"gflops\":" << fmt_num(r.gflops)
            << ",\"workspace_bytes\":" << r.workspace_bytes
            << ",\"kernel\":\"" << json_escape(kernel_kind_to_string(r.plan.kernel)) << "\""
            << ",\"validation\":{\"executed\":" << (r.validation.executed ? "true" : "false")
            << ",\"passed\":" << (r.validation.passed ? "true" : "false")
            << ",\"max_abs_error\":" << fmt_num(r.validation.max_abs_error)
            << ",\"max_rel_error\":" << fmt_num(r.validation.max_rel_error)
            << ",\"rmse\":" << fmt_num(r.validation.rmse) << "}}";
        return oss.str();
    }

    if (method == "POST" && path == "/api/reliability/run") {
        const std::string prec = json_get_string(body, "precision", "f32");
        const std::string bk = json_get_string(body, "backend", "auto_asm");
        const uint32_t seed = static_cast<uint32_t>(std::max(0.0, json_get_number(body, "seed", 42)));
        std::string err;
        auto results = engine_.run_reliability_suite(prec, bk, seed, err);
        if (!err.empty()) {
            out_status_code = 409;
            return "{\"ok\":false,\"error\":\"" + json_escape(err) + "\"}";
        }
        std::ostringstream o;
        o << "{\"ok\":true,\"cases\":[";
        for (size_t i = 0; i < results.size(); ++i) {
            if (i) o << ",";
            o << serialize_validation(results[i]);
        }
        o << "]}";
        return o.str();
    }

    if (method == "GET" && path == "/api/report/markdown") {
        const DiagnosticsReport diag = collect_diagnostics_report();
        const auto history = engine_.get_history();
        const auto insights = generate_engineering_insights(diag, history);
        out_content_type = "text/markdown; charset=utf-8";
        return export_report_markdown(diag, history, insights);
    }

    if (method == "GET" && path == "/api/report/csv") {
        const auto history = engine_.get_history();
        out_content_type = "text/csv; charset=utf-8";
        return export_report_csv(history);
    }

    out_status_code = 404;
    return "{\"ok\":false,\"error\":\"Not found\"}";
}

bool WorkbenchHttpServer::start(uint16_t port, std::string& out_error) {
#if defined(_WIN32)
    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        out_error = "WSAStartup failed";
        return false;
    }
#endif
    intptr_t sock = static_cast<intptr_t>(socket(AF_INET, SOCK_STREAM, 0));
    if (sock < 0) {
#if defined(_WIN32)
        WSACleanup();
#endif
        out_error = "socket() failed";
        return false;
    }

    int opt = 1;
    setsockopt(NATIVE_SOCK(sock), SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char*>(&opt), sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // Strictly 127.0.0.1
    addr.sin_port = htons(port);

    if (bind(NATIVE_SOCK(sock), reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        CLOSE_SOCK(sock);
#if defined(_WIN32)
        WSACleanup();
#endif
        out_error = "bind() failed on 127.0.0.1:" + std::to_string(port);
        return false;
    }

    socklen_t len = sizeof(addr);
    if (getsockname(NATIVE_SOCK(sock), reinterpret_cast<sockaddr*>(&addr), &len) == 0) {
        port_ = ntohs(addr.sin_port);
    } else {
        port_ = port;
    }

    if (listen(NATIVE_SOCK(sock), 16) < 0) {
        CLOSE_SOCK(sock);
#if defined(_WIN32)
        WSACleanup();
#endif
        out_error = "listen() failed";
        return false;
    }

    listen_sock_ = sock;
    running_.store(true);

    while (running_.load()) {
        sockaddr_in cli{};
        socklen_t clen = sizeof(cli);
        intptr_t cs = static_cast<intptr_t>(
            accept(NATIVE_SOCK(listen_sock_), reinterpret_cast<sockaddr*>(&cli), &clen));
        if (cs < 0) {
            if (!running_.load()) break;
            continue;
        }
        std::thread(&WorkbenchHttpServer::serve_client, this, cs).detach();
    }
    return true;
}

void WorkbenchHttpServer::stop() {
    if (running_.exchange(false)) {
        if (listen_sock_ >= 0) {
            CLOSE_SOCK(listen_sock_);
            listen_sock_ = -1;
        }
#if defined(_WIN32)
        WSACleanup();
#endif
    }
}

void WorkbenchHttpServer::serve_client(intptr_t client_sock) {
    std::string req;
    char buf[4096];
    size_t content_length = 0;
    size_t header_end = std::string::npos;

    constexpr size_t kMaxReqBytes = 2 * 1024 * 1024;
    while (req.size() < kMaxReqBytes) {
        const auto n = recv(NATIVE_SOCK(client_sock), buf, sizeof(buf), 0);
        if (n <= 0) break;
        req.append(buf, static_cast<size_t>(n));
        if (header_end == std::string::npos) {
            header_end = req.find("\r\n\r\n");
            if (header_end != std::string::npos) {
                std::string hdrs = req.substr(0, header_end);
                std::string lower = hdrs;
                std::transform(lower.begin(), lower.end(), lower.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                size_t cl = lower.find("content-length:");
                if (cl != std::string::npos) {
                    const unsigned long long parsed = std::strtoull(lower.c_str() + cl + 15, nullptr, 10);
                    content_length = (parsed > kMaxReqBytes) ? kMaxReqBytes : static_cast<size_t>(parsed);
                }
            }
        }
        if (header_end != std::string::npos &&
            req.size() - (header_end + 4) >= content_length) {
            break;
        }
    }

    if (header_end == std::string::npos) {
        CLOSE_SOCK(client_sock);
        return;
    }

    const size_t line_end = req.find("\r\n");
    std::istringstream rl(req.substr(0, line_end));
    std::string method, full_path;
    rl >> method >> full_path;
    std::string path = full_path;
    const size_t qpos = path.find('?');
    if (qpos != std::string::npos) path = path.substr(0, qpos);
    const std::string body = req.substr(header_end + 4, content_length);

    int status_code = 200;
    std::string content_type = "application/json";
    std::string resp_body = handle_api_request(method, path, body, status_code, content_type);

    const char* status_text = (status_code == 200) ? "OK"
                              : (status_code == 400) ? "Bad Request"
                              : (status_code == 409) ? "Conflict"
                                                     : "Not Found";

    std::ostringstream resp;
    resp << "HTTP/1.1 " << status_code << " " << status_text << "\r\n"
         << "Content-Type: " << content_type << "\r\n"
         << "Content-Length: " << resp_body.size() << "\r\n"
         << "Cache-Control: no-store\r\n"
         << "Connection: close\r\n\r\n"
         << resp_body;

    const std::string wire = resp.str();
    size_t sent = 0;
    while (sent < wire.size()) {
        const auto s = send(NATIVE_SOCK(client_sock), wire.data() + sent,
                            static_cast<int>(wire.size() - sent), 0);
        if (s <= 0) break;
        sent += static_cast<size_t>(s);
    }
    CLOSE_SOCK(client_sock);
}

}  // namespace nvxk_wb
