#ifndef NVXK_INTERNAL_H
#define NVXK_INTERNAL_H

#include <nvxk.h>

namespace xk_internal {

XK_Workspace& default_thread_workspace();

// Portable scalar kernels (src/nvxk_kernels_scalar.cpp)
void scalar_gemm_f32(const float* A, const float* B, float* C,
                     size_t M, size_t K, size_t N, int threads,
                     int* actual_thread_count);

void scalar_gemm_f64(const double* A, const double* B, double* C,
                     size_t M, size_t K, size_t N, int threads,
                     int* actual_thread_count);

void scalar_gemm_blas_f32(XK_Transpose trans_a, XK_Transpose trans_b,
                          size_t M, size_t N, size_t K,
                          float alpha, const float* A, size_t lda,
                          const float* B, size_t ldb,
                          float beta, float* C, size_t ldc,
                          int threads, int* actual_thread_count);

void scalar_gemm_blas_f64(XK_Transpose trans_a, XK_Transpose trans_b,
                          size_t M, size_t N, size_t K,
                          double alpha, const double* A, size_t lda,
                          const double* B, size_t ldb,
                          double beta, double* C, size_t ldc,
                          int threads, int* actual_thread_count);

void scalar_gemm_i32_i64(const int32_t* A, const int32_t* B, int64_t* C,
                         size_t M, size_t K, size_t N);

// AVX2+FMA runtime-dispatched kernels (src/nvxk_kernels_avx2.cpp)
bool has_avx2_only();

void vector_add_avx2_f32_dispatch(const float* a, const float* b, float* c, size_t n);
void vector_add_avx2_f64_dispatch(const double* a, const double* b, double* c, size_t n);

void gemm_avx2_blis_f32(const float* A, const float* B, float* C,
                        size_t M, size_t K, size_t N,
                        size_t MC, size_t KC, size_t NC,
                        XK_Workspace& ws, bool use_asm_kernel);

void gemm_avx2_blis_f32_omp(const float* A, const float* B, float* C,
                            size_t M, size_t K, size_t N,
                            size_t MC, size_t KC, size_t NC,
                            int thread_count, XK_ParallelAxis axis,
                            XK_Workspace& ws, int* actual_thread_count,
                            bool use_asm_kernel);

void gemm_avx2_thin_m_f32(const float* A, const float* B, float* C,
                          size_t M, size_t K, size_t N,
                          int thread_count, int* actual_thread_count);

void gemm_avx2_blis_f64(const double* A, const double* B, double* C,
                        size_t M, size_t K, size_t N,
                        size_t MC, size_t KC, size_t NC,
                        XK_Workspace& ws);

void gemm_avx2_blis_f64_omp(const double* A, const double* B, double* C,
                            size_t M, size_t K, size_t N,
                            size_t MC, size_t KC, size_t NC,
                            int thread_count, XK_ParallelAxis axis,
                            XK_Workspace& ws, int* actual_thread_count);

}  // namespace xk_internal

#endif