# DEPENDENCY AUDIT — NVXK UNIFIED COMPUTING ENGINE

## 1. Dependency Classification

| Dependency | Version Audited | Relationship | Required? | Isolation Boundary | Fallback When Absent |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **C++17 Standard Library** | ISO C++17 | System runtime | Required | Public & internal | None |
| **POSIX Threads / Win32 Threads** | System | System runtime | Required | Internal | None |
| **OpenMP** | OpenMP 4.5+ (`libgomp`) | Optional system | Optional (`NVXK_ENABLE_OPENMP`) | Internal | Single-threaded execution |
| **Google Highway** | `1.4.0` | Vendored core (`third_party/highway/`) or system `hwy` | Optional (`NVXK_ENABLE_HIGHWAY`) | `src/backends/highway/` only | Scalar reference vector kernels |
| **hwloc** | `3.0.0a1-git` | Vendored core (`third_party/hwloc/`) or system `libhwloc` | Optional (`NVXK_ENABLE_HWLOC`) | `src/topology/` only | Native CPUID + `/proc/cpuinfo` + OS affinity |
| **BLIS** | `0.8.1-195` | External linked library (`find_package(BLIS)`) | Optional (`NVXK_ENABLE_BLIS`) | `src/backends/blis/` only | NVXK Native AVX2+FMA / Scalar kernels |
| **OpenBLAS** | System (optional) | Optional comparison baseline | Optional (`NVXK_USE_OPENBLAS`) | `src/nvxk_api.cpp` | Disabled by default |
| **GLFW / Dear ImGui / ImPlot** | 3.4 / docking / 0.16 | Optional CMake FetchContent | Optional (`NVXK_BUILD_DESKTOP_IMGUI`) | `ui/imgui/` only | CLI + Web Workbench unaffected |

## 2. Circular Dependency Verification

Verified strict directed acyclic graph (DAG):
- `Layer 5 (CLI / Web / Desktop)` → `Layer 4 (Tests / Benchmarks)` → `Layer 1 (NVXK Core / Dispatcher)` → `Layer 2 (Topology)` & `Layer 3 (Backends: Native, Highway, BLIS, Scalar)`.
- No Layer 3 backend mutates NVXK Core global state or depends on another Layer 3 backend.
- No public header under `include/` includes any third-party header (`hwy/*`, `blis/*`, `hwloc.h`).
