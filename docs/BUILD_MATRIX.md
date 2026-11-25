# BUILD MATRIX & CROSS-PLATFORM STATUS — NVXK v5.0.0

## 1. Verified vs. Supported Build Matrix

| Platform | Compiler | Build System | Subsystems Tested | Verification Status |
| :--- | :--- | :--- | :--- | :--- |
| **Linux x86_64** (Debian 12, Xeon 8481C) | `g++ 12.2.0` + `gcc 12.2.0` | `makefile` | Highway 1.4.0 (`AVX3_SPR`) + hwloc 3.0.0a1 + OpenMP (`BLIS=OFF`) | **TESTED & PASSED** (0 warnings, `nvxk-test`, `nvxk-unified-test`, `workbench-test`, `nvxk_cli`) |
| **Linux x86_64** (ASan + UBSan) | `g++ 12.2.0` (`-fsanitize=address,undefined`) | `makefile test-san` | Highway 1.4.0 + hwloc 3.0.0a1 + OpenMP | **TESTED & PASSED** (0 leaks, 0 UB, 0 OOB) |
| **Linux x86_64** (Minimal Fallback) | `g++ 12.2.0` (`-Wall -Wextra -Werror`) | `g++` direct | `HIGHWAY=0`, `HWLOC=0`, `BLIS=0` | **TESTED & PASSED** (0 warnings, native fallback active) |
| **Linux x86_64 + BLIS** | GCC / Clang | CMake (`-DNVXK_ENABLE_BLIS=ON`) | Highway + hwloc + external `libblis` | **CODE-VERIFIED / NOT LINK-TESTED ON VM** (`libblis` absent on sandbox VM) |
| **Windows x64 (MSVC / MinGW)** | MSVC 2022 / MinGW-w64 | CMake / PowerShell | Highway + optional hwloc/BLIS + ImGui Desktop | **SUPPORTED IN CMAKE / NOT EXECUTED ON LINUX VM** |
| **macOS (Apple Silicon / x86_64)** | Apple Clang | CMake | Highway (NEON/AVX2) + Scalar fallback | **SUPPORTED IN CMAKE / NOT EXECUTED ON LINUX VM** |

## 2. Portability Guarantees

- **No Mandatory Global AVX2 Flag**: Neither `CMakeLists.txt` nor `makefile` passes `-mavx2` or `-march=native` globally. AVX2/FMA/AVX-512 code is compiled only in target-attributed functions (`nvxk_kernels_avx2.cpp`) and Highway multi-target dispatch (`foreach_target.h`), so binaries run safely on CPUs without AVX2 via scalar fallback.
- **All 3 External Subsystems Are Optional**: `NVXK_ENABLE_HIGHWAY`, `NVXK_ENABLE_BLIS`, and `NVXK_ENABLE_HWLOC` can each be toggled independently (`ON`/`OFF`).
