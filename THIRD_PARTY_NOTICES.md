# THIRD-PARTY NOTICES — NVXK UNIFIED COMPUTING ENGINE

NVXK Unified Computing Engine incorporates or optionally links against the following third-party open-source projects. NVXK does **not** claim authorship of any third-party library; all original copyright notices and license texts are preserved verbatim in `licenses/`.

---

## 1. Google Highway (`1.4.0`)
- **Integration**: Vendored core library in `third_party/highway/` (unmodified upstream source).
- **License**: Dual-licensed under Apache License 2.0 OR BSD 3-Clause License (`hwy/contrib/random/random-inl.h` under CC0 1.0 Universal).
- **Copyright**: Copyright 2019–2024 Google LLC; Copyright 2024 Arm Limited and/or its affiliates.
- **Full License Text**: `licenses/HIGHWAY-LICENSE.txt`

## 2. BLIS (`0.8.1-195`)
- **Integration**: Optional externally linked library via `NVXK_ENABLE_BLIS=ON` (`cmake/FindBLIS.cmake` / `-lblis`). Not vendored as binary; zero upstream source files modified.
- **License**: BSD 3-Clause License.
- **Copyright**:
  - Copyright (C) 2012 - 2022, The University of Texas at Austin
  - Copyright (C) 2018 - 2025, Southern Methodist University
  - Copyright (C) 2016, Hewlett Packard Enterprise Development LP
  - Copyright (C) 2018 - 2019, Advanced Micro Devices, Inc.
  - Copyright (C) 2022, Oracle Corporation
- **Full License Text**: `licenses/BLIS-LICENSE.txt`

## 3. Portable Hardware Locality — hwloc (`3.0.0a1-git`)
- **Integration**: Vendored core CPU/cache/NUMA topology sources in `third_party/hwloc/` (unmodified upstream source) or optional system `libhwloc` via `NVXK_ENABLE_HWLOC=ON`.
- **License**: BSD 3-Clause License.
- **Copyright**: Copyright © 2004–2025 Indiana University, University of Tennessee, HLRS Stuttgart, University of California, CNRS, Inria, Université Bordeaux, Cisco Systems, Inc., Oracle, IBM, Intel, Inc., et al.
- **Full License Text**: `licenses/HWLOC-COPYING.txt`
