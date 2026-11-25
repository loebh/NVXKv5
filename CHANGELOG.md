# Changelog

All notable changes to NVXK are documented in this file.

## [3.0.0] - 2026-11-24
### Added
- **Generalized BLAS GEMM (`XK_gemm_blas`)** for `FP32` and `FP64` supporting `XK_Layout::RowMajor`/`ColMajor`, `XK_Transpose::NoTrans`/`Trans`, arbitrary `lda`/`ldb`/`ldc`, `alpha`/`beta`, NaN-safe `beta == 0`, and zero-overhead fast-path routing to BLIS/ASM/Thin-M kernels.
- **C ABI v3.0 (`include/nvxk_c.h`)**: `NVXK_VERSION_MAJOR/MINOR/PATCH`, `nvxk_version_major/minor/patch/string()`, `nvxk_get_hw_info()`, `nvxk_sgemm_ex()`, `nvxk_dgemm_ex()`.
- **Workbench & UI Upgrades**:
  - `GET /api/logs`, `POST /api/matrix_lab/blas`, and `"version":"3.0.0"` in `GET /api/state`.
  - Dark/Light theme toggle with `localStorage` (Web) and `nvxk_workbench.ini` (ImGui Desktop) persistence.
  - Keyboard shortcuts (`Ctrl+B`, `Ctrl+K`, `Alt+T`, `?`/`Esc`) and built-in Help + Generalized BLAS Quick-Tester modal.
  - Dynamic `bench_bk_flags_` vector in ImGui (`INC-09`) and dynamic `physical_cores` default in Web Quick Benchmark (`INC-10`).
- **Build & Portability**: Added `debug`, `release`, `simd`, `scalar`, and `NO_OMP=1` targets in `makefile`; added `.gitignore`; updated `CMakeLists.txt` to `VERSION 3.0.0`.
- **Audit & Documentation**: Added complete Phase 1–2 audit suite (`docs/audit/*`) and consolidated 10-document canonical docs suite.

### Fixed
- Eliminated `-Wunused-parameter` warnings in `src/nvxk_kernels_avx2.cpp` when compiling with `NO_OMP=1`.
- Guarded OpenMP-only `AxisN` assertion in `tests/nvxk_test.cpp` so `NO_OMP=1` builds pass 100% of unit tests.
- Fixed `scripts/bench_vs_openblas.py` to auto-resolve `nvxk.dll` (Windows), `libnvxk.dylib` (macOS), and `libnvxk.so` (Linux) (`INC-08`).
- Added non-blocking Linux `xdg-open` browser launch when `DISPLAY` is set (`INC-07`).
- Resolved all 10 documentation/code inconsistencies (`INC-01`..`INC-10`).

## [2.0.0] - Previous Audit Baseline
- Modularized core into 6 modules (`nvxk_api`, `nvxk_hw`, `nvxk_planner`, `nvxk_workspace`, `nvxk_kernels_avx2`, `nvxk_kernels_scalar`).
- Added x86-64 inline ASM `6x16` microkernel, `Thin-M` zero-pack streaming, Roofline/SMT planner, and C++17 Engineering Workbench (Web + Dear ImGui).