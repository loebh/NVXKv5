# INTEGRATION PLAN — NVXK UNIFIED COMPUTING ENGINE (Phases 0–13)

| Phase | Objective | Deliverables / Modified Files | Acceptance Criteria & Verification Status |
| :--- | :--- | :--- | :--- |
| **Phase 0** | Audit all 4 source repositories | `docs/audit/ARCHITECTURE_AUDIT.md`, `DEPENDENCY_AUDIT.md`, `LICENSE_AUDIT.md`, `FEATURE_MAPPING.md` | **DONE** — All 4 repos audited verbatim from source. |
| **Phase 1** | 5-layer NVXK-centric architecture | `include/nvxk/*.hpp`, `docs/UNIFIED_ARCHITECTURE.md` | **DONE** — Zero circular dependencies; zero 3rd-party header leakage. |
| **Phase 2** | Dependency & license compliance | `THIRD_PARTY_NOTICES.md`, `LICENSE`, `licenses/*.txt`, `sbom.spdx.json` | **DONE** — All copyright notices and license texts preserved verbatim. |
| **Phase 3** | NVXK Core stabilization | `include/nvxk/config.hpp`, `src/core/nvxk_config.cpp` | **DONE** — 100% backward compatibility with `NVXK_v3` (`nvxk-test` + `workbench-test` pass). |
| **Phase 4** | Google Highway 1.4.0 integration | `include/nvxk/simd.hpp`, `src/backends/highway/nvxk_highway_{kernels-inl.h,backend.cpp}` | **DONE** — Compiled real `libhwy.a`; verified `AVX3_SPR` dynamic dispatch & cross-backend equivalence. |
| **Phase 5** | BLIS matrix backend integration | `include/nvxk/blas_backend.hpp`, `src/backends/blis/nvxk_blis_backend.cpp`, `cmake/FindBLIS.cmake` | **DONE (Optional External Link)** — Full official BLIS C API bridge + verified native fallback (note: `blis.txt` omitted `build/`). |
| **Phase 6** | hwloc 3.0.0a1 topology integration | `include/nvxk/topology.hpp`, `src/topology/nvxk_topology.cpp`, `cmake/FindHWLOC.cmake` | **DONE** — Compiled real `libhwloc.a`; verified live cache/core/NUMA discovery & fallback-only build. |
| **Phase 7** | Unified backend orchestration | `include/nvxk/dispatcher.hpp`, `src/core/nvxk_dispatcher.cpp` | **DONE** — Explainable policy, env/CLI overrides, trace logging, safe fallback. |
| **Phase 8** | Cross-platform build system | `CMakeLists.txt`, `makefile`, `docs/BUILD_MATRIX.md` | **DONE** — Portable binary (no global `-mavx2`), optional deps, sanitizers. |
| **Phase 9** | Correctness, numerical & sanitizer testing | `tests/unified_integration_test.cpp`, `tests/nvxk_test.cpp`, `tests/workbench_test.cpp` | **DONE** — All 3 test suites + ASan/UBSan + fallback build pass with 0 warnings. |
| **Phase 10** | Authentic benchmarking | `src/cli/nvxk_cli.cpp`, `results/unified_bench.{csv,json}`, `docs/PERFORMANCE_REPORT.md` | **DONE** — Real measured benchmarks on Intel Xeon Platinum 8481C. |
| **Phase 11** | CLI, Web & Desktop integration | `src/cli/nvxk_cli.cpp`, `nvxk_workbench` | **DONE** — `nvxk_cli` and `nvxk_workbench` compiled & tested. |
| **Phase 12** | Complete documentation rewrite | `README.md`, `docs/UNIFIED_ARCHITECTURE.md`, `docs/BUILD_MATRIX.md`, `docs/PERFORMANCE_REPORT.md` | **DONE** — Grounded in actual code; planned items clearly marked. |
| **Phase 13** | Final independent audit & packaging | `docs/audit/FINAL_INDEPENDENT_AUDIT.md`, `nvxk_unified_computing_engine.zip` | **DONE** — Clean tree packaged and verified. |
