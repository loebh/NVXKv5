# NVXK Legacy Migration Map (Phase 2)

Classification of every component between **Source A (AUDIT)** and **Source B (REPOMIX)** using the 6 mandatory merge categories:
1. **Keep AUDIT** | 2. **Keep REPOMIX** | 3. **Recover REPOMIX -> AUDIT** | 4. **Merge Both** | 5. **Rewrite** | 6. **Remove**

| Component / Path | Source A (AUDIT) | Source B (REPOMIX) | Decision | Technical Rationale |
| :--- | :--- | :--- | :--- | :--- |
| `include/nvxk.h` | v2.0 C++11 API | Identical | **1. Keep AUDIT + Extend** | Zero regressions; extend in v3 with `XK_Layout`, `XK_Transpose`, `XK_gemm_blas` while keeping all 25+ legacy APIs intact. |
| `include/nvxk_c.h` | 7 C ABI functions | Identical | **1. Keep AUDIT + Extend** | Preserve 100% ABI compatibility; add `nvxk_version_*`, `nvxk_get_hw_info`, `nvxk_sgemm_ex`, `nvxk_dgemm_ex`. |
| `src/nvxk_kernels_avx2.cpp` | BLIS 6x16 (Intrinsics+ASM), 6x8 f64, Thin-M | Identical | **1. Keep AUDIT + Extend** | Proven high throughput (+7.4% ASM gain, 2.64x Thin-M vs OpenBLAS); add strided/alpha/beta support without penalizing fast-path. |
| `src/nvxk_kernels_scalar.cpp` | Portable OpenMP scalar IKJ | Identical | **1. Keep AUDIT + Extend** | Add general `layout`/`trans`/`alpha`/`beta`/`ld` scalar fallback so every platform stays 100% correct. |
| `src/nvxk_planner.cpp` | Roofline + SMT + Budget planner | Identical | **1. Keep AUDIT** | Verified cache blocking and memory-budget graceful degradation. |
| `src/nvxk_hw.cpp` | CPUID/XGETBV + calibration + `.cfg` | Identical | **1. Keep AUDIT** | Verified cross-platform detection and thread-safe active profile. |
| `src/nvxk_workspace.cpp` | 64B aligned, false-sharing padded | Identical | **1. Keep AUDIT** | Verified zero leaks and strict budget watermark enforcement. |
| `src/nvxk_api.cpp` | Entrypoints + validation + C ABI | Identical | **1. Keep AUDIT + Extend** | Add generalized BLAS GEMM dispatch, strided aliasing validation, and new C ABI implementations. |
| `benchmarks/main.cpp` | 8-mode CLI benchmark suite | Identical | **1. Keep AUDIT + Extend** | Add `--dtype f32|f64`, `--trans`, `--alpha`, `--beta` options for full v3 validation. |
| `apps/my_app.cpp` | Interactive CLI demo | Identical | **1. Keep AUDIT + Extend** | Add optional precision & transpose selection while preserving default interactive workflow. |
| `tests/nvxk_test.cpp` | 11 core test groups + 4 regressions | Identical | **1. Keep AUDIT + Extend** | Keep all existing tests untouched; add generalized GEMM, NaN-safe `beta=0`, C ABI v3, and stress tests. |
| `adapters/core_adapter/` | C++17 Workbench Adapter | Absent | **1. Keep AUDIT + Extend** | Add generalized GEMM parameters (`layout`, `transA`, `transB`, `alpha`, `beta`) to Matrix Lab & Planner. |
| `diagnostics/cpu/` | 5-tier ISA + OS telemetry | Absent | **1. Keep AUDIT** | Truthful telemetry without fabricated sensors. |
| `benchmark/{runner,insights,reports}/` | Async runner, insights, CSV/MD export | Absent | **1. Keep AUDIT + Extend** | Add structured in-memory event log for UI Diagnostics/Logs view. |
| `ui/server/` & `ui/web/index.html` | Loopback HTTP + offline Web UI | Absent | **1. Keep AUDIT + Extend** | Fix `INC-06/07/10`; add Dark/Light theme, shortcuts, Help, `localStorage` config, generalized GEMM, logs. |
| `ui/imgui/` | Dear ImGui + ImPlot Desktop GUI | Absent | **1. Keep AUDIT + Extend** | Fix `INC-04/05/09`; wire custom matrices, session filter, CSV import, theme, shortcuts, Help, INI config. |
| `tests/workbench_test.cpp` | 8 integration tests | Absent | **1. Keep AUDIT + Extend** | Add tests for generalized Matrix Lab, version/log endpoints, and config handling. |
| `makefile` & `CMakeLists.txt` | Core + Workbench + ImGui | Core only | **4. Merge Both (AUDIT base + REPOMIX lessons)** | Keep AUDIT's complete targets; apply REPOMIX MinGW OpenMP lesson (`NO_OMP=1`, optional OpenMP) + add `debug`/`release`/`scalar`/`simd` targets. |
| `scripts/bench_vs_openblas.py` | Linux-only `libnvxk.so` | Identical | **1. Keep AUDIT + Fix (`INC-08`)** | Add automatic `.dll`/`.so`/`.dylib` detection for Windows/Linux/macOS. |
| `build_no_omp/`, `build_omp/` | Absent | 34 CMake build/log files | **3/6. Extract Diagnostic Lesson -> Remove Build Artifacts** | Do not keep generated build directories in repo; preserve MinGW `-lpthread` diagnostic lesson in `TROUBLESHOOTING.md` and block via `.gitignore`. |
| `README.md` & `docs/` | Fragmented (`INC-01`..`INC-10`) | Minimal | **5. Rewrite / Consolidate** | Produce unified, code-accurate 10-document suite matching NVXK v3. |