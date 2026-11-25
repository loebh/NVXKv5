# NVXK v3.0 Troubleshooting Guide

## 1. Windows MinGW: `cannot find -lpthread` when linking `-fopenmp`
- **Cause** (recovered from legacy `build_omp` logs): Legacy MinGW.org GCC 6.3.0 ships `libgomp` without `libpthread`.
- **Fix 1 (Recommended)**: Use **MSYS2 UCRT64 GCC 13/14+**, which includes full OpenMP + winpthreads support.
- **Fix 2 (Fallback)**: Build without OpenMP using `make NO_OMP=1 all` (or CMake, where `find_package(OpenMP)` is optional).

## 2. `bench_vs_openblas.py` cannot find shared library
- Build the shared library first with `make all` (`libnvxk.so` on Linux, `nvxk.dll` on Windows, `libnvxk.dylib` on macOS). NVXK v3.0 automatically resolves the right library name per OS.

## 3. Port 8765 already in use when starting `nvxk_workbench.exe`
- Start on an alternate port: `./nvxk_workbench.exe --port 8766`.

## 4. Testing without AVX2 hardware
- Run `make test-scalar` to force the portable scalar fallback path (`-DXK_FORCE_SCALAR_FALLBACK`) and verify 100% functional and numerical parity.