#include "nvxk_internal.h"
#include <algorithm>
#include <cstddef>
#include <limits>

#if defined(_OPENMP)
#include <omp.h>
#endif

namespace xk_internal {

void scalar_gemm_f32(const float* A, const float* B, float* C,
                     size_t M, size_t K, size_t N, int threads,
                     int* actual_thread_count) {
    if (actual_thread_count != nullptr) *actual_thread_count = 1;
#if defined(_OPENMP)
    if (threads > 1 && M >= 2 &&
        M <= static_cast<size_t>(std::numeric_limits<std::ptrdiff_t>::max())) {
        const int tc = static_cast<int>(
            std::min(static_cast<size_t>(threads), M));
        const std::ptrdiff_t m_signed = static_cast<std::ptrdiff_t>(M);
        #pragma omp parallel num_threads(tc)
        {
            #pragma omp single nowait
            if (actual_thread_count != nullptr) {
                *actual_thread_count = omp_get_num_threads();
            }
            #pragma omp for schedule(static)
            for (std::ptrdiff_t i_s = 0; i_s < m_signed; ++i_s) {
                const size_t i = static_cast<size_t>(i_s);
                float* c_row = &C[i * N];
                for (size_t j = 0; j < N; ++j) c_row[j] = 0.0f;
                for (size_t k = 0; k < K; ++k) {
                    const float a_ik = A[i * K + k];
                    const float* b_row = &B[k * N];
                    for (size_t j = 0; j < N; ++j) {
                        c_row[j] += a_ik * b_row[j];
                    }
                }
            }
        }
        return;
    }
#else
    (void)threads;
#endif
    XK_gemm_ikj(A, B, C, M, K, N);
}

void scalar_gemm_f64(const double* A, const double* B, double* C,
                     size_t M, size_t K, size_t N, int threads,
                     int* actual_thread_count) {
    if (actual_thread_count != nullptr) *actual_thread_count = 1;
#if defined(_OPENMP)
    if (threads > 1 && M >= 2 &&
        M <= static_cast<size_t>(std::numeric_limits<std::ptrdiff_t>::max())) {
        const int tc = static_cast<int>(
            std::min(static_cast<size_t>(threads), M));
        const std::ptrdiff_t m_signed = static_cast<std::ptrdiff_t>(M);
        #pragma omp parallel num_threads(tc)
        {
            #pragma omp single nowait
            if (actual_thread_count != nullptr) {
                *actual_thread_count = omp_get_num_threads();
            }
            #pragma omp for schedule(static)
            for (std::ptrdiff_t i_s = 0; i_s < m_signed; ++i_s) {
                const size_t i = static_cast<size_t>(i_s);
                double* c_row = &C[i * N];
                for (size_t j = 0; j < N; ++j) c_row[j] = 0.0;
                for (size_t k = 0; k < K; ++k) {
                    const double a_ik = A[i * K + k];
                    const double* b_row = &B[k * N];
                    for (size_t j = 0; j < N; ++j) {
                        c_row[j] += a_ik * b_row[j];
                    }
                }
            }
        }
        return;
    }
#else
    (void)threads;
#endif
    XK_gemm_ikj(A, B, C, M, K, N);
}

namespace {

template <typename T>
void scalar_gemm_blas_row_impl(XK_Transpose trans_a, XK_Transpose trans_b,
                               size_t M, size_t N, size_t K,
                               T alpha, const T* A, size_t lda,
                               const T* B, size_t ldb,
                               T beta, T* C, size_t ldc,
                               int threads, int* actual_thread_count) {
    if (actual_thread_count != nullptr) *actual_thread_count = 1;
    const bool ta = (trans_a == XK_Transpose::Trans);
    const bool tb = (trans_b == XK_Transpose::Trans);
    const T zero = static_cast<T>(0);
    const T one = static_cast<T>(1);

    auto run_row = [&](size_t i) {
        T* c_row = C + i * ldc;
        // NaN-safe beta scaling: never read C when beta == 0
        if (beta == zero) {
            for (size_t j = 0; j < N; ++j) c_row[j] = zero;
        } else if (beta != one) {
            for (size_t j = 0; j < N; ++j) c_row[j] *= beta;
        }
        if (K == 0 || alpha == zero) return;

        if (!tb) {
            // B is RowMajor (K x N): IKJ loop order for contiguous B access
            for (size_t k = 0; k < K; ++k) {
                const T a_val = alpha * (ta ? A[k * lda + i] : A[i * lda + k]);
                const T* b_row = B + k * ldb;
                for (size_t j = 0; j < N; ++j) {
                    c_row[j] += a_val * b_row[j];
                }
            }
        } else {
            // B is Transposed (stored as N x K): dot product along contiguous K
            for (size_t j = 0; j < N; ++j) {
                const T* b_col = B + j * ldb;
                T sum = zero;
                if (!ta) {
                    const T* a_row = A + i * lda;
                    for (size_t k = 0; k < K; ++k) {
                        sum += a_row[k] * b_col[k];
                    }
                } else {
                    for (size_t k = 0; k < K; ++k) {
                        sum += A[k * lda + i] * b_col[k];
                    }
                }
                c_row[j] += alpha * sum;
            }
        }
    };

#if defined(_OPENMP)
    if (threads > 1 && M >= 2 &&
        M <= static_cast<size_t>(std::numeric_limits<std::ptrdiff_t>::max())) {
        const int tc = static_cast<int>(std::min(static_cast<size_t>(threads), M));
        const std::ptrdiff_t m_signed = static_cast<std::ptrdiff_t>(M);
        #pragma omp parallel num_threads(tc)
        {
            #pragma omp single nowait
            if (actual_thread_count != nullptr) {
                *actual_thread_count = omp_get_num_threads();
            }
            #pragma omp for schedule(static)
            for (std::ptrdiff_t i_s = 0; i_s < m_signed; ++i_s) {
                run_row(static_cast<size_t>(i_s));
            }
        }
        return;
    }
#else
    (void)threads;
#endif
    for (size_t i = 0; i < M; ++i) {
        run_row(i);
    }
}

}  // namespace

void scalar_gemm_blas_f32(XK_Transpose trans_a, XK_Transpose trans_b,
                          size_t M, size_t N, size_t K,
                          float alpha, const float* A, size_t lda,
                          const float* B, size_t ldb,
                          float beta, float* C, size_t ldc,
                          int threads, int* actual_thread_count) {
    scalar_gemm_blas_row_impl<float>(trans_a, trans_b, M, N, K,
                                     alpha, A, lda, B, ldb,
                                     beta, C, ldc, threads, actual_thread_count);
}

void scalar_gemm_blas_f64(XK_Transpose trans_a, XK_Transpose trans_b,
                          size_t M, size_t N, size_t K,
                          double alpha, const double* A, size_t lda,
                          const double* B, size_t ldb,
                          double beta, double* C, size_t ldc,
                          int threads, int* actual_thread_count) {
    scalar_gemm_blas_row_impl<double>(trans_a, trans_b, M, N, K,
                                      alpha, A, lda, B, ldb,
                                      beta, C, ldc, threads, actual_thread_count);
}

void scalar_gemm_i32_i64(const int32_t* A, const int32_t* B, int64_t* C,
                         size_t M, size_t K, size_t N) {
    const size_t c_count = xk_detail::checked_element_count(M, N);
    for (size_t i = 0; i < c_count; ++i) C[i] = 0;
    for (size_t i = 0; i < M; ++i) {
        int64_t* c_row = &C[i * N];
        for (size_t k = 0; k < K; ++k) {
            const int64_t a_ik = static_cast<int64_t>(A[i * K + k]);
            const int32_t* b_row = &B[k * N];
            for (size_t j = 0; j < N; ++j) {
                const int64_t prod = a_ik * static_cast<int64_t>(b_row[j]);
                c_row[j] = xk_detail::add_checked(c_row[j], prod);
            }
        }
    }
}

}  // namespace xk_internal