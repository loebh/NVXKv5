# NVXK v3.0 API Reference

## 1. C++11 Public API (`include/nvxk.h`)
- **Version**: `NVXK_CPP_VERSION_MAJOR` (3), `NVXK_CPP_VERSION_MINOR` (0), `NVXK_CPP_VERSION_PATCH` (0), `const char* XK_version_string()`.
- **Enums**: `XK_Layout { RowMajor, ColMajor }`, `XK_Transpose { NoTrans, Trans }`, `XK_ExecMode { Auto, Manual }`, `XK_ParallelAxis { Auto, None, AxisM, AxisN }`, `XK_KernelKind { ScalarIkj, Avx2Blis6x16, Avx2ThinM, Avx2Blis6x16Asm }`.
- **Generalized BLAS GEMM (v3.0+)**:
  ```cpp
  XK_ExecutionPlan XK_gemm_blas(
      XK_Layout layout, XK_Transpose trans_a, XK_Transpose trans_b,
      size_t M, size_t N, size_t K,
      float alpha, const float* A, size_t lda,
      const float* B, size_t ldb,
      float beta, float* C, size_t ldc,
      const XK_WorkloadDesc* opts = nullptr,
      XK_Workspace* workspace = nullptr,
      const XK_HardwareProfile* profile = nullptr,
      int* actual_thread_count = nullptr);
  ```
  (Identical overload provided for `double`.)
- **Adaptive & Legacy Entrypoints (100% Backward Compatible)**: `XK_gemm`, `XK_gemm_ex`, `XK_gemm_f64_ex`, `XK_gemm_i32_i64`, `XK_gemm_avx2`, `XK_gemm_avx2_asm`, `XK_gemm_omp`, `XK_gemm_blocked`, `XK_gemm_ikj`, `XK_gemm_naive`, `vector_add_avx2`, `XK_detect_hardware`, `XK_calibrate_hardware`, `XK_plan_gemm`.

## 2. Stable C ABI (`include/nvxk_c.h`)
- **Return Codes**: `NVXK_OK (0)`, `NVXK_ERR_INVALID_ARG (-1)`, `NVXK_ERR_LENGTH (-2)`, `NVXK_ERR_OOM (-3)`, `NVXK_ERR_OVERFLOW (-4)`, `NVXK_ERR_INTERNAL (-5)`.
- **Version & Hardware**: `nvxk_version_major()`, `nvxk_version_minor()`, `nvxk_version_patch()`, `nvxk_version_string()`, `nvxk_has_avx2_fma()`, `nvxk_get_hw_info(nvxk_hw_info_t*)`, `nvxk_calibrate(double budget_ms)`, `nvxk_last_error()`.
- **GEMM Functions**: `nvxk_sgemm`, `nvxk_sgemm_manual`, `nvxk_sgemm_ex`, `nvxk_dgemm`, `nvxk_dgemm_ex`, `nvxk_gemm_i32_i64`.