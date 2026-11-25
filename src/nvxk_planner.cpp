#include <nvxk.h>
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <vector>

#if defined(_OPENMP)
#include <omp.h>
#endif

namespace {

size_t round_down_multiple(size_t val, size_t mult, size_t min_val) {
    if (val < min_val) return min_val;
    return (val / mult) * mult;
}

size_t blis_workspace_bytes(size_t M, size_t K, size_t N,
                            size_t elem_bytes,
                            size_t mc, size_t kc, size_t nc,
                            int threads) {
    const size_t mr = 6;
    const size_t nr = (elem_bytes == sizeof(double)) ? 8 : 16;
    const size_t align_elems = (elem_bytes >= 64) ? 1 : (64 / elem_bytes);
    const size_t t_sz = static_cast<size_t>(std::max(1, threads));
    const size_t mc_cap = xk_detail::checked_round_up(std::min(M, mc), mr);
    const size_t kc_cap = std::min(K, kc);
    const size_t nc_cap = xk_detail::checked_round_up(std::min(N, nc), nr);
    const size_t b_bytes =
        xk_detail::checked_element_count(
            xk_detail::checked_element_count(kc_cap, nc_cap), elem_bytes);
    const size_t a_slot = xk_detail::checked_round_up(
        xk_detail::checked_element_count(mc_cap, kc_cap), align_elems);
    const size_t a_bytes =
        xk_detail::checked_element_count(
            xk_detail::checked_element_count(a_slot, t_sz), elem_bytes);
    if (b_bytes > std::numeric_limits<size_t>::max() - a_bytes) {
        return std::numeric_limits<size_t>::max();
    }
    return b_bytes + a_bytes;
}

bool compute_cache_blocking(size_t M, size_t K, size_t N,
                            size_t elem_bytes,
                            const XK_HardwareInfo& hw,
                            int threads,
                            size_t memory_budget_bytes,
                            size_t manual_mc, size_t manual_kc, size_t manual_nc,
                            size_t& out_mc, size_t& out_kc, size_t& out_nc,
                            bool& out_smt_l2_shared,
                            bool& out_budget_clamped) {
    const size_t mr = 6;
    const size_t nr = (elem_bytes == sizeof(double)) ? 8 : 16;
    out_budget_clamped = false;

    // 1. KC from L1d: (MR + NR) * KC * elem_bytes <= ~70% of L1d
    const size_t l1_budget = std::max(static_cast<size_t>(16 * 1024),
                                      (hw.l1d_bytes * 7) / 10);
    size_t kc = l1_budget / ((mr + nr) * elem_bytes);
    const size_t kc_max = (elem_bytes == sizeof(double)) ? 256 : 512;
    kc = std::min(std::max(kc, static_cast<size_t>(64)), kc_max);
    kc = round_down_multiple(kc, 16, 64);

    // 2. MC from effective L2 per thread (halve L2 budget when SMT threads share L2)
    out_smt_l2_shared = (threads > hw.physical_cores) && (hw.physical_cores > 0);
    const size_t eff_l2 = out_smt_l2_shared ? (hw.l2_bytes / 2) : hw.l2_bytes;
    const size_t l2_budget = std::max(static_cast<size_t>(32 * 1024),
                                      (eff_l2 * 55) / 100);
    size_t mc = l2_budget / (kc * elem_bytes);
    const size_t mc_max = (elem_bytes == sizeof(double)) ? 120 : 240;
    mc = std::min(std::max(mc, mr), mc_max);
    mc = round_down_multiple(mc, mr, mr);

    // 3. NC from L3: KC * NC * elem_bytes <= ~25% of L3
    const size_t l3_budget = std::max(static_cast<size_t>(256 * 1024),
                                      hw.l3_bytes / 4);
    size_t nc = l3_budget / (kc * elem_bytes);
    const size_t nc_max = (elem_bytes == sizeof(double)) ? 1024 : 2048;
    nc = std::min(std::max(nc, nr), nc_max);
    nc = round_down_multiple(nc, nr, nr);

    // Apply manual overrides BEFORE budget enforcement so memory budget is always respected
    if (manual_mc > 0) mc = std::max(mr, manual_mc);
    if (manual_kc > 0) kc = std::max(static_cast<size_t>(1), manual_kc);
    if (manual_nc > 0) nc = std::max(nr, manual_nc);

    // 4. Enforce explicit/workspace memory budget gracefully down to minimum tile (mr x 1 x nr)
    bool fits_budget = true;
    if (memory_budget_bytes > 0) {
        while (blis_workspace_bytes(M, K, N, elem_bytes, mc, kc, nc, threads) >
               memory_budget_bytes) {
            out_budget_clamped = true;
            if (nc > nr) {
                nc = round_down_multiple(nc / 2, nr, nr);
            } else if (mc > mr) {
                mc = round_down_multiple(mc / 2, mr, mr);
            } else if (kc > 16) {
                kc = round_down_multiple(kc / 2, 16, 16);
            } else if (kc > 1) {
                kc = std::max(static_cast<size_t>(1), kc / 2);
            } else {
                fits_budget = false;
                break;
            }
        }
    }

    out_mc = mc;
    out_kc = kc;
    out_nc = nc;
    return fits_budget;
}

XK_ParallelAxis choose_axis(size_t M, size_t N, size_t elem_bytes,
                            int threads, XK_ParallelAxis requested) {
    if (threads <= 1) return XK_ParallelAxis::None;
    if (requested == XK_ParallelAxis::AxisM || requested == XK_ParallelAxis::AxisN) {
        return requested;
    }
    const size_t nr = (elem_bytes == sizeof(double)) ? 8 : 16;
    const size_t m_panels = (M / 6) + ((M % 6) != 0 ? 1 : 0);
    const size_t n_slabs = (N / nr) + ((N % nr) != 0 ? 1 : 0);
    if (m_panels < static_cast<size_t>(threads) && n_slabs > m_panels) {
        return XK_ParallelAxis::AxisN;
    }
    return XK_ParallelAxis::AxisM;
}

double predict_time_us(size_t M, size_t K, size_t N,
                       size_t elem_bytes,
                       size_t mc, size_t kc, size_t nc,
                       int threads, XK_ParallelAxis axis,
                       bool is_thin_m,
                       const XK_HardwareProfile& profile,
                       double& out_omp_overhead_us) {
    const double flops = 2.0 * static_cast<double>(M) *
                         static_cast<double>(K) * static_cast<double>(N);
    const double total_bytes =
        (static_cast<double>(M) * K + static_cast<double>(K) * N +
         static_cast<double>(M) * N) *
        static_cast<double>(elem_bytes);

    // f64 AVX2 has half the FLOP/cycle throughput of f32 AVX2
    const double precision_scale = (elem_bytes == sizeof(double)) ? 0.5 : 1.0;
    const double peak_1t = std::max(1.0, profile.peak_gflops_1t * precision_scale);
    const double mem_bw_1t = std::max(1.0, profile.mem_bandwidth_gbs);

    const int phys = std::max(1, profile.hw.physical_cores);
    double eff_cores = 1.0;
    if (threads <= phys) {
        eff_cores = static_cast<double>(threads);
    } else {
        const int extra_smt = threads - phys;
        eff_cores = static_cast<double>(phys) +
                    static_cast<double>(extra_smt) * profile.smt_efficiency;
    }

    const double bw_scale =
        (threads <= 1) ? 1.0 : std::min(2.4, 1.0 + 0.70 * (eff_cores - 1.0));
    const double eff_bw = mem_bw_1t * bw_scale;

    const size_t nr = (elem_bytes == sizeof(double)) ? 8 : 16;
    double imbalance = 1.0;
    if (threads > 1) {
        const size_t work_units =
            (axis == XK_ParallelAxis::AxisN)
                ? ((N / nr) + ((N % nr) != 0 ? 1 : 0))
                : ((M / 6) + ((M % 6) != 0 ? 1 : 0));
        if (work_units == 0) {
            imbalance = static_cast<double>(threads);
        } else {
            const size_t max_per_thread =
                (work_units + static_cast<size_t>(threads) - 1) /
                static_cast<size_t>(threads);
            imbalance = (static_cast<double>(max_per_thread) * threads) /
                        static_cast<double>(work_units);
        }
    }

    const double k_blocks = std::max(1.0, std::ceil(static_cast<double>(K) / kc));
    const double n_blocks = std::max(1.0, std::ceil(static_cast<double>(N) / nc));
    const double kc_eff =
        (kc >= 128) ? 1.0 : (0.75 + 0.25 * (static_cast<double>(kc) / 128.0));

    const double eff_gflops = (peak_1t * eff_cores * kc_eff) / imbalance;
    const double t_compute_us = (flops / (eff_gflops * 1e9)) * 1e6;
    const double t_mem_us = (total_bytes / (eff_bw * 1e9)) * 1e6;

    double smt_penalty = 1.0;
    if (threads > phys && flops < 1e8) {
        smt_penalty = 1.25;
    }

    out_omp_overhead_us = 0.0;
    if (threads > 1) {
        const double thread_scale = 0.65 + 0.12 * static_cast<double>(threads);
        if (is_thin_m || axis == XK_ParallelAxis::AxisN) {
            const double num_barriers = is_thin_m ? 0.0 : std::max(0.0, n_blocks - 1.0);
            out_omp_overhead_us =
                (profile.omp_fork_overhead_us +
                 num_barriers * profile.omp_barrier_overhead_us) *
                thread_scale;
        } else {
            const double num_barriers = std::max(1.0, n_blocks * k_blocks * 2.0 - 1.0);
            const double coherency_us =
                32.0 * (eff_cores - 1.0) / std::max(1.0, eff_cores);
            out_omp_overhead_us =
                (profile.omp_fork_overhead_us +
                 num_barriers * profile.omp_barrier_overhead_us +
                 coherency_us) *
                thread_scale;
        }
    }

    (void)mc;
    return std::max(t_compute_us, t_mem_us) * smt_penalty + out_omp_overhead_us;
}

}  // namespace

XK_ExecutionPlan XK_plan_gemm(const XK_WorkloadDesc& workload,
                              const XK_HardwareProfile& profile) {
    XK_ExecutionPlan plan;
    const size_t M = workload.M;
    const size_t K = workload.K;
    const size_t N = workload.N;
    const size_t eb = (workload.elem_bytes == 0) ? sizeof(float) : workload.elem_bytes;

    const double flops = 2.0 * static_cast<double>(M) *
                         static_cast<double>(K) * static_cast<double>(N);
    const double bytes =
        (static_cast<double>(M) * K + static_cast<double>(K) * N +
         static_cast<double>(M) * N) *
        static_cast<double>(eb);
    const double ai = (bytes > 0.0) ? (flops / bytes) : 0.0;
    const double ridge =
        (profile.peak_gflops_1t * ((eb == sizeof(double)) ? 0.5 : 1.0)) /
        std::max(1.0, profile.mem_bandwidth_gbs);

    plan.explanation.arithmetic_intensity = ai;
    plan.explanation.is_compute_bound = (ai >= ridge);

    const bool use_avx2 = profile.hw.has_avx2 && profile.hw.has_fma && XK_has_avx2_fma();
    const bool use_thin_m =
        use_avx2 && (eb == sizeof(float)) &&
        (M >= 1 && M <= 4) && (N >= 16) && (K >= 8) &&
        (workload.mode == XK_ExecMode::Auto ||
         (workload.manual_mc == 0 && workload.manual_kc == 0 &&
          workload.manual_nc == 0 && workload.manual_axis != XK_ParallelAxis::AxisM));

    if (!use_avx2) {
        plan.kernel = XK_KernelKind::ScalarIkj;
    } else if (use_thin_m) {
        plan.kernel = XK_KernelKind::Avx2ThinM;
    } else if (workload.prefer_asm_kernel && eb == sizeof(float) &&
               XK_has_avx2_asm_kernel()) {
        plan.kernel = XK_KernelKind::Avx2Blis6x16Asm;
    } else {
        plan.kernel = XK_KernelKind::Avx2Blis6x16;
    }

    int max_threads = 1;
#if defined(_OPENMP)
    max_threads = std::max(1, std::min(profile.hw.logical_threads, omp_get_max_threads()));
#endif

    std::vector<int> candidates;
    if (workload.mode == XK_ExecMode::Manual) {
        const int req = std::max(1, workload.manual_threads);
        candidates.push_back(std::min(req, max_threads));
    } else {
        candidates.push_back(1);
        for (int t = 2; t <= profile.hw.physical_cores && t <= max_threads; t *= 2) {
            candidates.push_back(t);
        }
        if (profile.hw.physical_cores > 1 &&
            profile.hw.physical_cores <= max_threads &&
            std::find(candidates.begin(), candidates.end(), profile.hw.physical_cores) ==
                candidates.end()) {
            candidates.push_back(profile.hw.physical_cores);
        }
        if (max_threads > profile.hw.physical_cores) {
            candidates.push_back(max_threads);
        }
    }

    const bool needs_blis_ws =
        (plan.kernel == XK_KernelKind::Avx2Blis6x16 ||
         plan.kernel == XK_KernelKind::Avx2Blis6x16Asm);

    size_t mc1 = 120, kc1 = 256, nc1 = 1024;
    bool smt1 = false, clamp1 = false;
    const bool fits_1t = compute_cache_blocking(
        M, K, N, eb, profile.hw, 1, workload.memory_budget_bytes,
        workload.manual_mc, workload.manual_kc, workload.manual_nc,
        mc1, kc1, nc1, smt1, clamp1);

    double omp1 = 0.0;
    const double t1_us = predict_time_us(M, K, N, eb, mc1, kc1, nc1, 1,
                                         XK_ParallelAxis::None, use_thin_m,
                                         profile, omp1);

    int best_t = candidates[0];
    size_t best_mc = mc1, best_kc = kc1, best_nc = nc1;
    bool best_smt = false, best_clamped = clamp1;
    XK_ParallelAxis best_axis = XK_ParallelAxis::None;
    double best_time = 1e30;
    double best_omp = 0.0;
    bool found_fitting_blis = false;

    for (size_t i = 0; i < candidates.size(); ++i) {
        int t = candidates[i];
        size_t mc = 120, kc = 256, nc = 1024;
        bool smt = false, clamped = false;
        bool fits = compute_cache_blocking(
            M, K, N, eb, profile.hw, t, workload.memory_budget_bytes,
            workload.manual_mc, workload.manual_kc, workload.manual_nc,
            mc, kc, nc, smt, clamped);

        // In Manual mode under a tight budget, step down thread count until workspace fits
        if (needs_blis_ws && workload.mode == XK_ExecMode::Manual && !fits && t > 1) {
            while (t > 1 && !fits) {
                --t;
                fits = compute_cache_blocking(
                    M, K, N, eb, profile.hw, t, workload.memory_budget_bytes,
                    workload.manual_mc, workload.manual_kc, workload.manual_nc,
                    mc, kc, nc, smt, clamped);
            }
        }

        if (needs_blis_ws && workload.mode == XK_ExecMode::Auto && !fits && t > 1) {
            continue;
        }

        const XK_ParallelAxis axis =
            use_thin_m
                ? ((t > 1) ? XK_ParallelAxis::AxisN : XK_ParallelAxis::None)
                : choose_axis(M, N, eb, t, workload.manual_axis);
        double omp_us = 0.0;
        const double pred_us =
            predict_time_us(M, K, N, eb, mc, kc, nc, t, axis, use_thin_m,
                            profile, omp_us);

        // Require >= 25% predicted speedup over 1T before spawning threads in Auto mode
        if (workload.mode == XK_ExecMode::Auto && t > 1 && pred_us > t1_us / 1.25) {
            continue;
        }
        if (pred_us < best_time) {
            best_time = pred_us;
            best_t = t;
            best_mc = mc;
            best_kc = kc;
            best_nc = nc;
            best_smt = smt;
            best_clamped = clamped;
            best_axis = axis;
            best_omp = omp_us;
            if (fits) found_fitting_blis = true;
        }
    }

    // If even 1-thread minimal BLIS tile exceeds memory_budget_bytes, degrade gracefully to 0-workspace ScalarIkj
    if (needs_blis_ws && workload.memory_budget_bytes > 0 && !found_fitting_blis && !fits_1t) {
        plan.kernel = XK_KernelKind::ScalarIkj;
        best_clamped = true;
    }

    plan.threads = best_t;
    plan.axis = best_axis;
    plan.mc = best_mc;
    plan.kc = best_kc;
    plan.nc = best_nc;

    if (plan.kernel == XK_KernelKind::Avx2ThinM || plan.kernel == XK_KernelKind::ScalarIkj) {
        plan.required_workspace_bytes = 0;
    } else {
        plan.required_workspace_bytes =
            blis_workspace_bytes(M, K, N, eb, best_mc, best_kc, best_nc, best_t);
    }

    plan.explanation.predicted_1t_time_us = t1_us;
    plan.explanation.predicted_time_us = best_time;
    plan.explanation.predicted_omp_overhead_us = best_omp;
    plan.explanation.smt_l2_shared = best_smt;
    plan.explanation.budget_clamped = best_clamped;

    const char* kernel_name = "Scalar IKJ";
    if (plan.kernel == XK_KernelKind::Avx2ThinM) {
        kernel_name = "AVX2+FMA Thin-M Streaming (0-pack)";
    } else if (plan.kernel == XK_KernelKind::Avx2Blis6x16Asm) {
        kernel_name = "AVX2+FMA BLIS 6x16 (ASM)";
    } else if (plan.kernel == XK_KernelKind::Avx2Blis6x16) {
        kernel_name = (eb == sizeof(double)) ? "AVX2+FMA BLIS 6x8 (f64)"
                                             : "AVX2+FMA BLIS 6x16";
    }

    std::ostringstream oss;
    oss << (workload.mode == XK_ExecMode::Manual ? "[Manual] " : "[Auto] ")
        << kernel_name
        << " | threads=" << best_t
        << " (phys=" << profile.hw.physical_cores
        << ", log=" << profile.hw.logical_threads << ")"
        << " | axis="
        << (best_axis == XK_ParallelAxis::AxisM
                ? "M"
                : (best_axis == XK_ParallelAxis::AxisN ? "N" : "None"))
        << " | block=(" << best_mc << "," << best_kc << "," << best_nc << ")"
        << " | AI=" << std::fixed << std::setprecision(1) << ai << " FLOP/B ("
        << (plan.explanation.is_compute_bound ? "compute-bound" : "memory-bound")
        << ")";
    if (best_smt) oss << " | SMT L2-halved";
    if (best_clamped) oss << " | budget-clamped";
    if (best_t == 1 && max_threads > 1 && workload.mode == XK_ExecMode::Auto) {
        oss << " | stayed 1T (OMP/coherency overhead dominates)";
    }
    plan.explanation.reason = oss.str();

    return plan;
}
