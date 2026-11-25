# NVXK v3.0 Architecture

## Layered Structure
1. **Core Compute Layer (`include/nvxk.h`, `include/nvxk_c.h`, `src/nvxk_*.cpp`, C++11)**:
   - `nvxk_api.cpp`: Input/aliasing validation, `XK_gemm_blas` canonicalization (`ColMajor` -> `RowMajor` via transpose identity), fast-path dispatch, and C ABI wrappers.
   - `nvxk_hw.cpp`: CPUID/XGETBV ISA discovery, cache hierarchy probing, bounded runtime calibration, and thread-safe active profile storage.
   - `nvxk_planner.cpp`: Roofline + OpenMP fork/barrier + SMT L2-halving analytical planner with graceful memory-budget clamping.
   - `nvxk_workspace.cpp`: 64-byte aligned, false-sharing-padded scratchpad allocator reused across GEMM invocations.
   - `nvxk_kernels_avx2.cpp`: Runtime-dispatched BLIS `6x16` f32 (Intrinsics + x86-64 Inline ASM), `6x8` f64, and `Thin-M` zero-pack streaming kernels.
   - `nvxk_kernels_scalar.cpp`: Portable OpenMP/single-thread scalar IKJ and generalized BLAS fallbacks.
2. **Workbench Engine Layer (`adapters/`, `diagnostics/`, `benchmark/`, C++17)**:
   - Decoupled from UI; handles 64B `AlignedBuffer<T>`, `f64` numerical verification, truthful OS telemetry, async benchmark execution, event logging, insights, and Markdown/CSV export.
3. **Presentation Layer (`ui/server/`, `ui/web/`, `ui/imgui/`)**:
   - Loopback HTTP/REST (`127.0.0.1:8765`) + offline Web UI and optional native Dear ImGui + ImPlot Desktop GUI.