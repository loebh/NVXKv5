#include <nvxk/dispatcher.hpp>
#include <nvxk/blas_backend.hpp>
#include <nvxk/simd.hpp>
#include <nvxk/topology.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <sstream>
#include <vector>

namespace nvxk {
namespace dispatch {

namespace {

int resolve_thread_budget(const RuntimeConfig& cfg, int planned_threads) {
    const auto& topo = topology::cached_topology();
    int t = (cfg.max_threads > 0) ? cfg.max_threads : planned_threads;
    t = std::max(1, t);
    if (topo.logical_pus > 0) {
        t = std::min(t, topo.logical_pus);
    }
    return t;
}

void maybe_pin_team_once(int threads, bool pin_enabled) {
    if (!pin_enabled || threads <= 1) return;
    static std::atomic<int> last_pinned_threads{0};
    if (last_pinned_threads.load(std::memory_order_relaxed) != threads) {
        if (topology::pin_openmp_team(threads) > 0) {
            last_pinned_threads.store(threads, std::memory_order_relaxed);
        }
    }
}

void log_trace(const DispatchDecision& dec, size_t M, size_t N, size_t K, size_t eb) {
    std::fprintf(stderr,
                 "[NVXK::Dispatch] GEMM(%zux%zux%zu, %zuB) -> backend=%s threads=%d pin=%d fallback=%d | %s\n",
                 M, N, K, eb, backend_kind_name(dec.selected_backend),
                 dec.threads, dec.pin_threads ? 1 : 0, dec.fallback_occurred ? 1 : 0,
                 dec.reason.c_str());
}

}  // namespace

double calibrate_blis_crossover() {
    RuntimeConfig cfg = get_runtime_config();
    const auto blis_info = blas::query_info();
    if (!blis_info.available) {
        return cfg.blis_gemm_flops_threshold;
    }

    const size_t sizes[4] = {64, 128, 256, 512};
    const int threads = std::max(1, topology::cached_topology().physical_cores);
    double crossover = 1e18;  // stay NVXK native unless BLIS wins
    XK_Workspace ws(32 * 1024 * 1024);

    for (size_t n : sizes) {
        std::vector<float> A(n * n, 0.5f), B(n * n, 0.25f), C(n * n, 0.0f);
        XK_WorkloadDesc wd(n, n, n, sizeof(float));
        wd.mode = XK_ExecMode::Manual;
        wd.manual_threads = threads;

        // Warm-up
        XK_gemm_blas(XK_Layout::RowMajor, XK_Transpose::NoTrans, XK_Transpose::NoTrans,
                     n, n, n, 1.0f, A.data(), n, B.data(), n, 0.0f, C.data(), n, &wd, &ws);
        blas::blis_gemm_f32(XK_Layout::RowMajor, XK_Transpose::NoTrans, XK_Transpose::NoTrans,
                            n, n, n, 1.0f, A.data(), n, B.data(), n, 0.0f, C.data(), n, threads);

        auto t0 = std::chrono::steady_clock::now();
        for (int it = 0; it < 3; ++it) {
            XK_gemm_blas(XK_Layout::RowMajor, XK_Transpose::NoTrans, XK_Transpose::NoTrans,
                         n, n, n, 1.0f, A.data(), n, B.data(), n, 0.0f, C.data(), n, &wd, &ws);
        }
        auto t1 = std::chrono::steady_clock::now();
        for (int it = 0; it < 3; ++it) {
            blas::blis_gemm_f32(XK_Layout::RowMajor, XK_Transpose::NoTrans, XK_Transpose::NoTrans,
                                n, n, n, 1.0f, A.data(), n, B.data(), n, 0.0f, C.data(), n, threads);
        }
        auto t2 = std::chrono::steady_clock::now();

        const double native_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        const double blis_ms = std::chrono::duration<double, std::milli>(t2 - t1).count();
        if (blis_ms < native_ms * 0.95) {
            crossover = 2.0 * static_cast<double>(n) * static_cast<double>(n) * static_cast<double>(n);
            break;
        }
    }

    cfg.blis_gemm_flops_threshold = crossover;
    cfg.blis_crossover_calibrated = true;
    set_runtime_config(cfg);
    return crossover;
}

DispatchDecision select_gemm_backend(size_t M, size_t N, size_t K,
                                     size_t elem_bytes,
                                     const RuntimeConfig* override_cfg) {
    const RuntimeConfig cfg = override_cfg ? *override_cfg : get_runtime_config();
    DispatchDecision dec;
    dec.flops = 2.0 * static_cast<double>(M) * static_cast<double>(N) * static_cast<double>(K);

    XK_WorkloadDesc wd(M, K, N, elem_bytes);
    wd.memory_budget_bytes = cfg.memory_budget_bytes;
    if (cfg.max_threads > 0) {
        wd.mode = XK_ExecMode::Manual;
        wd.manual_threads = cfg.max_threads;
    }
    const XK_HardwareProfile prof = XK_get_active_profile();
    dec.native_plan = XK_plan_gemm(wd, prof);
    dec.threads = resolve_thread_budget(cfg, dec.native_plan.threads);

    const auto& topo = topology::cached_topology();
    dec.pin_threads = cfg.enable_hwloc_affinity && topo.affinity_supported && dec.threads > 1;

    const auto blis_info = blas::query_info();
    const bool has_native_avx2 = prof.hw.has_avx2 && prof.hw.has_fma && XK_has_avx2_fma();

    if (cfg.gemm_backend == BackendKind::Scalar) {
        dec.selected_backend = BackendKind::Scalar;
        dec.reason = "Explicitly forced to Scalar reference backend via RuntimeConfig/NVXK_BACKEND";
        return dec;
    }
    if (cfg.gemm_backend == BackendKind::Blis) {
        if (blis_info.available) {
            dec.selected_backend = BackendKind::Blis;
            dec.reason = "Explicitly requested BLIS backend (available: " + blis_info.version + ")";
            return dec;
        }
        dec.fallback_occurred = true;
        dec.selected_backend = has_native_avx2 ? BackendKind::NvxkNative : BackendKind::Scalar;
        dec.reason = "Requested BLIS backend unavailable; safely falling back to " +
                     std::string(backend_kind_name(dec.selected_backend));
        return dec;
    }
    if (cfg.gemm_backend == BackendKind::NvxkNative) {
        dec.selected_backend = has_native_avx2 ? BackendKind::NvxkNative : BackendKind::Scalar;
        dec.fallback_occurred = !has_native_avx2;
        dec.reason = has_native_avx2
                         ? "Explicitly requested NVXK native AVX2/BLIS-style kernel"
                         : "NVXK native AVX2 unavailable on host CPU; safely falling back to Scalar";
        return dec;
    }

    if (M == 0 || N == 0 || K == 0 || dec.flops < cfg.small_gemm_flops_threshold) {
        dec.selected_backend = BackendKind::Scalar;
        dec.threads = 1;
        dec.pin_threads = false;
        dec.reason = "Tiny workload (< small_gemm_flops_threshold); Scalar avoids packing/dispatch overhead";
        return dec;
    }

    if (has_native_avx2 && elem_bytes == sizeof(float) && M <= 4 && N >= 16 && K >= 8) {
        dec.selected_backend = BackendKind::NvxkNative;
        dec.reason = "Thin-M workload (M<=4); routing to NVXK native AVX2 Thin-M kernel";
        return dec;
    }

    if (blis_info.available && dec.flops >= cfg.blis_gemm_flops_threshold) {
        dec.selected_backend = BackendKind::Blis;
        dec.reason = "Large GEMM (>= blis_gemm_flops_threshold) with BLIS linked; routing to BLIS";
        return dec;
    }

    if (has_native_avx2) {
        dec.selected_backend = BackendKind::NvxkNative;
        dec.reason = "Routing to NVXK native AVX2+FMA 6x16/6x8 kernel (" + dec.native_plan.explanation.reason + ")";
        return dec;
    }

    dec.selected_backend = BackendKind::Scalar;
    dec.reason = "No AVX2+FMA or BLIS available; routing to portable Scalar reference kernel";
    return dec;
}

DispatchDecision select_vector_backend(size_t n, const RuntimeConfig* override_cfg) {
    const RuntimeConfig cfg = override_cfg ? *override_cfg : get_runtime_config();
    DispatchDecision dec;
    dec.threads = 1;
    dec.flops = static_cast<double>(n);
    const auto simd_info = simd::query_info();

    if (cfg.vector_backend == BackendKind::Scalar) {
        dec.selected_backend = BackendKind::Scalar;
        dec.reason = "Explicitly forced Scalar vector backend";
        return dec;
    }
    if (simd_info.highway_active &&
        (cfg.vector_backend == BackendKind::Highway || n >= cfg.highway_min_elems_threshold)) {
        dec.selected_backend = BackendKind::Highway;
        dec.reason = "Google Highway dynamic dispatch (" + simd_info.active_target + ")";
        return dec;
    }
    dec.selected_backend = BackendKind::Scalar;
    dec.fallback_occurred = (cfg.vector_backend == BackendKind::Highway && !simd_info.highway_active);
    dec.reason = simd_info.highway_active
                     ? "Short vector (< highway_min_elems_threshold); Scalar avoids dispatch overhead"
                     : "Highway not enabled; Scalar reference fallback";
    return dec;
}

DispatchDecision gemm_f32(XK_Layout layout, XK_Transpose trans_a, XK_Transpose trans_b,
                          size_t M, size_t N, size_t K,
                          float alpha, const float* A, size_t lda,
                          const float* B, size_t ldb,
                          float beta, float* C, size_t ldc,
                          const RuntimeConfig* override_cfg,
                          XK_Workspace* workspace) {
    const RuntimeConfig cfg = override_cfg ? *override_cfg : get_runtime_config();
    DispatchDecision dec = select_gemm_backend(M, N, K, sizeof(float), &cfg);
    maybe_pin_team_once(dec.threads, dec.pin_threads);

    if (dec.selected_backend == BackendKind::Blis) {
        const bool ok = blas::blis_gemm_f32(layout, trans_a, trans_b, M, N, K,
                                            alpha, A, lda, B, ldb, beta, C, ldc,
                                            dec.threads);
        if (ok) {
            if (cfg.enable_trace) log_trace(dec, M, N, K, sizeof(float));
            return dec;
        }
        dec.fallback_occurred = true;
        dec.selected_backend = BackendKind::NvxkNative;
        dec.reason += " [BLIS runtime fallback -> NVXK Native]";
    }

    XK_WorkloadDesc wd(M, K, N, sizeof(float));
    wd.memory_budget_bytes = cfg.memory_budget_bytes;
    if (dec.selected_backend == BackendKind::Scalar) {
        XK_HardwareProfile sp = XK_get_active_profile();
        sp.hw.has_avx2 = false;
        sp.hw.has_fma = false;
        wd.mode = XK_ExecMode::Manual;
        wd.manual_threads = dec.threads;
        dec.native_plan = XK_gemm_blas(layout, trans_a, trans_b, M, N, K,
                                       alpha, A, lda, B, ldb, beta, C, ldc,
                                       &wd, workspace, &sp, &dec.threads);
    } else {
        if (cfg.max_threads > 0) {
            wd.mode = XK_ExecMode::Manual;
            wd.manual_threads = dec.threads;
        }
        dec.native_plan = XK_gemm_blas(layout, trans_a, trans_b, M, N, K,
                                       alpha, A, lda, B, ldb, beta, C, ldc,
                                       &wd, workspace, nullptr, &dec.threads);
    }

    if (cfg.enable_trace) log_trace(dec, M, N, K, sizeof(float));
    return dec;
}

DispatchDecision gemm_f64(XK_Layout layout, XK_Transpose trans_a, XK_Transpose trans_b,
                          size_t M, size_t N, size_t K,
                          double alpha, const double* A, size_t lda,
                          const double* B, size_t ldb,
                          double beta, double* C, size_t ldc,
                          const RuntimeConfig* override_cfg,
                          XK_Workspace* workspace) {
    const RuntimeConfig cfg = override_cfg ? *override_cfg : get_runtime_config();
    DispatchDecision dec = select_gemm_backend(M, N, K, sizeof(double), &cfg);
    maybe_pin_team_once(dec.threads, dec.pin_threads);

    if (dec.selected_backend == BackendKind::Blis) {
        const bool ok = blas::blis_gemm_f64(layout, trans_a, trans_b, M, N, K,
                                            alpha, A, lda, B, ldb, beta, C, ldc,
                                            dec.threads);
        if (ok) {
            if (cfg.enable_trace) log_trace(dec, M, N, K, sizeof(double));
            return dec;
        }
        dec.fallback_occurred = true;
        dec.selected_backend = BackendKind::NvxkNative;
        dec.reason += " [BLIS runtime fallback -> NVXK Native]";
    }

    XK_WorkloadDesc wd(M, K, N, sizeof(double));
    wd.memory_budget_bytes = cfg.memory_budget_bytes;
    if (dec.selected_backend == BackendKind::Scalar) {
        XK_HardwareProfile sp = XK_get_active_profile();
        sp.hw.has_avx2 = false;
        sp.hw.has_fma = false;
        wd.mode = XK_ExecMode::Manual;
        wd.manual_threads = dec.threads;
        dec.native_plan = XK_gemm_blas(layout, trans_a, trans_b, M, N, K,
                                       alpha, A, lda, B, ldb, beta, C, ldc,
                                       &wd, workspace, &sp, &dec.threads);
    } else {
        if (cfg.max_threads > 0) {
            wd.mode = XK_ExecMode::Manual;
            wd.manual_threads = dec.threads;
        }
        dec.native_plan = XK_gemm_blas(layout, trans_a, trans_b, M, N, K,
                                       alpha, A, lda, B, ldb, beta, C, ldc,
                                       &wd, workspace, nullptr, &dec.threads);
    }

    if (cfg.enable_trace) log_trace(dec, M, N, K, sizeof(double));
    return dec;
}

DispatchDecision gemm(const float* A, const float* B, float* C,
                      size_t M, size_t K, size_t N,
                      const RuntimeConfig* override_cfg,
                      XK_Workspace* workspace) {
    return gemm_f32(XK_Layout::RowMajor, XK_Transpose::NoTrans, XK_Transpose::NoTrans,
                    M, N, K, 1.0f, A, K, B, N, 0.0f, C, N, override_cfg, workspace);
}

DispatchDecision gemm(const double* A, const double* B, double* C,
                      size_t M, size_t K, size_t N,
                      const RuntimeConfig* override_cfg,
                      XK_Workspace* workspace) {
    return gemm_f64(XK_Layout::RowMajor, XK_Transpose::NoTrans, XK_Transpose::NoTrans,
                    M, N, K, 1.0, A, K, B, N, 0.0, C, N, override_cfg, workspace);
}

}  // namespace dispatch
}  // namespace nvxk
