#ifndef NVXK_C_H
#define NVXK_C_H

#include <stddef.h>
#include <stdint.h>

#define NVXK_VERSION_MAJOR 3
#define NVXK_VERSION_MINOR 0
#define NVXK_VERSION_PATCH 0

#if defined(_WIN32) || defined(__CYGWIN__)
  #if defined(NVXK_BUILDING_DLL)
    #define NVXK_API __declspec(dllexport)
  #elif defined(NVXK_USING_DLL)
    #define NVXK_API __declspec(dllimport)
  #else
    #define NVXK_API
  #endif
#elif defined(__GNUC__) || defined(__clang__)
  #define NVXK_API __attribute__((visibility("default")))
#else
  #define NVXK_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Return codes for the stable C ABI (0 = success, negative = error)
#define NVXK_OK               0
#define NVXK_ERR_INVALID_ARG -1
#define NVXK_ERR_LENGTH      -2
#define NVXK_ERR_OOM         -3
#define NVXK_ERR_OVERFLOW    -4
#define NVXK_ERR_INTERNAL    -5

// Matrix storage layout and transpose flags for generalized GEMM (v3.0+)
#define NVXK_ROW_MAJOR 0
#define NVXK_COL_MAJOR 1
#define NVXK_NO_TRANS  0
#define NVXK_TRANS     1

// POD hardware capability snapshot for C/FFI callers (v3.0+)
typedef struct nvxk_hw_info_t {
    int physical_cores;
    int logical_threads;
    size_t l1d_bytes;
    size_t l2_bytes;
    size_t l3_bytes;
    int has_avx2;
    int has_fma;
    int has_avx512f;
    int has_neon;
    int has_asm_kernel;
    char vendor[32];
    char brand[64];
} nvxk_hw_info_t;

// Version queries (v3.0+)
NVXK_API int nvxk_version_major(void);
NVXK_API int nvxk_version_minor(void);
NVXK_API int nvxk_version_patch(void);
NVXK_API const char* nvxk_version_string(void);

// Query runtime CPU AVX2+FMA capability (1 = supported, 0 = scalar fallback)
NVXK_API int nvxk_has_avx2_fma(void);

// Query hardware topology and ISA capabilities into caller-provided struct (v3.0+)
NVXK_API int nvxk_get_hw_info(nvxk_hw_info_t* out_info);

// Run bounded hardware calibration and activate the resulting profile
NVXK_API int nvxk_calibrate(double budget_ms);

// Adaptive single-precision GEMM: C(MxN) = A(MxK) * B(KxN)
NVXK_API int nvxk_sgemm(const float* A, const float* B, float* C,
                        size_t M, size_t K, size_t N,
                        int* out_threads_used);

// Manual thread-count single-precision GEMM
NVXK_API int nvxk_sgemm_manual(const float* A, const float* B, float* C,
                               size_t M, size_t K, size_t N,
                               int threads, int* out_threads_used);

// Generalized BLAS-style single-precision GEMM (v3.0+):
//   C = alpha * op(A) * op(B) + beta * C
// threads <= 0 selects Auto planner mode; threads > 0 selects Manual mode.
NVXK_API int nvxk_sgemm_ex(int layout, int trans_a, int trans_b,
                           size_t M, size_t N, size_t K,
                           float alpha, const float* A, size_t lda,
                           const float* B, size_t ldb,
                           float beta, float* C, size_t ldc,
                           int threads, int* out_threads_used);

// Adaptive double-precision GEMM: C(MxN) = A(MxK) * B(KxN)
NVXK_API int nvxk_dgemm(const double* A, const double* B, double* C,
                        size_t M, size_t K, size_t N,
                        int* out_threads_used);

// Generalized BLAS-style double-precision GEMM (v3.0+):
//   C = alpha * op(A) * op(B) + beta * C
// threads <= 0 selects Auto planner mode; threads > 0 selects Manual mode.
NVXK_API int nvxk_dgemm_ex(int layout, int trans_a, int trans_b,
                           size_t M, size_t N, size_t K,
                           double alpha, const double* A, size_t lda,
                           const double* B, size_t ldb,
                           double beta, double* C, size_t ldc,
                           int threads, int* out_threads_used);

// Overflow-checked int32 -> int64 GEMM
NVXK_API int nvxk_gemm_i32_i64(const int32_t* A, const int32_t* B, int64_t* C,
                               size_t M, size_t K, size_t N);

// Thread-local human-readable error message from the last failed C ABI call
NVXK_API const char* nvxk_last_error(void);

#ifdef __cplusplus
}
#endif

#endif