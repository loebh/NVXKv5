#include "insights_engine.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace nvxk_wb {

namespace {

std::string fmt_dbl(double v, int prec = 2) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(prec) << v;
    return oss.str();
}

std::string fmt_sci(double v) {
    std::ostringstream oss;
    oss << std::scientific << std::setprecision(2) << v;
    return oss.str();
}

}  // namespace

std::vector<EngineeringInsight> generate_engineering_insights(
    const DiagnosticsReport& diag,
    const std::vector<BenchmarkResultRow>& history) {
    std::vector<EngineeringInsight> out;
    int seq = 1;
    auto next_id = [&]() { return "INS-" + std::to_string(seq++); };

    // 1. CPU / ISA Diagnostics Insights
    if (!XK_has_avx2_fma()) {
        out.push_back({
            next_id(), "CPU",
            "Runtime dispatch fell back to Scalar IKJ",
            "warning", "Confirmed",
            "XK_has_avx2_fma() = false",
            "Either host CPU lacks AVX2+FMA or binary was compiled with XK_FORCE_SCALAR_FALLBACK / MSVC.",
            "Verify CPU flags in CPU Architecture tab and build with MinGW/GCC (MSYS2 UCRT64) without XK_FORCE_SCALAR_FALLBACK."
        });
    } else if (!diag.profile.calibrated) {
        out.push_back({
            next_id(), "CPU",
            "Hardware profile is using default estimates (uncalibrated)",
            "info", "Informational",
            "peak_1t=" + fmt_dbl(diag.profile.peak_gflops_1t, 1) + " GF (default), BW=" +
                fmt_dbl(diag.profile.mem_bandwidth_gbs, 1) + " GB/s",
            "Adaptive Planner thread and cache-blocking thresholds are using uncalibrated defaults.",
            "Run 'Calibrate Hardware' (~200 ms) from the top bar to measure actual 1T peak, RAM bandwidth, and OpenMP fork cost."
        });
    }

    if (diag.profile.hw.has_avx512f) {
        out.push_back({
            next_id(), "CPU",
            "Host CPU exposes AVX-512F; NVXK intentionally targets 256-bit AVX2+FMA",
            "info", "Confirmed",
            "CPUID AVX-512F=Yes, NVXK binary AVX-512=No",
            "NVXK v2 scopes SIMD kernels to 256-bit AVX2+FMA for Intel Core i7-7700 target parity.",
            "When comparing against AVX-512 BLAS libraries on Xeon CPUs, expect ~2x narrower vector width on compute-bound square GEMM."
        });
    }

    if (history.empty()) {
        out.push_back({
            next_id(), "Performance",
            "Awaiting benchmark measurements",
            "info", "Unverified",
            "0 measured runs in current session",
            "No computational kernels have been benchmarked yet in this session.",
            "Launch a benchmark from Overview or Performance Benchmark to populate KPIs, charts, and comparative insights."
        });
        return out;
    }

    // 2. Numerical Reliability Checks (highest priority)
    size_t validated_count = 0;
    size_t fail_count = 0;
    size_t nan_total = 0;
    double worst_rel = 0.0;
    std::string worst_fn;
    for (const auto& r : history) {
        if (r.validation.executed) {
            ++validated_count;
            if (!r.validation.passed) ++fail_count;
            nan_total += r.validation.nan_inf_count;
            if (r.validation.max_rel_error > worst_rel) {
                worst_rel = r.validation.max_rel_error;
                worst_fn = r.function_name;
            }
        }
    }
    if (nan_total > 0 || fail_count > 0) {
        out.push_back({
            next_id(), "Numerical Reliability",
            "Numerical validation failure detected",
            "critical", "Confirmed",
            std::to_string(fail_count) + " failed run(s), " + std::to_string(nan_total) + " NaN/Inf",
            "Worst max_rel_error=" + fmt_sci(worst_rel) + " in " + worst_fn + " exceeded tolerance against f64 reference.",
            "Open Numerical Reliability tab to inspect the error heatmap, input seed, and failing tile coordinates."
        });
    } else if (validated_count > 0) {
        out.push_back({
            next_id(), "Numerical Reliability",
            "Measured runs within f64 reference tolerance",
            "positive", "Confirmed",
            std::to_string(validated_count) + " validated run(s), worst max_rel_err = " + fmt_sci(worst_rel),
            "All validated runs in this session passed IEEE-754 reference comparison with 0 NaN/Inf values.",
            "Passing benchmark shapes does not prove edge-case correctness; run the 7-shape Edge-Case Suite in Numerical Reliability."
        });
    } else {
        out.push_back({
            next_id(), "Numerical Reliability",
            "Benchmark runs have not been numerically validated",
            "info", "Unverified",
            "0 of " + std::to_string(history.size()) + " run(s) validated",
            "Validation was disabled for recorded runs; numerical accuracy against f64 reference is unverified.",
            "Re-run with validation enabled or launch the 7-shape Edge-Case Suite in Numerical Reliability."
        });
    }

    // 3. Performance & Variance Analysis on latest session
    const std::string& latest_sess = history.back().session_name;
    const BenchmarkResultRow* best_row = nullptr;
    const BenchmarkResultRow* scalar_row = nullptr;
    const BenchmarkResultRow* asm_row = nullptr;
    const BenchmarkResultRow* int_row = nullptr;

    for (const auto& r : history) {
        if (r.session_name != latest_sess) continue;
        if (!best_row || r.gflops > best_row->gflops) best_row = &r;
        if (r.backend == "ikj" || r.backend == "naive") scalar_row = &r;
        if (r.backend == "auto_asm" || r.backend == "avx2_asm") asm_row = &r;
        if (r.backend == "auto" || r.backend == "avx2") int_row = &r;

        // Check high latency jitter (CV > 10%)
        if (r.mean_ms > 0.0 && r.sample_count >= 3) {
            const double cv = (r.stddev_ms / r.mean_ms) * 100.0;
            if (cv > 10.0) {
                out.push_back({
                    next_id(), "Performance",
                    "Elevated latency variance on " + r.backend,
                    "warning", "Suspected",
                    "CV = " + fmt_dbl(cv, 1) + "% (std=" + fmt_dbl(r.stddev_ms, 3) + " ms)",
                    "Sample-to-sample variance exceeded 10% across " + std::to_string(r.sample_count) + " runs.",
                    "Correlated with OS scheduling or CPU frequency scaling (not proven cause); re-run in Measurement Mode with >= 9 runs."
                });
            }
        }

        // Check memory budget clamping
        if (r.budget_clamped) {
            out.push_back({
                next_id(), "Memory",
                "Adaptive Planner clamped block sizes to fit memory budget",
                "info", "Confirmed",
                "Workspace=" + fmt_dbl(static_cast<double>(r.workspace_bytes) / 1024.0, 1) + " KiB, blocks=(" +
                    std::to_string(r.mc) + "," + std::to_string(r.kc) + "," + std::to_string(r.nc) + ")",
                "Graceful degradation prevented OOM by shrinking (NC, MC, KC) within configured workspace budget.",
                "Inspect Memory Analysis tab to evaluate throughput vs workspace budget trade-off."
            });
        }

        // Check zero-pack Thin-M activation
        if (r.kernel_selected == "Avx2ThinM") {
            out.push_back({
                next_id(), "Memory",
                "Zero-pack Avx2ThinM streaming kernel activated",
                "positive", "Confirmed",
                "Workspace = 0 KiB | " + fmt_dbl(r.gflops, 2) + " GFLOPS",
                "Thin-M shape (M=" + std::to_string(r.M) + ") bypassed B-matrix packing and inner-loop OpenMP barriers.",
                "Compare against manual BLIS packing to verify zero-pack streaming advantage on memory-bound shapes."
            });
        }

        // Check working set vs L3 cache
        if (r.working_set_bytes > diag.profile.hw.l3_bytes && r.working_set_bytes > 0) {
            out.push_back({
                next_id(), "Memory",
                "Workload working set exceeds L3 cache capacity",
                "warning", "Confirmed",
                "Working set=" + fmt_dbl(static_cast<double>(r.working_set_bytes) / (1024.0 * 1024.0), 2) +
                    " MiB > L3=" + fmt_dbl(static_cast<double>(diag.profile.hw.l3_bytes) / (1024.0 * 1024.0), 1) + " MiB",
                "Input/output matrices spill to main DRAM; cache blocking (MC, KC, NC) is critical for reuse.",
                "Test --cold-cache mode to measure DRAM-bound latency and verify measured effective bandwidth."
            });
        }
    }

    if (best_row && scalar_row && best_row != scalar_row && scalar_row->median_ms > 0.0) {
        const double sp = scalar_row->median_ms / best_row->median_ms;
        out.push_back({
            next_id(), "Performance",
            "Measured " + fmt_dbl(sp, 2) + "x speedup over scalar " + scalar_row->backend,
            "positive", "Confirmed",
            best_row->backend + ": " + fmt_dbl(best_row->gflops, 2) + " GF vs " +
                scalar_row->backend + ": " + fmt_dbl(scalar_row->gflops, 2) + " GF",
            "Measured on " + std::to_string(best_row->M) + "x" + std::to_string(best_row->K) +
                "x" + std::to_string(best_row->N) + " (" + best_row->precision + ", " +
                std::to_string(best_row->threads_used) + "T).",
            "Inspect Top Functions and Kernel Analysis for per-backend latency percentiles."
        });
    }

    if (asm_row && int_row && int_row->gflops > 0.0) {
        const double delta_pct = ((asm_row->gflops - int_row->gflops) / int_row->gflops) * 100.0;
        out.push_back({
            next_id(), "Performance",
            std::string(delta_pct >= 0.0 ? "Inline ASM outperformed C++ intrinsics by +"
                                         : "C++ intrinsics outperformed Inline ASM by ") +
                fmt_dbl(std::fabs(delta_pct), 1) + "%",
            delta_pct >= 0.0 ? "positive" : "info",
            "Confirmed",
            asm_row->backend + "=" + fmt_dbl(asm_row->gflops, 2) + " GF vs " +
                int_row->backend + "=" + fmt_dbl(int_row->gflops, 2) + " GF",
            "Direct comparison within session " + latest_sess + " on identical matrices.",
            "Verify whether delta exceeds 2x combined stddev before concluding microarchitectural gain."
        });
    }

    return out;
}

}  // namespace nvxk_wb
