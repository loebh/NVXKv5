#ifndef NVXK_DISPATCHER_HPP
#define NVXK_DISPATCHER_HPP

#include <nvxk.h>
#include <nvxk/config.hpp>
#include <cstddef>
#include <string>

namespace nvxk {
namespace dispatch {

struct DispatchDecision {
    BackendKind selected_backend = BackendKind::Scalar;
    int threads = 1;
    bool pin_threads = false;
    bool fallback_occurred = false;
    double flops = 0.0;
    std::string reason;
    XK_ExecutionPlan native_plan;
};

// Explainable backend selection policy governed strictly by NVXK Core.
DispatchDecision select_gemm_backend(size_t M, size_t N, size_t K,
                                     size_t elem_bytes,
                                     const RuntimeConfig* override_cfg = nullptr);

DispatchDecision select_vector_backend(size_t n,
                                       const RuntimeConfig* override_cfg = nullptr);

// Empirically calibrate BLIS vs NVXK Native GEMM crossover threshold when BLIS
// is linked; returns calibrated FLOP threshold (or existing threshold if BLIS unavailable).
double calibrate_blis_crossover();

// Unified GEMM execution routed through NVXK Core Dispatcher.
DispatchDecision gemm_f32(XK_Layout layout, XK_Transpose trans_a, XK_Transpose trans_b,
                          size_t M, size_t N, size_t K,
                          float alpha, const float* A, size_t lda,
                          const float* B, size_t ldb,
                          float beta, float* C, size_t ldc,
                          const RuntimeConfig* override_cfg = nullptr,
                          XK_Workspace* workspace = nullptr);

DispatchDecision gemm_f64(XK_Layout layout, XK_Transpose trans_a, XK_Transpose trans_b,
                          size_t M, size_t N, size_t K,
                          double alpha, const double* A, size_t lda,
                          const double* B, size_t ldb,
                          double beta, double* C, size_t ldc,
                          const RuntimeConfig* override_cfg = nullptr,
                          XK_Workspace* workspace = nullptr);

// Convenience row-major C = A * B
DispatchDecision gemm(const float* A, const float* B, float* C,
                      size_t M, size_t K, size_t N,
                      const RuntimeConfig* override_cfg = nullptr,
                      XK_Workspace* workspace = nullptr);

DispatchDecision gemm(const double* A, const double* B, double* C,
                      size_t M, size_t K, size_t N,
                      const RuntimeConfig* override_cfg = nullptr,
                      XK_Workspace* workspace = nullptr);

}  // namespace dispatch
}  // namespace nvxk

#endif  // NVXK_DISPATCHER_HPP
