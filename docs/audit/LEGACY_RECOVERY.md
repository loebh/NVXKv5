# NVXK Legacy Recovery Analysis (Phase 1 & Phase 2)

## 1. Byte-for-Byte Forensic Diff Summary (Source A vs Source B)
An automated cryptographic and line-level diff between **Source A (`nvxk-audit.txt`, 82 files)** and **Source B (`repomix-full.txt`, 80 files)** established:
- **Zero lost source code**: All 44 core source, header, test, benchmark, script, and CSV result files from Source B (`REPOMIX`) are **100% byte-for-byte identical** in Source A (`AUDIT`). No kernel, tiling heuristic, memory optimization, error check, or CLI feature was accidentally deleted during the transition from REPOMIX to AUDIT.
- **Only 2 modified files**: `CMakeLists.txt` (+103/-3 lines) and `makefile` (+28/-4 lines). Both are strict additive extensions in AUDIT to build `nvxk_workbench.exe`, `workbench-test.exe`, and optional `nvxk_desktop.exe` while preserving every original target (`nvxk.exe`, `my_app.exe`, `nvxk-test.exe`, `libnvxk.so`/`nvxk.dll`, `test-scalar`, `test-san`).
- **34 files unique to REPOMIX**: All 34 files reside under `build_no_omp/` and `build_omp/` (generated CMake cache, compiler ID, and configure logs).

## 2. Diagnostic Evidence Recovered from REPOMIX Build Logs
Although `build_no_omp/` and `build_omp/` must **not** be committed as source files, forensic inspection of `build_omp/CMakeFiles/CMakeConfigureLog.yaml` and `build_no_omp/CMakeFiles/CMakeConfigureLog.yaml` recovered a critical Windows portability lesson:
- On legacy **Windows MinGW GCC 6.3.0** (MinGW.org 32-bit/legacy toolchain), `-fopenmp` failed at link time with `cannot find -lpthread` because `libgomp` required winpthreads, which was absent in MinGW.org GCC 6.3.0 (unlike **MSYS2 UCRT64 GCC 13/14+**, which ships `libgomp` + `libwinpthread` out of the box).
- **Recovery Action Applied in NVXK v3**:
  1. Keep `find_package(OpenMP)` **non-REQUIRED (optional)** in `CMakeLists.txt` so builds automatically succeed in single-thread mode on toolchains lacking `libgomp`/`libpthread`.
  2. Add explicit `NO_OMP=1` / `scalar` build options in `makefile` so users on minimal MinGW toolchains can build without editing the makefile.
  3. Document the exact MinGW GCC 6.3.0 `-lpthread` error and the MSYS2 UCRT64 resolution in `docs/TROUBLESHOOTING.md` and `docs/BUILD_GUIDE.md`.
  4. Add `.gitignore` excluding `build*/`, `*.exe`, `*.o`, `*.so`, `*.dll`, `*.dylib` so build directories never pollute future repository snapshots.

## 3. Legacy Public API Compatibility Guarantee
All 25+ legacy C++ functions (`XK_naive`, `XK_ikj`, `XK_avx2`, `XK_avx2_omp`, `XK_auto`, `XK_omp`, `XK_blocked`, `XK_gemm_naive`, `XK_gemm_ikj`, `XK_gemm_avx2`, `XK_gemm_avx2_asm`, `XK_gemm_blocked`, `XK_gemm_omp`, `XK_gemm_packed_omp`, `XK_gemm`, `XK_gemm_i32_i64`, `vector_add_avx2`) and all 7 existing C ABI functions (`nvxk_has_avx2_fma`, `nvxk_calibrate`, `nvxk_sgemm`, `nvxk_sgemm_manual`, `nvxk_dgemm`, `nvxk_gemm_i32_i64`, `nvxk_last_error`) are preserved with **100% source and binary backward compatibility**.