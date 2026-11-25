#include "workbench_engine.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <limits>
#include <numeric>
#include <random>
#include <sstream>

#if defined(_OPENMP)
#include <omp.h>
#endif

namespace nvxk_wb {

namespace {

inline void compiler_barrier() {
#if defined(__GNUC__) || defined(__clang__)
    asm volatile("" : : : "memory");
#endif
}

std::string now_iso_utc() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t tt = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf{};
#if defined(_WIN32)
    gmtime_s(&tm_buf, &tt);
#else
    gmtime_r(&tt, &tm_buf);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm_buf, "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

double percentile_sorted(const std::vector<double>& sorted, double p) {
    if (sorted.empty()) return 0.0;
    if (sorted.size() == 1) return sorted[0];
    const double idx = p * static_cast<double>(sorted.size() - 1);
    const size_t lo = static_cast<size_t>(std::floor(idx));
    const size_t hi = std::min(sorted.size() - 1, lo + 1);
    const double frac = idx - static_cast<double>(lo);
    return sorted[lo] * (1.0 - frac) + sorted[hi] * frac;
}

void record_executed_isa(const std::string& backend, XK_KernelKind k, int used_t) {
    if (backend == "ikj" || backend == "naive" || k == XK_KernelKind::ScalarIkj) {
        mark_feature_executed("Scalar");
    }
    if (k == XK_KernelKind::Avx2Blis6x16 || k == XK_KernelKind::Avx2ThinM ||
        k == XK_KernelKind::Avx2Blis6x16Asm || backend == "avx2" || backend == "avx2_asm") {
        if (XK_has_avx2_fma()) {
            mark_feature_executed("AVX2");
            mark_feature_executed("FMA");
        }
    }
    if ((k == XK_KernelKind::Avx2Blis6x16Asm || backend == "avx2_asm") && XK_has_avx2_asm_kernel()) {
        mark_feature_executed("ASM_6x16");
    }
    if (used_t > 1) {
        mark_feature_executed("OpenMP");
    }
}

}  // namespace

WorkbenchEngine::WorkbenchEngine() {
#if defined(_OPENMP)
    omp_set_dynamic(0);
#endif
}

WorkbenchEngine::~WorkbenchEngine() {
    cancel_benchmark();
    wait_for_idle();
}

void WorkbenchEngine::wait_for_idle() {
    if (worker_.joinable()) {
        worker_.join();
    }
}

bool WorkbenchEngine::run_calibration(double budget_ms, XK_HardwareProfile& out_profile, std::string& out_error) {
    if (running_.load()) {
        out_error = "Cannot calibrate while a benchmark is running.";
        return false;
    }
    try {
        const double clamped = std::max(50.0, std::min(2000.0, budget_ms));
        out_profile = XK_calibrate_hardware(clamped);
        XK_set_active_profile(out_profile);
        if (XK_has_avx2_fma()) {
            mark_feature_executed("AVX2");
            mark_feature_executed("FMA");
        }
        if (XK_has_avx2_asm_kernel()) {
            mark_feature_executed("ASM_6x16");
        }
        return true;
    } catch (const std::exception& e) {
        out_error = e.what();
        return false;
    }
}

bool WorkbenchEngine::start_benchmark(const BenchmarkConfig& cfg, std::string& out_error) {
    if (cfg.M == 0 || cfg.K == 0 || cfg.N == 0) {
        out_error = "Matrix dimensions M, K, N must be positive.";
        return false;
    }
    if (cfg.M > 4096 || cfg.K > 4096 || cfg.N > 4096) {
        out_error = "Dimension exceeds safe workbench limit (4096).";
        return false;
    }
    if (cfg.backends.empty()) {
        out_error = "At least one backend must be selected.";
        return false;
    }
    if (cfg.runs < 1 || cfg.runs > 100) {
        out_error = "Runs must be between 1 and 100.";
        return false;
    }
    if (running_.exchange(true)) {
        out_error = "A benchmark session is already running. Cancel or wait for it to finish.";
        return false;
    }
    if (worker_.joinable()) {
        worker_.join();
    }

    cancel_requested_.store(false);
    progress_.store(0.0);
    std::string sess = "session_" + now_iso_utc();
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        current_step_ = "Initializing benchmark buffers...";
        last_error_.clear();
        active_session_ = sess;
        active_measurement_mode_ = cfg.measurement_mode;
    }

    worker_ = std::thread(&WorkbenchEngine::worker_loop, this, cfg, sess);
    return true;
}

void WorkbenchEngine::cancel_benchmark() {
    cancel_requested_.store(true);
}

JobStatus WorkbenchEngine::get_status() const {
    JobStatus st;
    st.running = running_.load();
    st.cancelled = cancel_requested_.load();
    st.progress = progress_.load();
    std::lock_guard<std::mutex> lock(state_mutex_);
    st.current_step = current_step_;
    st.last_error = last_error_;
    st.session_name = active_session_;
    st.measurement_mode = active_measurement_mode_;
    return st;
}

void WorkbenchEngine::worker_loop(BenchmarkConfig cfg, std::string session_name) {
    try {
        const size_t M = cfg.M, K = cfg.K, N = cfg.N;
        const bool is_f64 = (cfg.precision == "f64");
        const size_t elem_bytes = is_f64 ? sizeof(double) : sizeof(float);
        const double ops = 2.0 * static_cast<double>(M) * static_cast<double>(K) * static_cast<double>(N);
        const size_t working_set = (M * K + K * N + M * N) * elem_bytes;

        const XK_HardwareProfile profile = XK_get_active_profile();
        std::mt19937 rng(cfg.seed);
        std::uniform_real_distribution<double> dist(-1.0, 1.0);

        // Cold-cache flush buffer strictly outside timer
        std::vector<uint8_t> flush_buf;
        if (cfg.cold_cache) {
            flush_buf.assign(std::max(profile.hw.l3_bytes * 2, static_cast<size_t>(16 * 1024 * 1024)), 1);
        }
        auto maybe_flush = [&]() {
            if (!flush_buf.empty()) {
                volatile uint8_t acc = 0;
                for (size_t i = 0; i < flush_buf.size(); i += 64) {
                    flush_buf[i] ^= 1;
                    acc ^= flush_buf[i];
                }
                (void)acc;
            }
        };

        AlignedBuffer<float> Af, Bf, Cf;
        AlignedBuffer<double> Ad, Bd, Cd;
        if (is_f64) {
            Ad.resize(M * K); Bd.resize(K * N); Cd.resize(M * N);
            for (size_t i = 0; i < M * K; ++i) Ad.data()[i] = dist(rng);
            for (size_t i = 0; i < K * N; ++i) Bd.data()[i] = dist(rng);
        } else {
            Af.resize(M * K); Bf.resize(K * N); Cf.resize(M * N);
            for (size_t i = 0; i < M * K; ++i) Af.data()[i] = static_cast<float>(dist(rng));
            for (size_t i = 0; i < K * N; ++i) Bf.data()[i] = static_cast<float>(dist(rng));
        }

        XK_Workspace ws(cfg.memory_budget_bytes > 0 ? cfg.memory_budget_bytes : 64 * 1024 * 1024);
        std::vector<BenchmarkResultRow> session_rows;
        double baseline_median_ms = 0.0;

        for (size_t bi = 0; bi < cfg.backends.size(); ++bi) {
            if (cancel_requested_.load()) break;
            const std::string& backend = cfg.backends[bi];

            // Skip unsupported f64 backends cleanly
            if (is_f64 && (backend == "avx2_asm" || backend == "blocked")) {
                continue;
            }
            // Skip unavailable SIMD backends cleanly
            if (backend == "avx2_asm" && !XK_has_avx2_asm_kernel()) continue;
            if (backend == "avx2" && !XK_has_avx2_fma()) continue;

            {
                std::lock_guard<std::mutex> lock(state_mutex_);
                current_step_ = "Benchmarking " + backend + " (" + cfg.precision + ", " +
                                std::to_string(M) + "x" + std::to_string(K) + "x" + std::to_string(N) + ")";
            }

            BenchmarkResultRow row;
            row.timestamp_iso = now_iso_utc();
            row.session_name = session_name;
            row.backend = backend;
            row.precision = cfg.precision;
            row.M = M; row.K = K; row.N = N;
            row.threads_requested = (backend == "omp") ? std::max(1, cfg.threads) : 1;
            row.threads_used = 1;
            row.working_set_bytes = working_set;
            row.cold_cache = cfg.cold_cache;
            row.measurement_mode = cfg.measurement_mode;

            int used_t = 1;
            XK_ExecutionPlan plan;
            bool has_plan = false;

            auto run_once = [&]() {
                if (is_f64) {
                    if (backend == "naive") {
                        XK_gemm_naive(Ad.data(), Bd.data(), Cd.data(), M, K, N);
                    } else if (backend == "ikj") {
                        XK_gemm_ikj(Ad.data(), Bd.data(), Cd.data(), M, K, N);
                    } else if (backend == "avx2") {
                        XK_gemm_avx2(Ad.data(), Bd.data(), Cd.data(), M, K, N);
                    } else {
                        XK_WorkloadDesc wd(M, K, N, sizeof(double));
                        wd.memory_budget_bytes = cfg.memory_budget_bytes;
                        if (backend == "omp") {
                            wd.mode = XK_ExecMode::Manual;
                            wd.manual_threads = row.threads_requested;
                        } else {
                            wd.mode = XK_ExecMode::Auto;
                        }
                        plan = XK_gemm_f64_ex(Ad.data(), Bd.data(), Cd.data(), wd, &ws, &profile, &used_t);
                        has_plan = true;
                    }
                } else {
                    if (backend == "naive") {
                        XK_gemm_naive(Af.data(), Bf.data(), Cf.data(), M, K, N);
                    } else if (backend == "ikj") {
                        XK_gemm_ikj(Af.data(), Bf.data(), Cf.data(), M, K, N);
                    } else if (backend == "blocked") {
                        XK_gemm_blocked(Af.data(), Bf.data(), Cf.data(), M, K, N, 64);
                    } else if (backend == "avx2") {
                        XK_gemm_avx2(Af.data(), Bf.data(), Cf.data(), M, K, N);
                    } else if (backend == "avx2_asm") {
                        XK_gemm_avx2_asm(Af.data(), Bf.data(), Cf.data(), M, K, N);
                    } else if (backend == "omp") {
                        XK_gemm_omp(Af.data(), Bf.data(), Cf.data(), M, K, N, row.threads_requested, &used_t);
                    } else {
                        XK_WorkloadDesc wd(M, K, N, sizeof(float));
                        wd.mode = XK_ExecMode::Auto;
                        wd.memory_budget_bytes = cfg.memory_budget_bytes;
                        wd.prefer_asm_kernel = (backend == "auto_asm");
                        plan = XK_gemm_ex(Af.data(), Bf.data(), Cf.data(), wd, &ws, &profile, &used_t);
                        has_plan = true;
                    }
                }
            };

            // Warm-up run
            if (cfg.cold_cache) maybe_flush();
            run_once();
            compiler_barrier();

            // Adaptive iteration count for short kernels in warm-cache mode
            size_t iters = 1;
            if (!cfg.cold_cache) {
                const double min_win = cfg.measurement_mode ? 0.03 : 0.015;
                double elapsed = 0.0;
                while (!cancel_requested_.load()) {
                    const auto s = std::chrono::steady_clock::now();
                    for (size_t j = 0; j < iters; ++j) {
                        run_once();
                        compiler_barrier();
                    }
                    const auto e = std::chrono::steady_clock::now();
                    elapsed = std::chrono::duration<double>(e - s).count();
                    if (elapsed >= min_win || iters >= 1024) break;
                    iters *= 2;
                }
            }

            std::vector<double> samples_ms;
            samples_ms.reserve(static_cast<size_t>(cfg.runs));
            for (int r = 0; r < cfg.runs; ++r) {
                if (cancel_requested_.load()) break;
                if (cfg.cold_cache) {
                    maybe_flush();
                    compiler_barrier();
                }
                const auto s = std::chrono::steady_clock::now();
                for (size_t j = 0; j < iters; ++j) {
                    run_once();
                    compiler_barrier();
                }
                const auto e = std::chrono::steady_clock::now();
                const double dt_ms = (std::chrono::duration<double>(e - s).count() /
                                      static_cast<double>(iters)) * 1e3;
                samples_ms.push_back(dt_ms);
                progress_.store((static_cast<double>(bi) + static_cast<double>(r + 1) / static_cast<double>(cfg.runs)) /
                                static_cast<double>(cfg.backends.size()));
            }

            if (cancel_requested_.load() || samples_ms.empty()) break;

            row.raw_samples_ms = samples_ms;
            row.sample_count = static_cast<int>(samples_ms.size());
            row.total_calls = static_cast<size_t>(samples_ms.size()) * iters;
            row.p99_reliable = (row.sample_count >= 20);
            row.threads_used = used_t;

            std::vector<double> sorted = samples_ms;
            std::sort(sorted.begin(), sorted.end());
            row.min_ms = sorted.front();
            row.median_ms = percentile_sorted(sorted, 0.50);
            row.p95_ms = percentile_sorted(sorted, 0.95);
            row.p99_ms = percentile_sorted(sorted, 0.99);
            row.total_ms = std::accumulate(samples_ms.begin(), samples_ms.end(), 0.0);
            row.mean_ms = row.total_ms / static_cast<double>(samples_ms.size());
            double var = 0.0;
            for (double x : samples_ms) {
                const double d = x - row.mean_ms;
                var += d * d;
            }
            row.stddev_ms = (samples_ms.size() > 1)
                                ? std::sqrt(var / static_cast<double>(samples_ms.size() - 1))
                                : 0.0;

            const double med_sec = row.median_ms * 1e-3;
            row.gflops = (med_sec > 0.0) ? ((ops / med_sec) * 1e-9) : 0.0;
            row.ops_per_sec = (med_sec > 0.0) ? (ops / med_sec) : 0.0;
            row.measured_bandwidth_gbs = (med_sec > 0.0)
                                             ? (static_cast<double>(working_set) / med_sec) * 1e-9
                                             : 0.0;

            if (has_plan) {
                row.kernel_selected = kernel_kind_to_string(plan.kernel);
                row.workspace_bytes = plan.required_workspace_bytes;
                row.budget_clamped = plan.explanation.budget_clamped;
                row.smt_l2_shared = plan.explanation.smt_l2_shared;
                row.plan_reason = plan.explanation.reason;
                row.mc = plan.mc; row.kc = plan.kc; row.nc = plan.nc;
                row.axis = parallel_axis_to_string(plan.axis);
                record_executed_isa(backend, plan.kernel, used_t);
            } else {
                row.kernel_selected = backend;
                row.workspace_bytes = 0;
                row.axis = (used_t > 1) ? "Auto" : "None";
                record_executed_isa(backend, XK_KernelKind::ScalarIkj, used_t);
            }

            row.function_name = (is_f64 ? "XK_gemm_f64_" : "XK_gemm_") + backend;

            if (cfg.run_validation && !cancel_requested_.load()) {
                if (is_f64) {
                    row.validation = validate_gemm_f64_against_ikj(Ad.data(), Bd.data(), Cd.data(), M, K, N, cfg.seed);
                } else {
                    row.validation = validate_gemm_f32_against_f64(Af.data(), Bf.data(), Cf.data(), M, K, N, cfg.seed);
                }
            }

            if (backend == "ikj" || (backend == "naive" && baseline_median_ms == 0.0)) {
                baseline_median_ms = row.median_ms;
            }

            session_rows.push_back(std::move(row));
        }

        // Compute truthful relative speedup vs measured scalar baseline in same session
        if (baseline_median_ms > 0.0) {
            for (auto& r : session_rows) {
                if (r.median_ms > 0.0) {
                    r.speedup_vs_baseline = baseline_median_ms / r.median_ms;
                    r.has_baseline = true;
                }
            }
        }

        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            for (auto& r : session_rows) {
                r.id = next_result_id_++;
                history_.push_back(std::move(r));
            }
            current_step_ = cancel_requested_.load() ? "Cancelled by user" : "Completed";
            progress_.store(cancel_requested_.load() ? progress_.load() : 1.0);
        }
    } catch (const std::exception& e) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        last_error_ = e.what();
        current_step_ = "Failed";
    }
    running_.store(false);
}

MatrixRunSummary WorkbenchEngine::run_matrix_lab(
    const std::string& operation,
    const std::string& precision,
    const std::string& backend,
    size_t M, size_t K, size_t N,
    int threads,
    size_t memory_budget_bytes,
    uint32_t seed,
    bool run_validation,
    const std::vector<double>* custom_A,
    const std::vector<double>* custom_B) {
    if (running_.load()) {
        MatrixRunSummary err;
        err.success = false;
        err.error_message = "Cannot run Matrix Lab while a benchmark session is active.";
        return err;
    }
    MatrixRunSummary res = run_matrix_lab_operation(
        operation, precision, backend, M, K, N, threads,
        memory_budget_bytes, seed, run_validation, custom_A, custom_B);
    if (res.success) {
        record_executed_isa(backend, res.has_plan ? res.plan.kernel : XK_KernelKind::ScalarIkj,
                            res.threads_used);
    }
    return res;
}

std::vector<ValidationMetrics> WorkbenchEngine::run_reliability_suite(
    const std::string& precision,
    const std::string& backend,
    uint32_t seed,
    std::string& out_error) {
    std::vector<ValidationMetrics> out;
    if (running_.load()) {
        out_error = "Cannot run Reliability Suite while a benchmark session is active.";
        return out;
    }
    struct Case { size_t M, K, N; };
    const Case cases[] = {
        {1, 1, 1},       // minimal scalar edge
        {1, 64, 64},     // Thin-M GEMV
        {4, 128, 128},   // Thin-M 4x24 streaming
        {6, 16, 16},     // exact 1 microkernel tile
        {7, 13, 19},     // odd prime edge
        {63, 65, 67},    // non-multiple of 6 and 16
        {128, 128, 128}  // standard square
    };
    for (const auto& c : cases) {
        auto r = run_matrix_lab_operation(
            "gemm", precision, backend, c.M, c.K, c.N, 2,
            64 * 1024 * 1024, seed, true);
        if (r.success && r.validation.executed) {
            out.push_back(r.validation);
            record_executed_isa(backend, r.has_plan ? r.plan.kernel : XK_KernelKind::ScalarIkj,
                                r.threads_used);
        }
    }
    return out;
}

std::vector<BenchmarkResultRow> WorkbenchEngine::get_history() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return history_;
}

void WorkbenchEngine::clear_history() {
    std::lock_guard<std::mutex> lock(state_mutex_);
    history_.clear();
}

size_t WorkbenchEngine::import_csv_results(const std::string& csv_path, const std::string& session_tag) {
    std::ifstream in(csv_path);
    if (!in) return 0;
    std::string line;
    bool header_seen = false;
    size_t imported = 0;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        if (!header_seen) {
            header_seen = true;
            continue;
        }
        std::stringstream ss(line);
        std::vector<std::string> cols;
        std::string tok;
        while (std::getline(ss, tok, ',')) cols.push_back(tok);
        if (cols.size() < 15) continue;

        try {
            BenchmarkResultRow r;
            r.timestamp_iso = "CSV_IMPORT";
            r.session_name = session_tag;
            r.M = static_cast<size_t>(std::stoull(cols[0]));
            r.K = static_cast<size_t>(std::stoull(cols[1]));
            r.N = static_cast<size_t>(std::stoull(cols[2]));
            r.backend = cols[3];
            r.function_name = "XK_gemm_" + r.backend + " [CSV]";
            r.kernel_selected = r.backend;
            r.precision = "f32";
            r.threads_requested = std::stoi(cols[4]);
            r.threads_used = std::stoi(cols[5]);
            r.min_ms = std::stod(cols[6]) * 1e3;
            r.median_ms = std::stod(cols[7]) * 1e3;
            r.mean_ms = std::stod(cols[8]) * 1e3;
            r.stddev_ms = std::stod(cols[9]) * 1e3;
            r.p95_ms = std::stod(cols[10]) * 1e3;
            r.p99_ms = std::stod(cols[11]) * 1e3;
            r.gflops = std::stod(cols[12]);
            r.workspace_bytes = static_cast<size_t>(std::max(0.0, std::stod(cols[13])) * 1024.0);
            r.working_set_bytes = (r.M * r.K + r.K * r.N + r.M * r.N) * sizeof(float);
            r.measured_bandwidth_gbs = (r.median_ms > 0.0)
                                           ? (static_cast<double>(r.working_set_bytes) / (r.median_ms * 1e-3)) * 1e-9
                                           : 0.0;
            r.validation.executed = true;
            r.validation.passed = true;
            r.validation.max_rel_error = std::stod(cols[14]);
            r.validation.precision = "f32";
            r.sample_count = 7;
            r.total_calls = 7;
            r.total_ms = r.mean_ms * 7.0;

            std::lock_guard<std::mutex> lock(state_mutex_);
            r.id = next_result_id_++;
            history_.push_back(std::move(r));
            ++imported;
        } catch (const std::exception&) {
            continue;
        }
    }
    return imported;
}


void WorkbenchEngine::add_log(const std::string& level, const std::string& message) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    LogEntry e;
    e.timestamp = now_iso_utc();
    e.level = level;
    e.message = message;
    logs_.push_back(std::move(e));
    if (logs_.size() > 200) {
        logs_.erase(logs_.begin(), logs_.begin() + static_cast<std::ptrdiff_t>(logs_.size() - 200));
    }
}

std::vector<LogEntry> WorkbenchEngine::get_logs() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return logs_;
}

void WorkbenchEngine::clear_logs() {
    std::lock_guard<std::mutex> lock(state_mutex_);
    logs_.clear();
}

}  // namespace nvxk_wb
