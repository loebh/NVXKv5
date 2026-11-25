#include "report_exporter.hpp"
#include <iomanip>
#include <sstream>

namespace nvxk_wb {

std::string json_escape(const std::string& s) {
    std::ostringstream oss;
    for (unsigned char c : s) {
        switch (c) {
            case '"':  oss << "\\\""; break;
            case '\\': oss << "\\\\"; break;
            case '\b': oss << "\\b"; break;
            case '\f': oss << "\\f"; break;
            case '\n': oss << "\\n"; break;
            case '\r': oss << "\\r"; break;
            case '\t': oss << "\\t"; break;
            default:
                if (c < 0x20) {
                    oss << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(c) << std::dec;
                } else {
                    oss << static_cast<char>(c);
                }
        }
    }
    return oss.str();
}

std::string export_report_markdown(
    const DiagnosticsReport& diag,
    const std::vector<BenchmarkResultRow>& history,
    const std::vector<EngineeringInsight>& insights) {
    std::ostringstream oss;
    oss << "# NVXK Engineering Intelligence Report\n\n";
    oss << "## 1. Hardware & Build Environment\n\n";
    oss << "- **CPU**: " << diag.profile.hw.brand << " (" << diag.profile.hw.vendor << ")\n";
    oss << "- **Topology**: " << diag.profile.hw.physical_cores << " physical cores / "
        << diag.profile.hw.logical_threads << " logical threads\n";
    oss << "- **Caches**: L1d=" << (diag.profile.hw.l1d_bytes / 1024) << " KiB, "
        << "L2=" << (diag.profile.hw.l2_bytes / 1024) << " KiB, "
        << "L3=" << (diag.profile.hw.l3_bytes / 1024) << " KiB\n";
    oss << "- **OS / Compiler / Standard**: " << diag.os_name << " / " << diag.compiler_name
        << " / " << diag.cpp_standard << "\n";
    oss << "- **Calibration Status**: " << (diag.profile.calibrated ? "Calibrated" : "Default (Uncalibrated)")
        << " (1T=" << std::fixed << std::setprecision(1) << diag.profile.peak_gflops_1t
        << " GFLOPS, Streaming BW=" << diag.profile.mem_bandwidth_gbs << " GB/s)\n\n";

    oss << "## 2. 5-Stage ISA Capability Matrix\n\n";
    oss << "| Feature | CPU Supports | Compiler Supports | Binary Contains | Runtime Selected | Execution Verified |\n";
    oss << "| :--- | :---: | :---: | :---: | :---: | :---: |\n";
    for (const auto& f : diag.isa_matrix) {
        oss << "| " << f.feature_name << " | "
            << (f.cpu_supports ? "Yes" : "No") << " | "
            << (f.compiler_supports ? "Yes" : "No") << " | "
            << (f.binary_contains ? "Yes" : "No") << " | "
            << (f.runtime_dispatch_selected ? "Yes" : "No") << " | "
            << (f.execution_verified ? "Verified" : "Not yet") << " |\n";
    }
    oss << "\n";

    oss << "## 3. Measured Benchmark Results\n\n";
    if (history.empty()) {
        oss << "_No benchmark runs recorded in this session._\n\n";
    } else {
        oss << "| Session | Function | Backend | Shape | Precision | Threads | Median (ms) | Mean (ms) | P95 (ms) | GFLOPS | Speedup | MaxRelErr | Status |\n";
        oss << "| :--- | :--- | :--- | :--- | :---: | :---: | ---: | ---: | ---: | ---: | ---: | ---: | :---: |\n";
        for (const auto& r : history) {
            oss << "| " << r.session_name << " | " << r.function_name << " | " << r.backend
                << " | " << r.M << "x" << r.K << "x" << r.N << " | " << r.precision
                << " | " << r.threads_used << " | "
                << std::fixed << std::setprecision(4) << r.median_ms << " | "
                << r.mean_ms << " | " << r.p95_ms << " | "
                << std::setprecision(2) << r.gflops << " | ";
            if (r.has_baseline) oss << r.speedup_vs_baseline << "x | ";
            else oss << "N/A | ";
            if (r.validation.executed) {
                oss << std::scientific << std::setprecision(2) << r.validation.max_rel_error << " | "
                    << (r.validation.passed ? "PASS" : "FAIL") << " |\n";
            } else {
                oss << "Not measured | Unverified |\n";
            }
        }
        oss << "\n";
    }

    oss << "## 4. Engineering Insights\n\n";
    for (const auto& ins : insights) {
        oss << "- **[" << ins.category << " | " << ins.confidence << "] " << ins.title << "**: "
            << ins.evidence << " _(Measurement: " << ins.supporting_measurement << ")_. "
            << "Next step: " << ins.suggested_investigation << "\n";
    }
    return oss.str();
}

std::string export_report_csv(const std::vector<BenchmarkResultRow>& history) {
    std::ostringstream oss;
    oss << "id,timestamp,session,function,backend,kernel,precision,M,K,N,threads_used,samples,"
        << "min_ms,median_ms,mean_ms,stddev_ms,p95_ms,p99_ms,gflops,speedup,workspace_bytes,"
        << "working_set_bytes,bandwidth_gbs,max_abs_err,max_rel_err,rmse,validation_passed\n";
    for (const auto& r : history) {
        oss << r.id << "," << r.timestamp_iso << "," << r.session_name << ","
            << r.function_name << "," << r.backend << "," << r.kernel_selected << ","
            << r.precision << "," << r.M << "," << r.K << "," << r.N << ","
            << r.threads_used << "," << r.sample_count << ","
            << std::fixed << std::setprecision(6)
            << r.min_ms << "," << r.median_ms << "," << r.mean_ms << ","
            << r.stddev_ms << "," << r.p95_ms << "," << r.p99_ms << ","
            << std::setprecision(3) << r.gflops << ","
            << (r.has_baseline ? r.speedup_vs_baseline : 0.0) << ","
            << r.workspace_bytes << "," << r.working_set_bytes << ","
            << r.measured_bandwidth_gbs << ","
            << std::scientific << std::setprecision(4)
            << r.validation.max_abs_error << "," << r.validation.max_rel_error << ","
            << r.validation.rmse << "," << (r.validation.executed ? (r.validation.passed ? "PASS" : "FAIL") : "UNVERIFIED")
            << "\n";
    }
    return oss.str();
}

}  // namespace nvxk_wb