#include <nvxk/blas_backend.hpp>

#include <algorithm>
#include <stdexcept>
#include <string>

#if defined(NVXK_ENABLE_BLIS)
#include <blis/blis.h>
#endif

namespace nvxk {
namespace blas {

BlisInfo query_info() {
    BlisInfo info;
#if defined(NVXK_ENABLE_BLIS)
    bli_init_auto();
    info.compiled_in = true;
    info.available = true;
    const char* ver = bli_info_get_version_str();
    info.version = ver ? std::string(ver) : std::string("BLIS (linked)");
#if defined(BLIS_ENABLE_OPENMP)
    info.threading = "openmp";
#elif defined(BLIS_ENABLE_PTHREADS)
    info.threading = "pthreads";
#else
    info.threading = "single-thread";
#endif
#else
    info.compiled_in = false;
    info.available = false;
    info.version = "not linked (NVXK_ENABLE_BLIS=OFF; fallback to NVXK native kernels)";
    info.threading = "none";
#endif
    return info;
}

bool blis_gemm_f32(XK_Layout layout, XK_Transpose trans_a, XK_Transpose trans_b,
                   size_t M, size_t N, size_t K,
                   float alpha, const float* A, size_t lda,
                   const float* B, size_t ldb,
                   float beta, float* C, size_t ldc,
                   int num_threads) {
#if defined(NVXK_ENABLE_BLIS)
    bli_init_auto();
    if (num_threads > 0) {
        bli_thread_set_num_threads(static_cast<dim_t>(num_threads));
    }
    const trans_t ta = (trans_a == XK_Transpose::Trans) ? BLIS_TRANSPOSE : BLIS_NO_TRANSPOSE;
    const trans_t tb = (trans_b == XK_Transpose::Trans) ? BLIS_TRANSPOSE : BLIS_NO_TRANSPOSE;
    // Zero-copy generalized stride mapping: RowMajor => (rs = ld, cs = 1), ColMajor => (rs = 1, cs = ld)
    const inc_t rsa = (layout == XK_Layout::RowMajor) ? static_cast<inc_t>(lda) : 1;
    const inc_t csa = (layout == XK_Layout::RowMajor) ? 1 : static_cast<inc_t>(lda);
    const inc_t rsb = (layout == XK_Layout::RowMajor) ? static_cast<inc_t>(ldb) : 1;
    const inc_t csb = (layout == XK_Layout::RowMajor) ? 1 : static_cast<inc_t>(ldb);
    const inc_t rsc = (layout == XK_Layout::RowMajor) ? static_cast<inc_t>(ldc) : 1;
    const inc_t csc = (layout == XK_Layout::RowMajor) ? 1 : static_cast<inc_t>(ldc);

    bli_sgemm(ta, tb,
              static_cast<dim_t>(M), static_cast<dim_t>(N), static_cast<dim_t>(K),
              &alpha, const_cast<float*>(A), rsa, csa,
              const_cast<float*>(B), rsb, csb,
              &beta, C, rsc, csc);
    return true;
#else
    (void)layout; (void)trans_a; (void)trans_b; (void)M; (void)N; (void)K;
    (void)alpha; (void)A; (void)lda; (void)B; (void)ldb;
    (void)beta; (void)C; (void)ldc; (void)num_threads;
    return false;
#endif
}

bool blis_gemm_f64(XK_Layout layout, XK_Transpose trans_a, XK_Transpose trans_b,
                   size_t M, size_t N, size_t K,
                   double alpha, const double* A, size_t lda,
                   const double* B, size_t ldb,
                   double beta, double* C, size_t ldc,
                   int num_threads) {
#if defined(NVXK_ENABLE_BLIS)
    bli_init_auto();
    if (num_threads > 0) {
        bli_thread_set_num_threads(static_cast<dim_t>(num_threads));
    }
    const trans_t ta = (trans_a == XK_Transpose::Trans) ? BLIS_TRANSPOSE : BLIS_NO_TRANSPOSE;
    const trans_t tb = (trans_b == XK_Transpose::Trans) ? BLIS_TRANSPOSE : BLIS_NO_TRANSPOSE;
    const inc_t rsa = (layout == XK_Layout::RowMajor) ? static_cast<inc_t>(lda) : 1;
    const inc_t csa = (layout == XK_Layout::RowMajor) ? 1 : static_cast<inc_t>(lda);
    const inc_t rsb = (layout == XK_Layout::RowMajor) ? static_cast<inc_t>(ldb) : 1;
    const inc_t csb = (layout == XK_Layout::RowMajor) ? 1 : static_cast<inc_t>(ldb);
    const inc_t rsc = (layout == XK_Layout::RowMajor) ? static_cast<inc_t>(ldc) : 1;
    const inc_t csc = (layout == XK_Layout::RowMajor) ? 1 : static_cast<inc_t>(ldc);

    bli_dgemm(ta, tb,
              static_cast<dim_t>(M), static_cast<dim_t>(N), static_cast<dim_t>(K),
              &alpha, const_cast<double*>(A), rsa, csa,
              const_cast<double*>(B), rsb, csb,
              &beta, C, rsc, csc);
    return true;
#else
    (void)layout; (void)trans_a; (void)trans_b; (void)M; (void)N; (void)K;
    (void)alpha; (void)A; (void)lda; (void)B; (void)ldb;
    (void)beta; (void)C; (void)ldc; (void)num_threads;
    return false;
#endif
}

template <typename T>
static void syrk_fallback(bool upper, XK_Transpose trans, size_t N, size_t K,
                          T alpha, const T* A, size_t lda,
                          T beta, T* C, size_t ldc) {
    if (N == 0) return;
    if (K > 0 && A == nullptr) throw std::invalid_argument("syrk: null A");
    if (C == nullptr) throw std::invalid_argument("syrk: null C");
    const size_t min_lda = (trans == XK_Transpose::NoTrans) ? K : N;
    if (K > 0 && lda < min_lda) throw std::invalid_argument("syrk: lda smaller than required inner dimension");
    if (ldc < N) throw std::invalid_argument("syrk: ldc smaller than N");
    for (size_t i = 0; i < N; ++i) {
        const size_t j_start = upper ? i : 0;
        const size_t j_end = upper ? N : (i + 1);
        for (size_t j = j_start; j < j_end; ++j) {
            double acc = 0.0;
            for (size_t k = 0; k < K; ++k) {
                const T aik = (trans == XK_Transpose::NoTrans) ? A[i * lda + k] : A[k * lda + i];
                const T ajk = (trans == XK_Transpose::NoTrans) ? A[j * lda + k] : A[k * lda + j];
                acc += static_cast<double>(aik) * static_cast<double>(ajk);
            }
            C[i * ldc + j] = (beta == static_cast<T>(0))
                                 ? static_cast<T>(alpha * acc)
                                 : static_cast<T>(alpha * acc + beta * C[i * ldc + j]);
        }
    }
}

void syrk_f32(bool upper, XK_Transpose trans, size_t N, size_t K,
              float alpha, const float* A, size_t lda,
              float beta, float* C, size_t ldc, int num_threads) {
#if defined(NVXK_ENABLE_BLIS)
    bli_init_auto();
    if (num_threads > 0) bli_thread_set_num_threads(static_cast<dim_t>(num_threads));
    bli_ssyrk(upper ? BLIS_UPPER : BLIS_LOWER,
              (trans == XK_Transpose::Trans) ? BLIS_TRANSPOSE : BLIS_NO_TRANSPOSE,
              static_cast<dim_t>(N), static_cast<dim_t>(K),
              &alpha, const_cast<float*>(A), static_cast<inc_t>(lda), 1,
              &beta, C, static_cast<inc_t>(ldc), 1);
#else
    (void)num_threads;
    syrk_fallback<float>(upper, trans, N, K, alpha, A, lda, beta, C, ldc);
#endif
}

void syrk_f64(bool upper, XK_Transpose trans, size_t N, size_t K,
              double alpha, const double* A, size_t lda,
              double beta, double* C, size_t ldc, int num_threads) {
#if defined(NVXK_ENABLE_BLIS)
    bli_init_auto();
    if (num_threads > 0) bli_thread_set_num_threads(static_cast<dim_t>(num_threads));
    bli_dsyrk(upper ? BLIS_UPPER : BLIS_LOWER,
              (trans == XK_Transpose::Trans) ? BLIS_TRANSPOSE : BLIS_NO_TRANSPOSE,
              static_cast<dim_t>(N), static_cast<dim_t>(K),
              &alpha, const_cast<double*>(A), static_cast<inc_t>(lda), 1,
              &beta, C, static_cast<inc_t>(ldc), 1);
#else
    (void)num_threads;
    syrk_fallback<double>(upper, trans, N, K, alpha, A, lda, beta, C, ldc);
#endif
}

template <typename T>
static void trsm_lower_left_fallback(bool unit_diag, size_t M, size_t N,
                                     T alpha, const T* L, size_t ldl,
                                     T* B, size_t ldb) {
    if (M == 0 || N == 0) return;
    if (L == nullptr || B == nullptr) throw std::invalid_argument("trsm: null pointer");
    if (ldl < M) throw std::invalid_argument("trsm: ldl smaller than M");
    if (ldb < N) throw std::invalid_argument("trsm: ldb smaller than N");
    for (size_t i = 0; i < M; ++i) {
        const T diag = unit_diag ? static_cast<T>(1) : L[i * ldl + i];
        if (!unit_diag && diag == static_cast<T>(0)) {
            throw std::runtime_error("trsm: singular triangular matrix (zero diagonal)");
        }
        for (size_t j = 0; j < N; ++j) {
            double rhs = static_cast<double>(alpha) * static_cast<double>(B[i * ldb + j]);
            for (size_t k = 0; k < i; ++k) {
                rhs -= static_cast<double>(L[i * ldl + k]) * static_cast<double>(B[k * ldb + j]);
            }
            B[i * ldb + j] = static_cast<T>(rhs / static_cast<double>(diag));
        }
    }
}

void trsm_lower_left_f32(bool unit_diag, size_t M, size_t N,
                         float alpha, const float* L, size_t ldl,
                         float* B, size_t ldb, int num_threads) {
#if defined(NVXK_ENABLE_BLIS)
    bli_init_auto();
    if (num_threads > 0) bli_thread_set_num_threads(static_cast<dim_t>(num_threads));
    bli_strsm(BLIS_LEFT, BLIS_LOWER, BLIS_NO_TRANSPOSE,
              unit_diag ? BLIS_UNIT_DIAG : BLIS_NONUNIT_DIAG,
              static_cast<dim_t>(M), static_cast<dim_t>(N),
              &alpha, const_cast<float*>(L), static_cast<inc_t>(ldl), 1,
              B, static_cast<inc_t>(ldb), 1);
#else
    (void)num_threads;
    trsm_lower_left_fallback<float>(unit_diag, M, N, alpha, L, ldl, B, ldb);
#endif
}

void trsm_lower_left_f64(bool unit_diag, size_t M, size_t N,
                         double alpha, const double* L, size_t ldl,
                         double* B, size_t ldb, int num_threads) {
#if defined(NVXK_ENABLE_BLIS)
    bli_init_auto();
    if (num_threads > 0) bli_thread_set_num_threads(static_cast<dim_t>(num_threads));
    bli_dtrsm(BLIS_LEFT, BLIS_LOWER, BLIS_NO_TRANSPOSE,
              unit_diag ? BLIS_UNIT_DIAG : BLIS_NONUNIT_DIAG,
              static_cast<dim_t>(M), static_cast<dim_t>(N),
              &alpha, const_cast<double*>(L), static_cast<inc_t>(ldl), 1,
              B, static_cast<inc_t>(ldb), 1);
#else
    (void)num_threads;
    trsm_lower_left_fallback<double>(unit_diag, M, N, alpha, L, ldl, B, ldb);
#endif
}

}  // namespace blas
}  // namespace nvxk
