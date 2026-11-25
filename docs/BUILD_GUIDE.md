# NVXK v3.0 Build Guide

NVXK never passes global `-mavx2`, `-mfma`, or `-march=native` flags. SIMD kernels are compiled with per-function `__attribute__((target("avx2,fma")))` and dispatched at runtime via CPUID/XGETBV.

## Make Build Targets
| Command | Output / Purpose |
| :--- | :--- |
| `make all` / `make release` / `make simd` | Optimized (`-O3 -fopenmp`) build of `nvxk.exe`, `my_app.exe`, shared library (`libnvxk.so` / `nvxk.dll`), and `nvxk_workbench.exe`. |
| `make debug` | Unoptimized debug build (`-O0 -g`) of `nvxk-debug.exe` and `nvxk-test-debug.exe`. |
| `make scalar` | Forces portable scalar fallback (`-DXK_FORCE_SCALAR_FALLBACK`) into `nvxk-scalar.exe` and `nvxk-test-scalar.exe`. |
| `make NO_OMP=1 all` | Single-threaded build without `-fopenmp` (useful on legacy MinGW toolchains lacking `libgomp`/`libpthread`). |
| `make OPENBLAS=1 all` | Links optional CBLAS/OpenBLAS reference (`XK_cblas_sgemm`). |
| `make test` | Builds and runs both `nvxk-test.exe` (core) and `workbench-test.exe` (C++17 workbench). |
| `make test-scalar` | Runs core test suite with AVX2 dispatch completely disabled. |
| `make test-san` | Runs core test suite under AddressSanitizer + UndefinedBehaviorSanitizer (`-fsanitize=address,undefined`). |
| `make clean` | Removes all compiled binaries and shared libraries. |

## CMake Build Options
```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release   -DNVXK_BUILD_SHARED=ON   -DNVXK_BUILD_WORKBENCH=ON   -DNVXK_BUILD_DESKTOP_IMGUI=OFF   -DNVXK_FORCE_SCALAR_FALLBACK=OFF   -DNVXK_USE_OPENBLAS=OFF
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```
Note: `find_package(OpenMP)` is optional in `CMakeLists.txt`; if unavailable, CMake configures a clean single-thread build automatically.