#ifndef NVXK_CPU_DIAGNOSTICS_HPP
#define NVXK_CPU_DIAGNOSTICS_HPP

#include <nvxk.h>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace nvxk_wb {

struct IsaFeatureStage {
    std::string feature_name;
    bool cpu_supports = false;
    bool compiler_supports = false;
    bool binary_contains = false;
    bool runtime_dispatch_selected = false;
    bool execution_verified = false;
    std::string notes;
};

struct SystemTelemetrySnapshot {
    bool cpu_util_available = false;
    double cpu_util_percent = 0.0;
    std::vector<double> per_core_util_percent;
    double sampling_window_ms = 0.0;

    bool memory_available = false;
    size_t process_rss_bytes = 0;
    size_t process_peak_rss_bytes = 0;
    size_t system_total_ram_bytes = 0;
    size_t system_avail_ram_bytes = 0;

    bool hw_counters_available = false;
    std::string hw_counters_note = "Unavailable (Intel PCM / Linux perf_event MSR access not enabled)";
};

struct DiagnosticsReport {
    XK_HardwareProfile profile;
    std::string os_name;
    std::string compiler_name;
    std::string cpp_standard;
    bool openmp_enabled = false;
    int openmp_max_threads = 1;
    std::vector<IsaFeatureStage> isa_matrix;
    SystemTelemetrySnapshot telemetry;
};

// Track verified executions per feature in the current session.
void mark_feature_executed(const std::string& feature_name);

// Sample real OS-level CPU & memory telemetry (Linux /proc or Win32 APIs).
SystemTelemetrySnapshot sample_system_telemetry();

// Collect full 5-stage ISA matrix + hardware + compiler + live telemetry.
DiagnosticsReport collect_diagnostics_report();

}  // namespace nvxk_wb

#endif