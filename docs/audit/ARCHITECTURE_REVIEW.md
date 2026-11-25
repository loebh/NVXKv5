# NVXK Architecture Review (Phase 1)

## 1. Current Layered Architecture
NVXK follows a clean **two-tier architecture** separating the zero-dependency C++11 compute core from the C++17 Engineering Workbench:

1. **Core Engine (`include/nvxk.h`, `include/nvxk_c.h`, `src/nvxk_*.cpp`, C++11)**:
   - `nvxk_api.cpp`: Public C++ entrypoints, validation (null pointers, overflow, aliasing), and `extern "C"` ABI wrapper with thread-local error strings.
   - `nvxk_hw.cpp`: Cross-platform CPUID/XGETBV feature discovery, L1d/L2/L3 cache sizing, physical/logical core detection, bounded runtime calibration, and `.cfg` profile persistence.
   - `nvxk_planner.cpp`: Roofline + OpenMP overhead + SMT L2-sharing analytical planner producing `XK_ExecutionPlan` with human-readable `reason`.
   - `nvxk_workspace.cpp`: 64-byte aligned, false-sharing-padded scratchpad allocator with strict memory budget enforcement.
   - `nvxk_kernels_avx2.cpp`: Runtime-dispatched (`__attribute__((target("avx2,fma")))`) BLIS `6x16` f32 (intrinsics + x86-64 inline ASM), `6x8` f64, and `Thin-M` zero-pack streaming kernels.
   - `nvxk_kernels_scalar.cpp`: Portable OpenMP/single-thread scalar IKJ fallbacks (`f32`, `f64`, `i32->i64`).
2. **Workbench & UI Tier (C++17)**:
   - **Adapter Layer** (`adapters/core_adapter/`): Wraps core APIs, provides 64B `AlignedBuffer<T>`, Matrix Lab execution, and `f64` numerical validation + error heatmaps.
   - **Diagnostics Layer** (`diagnostics/cpu/`): 5-tier ISA capability matrix and truthful OS telemetry (explicitly marks unsupported PMU/thermal sensors as `Unavailable`).
   - **Service Layer** (`benchmark/runner/`, `benchmark/insights/`, `benchmark/reports/`): Async `std::thread` benchmark worker with safe cancellation, Measurement Mode, rule-based insights, and CSV/Markdown exporters.
   - **Presentation Layer**: Native Desktop (`ui/imgui/`, Dear ImGui Docking + ImPlot + GLFW) and Offline Web Workbench (`ui/server/` loopback HTTP/REST + single-file `ui/web/index.html`).

## 2. Architectural Strengths
- **Strict ISA Isolation**: No global `-mavx2`/`-mfma` compiler flags; portable binaries run safely on pre-AVX2 CPUs via scalar fallback.
- **Zero-Allocation Hot Path**: `XK_Workspace` pre-allocates outside OpenMP parallel regions and reuses aligned scratchpads across calls.
- **No Mock Telemetry**: Unsupported hardware counters (IPC, cache hit rate, live YMM registers) are transparently marked `Unavailable` or `[CONCEPTUAL]`.
- **Clean Engine/UI Decoupling**: Both `nvxk_desktop` and `nvxk_workbench` share the exact same `nvxk_workbench_engine` static library.

## 3. Architectural Bottlenecks & Gaps Addressed in v3
1. **Tight-Layout Assumption in Core API**: `XK_gemm_ex` assumes `RowMajor`, `NoTrans`, `NoTrans`, `alpha=1`, `beta=0`, `lda=K, ldb=N, ldc=N`. Adding `XK_GemmParams` / `XK_gemm_blas` with fast-path detection preserves 100% of existing peak performance while unlocking full BLAS GEMM semantics.
2. **Missing C ABI Metadata & Extended GEMM**: External callers (Python/Rust/C) cannot query library version, hardware info, or pass `trans`/`alpha`/`beta`/`ld` through `include/nvxk_c.h`.
3. **Desktop vs Web Feature Parity**: `ui/imgui/` lagged behind `ui/web/` on custom matrix entry, session filtering, and CSV import, while both lacked Dark/Light theme toggle, keyboard shortcuts, Help documentation, and config persistence.
4. **Documentation Fragmentation**: Overlapping files across `docs/` and `docs/*/` created 10 sync inconsistencies (`INC-01`..`INC-10`).