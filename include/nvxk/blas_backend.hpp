#ifndef NVXK_BLAS_BACKEND_HPP
#define NVXK_BLAS_BACKEND_HPP

#include <nvxk.h>
#include <cstddef>
#include <string>

namespace nvxk {
namespace blas {

struct BlisInfo {
    bool compiled_in = false;
    bool available = false;
    std::string version = "unavailable (compiled with NVXK_ENABLE_BLIS=OFF)";
    std::string threading = "none";
};

BlisInfo query_info();

// Zero-copy BLIS GEMM bridge: C = alpha * op(A) * op(B) + beta * C.
// Returns true if executed via BLIS, false if BLIS is not available (caller
// should fall back to NVXK native kernels).
bool blis_gemm_f32(XK_Layout layout, XK_Transpose trans_a, XK_Transpose trans_b,
                   size_t M, size_t N, size_t K,
                   float alpha, const float* A, size_t lda,
                   const float* B, size_t ldb,
                   float beta, float* C, size_t ldc,
                   int num_threads);

bool blis_gemm_f64(XK_Layout layout, XK_Transpose trans_a, XK_Transpose trans_b,
                   size_t M, size_t N, size_t K,
                   double alpha, const double* A, size_t lda,
                   const double* B, size_t ldb,
                   double beta, double* C, size_t ldc,
                   int num_threads);

// Additional BLAS Level-2/3 operations (BLIS when compiled in, NVXK native fallback otherwise)
void syrk_f32(bool upper, XK_Transpose trans, size_t N, size_t K,
              float alpha, const float* A, size_t lda,
              float beta, float* C, size_t ldc, int num_threads = 1);

void syrk_f64(bool upper, XK_Transpose trans, size_t N, size_t K,
              double alpha, const double* A, size_t lda,
              double beta, double* C, size_t ldc, int num_threads = 1);

// Solve L*X = alpha*B (lower unit/non-unit triangular, RowMajor, Left side)
void trsm_lower_left_f32(bool unit_diag, size_t M, size_t N,
                         float alpha, const float* L, size_t ldl,
                         float* B, size_t ldb, int num_threads = 1);

void trsm_lower_left_f64(bool unit_diag, size_t M, size_t N,
                         double alpha, const double* L, size_t ldl,
                         double* B, size_t ldb, int num_threads = 1);

}  // namespace blas
}  // namespace nvxk

#endif  // NVXK_BLAS_BACKEND_HPP
