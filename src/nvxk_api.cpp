#include "nvxk_internal.h"
#include <nvxk_c.h>
#include <algorithm>
#include <cstring>
#include <vector>
#include <new>
#include <stdexcept>
#include <string>

#ifdef NVXK_USE_OPENBLAS
#include <cblas.h>
#include <climits>
#endif

namespace xk_internal {

XK_Workspace& default_thread_workspace() {
    static thread_local XK_Workspace tl_ws(64 * 1024 * 1024);
    return tl_ws;
}

}  // namespace xk_internal

namespace {

thread_local std::string g_last_c_error;

void set_c_error(const char* msg) {
    g_last_c_error = msg ? msg : "";
}

size_t resolve_effective_budget(size_t workload_budget, const XK_Workspace& ws) {
    const size_t ws_budget = ws.memory_budget_bytes();
    if (workload_budget == 0) return ws_budget;
    if (ws_budget == 0) return workload_budget;
    return std::min(workload_budget, ws_budget);
}

}  // namespace

// ============================================================================
// Vector Add APIs
// ============================================================================
void vector_add_avx2(const float* a, const float* b, float* c, size_t n) {
    if (n == 0) return;
    if (a == nullptr || b == nullptr || c == nullptr) {
        throw std::invalid_argument("vector_add_avx2: null vector pointer");
    }
    xk_internal::vector_add_avx2_f32_dispatch(a, b, c, n);
}

void vector_add_avx2(const double* a, const double* b, double* c, size_t n) {
    if (n == 0) return;
    if (a == nullptr || b == nullptr || c == nullptr) {
        throw std::invalid_argument("vector_add_avx2: null vector pointer");
    }
    xk_internal::vector_add_avx2_f64_dispatch(a, b, c, n);
}

// ============================================================================
// Adaptive Execution Entrypoints (f32 & f64)
// ============================================================================
XK_ExecutionPlan XK_gemm_ex(const float* A, const float* B, float* C,
                            const XK_WorkloadDesc& workload,
                            XK_Workspace* workspace,
                            const XK_HardwareProfile* profile,
                            int* actual_thread_count) {
    const size_t M = workload.M;
    const size_t K = workload.K;
    const size_t N = workload.N;
    xk_detail::checked_element_count(M, K);
    xk_detail::checked_element_count(K, N);
    const size_t c_count = xk_detail::checked_element_count(M, N);
    if (actual_thread_count != nullptr) *actual_thread_count = 1;

    XK_Workspace& ws = workspace ? *workspace : xk_internal::default_thread_workspace();
    XK_WorkloadDesc eff_desc = workload;
    eff_desc.elem_bytes = sizeof(float);
    eff_desc.memory_budget_bytes =
        resolve_effective_budget(workload.memory_budget_bytes, ws);

    const XK_HardwareProfile active =
        profile ? *profile : XK_get_active_profile();
    XK_ExecutionPlan plan = XK_plan_gemm(eff_desc, active);

    if (M == 0 || N == 0) return plan;
    xk_detail::validate_ptrs(A, B, C, M, K, N, "XK_gemm_ex");
    xk_detail::validate_no_aliasing(A, B, C, M, K, N,
                                    sizeof(float), sizeof(float), sizeof(float),
                                    "XK_gemm_ex");
    if (K == 0) {
        for (size_t i = 0; i < c_count; ++i) C[i] = 0.0f;
        return plan;
    }

    if (plan.kernel == XK_KernelKind::Avx2ThinM && XK_has_avx2_fma()) {
        xk_internal::gemm_avx2_thin_m_f32(A, B, C, M, K, N, plan.threads,
                                          actual_thread_count);
        return plan;
    }

    if ((plan.kernel == XK_KernelKind::Avx2Blis6x16 ||
         plan.kernel == XK_KernelKind::Avx2Blis6x16Asm) &&
        XK_has_avx2_fma()) {
        const bool use_asm = (plan.kernel == XK_KernelKind::Avx2Blis6x16Asm);
        if (plan.threads > 1) {
            xk_internal::gemm_avx2_blis_f32_omp(
                A, B, C, M, K, N, plan.mc, plan.kc, plan.nc,
                plan.threads, plan.axis, ws, actual_thread_count, use_asm);
        } else {
            xk_internal::gemm_avx2_blis_f32(
                A, B, C, M, K, N, plan.mc, plan.kc, plan.nc, ws, use_asm);
        }
        return plan;
    }

    xk_internal::scalar_gemm_f32(A, B, C, M, K, N, plan.threads, actual_thread_count);
    return plan;
}

XK_ExecutionPlan XK_gemm_f64_ex(const double* A, const double* B, double* C,
                                const XK_WorkloadDesc& workload,
                                XK_Workspace* workspace,
                                const XK_HardwareProfile* profile,
                                int* actual_thread_count) {
    const size_t M = workload.M;
    const size_t K = workload.K;
    const size_t N = workload.N;
    xk_detail::checked_element_count(M, K);
    xk_detail::checked_element_count(K, N);
    const size_t c_count = xk_detail::checked_element_count(M, N);
    if (actual_thread_count != nullptr) *actual_thread_count = 1;

    XK_Workspace& ws = workspace ? *workspace : xk_internal::default_thread_workspace();
    XK_WorkloadDesc eff_desc = workload;
    eff_desc.elem_bytes = sizeof(double);
    eff_desc.memory_budget_bytes =
        resolve_effective_budget(workload.memory_budget_bytes, ws);

    const XK_HardwareProfile active =
        profile ? *profile : XK_get_active_profile();
    XK_ExecutionPlan plan = XK_plan_gemm(eff_desc, active);

    if (M == 0 || N == 0) return plan;
    xk_detail::validate_ptrs(A, B, C, M, K, N, "XK_gemm_f64_ex");
    xk_detail::validate_no_aliasing(A, B, C, M, K, N,
                                    sizeof(double), sizeof(double), sizeof(double),
                                    "XK_gemm_f64_ex");
    if (K == 0) {
        for (size_t i = 0; i < c_count; ++i) C[i] = 0.0;
        return plan;
    }

    if (plan.kernel == XK_KernelKind::Avx2Blis6x16 && XK_has_avx2_fma()) {
        if (plan.threads > 1) {
            xk_internal::gemm_avx2_blis_f64_omp(
                A, B, C, M, K, N, plan.mc, plan.kc, plan.nc,
                plan.threads, plan.axis, ws, actual_thread_count);
        } else {
            xk_internal::gemm_avx2_blis_f64(
                A, B, C, M, K, N, plan.mc, plan.kc, plan.nc, ws);
        }
        return plan;
    }

    xk_internal::scalar_gemm_f64(A, B, C, M, K, N, plan.threads, actual_thread_count);
    return plan;
}


// ============================================================================
// Generalized BLAS-Style GEMM (v3.0+): C = alpha * op(A) * op(B) + beta * C
// ============================================================================
namespace {

size_t checked_strided_span_bytes(size_t outer_dim, size_t inner_dim,
                                  size_t ld, size_t elem_bytes,
                                  const char* label) {
    if (outer_dim == 0 || inner_dim == 0) return 0;
    if (ld < inner_dim) {
        throw std::invalid_argument(std::string(label) + ": leading dimension is smaller than inner dimension");
    }
    const size_t full_rows = xk_detail::checked_element_count(outer_dim - 1, ld);
    if (full_rows > std::numeric_limits<size_t>::max() - inner_dim) {
        throw std::length_error(std::string(label) + ": matrix strided span overflows size_t");
    }
    const size_t span_elems = full_rows + inner_dim;
    return xk_detail::checked_element_count(span_elems, elem_bytes);
}

template <typename T>
XK_ExecutionPlan gemm_blas_impl(XK_Layout layout,
                                XK_Transpose trans_a,
                                XK_Transpose trans_b,
                                size_t M, size_t N, size_t K,
                                T alpha, const T* A, size_t lda,
                                const T* B, size_t ldb,
                                T beta, T* C, size_t ldc,
                                const XK_WorkloadDesc* opts,
                                XK_Workspace* workspace,
                                const XK_HardwareProfile* profile,
                                int* actual_thread_count) {
    if (actual_thread_count != nullptr) *actual_thread_count = 1;

    // Canonicalize ColMajor into equivalent RowMajor via:
    //   C^T = alpha * op(B)^T * op(A)^T + beta * C^T
    if (layout == XK_Layout::ColMajor) {
        return gemm_blas_impl<T>(XK_Layout::RowMajor,
                                 trans_b, trans_a,
                                 N, M, K,
                                 alpha, B, ldb,
                                 A, lda,
                                 beta, C, ldc,
                                 opts, workspace, profile, actual_thread_count);
    }

    // Now layout is strictly RowMajor.
    const bool ta = (trans_a == XK_Transpose::Trans);
    const bool tb = (trans_b == XK_Transpose::Trans);
    const size_t a_rows = ta ? K : M;
    const size_t a_cols = ta ? M : K;
    const size_t b_rows = tb ? N : K;
    const size_t b_cols = tb ? K : N;

    // Validate leading dimensions even when M==0 or N==0 if positive inner dim
    if (a_rows > 0 && a_cols > 0 && lda < a_cols) {
        throw std::invalid_argument("XK_gemm_blas: lda is smaller than required minimum");
    }
    if (b_rows > 0 && b_cols > 0 && ldb < b_cols) {
        throw std::invalid_argument("XK_gemm_blas: ldb is smaller than required minimum");
    }
    if (M > 0 && N > 0 && ldc < N) {
        throw std::invalid_argument("XK_gemm_blas: ldc is smaller than N");
    }

    XK_WorkloadDesc wd = opts ? *opts : XK_WorkloadDesc(M, K, N, sizeof(T));
    wd.M = M;
    wd.K = K;
    wd.N = N;
    wd.elem_bytes = sizeof(T);

    const XK_HardwareProfile active =
        profile ? *profile : XK_get_active_profile();
    XK_Workspace& ws = workspace ? *workspace : xk_internal::default_thread_workspace();
    XK_WorkloadDesc eff_wd = wd;
    eff_wd.memory_budget_bytes = resolve_effective_budget(wd.memory_budget_bytes, ws);
    XK_ExecutionPlan plan = XK_plan_gemm(eff_wd, active);

    if (M == 0 || N == 0) return plan;

    if (C == nullptr) {
        throw std::invalid_argument("XK_gemm_blas: null output matrix C");
    }

    const size_t c_bytes = checked_strided_span_bytes(M, N, ldc, sizeof(T), "XK_gemm_blas(C)");

    // Handle K == 0 or alpha == 0 cleanly (NaN-safe when beta == 0)
    const T zero = static_cast<T>(0);
    const T one = static_cast<T>(1);
    if (K == 0 || alpha == zero) {
        if (beta == zero) {
            for (size_t i = 0; i < M; ++i) {
                T* c_row = C + i * ldc;
                for (size_t j = 0; j < N; ++j) c_row[j] = zero;
            }
        } else if (beta != one) {
            for (size_t i = 0; i < M; ++i) {
                T* c_row = C + i * ldc;
                for (size_t j = 0; j < N; ++j) c_row[j] *= beta;
            }
        }
        return plan;
    }

    if (A == nullptr || B == nullptr) {
        throw std::invalid_argument("XK_gemm_blas: null input matrix A or B");
    }

    const size_t a_bytes = checked_strided_span_bytes(a_rows, a_cols, lda, sizeof(T), "XK_gemm_blas(A)");
    const size_t b_bytes = checked_strided_span_bytes(b_rows, b_cols, ldb, sizeof(T), "XK_gemm_blas(B)");
    if (xk_detail::ranges_overlap(C, c_bytes, A, a_bytes) ||
        xk_detail::ranges_overlap(C, c_bytes, B, b_bytes)) {
        throw std::invalid_argument("XK_gemm_blas: output matrix C must not alias A or B");
    }

    // Zero-overhead fast-path: RowMajor, NoTrans, NoTrans, alpha=1, beta=0, tight ld
    if (!ta && !tb && alpha == one && beta == zero &&
        lda == K && ldb == N && ldc == N) {
        if (sizeof(T) == sizeof(float)) {
            return XK_gemm_ex(reinterpret_cast<const float*>(A),
                              reinterpret_cast<const float*>(B),
                              reinterpret_cast<float*>(C),
                              wd, workspace, profile, actual_thread_count);
        } else {
            return XK_gemm_f64_ex(reinterpret_cast<const double*>(A),
                                  reinterpret_cast<const double*>(B),
                                  reinterpret_cast<double*>(C),
                                  wd, workspace, profile, actual_thread_count);
        }
    }

    // High-performance AVX2 BLIS path for general Trans/strided/alpha/beta when
    // AVX2+FMA is available and workload justifies staging buffers.
    const bool need_pack_a = ta || (lda != K);
    const bool need_pack_b = tb || (ldb != N);
    const bool need_stage_c = (alpha != one) || (beta != zero) || (ldc != N);
    const double flops = 2.0 * static_cast<double>(M) * static_cast<double>(K) * static_cast<double>(N);

    auto checked_add_size = [](size_t a, size_t b) -> size_t {
        if (a > std::numeric_limits<size_t>::max() - b) {
            return std::numeric_limits<size_t>::max();
        }
        return a + b;
    };
    size_t stage_bytes = 0;
    if (need_pack_a) stage_bytes = checked_add_size(stage_bytes, xk_detail::checked_element_count(xk_detail::checked_element_count(M, K), sizeof(T)));
    if (need_pack_b) stage_bytes = checked_add_size(stage_bytes, xk_detail::checked_element_count(xk_detail::checked_element_count(K, N), sizeof(T)));
    if (need_stage_c) stage_bytes = checked_add_size(stage_bytes, xk_detail::checked_element_count(xk_detail::checked_element_count(M, N), sizeof(T)));

    // Strictly honor caller/workspace memory_budget_bytes (including BLIS scratchpad).
    const size_t total_needed_bytes = checked_add_size(stage_bytes, plan.required_workspace_bytes);
    const size_t max_stage_bytes = (eff_wd.memory_budget_bytes > 0)
                                       ? eff_wd.memory_budget_bytes
                                       : static_cast<size_t>(256 * 1024 * 1024);

    if (XK_has_avx2_fma() && plan.kernel != XK_KernelKind::ScalarIkj &&
        flops >= 8192.0 && total_needed_bytes <= max_stage_bytes) {
        std::vector<T> a_buf;
        std::vector<T> b_buf;
        std::vector<T> c_buf;

        const T* a_ptr = A;
        if (need_pack_a) {
            a_buf.resize(M * K);
            if (!ta) {
                for (size_t i = 0; i < M; ++i) {
                    std::memcpy(&a_buf[i * K], &A[i * lda], K * sizeof(T));
                }
            } else {
                for (size_t k = 0; k < K; ++k) {
                    const T* src = A + k * lda;
                    for (size_t i = 0; i < M; ++i) {
                        a_buf[i * K + k] = src[i];
                    }
                }
            }
            a_ptr = a_buf.data();
        }

        const T* b_ptr = B;
        if (need_pack_b) {
            b_buf.resize(K * N);
            if (!tb) {
                for (size_t k = 0; k < K; ++k) {
                    std::memcpy(&b_buf[k * N], &B[k * ldb], N * sizeof(T));
                }
            } else {
                for (size_t j = 0; j < N; ++j) {
                    const T* src = B + j * ldb;
                    for (size_t k = 0; k < K; ++k) {
                        b_buf[k * N + j] = src[k];
                    }
                }
            }
            b_ptr = b_buf.data();
        }

        T* c_ptr = C;
        if (need_stage_c) {
            c_buf.resize(M * N);
            c_ptr = c_buf.data();
        }

        XK_ExecutionPlan exec_plan;
        if (sizeof(T) == sizeof(float)) {
            exec_plan = XK_gemm_ex(reinterpret_cast<const float*>(a_ptr),
                                   reinterpret_cast<const float*>(b_ptr),
                                   reinterpret_cast<float*>(c_ptr),
                                   wd, workspace, profile, actual_thread_count);
        } else {
            exec_plan = XK_gemm_f64_ex(reinterpret_cast<const double*>(a_ptr),
                                       reinterpret_cast<const double*>(b_ptr),
                                       reinterpret_cast<double*>(c_ptr),
                                       wd, workspace, profile, actual_thread_count);
        }

        if (need_stage_c) {
            if (beta == zero) {
                for (size_t i = 0; i < M; ++i) {
                    const T* src = &c_buf[i * N];
                    T* dst = C + i * ldc;
                    for (size_t j = 0; j < N; ++j) {
                        dst[j] = alpha * src[j];
                    }
                }
            } else if (beta == one) {
                for (size_t i = 0; i < M; ++i) {
                    const T* src = &c_buf[i * N];
                    T* dst = C + i * ldc;
                    for (size_t j = 0; j < N; ++j) {
                        dst[j] = alpha * src[j] + dst[j];
                    }
                }
            } else {
                for (size_t i = 0; i < M; ++i) {
                    const T* src = &c_buf[i * N];
                    T* dst = C + i * ldc;
                    for (size_t j = 0; j < N; ++j) {
                        dst[j] = alpha * src[j] + beta * dst[j];
                    }
                }
            }
        }
        return exec_plan;
    }

    // Zero-allocation portable scalar BLAS fallback
    plan.kernel = XK_KernelKind::ScalarIkj;
    plan.required_workspace_bytes = 0;
    if (sizeof(T) == sizeof(float)) {
        xk_internal::scalar_gemm_blas_f32(
            trans_a, trans_b, M, N, K,
            static_cast<float>(alpha), reinterpret_cast<const float*>(A), lda,
            reinterpret_cast<const float*>(B), ldb,
            static_cast<float>(beta), reinterpret_cast<float*>(C), ldc,
            plan.threads, actual_thread_count);
    } else {
        xk_internal::scalar_gemm_blas_f64(
            trans_a, trans_b, M, N, K,
            static_cast<double>(alpha), reinterpret_cast<const double*>(A), lda,
            reinterpret_cast<const double*>(B), ldb,
            static_cast<double>(beta), reinterpret_cast<double*>(C), ldc,
            plan.threads, actual_thread_count);
    }
    return plan;
}

}  // namespace

const char* XK_version_string() {
    return "3.0.0";
}

XK_ExecutionPlan XK_gemm_blas(XK_Layout layout,
                              XK_Transpose trans_a,
                              XK_Transpose trans_b,
                              size_t M, size_t N, size_t K,
                              float alpha, const float* A, size_t lda,
                              const float* B, size_t ldb,
                              float beta, float* C, size_t ldc,
                              const XK_WorkloadDesc* opts,
                              XK_Workspace* workspace,
                              const XK_HardwareProfile* profile,
                              int* actual_thread_count) {
    return gemm_blas_impl<float>(layout, trans_a, trans_b, M, N, K,
                                 alpha, A, lda, B, ldb, beta, C, ldc,
                                 opts, workspace, profile, actual_thread_count);
}

XK_ExecutionPlan XK_gemm_blas(XK_Layout layout,
                              XK_Transpose trans_a,
                              XK_Transpose trans_b,
                              size_t M, size_t N, size_t K,
                              double alpha, const double* A, size_t lda,
                              const double* B, size_t ldb,
                              double beta, double* C, size_t ldc,
                              const XK_WorkloadDesc* opts,
                              XK_Workspace* workspace,
                              const XK_HardwareProfile* profile,
                              int* actual_thread_count) {
    return gemm_blas_impl<double>(layout, trans_a, trans_b, M, N, K,
                                  alpha, A, lda, B, ldb, beta, C, ldc,
                                  opts, workspace, profile, actual_thread_count);
}

// ============================================================================
// Public Square APIs (100% Backward-Compatible)
// ============================================================================
void XK_naive(const float* A, const float* B, float* C, size_t N) {
    XK_gemm_naive(A, B, C, N, N, N);
}

void XK_ikj(const float* A, const float* B, float* C, size_t N) {
    XK_gemm_ikj(A, B, C, N, N, N);
}

void XK_avx2(const float* A, const float* B, float* C, size_t N) {
    XK_gemm_avx2(A, B, C, N, N, N);
}

void XK_auto(const float* A, const float* B, float* C, size_t N) {
    XK_gemm(A, B, C, N, N, N);
}

void XK_blocked(const float* A, const float* B, float* C, size_t N) {
    XK_gemm_blocked(A, B, C, N, N, N, 64);
}

void XK_blocked(const float* A, const float* B, float* C, size_t N, size_t block_size) {
    XK_gemm_blocked(A, B, C, N, N, N, block_size);
}

void XK_avx2_omp(const float* A, const float* B, float* C, size_t N,
                 int thread_count, int* actual_thread_count) {
    XK_gemm_omp(A, B, C, N, N, N, thread_count, actual_thread_count);
}

void XK_avx2_omp(const float* A, const float* B, float* C, size_t N, int thread_count) {
    XK_gemm_omp(A, B, C, N, N, N, thread_count, 0);
}

void XK_omp(const float* A, const float* B, float* C, size_t N, int thread_count) {
    XK_gemm_omp(A, B, C, N, N, N, thread_count, 0);
}

void XK_omp(const float* A, const float* B, float* C, size_t N,
            int thread_count, int* actual_thread_count) {
    XK_gemm_omp(A, B, C, N, N, N, thread_count, actual_thread_count);
}

// ============================================================================
// Public Rectangular GEMM APIs
// ============================================================================
void XK_gemm_avx2(const float* A, const float* B, float* C,
                  size_t M, size_t K, size_t N) {
    xk_detail::checked_element_count(M, K);
    xk_detail::checked_element_count(K, N);
    const size_t c_count = xk_detail::checked_element_count(M, N);
    if (M == 0 || N == 0) return;
    xk_detail::validate_ptrs(A, B, C, M, K, N, "XK_gemm_avx2");
    xk_detail::validate_no_aliasing(A, B, C, M, K, N,
                                    sizeof(float), sizeof(float), sizeof(float),
                                    "XK_gemm_avx2");
    if (K == 0) {
        for (size_t i = 0; i < c_count; ++i) C[i] = 0.0f;
        return;
    }
    xk_internal::gemm_avx2_blis_f32(
        A, B, C, M, K, N, 120, 256, 1024,
        xk_internal::default_thread_workspace(), false);
}

void XK_gemm_avx2_asm(const float* A, const float* B, float* C,
                      size_t M, size_t K, size_t N) {
    xk_detail::checked_element_count(M, K);
    xk_detail::checked_element_count(K, N);
    const size_t c_count = xk_detail::checked_element_count(M, N);
    if (M == 0 || N == 0) return;
    xk_detail::validate_ptrs(A, B, C, M, K, N, "XK_gemm_avx2_asm");
    xk_detail::validate_no_aliasing(A, B, C, M, K, N,
                                    sizeof(float), sizeof(float), sizeof(float),
                                    "XK_gemm_avx2_asm");
    if (K == 0) {
        for (size_t i = 0; i < c_count; ++i) C[i] = 0.0f;
        return;
    }
    xk_internal::gemm_avx2_blis_f32(
        A, B, C, M, K, N, 120, 256, 1024,
        xk_internal::default_thread_workspace(), true);
}

void XK_gemm_avx2(const double* A, const double* B, double* C,
                  size_t M, size_t K, size_t N) {
    xk_detail::checked_element_count(M, K);
    xk_detail::checked_element_count(K, N);
    const size_t c_count = xk_detail::checked_element_count(M, N);
    if (M == 0 || N == 0) return;
    xk_detail::validate_ptrs(A, B, C, M, K, N, "XK_gemm_avx2");
    xk_detail::validate_no_aliasing(A, B, C, M, K, N,
                                    sizeof(double), sizeof(double), sizeof(double),
                                    "XK_gemm_avx2");
    if (K == 0) {
        for (size_t i = 0; i < c_count; ++i) C[i] = 0.0;
        return;
    }
    xk_internal::gemm_avx2_blis_f64(
        A, B, C, M, K, N, 60, 128, 512,
        xk_internal::default_thread_workspace());
}

void XK_gemm(const float* A, const float* B, float* C,
             size_t M, size_t K, size_t N) {
    XK_WorkloadDesc desc(M, K, N, sizeof(float));
    desc.mode = XK_ExecMode::Auto;
    XK_gemm_ex(A, B, C, desc);
}

void XK_gemm(const double* A, const double* B, double* C,
             size_t M, size_t K, size_t N) {
    XK_WorkloadDesc desc(M, K, N, sizeof(double));
    desc.mode = XK_ExecMode::Auto;
    XK_gemm_f64_ex(A, B, C, desc);
}

void XK_gemm_i32_i64(const int32_t* A, const int32_t* B, int64_t* C,
                     size_t M, size_t K, size_t N) {
    xk_detail::checked_element_count(M, K);
    xk_detail::checked_element_count(K, N);
    xk_detail::checked_element_count(M, N);
    if (M == 0 || N == 0) return;
    xk_detail::validate_ptrs(A, B, C, M, K, N, "XK_gemm_i32_i64");
    xk_detail::validate_no_aliasing(A, B, C, M, K, N,
                                    sizeof(int32_t), sizeof(int32_t), sizeof(int64_t),
                                    "XK_gemm_i32_i64");
    xk_internal::scalar_gemm_i32_i64(A, B, C, M, K, N);
}

void XK_gemm_blocked(const float* A, const float* B, float* C,
                     size_t M, size_t K, size_t N, size_t block_size) {
    if (block_size == 0) throw std::invalid_argument("block size must be positive");
    xk_detail::checked_element_count(M, K);
    xk_detail::checked_element_count(K, N);
    const size_t c_count = xk_detail::checked_element_count(M, N);
    if (M == 0 || N == 0) return;
    xk_detail::validate_ptrs(A, B, C, M, K, N, "XK_gemm_blocked");
    xk_detail::validate_no_aliasing(A, B, C, M, K, N,
                                    sizeof(float), sizeof(float), sizeof(float),
                                    "XK_gemm_blocked");
    if (K == 0) {
        for (size_t i = 0; i < c_count; ++i) C[i] = 0.0f;
        return;
    }
    size_t mc = std::max(static_cast<size_t>(6), (block_size / 6) * 6);
    size_t kc = std::max(static_cast<size_t>(1), block_size);
    size_t nc = std::max(static_cast<size_t>(16),
                         xk_detail::checked_round_up(block_size, 16));
    XK_Workspace& ws = xk_internal::default_thread_workspace();
    const size_t budget = ws.memory_budget_bytes();
    if (budget > 0) {
        auto need_bytes = [&](size_t c_mc, size_t c_kc, size_t c_nc) -> size_t {
            const size_t mc_cap = xk_detail::checked_round_up(std::min(M, c_mc), 6);
            const size_t kc_cap = std::min(K, c_kc);
            const size_t nc_cap = xk_detail::checked_round_up(std::min(N, c_nc), 16);
            const size_t b_b = xk_detail::checked_element_count(
                xk_detail::checked_element_count(kc_cap, nc_cap), sizeof(float));
            const size_t a_slot = xk_detail::checked_round_up(
                xk_detail::checked_element_count(mc_cap, kc_cap), 16);
            const size_t a_b = xk_detail::checked_element_count(a_slot, sizeof(float));
            if (b_b > std::numeric_limits<size_t>::max() - a_b) {
                return std::numeric_limits<size_t>::max();
            }
            return b_b + a_b;
        };
        while (need_bytes(mc, kc, nc) > budget) {
            if (nc > 16) nc = std::max(static_cast<size_t>(16), ((nc / 2) / 16) * 16);
            else if (mc > 6) mc = std::max(static_cast<size_t>(6), ((mc / 2) / 6) * 6);
            else if (kc > 1) kc = std::max(static_cast<size_t>(1), kc / 2);
            else {
                xk_internal::scalar_gemm_f32(A, B, C, M, K, N, 1, nullptr);
                return;
            }
        }
    }
    xk_internal::gemm_avx2_blis_f32(A, B, C, M, K, N, mc, kc, nc, ws, false);
}

void XK_gemm_omp(const float* A, const float* B, float* C,
                 size_t M, size_t K, size_t N, int thread_count,
                 int* actual_thread_count) {
    xk_detail::checked_element_count(M, K);
    xk_detail::checked_element_count(K, N);
    xk_detail::checked_element_count(M, N);
    if (actual_thread_count != 0) *actual_thread_count = 1;
    if (thread_count < 1) thread_count = 1;
    if (M == 0 || N == 0) return;
    xk_detail::validate_ptrs(A, B, C, M, K, N, "XK_gemm_omp");
    xk_detail::validate_no_aliasing(A, B, C, M, K, N,
                                    sizeof(float), sizeof(float), sizeof(float),
                                    "XK_gemm_omp");
    if (K == 0 || thread_count == 1) {
        XK_gemm_avx2(A, B, C, M, K, N);
        return;
    }
    xk_internal::gemm_avx2_blis_f32_omp(
        A, B, C, M, K, N, 120, 256, 1024, thread_count,
        XK_ParallelAxis::Auto, xk_internal::default_thread_workspace(),
        actual_thread_count, false);
}

void XK_gemm_omp(const float* A, const float* B, float* C,
                 size_t M, size_t K, size_t N, int thread_count) {
    XK_gemm_omp(A, B, C, M, K, N, thread_count, 0);
}

void XK_gemm_packed_omp(const float* A, const float* B, float* C,
                        size_t M, size_t K, size_t N, int thread_count,
                        int* actual_thread_count) {
    XK_gemm_omp(A, B, C, M, K, N, thread_count, actual_thread_count);
}

#ifdef NVXK_USE_OPENBLAS
void XK_cblas_sgemm(const float* A, const float* B, float* C,
                    size_t M, size_t K, size_t N) {
    if (M > static_cast<size_t>(INT_MAX) ||
        K > static_cast<size_t>(INT_MAX) ||
        N > static_cast<size_t>(INT_MAX)) {
        throw std::length_error("OpenBLAS dimensions exceed the CBLAS integer range");
    }
    xk_detail::checked_element_count(M, K);
    xk_detail::checked_element_count(K, N);
    const size_t c_count = xk_detail::checked_element_count(M, N);
    if (M == 0 || N == 0) return;
    xk_detail::validate_ptrs(A, B, C, M, K, N, "XK_cblas_sgemm");
    xk_detail::validate_no_aliasing(A, B, C, M, K, N,
                                    sizeof(float), sizeof(float), sizeof(float),
                                    "XK_cblas_sgemm");
    if (K == 0) {
        for (size_t i = 0; i < c_count; ++i) C[i] = 0.0f;
        return;
    }
    cblas_sgemm(CblasRowMajor, CblasNoTrans, CblasNoTrans,
                static_cast<int>(M), static_cast<int>(N), static_cast<int>(K),
                1.0f, A, static_cast<int>(K), B, static_cast<int>(N),
                0.0f, C, static_cast<int>(N));
}
#endif

// ============================================================================
// Stable C ABI Implementation (Zero-Overhead FFI)
// ============================================================================
extern "C" {

NVXK_API int nvxk_version_major(void) { return NVXK_VERSION_MAJOR; }
NVXK_API int nvxk_version_minor(void) { return NVXK_VERSION_MINOR; }
NVXK_API int nvxk_version_patch(void) { return NVXK_VERSION_PATCH; }
NVXK_API const char* nvxk_version_string(void) { return XK_version_string(); }

NVXK_API int nvxk_get_hw_info(nvxk_hw_info_t* out_info) {
    if (out_info == nullptr) {
        set_c_error("nvxk_get_hw_info: out_info is null");
        return NVXK_ERR_INVALID_ARG;
    }
    try {
        XK_HardwareInfo hw = XK_detect_hardware();
        std::memset(out_info, 0, sizeof(nvxk_hw_info_t));
        out_info->physical_cores = hw.physical_cores;
        out_info->logical_threads = hw.logical_threads;
        out_info->l1d_bytes = hw.l1d_bytes;
        out_info->l2_bytes = hw.l2_bytes;
        out_info->l3_bytes = hw.l3_bytes;
        out_info->has_avx2 = hw.has_avx2 ? 1 : 0;
        out_info->has_fma = hw.has_fma ? 1 : 0;
        out_info->has_avx512f = hw.has_avx512f ? 1 : 0;
        out_info->has_neon = hw.has_neon ? 1 : 0;
        out_info->has_asm_kernel = XK_has_avx2_asm_kernel() ? 1 : 0;
        std::strncpy(out_info->vendor, hw.vendor.c_str(), sizeof(out_info->vendor) - 1);
        std::strncpy(out_info->brand, hw.brand.c_str(), sizeof(out_info->brand) - 1);
        set_c_error("");
        return NVXK_OK;
    } catch (const std::exception& e) {
        set_c_error(e.what());
        return NVXK_ERR_INTERNAL;
    } catch (...) {
        set_c_error("unknown error");
        return NVXK_ERR_INTERNAL;
    }
}

NVXK_API int nvxk_sgemm_ex(int layout, int trans_a, int trans_b,
                           size_t M, size_t N, size_t K,
                           float alpha, const float* A, size_t lda,
                           const float* B, size_t ldb,
                           float beta, float* C, size_t ldc,
                           int threads, int* out_threads_used) {
    try {
        if (layout != NVXK_ROW_MAJOR && layout != NVXK_COL_MAJOR) {
            throw std::invalid_argument("nvxk_sgemm_ex: invalid layout enum");
        }
        if (trans_a != NVXK_NO_TRANS && trans_a != NVXK_TRANS) {
            throw std::invalid_argument("nvxk_sgemm_ex: invalid trans_a enum");
        }
        if (trans_b != NVXK_NO_TRANS && trans_b != NVXK_TRANS) {
            throw std::invalid_argument("nvxk_sgemm_ex: invalid trans_b enum");
        }
        XK_WorkloadDesc wd(M, K, N, sizeof(float));
        if (threads > 0) {
            wd.mode = XK_ExecMode::Manual;
            wd.manual_threads = threads;
        } else {
            wd.mode = XK_ExecMode::Auto;
        }
        XK_gemm_blas(layout == NVXK_COL_MAJOR ? XK_Layout::ColMajor : XK_Layout::RowMajor,
                     trans_a == NVXK_TRANS ? XK_Transpose::Trans : XK_Transpose::NoTrans,
                     trans_b == NVXK_TRANS ? XK_Transpose::Trans : XK_Transpose::NoTrans,
                     M, N, K, alpha, A, lda, B, ldb, beta, C, ldc,
                     &wd, nullptr, nullptr, out_threads_used);
        set_c_error("");
        return NVXK_OK;
    } catch (const std::invalid_argument& e) {
        set_c_error(e.what());
        return NVXK_ERR_INVALID_ARG;
    } catch (const std::length_error& e) {
        set_c_error(e.what());
        return NVXK_ERR_LENGTH;
    } catch (const std::bad_alloc& e) {
        set_c_error(e.what());
        return NVXK_ERR_OOM;
    } catch (const std::exception& e) {
        set_c_error(e.what());
        return NVXK_ERR_INTERNAL;
    } catch (...) {
        set_c_error("unknown error");
        return NVXK_ERR_INTERNAL;
    }
}

NVXK_API int nvxk_dgemm_ex(int layout, int trans_a, int trans_b,
                           size_t M, size_t N, size_t K,
                           double alpha, const double* A, size_t lda,
                           const double* B, size_t ldb,
                           double beta, double* C, size_t ldc,
                           int threads, int* out_threads_used) {
    try {
        if (layout != NVXK_ROW_MAJOR && layout != NVXK_COL_MAJOR) {
            throw std::invalid_argument("nvxk_dgemm_ex: invalid layout enum");
        }
        if (trans_a != NVXK_NO_TRANS && trans_a != NVXK_TRANS) {
            throw std::invalid_argument("nvxk_dgemm_ex: invalid trans_a enum");
        }
        if (trans_b != NVXK_NO_TRANS && trans_b != NVXK_TRANS) {
            throw std::invalid_argument("nvxk_dgemm_ex: invalid trans_b enum");
        }
        XK_WorkloadDesc wd(M, K, N, sizeof(double));
        if (threads > 0) {
            wd.mode = XK_ExecMode::Manual;
            wd.manual_threads = threads;
        } else {
            wd.mode = XK_ExecMode::Auto;
        }
        XK_gemm_blas(layout == NVXK_COL_MAJOR ? XK_Layout::ColMajor : XK_Layout::RowMajor,
                     trans_a == NVXK_TRANS ? XK_Transpose::Trans : XK_Transpose::NoTrans,
                     trans_b == NVXK_TRANS ? XK_Transpose::Trans : XK_Transpose::NoTrans,
                     M, N, K, alpha, A, lda, B, ldb, beta, C, ldc,
                     &wd, nullptr, nullptr, out_threads_used);
        set_c_error("");
        return NVXK_OK;
    } catch (const std::invalid_argument& e) {
        set_c_error(e.what());
        return NVXK_ERR_INVALID_ARG;
    } catch (const std::length_error& e) {
        set_c_error(e.what());
        return NVXK_ERR_LENGTH;
    } catch (const std::bad_alloc& e) {
        set_c_error(e.what());
        return NVXK_ERR_OOM;
    } catch (const std::exception& e) {
        set_c_error(e.what());
        return NVXK_ERR_INTERNAL;
    } catch (...) {
        set_c_error("unknown error");
        return NVXK_ERR_INTERNAL;
    }
}

NVXK_API int nvxk_has_avx2_fma(void) {
    return XK_has_avx2_fma() ? 1 : 0;
}

NVXK_API int nvxk_calibrate(double budget_ms) {
    try {
        XK_HardwareProfile p = XK_calibrate_hardware(budget_ms);
        XK_set_active_profile(p);
        set_c_error("");
        return NVXK_OK;
    } catch (const std::exception& e) {
        set_c_error(e.what());
        return NVXK_ERR_INTERNAL;
    } catch (...) {
        set_c_error("unknown error");
        return NVXK_ERR_INTERNAL;
    }
}

NVXK_API int nvxk_sgemm(const float* A, const float* B, float* C,
                        size_t M, size_t K, size_t N,
                        int* out_threads_used) {
    try {
        XK_WorkloadDesc wd(M, K, N, sizeof(float));
        wd.mode = XK_ExecMode::Auto;
        XK_gemm_ex(A, B, C, wd, nullptr, nullptr, out_threads_used);
        set_c_error("");
        return NVXK_OK;
    } catch (const std::invalid_argument& e) {
        set_c_error(e.what());
        return NVXK_ERR_INVALID_ARG;
    } catch (const std::length_error& e) {
        set_c_error(e.what());
        return NVXK_ERR_LENGTH;
    } catch (const std::bad_alloc& e) {
        set_c_error(e.what());
        return NVXK_ERR_OOM;
    } catch (const std::exception& e) {
        set_c_error(e.what());
        return NVXK_ERR_INTERNAL;
    } catch (...) {
        set_c_error("unknown error");
        return NVXK_ERR_INTERNAL;
    }
}

NVXK_API int nvxk_sgemm_manual(const float* A, const float* B, float* C,
                               size_t M, size_t K, size_t N,
                               int threads, int* out_threads_used) {
    try {
        XK_WorkloadDesc wd(M, K, N, sizeof(float));
        wd.mode = XK_ExecMode::Manual;
        wd.manual_threads = threads;
        XK_gemm_ex(A, B, C, wd, nullptr, nullptr, out_threads_used);
        set_c_error("");
        return NVXK_OK;
    } catch (const std::invalid_argument& e) {
        set_c_error(e.what());
        return NVXK_ERR_INVALID_ARG;
    } catch (const std::length_error& e) {
        set_c_error(e.what());
        return NVXK_ERR_LENGTH;
    } catch (const std::bad_alloc& e) {
        set_c_error(e.what());
        return NVXK_ERR_OOM;
    } catch (const std::exception& e) {
        set_c_error(e.what());
        return NVXK_ERR_INTERNAL;
    } catch (...) {
        set_c_error("unknown error");
        return NVXK_ERR_INTERNAL;
    }
}

NVXK_API int nvxk_dgemm(const double* A, const double* B, double* C,
                        size_t M, size_t K, size_t N,
                        int* out_threads_used) {
    try {
        XK_WorkloadDesc wd(M, K, N, sizeof(double));
        wd.mode = XK_ExecMode::Auto;
        XK_gemm_f64_ex(A, B, C, wd, nullptr, nullptr, out_threads_used);
        set_c_error("");
        return NVXK_OK;
    } catch (const std::invalid_argument& e) {
        set_c_error(e.what());
        return NVXK_ERR_INVALID_ARG;
    } catch (const std::length_error& e) {
        set_c_error(e.what());
        return NVXK_ERR_LENGTH;
    } catch (const std::bad_alloc& e) {
        set_c_error(e.what());
        return NVXK_ERR_OOM;
    } catch (const std::exception& e) {
        set_c_error(e.what());
        return NVXK_ERR_INTERNAL;
    } catch (...) {
        set_c_error("unknown error");
        return NVXK_ERR_INTERNAL;
    }
}

NVXK_API int nvxk_gemm_i32_i64(const int32_t* A, const int32_t* B, int64_t* C,
                               size_t M, size_t K, size_t N) {
    try {
        XK_gemm_i32_i64(A, B, C, M, K, N);
        set_c_error("");
        return NVXK_OK;
    } catch (const std::invalid_argument& e) {
        set_c_error(e.what());
        return NVXK_ERR_INVALID_ARG;
    } catch (const std::length_error& e) {
        set_c_error(e.what());
        return NVXK_ERR_LENGTH;
    } catch (const std::overflow_error& e) {
        set_c_error(e.what());
        return NVXK_ERR_OVERFLOW;
    } catch (const std::exception& e) {
        set_c_error(e.what());
        return NVXK_ERR_INTERNAL;
    } catch (...) {
        set_c_error("unknown error");
        return NVXK_ERR_INTERNAL;
    }
}

NVXK_API const char* nvxk_last_error(void) {
    return g_last_c_error.c_str();
}

}  // extern "C"
