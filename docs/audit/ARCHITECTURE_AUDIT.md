# ARCHITECTURE AUDIT — NVXK UNIFIED COMPUTING ENGINE (Phase 0)

> Audit Date: 2026-11-24 | Auditor Environment: Linux 6.12.105+ x86_64, `Intel(R) Xeon(R) Platinum 8481C` (2 physical cores / 2 logical PUs, L1d 48 KiB, L2 2 MiB, L3 105 MiB, AVX2/FMA/AVX-512F/DQ/BW/VL), `g++ 12.2.0`.

## 1. Per-Repository Technical Inventory

| Attribute | NVXK (`NVXK_v3`) | Google Highway (`1.4.0`) | BLIS (`0.8.1-195`) | hwloc (`3.0.0a1-git`) |
| :--- | :--- | :--- | :--- | :--- |
| **Files Inspected** | 97 files (832 KB) | 272 files (6.4 MB) | 2,119 files (23 MB) | 684 files (8.1 MB) |
| **Primary Language** | C++11 (Core) / C++17 (Workbench) | C++17 | C99 + x86/ARM inline ASM | C99 |
| **Build System** | `CMakeLists.txt` + `makefile` | `CMakeLists.txt` + `meson.build` | Custom `./configure` + `Makefile` | Autotools + `contrib/windows-cmake/CMakeLists.txt` |
| **Compiler Requirements** | GCC / Clang / MSVC (C++11/17) | GCC / Clang / MSVC (C++17) | GCC / Clang / ICC (C99 + GNU `as`) | GCC / Clang / MSVC (C99) |
| **Supported Platforms** | Windows, Linux, macOS | x86, ARM (NEON/SVE), RISC-V, PPC, WASM, Scalar | x86_64, ARM64/SVE, Power, RISC-V, Generic | Linux, Windows, macOS, FreeBSD, Solaris, AIX |
| **Public API** | `include/nvxk.h`, `include/nvxk_c.h` | `hwy/highway.h`, `hwy/foreach_target.h`, `hwy/targets.h` | `frame/include/blis.h` (`bli_sgemm`, `bli_dgemm`, `bli_syrk`, `bli_trsm`, CBLAS) | `include/hwloc.h` (`hwloc_topology_*`, `hwloc_set_cpubind`, `hwloc_bitmap_*`) |
| **License** | NVXK Project License | Dual: Apache-2.0 OR BSD-3-Clause (`random-inl.h`: CC0-1.0) | BSD-3-Clause | BSD-3-Clause |

## 2. Actual Capabilities & Overlapping Functions

- **NVXK Core (`NVXK_v3`)**:
  - Hardware discovery (`src/nvxk_hw.cpp` via CPUID + `/proc/cpuinfo` + Windows `GetLogicalProcessorInformationEx`), hardware calibration (`XK_calibrate_hardware`), cache-aware GEMM execution planner (`src/nvxk_planner.cpp`), 64-byte aligned reusable workspace (`src/nvxk_workspace.cpp`), AVX2+FMA 6×16 intrinsics/ASM and Thin-M kernels (`src/nvxk_kernels_avx2.cpp`), portable scalar kernels (`src/nvxk_kernels_scalar.cpp`), stable C ABI (`nvxk_c.h`), Engineering Workbench HTTP server + Web UI + ImGui Desktop UI.
- **Overlaps Identified**:
  1. **Vector SIMD**: NVXK had only `vector_add_avx2` (f32/f64). Highway 1.4.0 provides portable multi-target runtime dispatch across AVX-512 (`AVX3_SPR`, `AVX3_DL`, `AVX3`), AVX2, SSE4, NEON, SVE, and Scalar. **Decision**: NVXK keeps AVX2 GEMM microkernels and delegates portable vector/reduction/activation/GEMV kernels to Highway under `nvxk::simd`.
  2. **GEMM**: Both NVXK native (6×16 AVX2+FMA/ASM) and BLIS implement GotoBLAS/BLIS-style cache-blocked GEMM. **Decision**: NVXK Core (`nvxk::dispatch`) governs routing — tiny matrices use Scalar, Thin-M uses NVXK Thin-M, medium matrices use NVXK Native, and large matrices route to BLIS when linked (`NVXK_ENABLE_BLIS=ON`).
  3. **CPU/Cache Topology**: NVXK `src/nvxk_hw.cpp` uses direct CPUID + `/proc/cpuinfo`. hwloc 3.0.0a1 provides full hierarchical topology (Packages, Cores, PUs, NUMA nodes, cache sharing, OS affinity). **Decision**: Layer 2 (`nvxk::topology`) uses hwloc when enabled and retains NVXK's native discovery as zero-dependency fallback.

## 3. Technical Limitations & Integration Risks Discovered in Source Audit

1. **BLIS Repomix Packaging Gap**: In the uploaded `blis.txt`, the upstream `build/` helper directory (containing `build/version`, `build/so_version`, and `build/detect/`) was omitted by Repomix's default `.gitignore`/`build/` exclusion filter, preventing `./configure` from running directly from the Repomix dump alone. **Mitigation**: NVXK implements the complete official BLIS C API backend (`src/backends/blis/nvxk_blis_backend.cpp`) and `cmake/FindBLIS.cmake` to link against system or user-built `libblis`, with automatic safe fallback to NVXK native kernels when `NVXK_ENABLE_BLIS=OFF`.
2. **hwloc ABI Struct Alignment Sensitivity**: `hwloc_thread_t` must be defined consistently (`#define hwloc_thread_t pthread_t` on Linux) across all hwloc translation units; mismatched definitions alter `struct hwloc_binding_hooks` layout (caught and resolved via AddressSanitizer during Phase 9).
3. **OpenMP Thread Oversubscription Risk**: If both NVXK's OpenMP loops and BLIS's internal threading spawn full thread teams concurrently, oversubscription degrades performance. **Mitigation**: `nvxk::dispatch` resolves a single thread budget bounded by hwloc `logical_pus` and explicitly sets `bli_thread_set_num_threads(dec.threads)` before invoking BLIS.
4. **Header Leakage Prevention**: None of `hwy/highway.h`, `blis/blis.h`, or `hwloc.h` are included in any public `include/nvxk*.h*` header; all third-party symbols remain strictly behind `src/` translation units.
