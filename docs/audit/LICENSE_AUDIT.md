# LICENSE AUDIT — NVXK UNIFIED COMPUTING ENGINE

> Note: Open-source licenses carry binding legal obligations for source and binary distribution. This audit records verified upstream license texts and compliance actions taken in this repository.

## 1. Upstream License Inventory (Verified from Source Files)

| Component | Source File Checked | License | Copyright Holders | Integration Type |
| :--- | :--- | :--- | :--- | :--- |
| **Google Highway 1.4.0** | `third_party/highway/LICENSE` | Dual: **Apache-2.0** OR **BSD-3-Clause** (`hwy/contrib/random/random-inl.h`: **CC0-1.0**) | Google LLC; Arm Limited and/or its affiliates | Vendored source (`third_party/highway/`) + static link |
| **BLIS 0.8.1-195** | `licenses/BLIS-LICENSE.txt` (from upstream `LICENSE`) | **BSD-3-Clause** | The University of Texas at Austin; Southern Methodist University; Hewlett Packard Enterprise Development LP; Advanced Micro Devices, Inc.; Oracle Corporation | Optional external linked library (`find_package(BLIS)`) |
| **hwloc 3.0.0a1-git** | `third_party/hwloc/COPYING` | **BSD-3-Clause** | Indiana Univ., Univ. of Tennessee, HLRS Stuttgart, UC Regents, CNRS, Inria, Univ. Bordeaux, Cisco Systems, Oracle, IBM, Intel, et al. | Vendored core (`third_party/hwloc/`) or system `libhwloc` |

## 2. License Compatibility & Binary Distribution Obligations

- **Compatibility**: Apache-2.0 and BSD-3-Clause are mutually permissive and compatible with one another and with NVXK. Choosing BSD-3-Clause (or Apache-2.0) for Google Highway introduces zero copyleft (GPL/LGPL) contamination.
- **Mandatory Binary Distribution Obligations**:
  1. Reproduce all copyright notices, conditions, and disclaimers for Google Highway, BLIS (when linked), and hwloc in accompanying documentation (`THIRD_PARTY_NOTICES.md` and `licenses/`).
  2. Do not use the names of the copyright holders (Google, UT Austin, SMU, Inria, Cisco, Intel, AMD, etc.) to endorse or promote NVXK without prior written permission.
  3. Preserve file-level license headers in all vendored source files; never re-label upstream code as authored by NVXK.
  4. Document any modifications to upstream sources: **zero modifications** were made to upstream `third_party/highway` or `third_party/hwloc` source files (Linux build config headers for hwloc are generated out-of-tree during build).
