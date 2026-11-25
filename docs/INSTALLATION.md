# NVXK v3.0 Installation Guide

## System Requirements
- **CPU**: Any 64-bit x86-64 CPU (AVX2+FMA recommended for SIMD acceleration; older CPUs and ARM/non-x86 automatically use portable scalar fallback).
- **GPU**: **Not required** (CPU-only engine; OpenGL 3.3 needed only for optional native Dear ImGui Desktop GUI).
- **RAM**: >= 4 GB recommended (tested target reference PC: Intel Core i7-7700, 16 GB RAM, Intel HD Graphics 630).
- **Compiler**: GCC >= 7 (recommended GCC 12+), Clang >= 10, or MSVC 2019+ (MSVC uses portable scalar fallback).

## 1. Windows (Recommended: MSYS2 UCRT64)
1. Install [MSYS2](https://www.msys2.org/) and open the **MSYS2 UCRT64** terminal.
2. Install GCC, Make, and CMake:
   ```bash
   pacman -S --needed mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-make mingw-w64-ucrt-x86_64-cmake
   ```
3. Build and test:
   ```bash
   make all
   make test
   ```
   This produces `nvxk.exe`, `my_app.exe`, `nvxk.dll`, and `nvxk_workbench.exe`.

## 2. Linux (Debian / Ubuntu / Fedora)
```bash
sudo apt update && sudo apt install -y g++ make
make all
make test
```
This produces `nvxk.exe`, `my_app.exe`, `libnvxk.so`, and `nvxk_workbench.exe`.

## 3. Native Dear ImGui Desktop App (Optional, Requires Internet for CMake FetchContent)
```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DNVXK_BUILD_DESKTOP_IMGUI=ON
cmake --build build --parallel
```
On Windows MSYS2 UCRT64, you may also run `powershell -ExecutionPolicy Bypass -File scripts/build_desktop_windows.ps1`.