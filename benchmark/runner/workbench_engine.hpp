#ifndef NVXK_WORKBENCH_ENGINE_HPP
#define NVXK_WORKBENCH_ENGINE_HPP

#include "../../adapters/core_adapter/nvxk_adapter.hpp"
#include "../../diagnostics/cpu/cpu_diagnostics.hpp"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace nvxk_wb {

struct BenchmarkConfig {
    size_t M = 256;
    size_t K = 256;
    size_t N = 256;
    std::string precision = "f32";  // "f32" or "f64"
    std::vector<std::string> backends = {"ikj", "avx2", "avx2_asm", "auto", "auto_asm"};
    int threads = 2;
    int runs = 5;
    bool cold_cache = false;
    bool measurement_mode = true;
    bool run_validation = true;
    size_t memory_budget_bytes = 64 * 1024 * 1024;
    uint32_t seed = 42;
};

struct BenchmarkResultRow {
    uint64_t id = 0;
    std::string timestamp_iso;
    std::string session_name;
    std::string function_name;   // e.g. "XK_gemm_ex (auto_asm)"
    std::string backend;         // "auto_asm", "avx2", "ikj", etc.
    std::string kernel_selected; // "Avx2Blis6x16Asm", "Avx2ThinM", etc.
    std::string precision;
    size_t M = 0, K = 0, N = 0;
    int threads_requested = 1;
    int threads_used = 1;
    int sample_count = 0;
    size_t total_calls = 0;

    double min_ms = 0.0;
    double median_ms = 0.0;
    double mean_ms = 0.0;
    double total_ms = 0.0;
    double stddev_ms = 0.0;
    double p95_ms = 0.0;
    double p99_ms = 0.0;
    bool p99_reliable = false; // true when sample_count >= 20

    double gflops = 0.0;
    double ops_per_sec = 0.0;
    double speedup_vs_baseline = 0.0; // 0.0 if no baseline measured for this shape
    bool has_baseline = false;

    size_t workspace_bytes = 0;
    size_t working_set_bytes = 0;
    double measured_bandwidth_gbs = 0.0;
    bool budget_clamped = false;
    bool smt_l2_shared = false;
    bool cold_cache = false;
    bool measurement_mode = true;
    std::string plan_reason;
    size_t mc = 0, kc = 0, nc = 0;
    std::string axis;

    ValidationMetrics validation;
    std::vector<double> raw_samples_ms;
};

struct LogEntry {
    std::string timestamp;
    std::string level;   // "INFO", "WARN", "ERROR"
    std::string message;
};

struct JobStatus {
    bool running = false;
    bool cancelled = false;
    double progress = 0.0; // 0.0 .. 1.0
    std::string current_step;
    std::string last_error;
    std::string session_name;
    bool measurement_mode = false;
};

class WorkbenchEngine {
public:
    WorkbenchEngine();
    ~WorkbenchEngine();

    WorkbenchEngine(const WorkbenchEngine&) = delete;
    WorkbenchEngine& operator=(const WorkbenchEngine&) = delete;

    // Hardware calibration (thread-safe, rejects if benchmark is running)
    bool run_calibration(double budget_ms, XK_HardwareProfile& out_profile, std::string& out_error);

    // Asynchronous benchmark launch & cancellation
    bool start_benchmark(const BenchmarkConfig& cfg, std::string& out_error);
    void cancel_benchmark();
    void wait_for_idle();
    JobStatus get_status() const;

    // Interactive Matrix Lab (runs synchronously on small/medium shapes, rejects if benchmark running)
    MatrixRunSummary run_matrix_lab(
        const std::string& operation,
        const std::string& precision,
        const std::string& backend,
        size_t M, size_t K, size_t N,
        int threads,
        size_t memory_budget_bytes,
        uint32_t seed,
        bool run_validation,
        const std::vector<double>* custom_A = nullptr,
        const std::vector<double>* custom_B = nullptr);

    // Numerical Reliability edge-case test suite runner
    std::vector<ValidationMetrics> run_reliability_suite(
        const std::string& precision,
        const std::string& backend,
        uint32_t seed,
        std::string& out_error);

    // History accessors
    std::vector<BenchmarkResultRow> get_history() const;
    void clear_history();

    // Structured event log (v3.0+)
    void add_log(const std::string& level, const std::string& message);
    std::vector<LogEntry> get_logs() const;
    void clear_logs();

    // Load historical CSV results from results/ directory (clearly tagged as historical CSV import)
    size_t import_csv_results(const std::string& csv_path, const std::string& session_tag);

private:
    void worker_loop(BenchmarkConfig cfg, std::string session_name);

    mutable std::mutex state_mutex_;
    std::thread worker_;
    std::atomic<bool> running_{false};
    std::atomic<bool> cancel_requested_{false};
    std::atomic<double> progress_{0.0};
    std::string current_step_;
    std::string last_error_;
    std::string active_session_;
    bool active_measurement_mode_ = false;
    uint64_t next_result_id_ = 1;
    std::vector<BenchmarkResultRow> history_;
    std::vector<LogEntry> logs_;
};

}  // namespace nvxk_wb

#endif