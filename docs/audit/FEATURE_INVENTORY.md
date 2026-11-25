# NVXK Feature Inventory (Phase 1)

Complete inventory of features across **Source A (AUDIT)**, **Source B (REPOMIX)**, and their status entering **NVXK v3**.

| Domain | Feature | Source A (AUDIT) | Source B (REPOMIX) | Status Pre-v3 | Target Status in v3 |
| :--- | :--- | :---: | :---: | :--- | :--- |
| **Core GEMM** | `FP32` BLIS `6x16` AVX2+FMA (Intrinsics) | Yes | Yes (identical) | VERIFIED | Keep & extend with generalized GEMM |
| **Core GEMM** | `FP32` BLIS `6x16` x86-64 Inline ASM | Yes | Yes (identical) | VERIFIED | Keep (fast-path) |
| **Core GEMM** | `FP32` Thin-M Zero-Pack Streaming (`M<=4`) | Yes | Yes (identical) | VERIFIED | Keep (fast-path) |
| **Core GEMM** | `FP64` BLIS `6x8` AVX2+FMA | Yes | Yes (identical) | VERIFIED | Keep & extend with generalized GEMM |
| **Core GEMM** | `INT32 -> INT64` Overflow-Checked GEMM | Yes | Yes (identical) | VERIFIED | Keep |
| **Core GEMM** | Portable Scalar Fallback (`XK_FORCE_SCALAR_FALLBACK`) | Yes | Yes (identical) | VERIFIED | Keep & extend for generalized GEMM |
| **Core GEMM** | Generalized BLAS (`ColMajor`, `TransA/B`, `lda/b/c`, `alpha/beta`) | No | No | MISSING | **NEW in v3** (`XK_gemm_blas`, `nvxk_sgemm_ex`, `nvxk_dgemm_ex`) |
| **Core Vector** | `vector_add_avx2` (`f32`, `f64`) | Yes | Yes (identical) | VERIFIED | Keep |
| **Hardware & Planner** | CPUID/XGETBV + L1/L2/L3 + Physical/Logical Cores | Yes | Yes (identical) | VERIFIED | Keep |
| **Hardware & Planner** | Bounded Runtime Calibration (`XK_calibrate_hardware`) | Yes | Yes (identical) | VERIFIED | Keep |
| **Hardware & Planner** | Roofline & SMT-Aware Planner (`XK_plan_gemm`) | Yes | Yes (identical) | VERIFIED | Keep & surface generalized GEMM info |
| **Hardware & Planner** | `XK_Workspace` 64B Aligned + Budget Clamping | Yes | Yes (identical) | VERIFIED | Keep |
| **C ABI** | `nvxk_sgemm`, `nvxk_sgemm_manual`, `nvxk_dgemm`, `nvxk_gemm_i32_i64` | Yes | Yes (identical) | VERIFIED | Keep (100% ABI compatible) |
| **C ABI** | ABI Versioning (`nvxk_version_*`) & Generalized `*_ex` + HW Query | No | No | MISSING | **NEW in v3** |
| **CLI** | `nvxk.exe` Benchmark CLI & `my_app.exe` Interactive CLI | Yes | Yes (identical) | VERIFIED | Enhance with `--dtype`, `--trans`, `--alpha`, `--beta` |
| **Workbench Backend** | `adapters/core_adapter/` + `diagnostics/cpu/` | Yes | No | VERIFIED | Extend with layout/transpose/alpha/beta support |
| **Workbench Backend** | `benchmark/runner/` + `insights/` + `reports/` | Yes | No | VERIFIED | Keep & add structured event log |
| **Web Workbench** | `ui/server/` (127.0.0.1:8765) + `ui/web/index.html` | Yes | No | VERIFIED | Upgrade: Dark/Light theme, shortcuts, Help, `localStorage`, generalized GEMM, dynamic thread default |
| **Desktop GUI** | `ui/imgui/` (Dear ImGui Docking + ImPlot + GLFW) | Yes | No | PARTIAL (`INC-04/05`) | Complete: wire custom matrices, session filter, CSV import, Dark/Light theme, shortcuts, Help, INI config |
| **Build & Scripts** | `makefile`, `CMakeLists.txt`, `scripts/` | Yes | Partial | PARTIAL (`INC-08`) | Add `debug`/`release`/`scalar`/`simd` targets; fix cross-platform OpenBLAS script |
| **Docs** | User, Install, Build, Desktop, API, Architecture, Benchmark, Troubleshooting | Fragmented | Minimal | INCONSISTENT | Consolidate & complete all 10 required docs |