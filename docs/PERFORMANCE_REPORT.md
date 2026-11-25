# PERFORMANCE REPORT — NVXK UNIFIED COMPUTING ENGINE v5.0.0

> All figures below are **genuinely measured** on the build host (`results/v5_unified_bench.csv` and `results/v5_unified_bench.json`). No simulated, extrapolated, or fabricated metrics are included.

## 1. Measurement Environment & Methodology

- **CPU Model**: `Intel(R) Xeon(R) Platinum 8481C` (Sapphire Rapids, Model 143)
- **Topology (verified via hwloc 3.0.0a1, synced into `XK_detect_hardware`)**: 1 socket, 2 physical cores, 2 logical PUs (SMT 1×), 1 NUMA node
- **Cache Hierarchy**: L1d `48 KiB`/core, L2 `2048 KiB`/core, L3 `107520 KiB` (105 MiB) shared, `64 B` cache line
- **Google Highway Active Target**: `AVX3_SPR` (16 `float` lanes / 8 `double` lanes) + `hwy/contrib/math/math-inl.h`
- **OS / Compiler / Flags**: `Linux 6.12.105+ x86_64`, `g++ 12.2.0`, `-std=c++17 -O3 -fopenmp -Wall -Wextra` (no global `-march=native` / `-mavx2`)
- **Methodology**: Median wall-clock time (`std::chrono::steady_clock`) across 7–15 timed iterations after 2 warm-up iterations; reusable 64-byte aligned `XK_Workspace`.
- **Verified Numerical Accuracy (`nvxk_cli --verify`)**:
  - `GEMM 97×83×71 f32`: `max_rel = 6.26e-7` (`< 1e-4`)
  - `Dot 4099 f32`: `rel_err = 2.03e-7` (`< 1e-4`)
  - `GELU 4099 f32`: `max_abs = 1.19e-7` (`< 1e-5`)
  - `Softmax 4099 f32`: `max_abs = 4.66e-10` (`< 1e-6`)
  - `LayerNorm 4099 f32`: `max_abs = 1.19e-7` (`< 1e-4`)

## 2. Measured Results (`results/v5_unified_bench.csv`)

| Category | Op | Dims | Dtype | Backend | Thr | Pin | Median (ms) | GFLOPS | GB/s | Speedup vs Scalar |
| :--- | :--- | :--- | :--- | :--- | ---: | :---: | ---: | ---: | ---: | ---: |
| Vector | `vec_add` | `1,048,576` | `f32` | `scalar` | 1 | no | `0.421` | `2.489` | `29.869` | 1.00× |
| Vector | `vec_add` | `1,048,576` | `f32` | `highway:AVX3_SPR` | 1 | no | `0.425` | `2.469` | `29.628` | 0.99× (BW-bound) |
| Vector | `dot` | `1,048,576` | `f32` | `scalar` | 1 | no | `0.697` | `3.007` | `12.028` | 1.00× |
| Vector | `dot` | `1,048,576` | `f32` | `highway:AVX3_SPR` | 1 | no | `0.250` | **`8.388`** | **`33.551`** | **2.79×** |
| Vector | `gelu` | `1,048,576` | `f32` | `scalar` | 1 | no | `14.753` | `0.711` | `0.569` | 1.00× |
| Vector | `gelu` | `1,048,576` | `f32` | `highway:AVX3_SPR` | 1 | no | `0.757` | **`13.848`** | **`11.078`** | **19.49×** |
| Vector | `softmax` | `1,048,576` | `f32` | `scalar` | 1 | no | `6.587` | `0.796` | `1.274` | 1.00× |
| Vector | `softmax` | `1,048,576` | `f32` | `highway:AVX3_SPR` | 1 | no | `0.657` | **`7.982`** | **`12.771`** | **10.03×** |
| Vector | `layernorm` | `1,048,576` | `f32` | `scalar` | 1 | no | `1.666` | `3.147` | `5.035` | 1.00× |
| Vector | `layernorm` | `1,048,576` | `f32` | `highway:AVX3_SPR` | 1 | no | `0.556` | **`9.431`** | **`15.090`** | **3.00×** |
| BLAS2 | `gemv` | `1024×1024` | `f32` | `scalar` | 1 | no | `0.669` | `3.136` | `6.284` | 1.00× |
| BLAS2 | `gemv` | `1024×1024` | `f32` | `highway:AVX3_SPR` | 2 | no | `0.058` | **`36.395`** | **`72.932`** | **11.53×** |
| GEMM | `sgemm` | `64×64×64` | `f32` | `nvxk-native` | 1 | no | `0.011` | `47.157` | — | — |
| GEMM | `sgemm` | `64×64×64` | `f32` | `nvxk-native` | 2 | yes | `0.011` | `45.602` | — | — |
| GEMM | `sgemm` | `256×256×256` | `f32` | `nvxk-native` | 1 | no | `0.384` | `87.276` | — | — |
| GEMM | `sgemm` | `256×256×256` | `f32` | `nvxk-native` | 2 | yes | `0.221` | **`151.609`** | — | 1.74× vs 1T |
| GEMM | `sgemm` | `512×512×512` | `f32` | `nvxk-native` | 1 | no | `2.907` | `92.350` | — | — |
| GEMM | `sgemm` | `512×512×512` | `f32` | `nvxk-native` | 2 | yes | `1.637` | **`163.990`** | — | 1.78× vs 1T |
| GEMM | `sgemm` | `4×1024×1024` | `f32` | `nvxk-native` | 1 | no | `0.449` | `18.679` | — | — |
| GEMM | `sgemm` | `4×1024×1024` | `f32` | `nvxk-native` | 2 | yes | `0.213` | **`39.354`** | — | 2.11× vs 1T |
| GEMM | `dgemm` | `256×256×256` | `f64` | `nvxk-native` | 2 | yes | `0.427` | **`78.495`** | — | — |

## 3. Scientific Interpretation

- **Compute-intensive transcendental vector kernels benefit dramatically from Highway (`AVX3_SPR`)**: `GELU` achieves **19.49×** and `Softmax` achieves **10.03×** over scalar `libm` calls because 16 lanes of polynomial `Tanh`/`Exp` execute in parallel inside ZMM registers while keeping `max_abs <= 1.2e-7`.
- **Memory-bound streaming (`vec_add`) saturates RAM bandwidth (~30 GB/s)** under both GCC `-O3` auto-vectorization and Highway, confirming NVXK's selective integration policy.
- **BLIS Crossover Note**: `nvxk::dispatch::calibrate_blis_crossover()` is implemented and ready; live BLIS vs. NVXK Native crossover numbers require building with `-DNVXK_ENABLE_BLIS=ON` on a host with `libblis` installed.
