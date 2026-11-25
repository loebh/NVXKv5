# FEATURE MAPPING & INTEGRATION PLAN — NVXK UNIFIED COMPUTING ENGINE

## 1. Selective Integration & Ownership Matrix

| Capability | Owner | Primary Backend | Fallback Backend | Activation Condition |
| :--- | :--- | :--- | :--- | :--- |
| **Runtime Config & Env Policy** | NVXK Core | Native (`nvxk_config.cpp`) | Always present | Always |
| **Execution Planning & Cost Model** | NVXK Core | Native (`nvxk_planner.cpp`) | Always present | Always |
| **Aligned Workspace & Memory Budget** | NVXK Core | Native (`nvxk_workspace.cpp`) | Always present | Always |
| **Hardware Topology & Thread Pinning** | NVXK Core (Layer 2) | **hwloc 3.0.0a1** | Native CPUID + `/proc/cpuinfo` + `pthread_setaffinity_np` | `NVXK_ENABLE_HWLOC=ON` |
| **Portable Vector SIMD (`add/sub/mul/axpy/dot/norm/sum/max/relu/softmax/gemv`)** | NVXK Core (Layer 3A) | **Google Highway 1.4.0** (`HWY_DYNAMIC_DISPATCH`) | Scalar reference | `NVXK_ENABLE_HIGHWAY=ON` and `n >= highway_min_elems_threshold` |
| **Small / Tiny GEMM (`2*M*N*K < 8192`)** | NVXK Core | Scalar reference | Always present | Automatic (avoids packing & OMP overhead) |
| **Thin-M GEMM (`M <= 4, N >= 16, K >= 8`, f32)** | NVXK Core | NVXK Native AVX2 Thin-M | Scalar reference | AVX2+FMA detected |
| **Medium / General GEMM** | NVXK Core | NVXK Native AVX2+FMA 6×16 / 6×8 (ASM/Intrinsics) | Scalar reference | AVX2+FMA detected |
| **Large GEMM (`2*M*N*K >= 2e7`) & Level-3 BLAS (`SYRK`, `TRSM`)** | NVXK Core (Layer 3B) | **BLIS** (`bli_sgemm`, `bli_dgemm`, `bli_syrk`, `bli_trsm`) | NVXK Native AVX2 / Scalar | `NVXK_ENABLE_BLIS=ON` and `libblis` linked |
| **Validation, Diagnostics & Benchmarks** | NVXK Core (Layer 4) | Native (`nvxk-test`, `nvxk-unified-test`, `nvxk_cli`) | Always present | Build options `NVXK_ENABLE_TESTS` / `NVXK_ENABLE_BENCHMARKS` |
| **CLI, Web Workbench & Desktop UI** | NVXK Core (Layer 5) | `nvxk_cli`, `nvxk`, `nvxk_workbench`, `nvxk_desktop` | CLI always available | Build options |

## 2. Explicit Non-Integrated Features (Deliberate Exclusions)

- **Highway `vqsort` & `contrib/image`**: Excluded from NVXK Core to keep binary size and build time small; not needed for dense numerical computing.
- **hwloc PCI/CUDA/OpenCL/NVML/GL/libxml2 plugins**: Excluded from NVXK's CPU topology layer to avoid heavy GPU/XML runtime dependencies; NVXK uses hwloc's minimal CPU/cache/NUMA/Linux/x86 components.
- **BLIS Fortran-77 BLAS compatibility symbols & sandbox plugins**: Excluded; NVXK invokes BLIS's typed C API (`bli_sgemm`, `bli_dgemm`, `bli_ssyrk`, `bli_dsyrk`, `bli_strsm`, `bli_dtrsm`) directly with zero-copy generalized strides.
