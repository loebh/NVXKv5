# NVXK v3.0 Benchmark Guide & Measured Results

## Methodology
- Warm-up run executed before timed samples; statistical reporting includes median, mean, stddev, GFLOPS, workspace KiB, and `MaxRelErr` against `f64` reference.
- Cold-cache mode (`--cold-cache`) flushes L3 strictly outside the timed window.
- OpenBLAS comparison (`scripts/bench_vs_openblas.py`) runs identical NumPy `f32`/`f64` matrices through `libnvxk.so`/`nvxk.dll` and `np.matmul`.

## Measured v3.0 Results (Linux GCC 12.2.0, Intel Xeon Platinum 8481C, 2C/2T, AVX2+FMA)
| Workload | Scalar IKJ (1T) | AVX2 Intrinsics (1T) | NVXK v3 `auto_asm` (2T) | OpenBLAS (2T, AVX-512) | MaxRelErr |
| :--- | ---: | ---: | ---: | ---: | ---: |
| `f32 128x128x128` | 16.98 GF | 75.18 GF | 81.75 GF (1T) | 101.98 GF | `2.29e-06` |
| `f32 256x256x256` | 16.90 GF | 81.36 GF | 151.54 GF | 205.29 GF | `6.26e-06` |
| `f32 512x512x512` | 17.15 GF | 80.82 GF | 164.52 GF (**9.59x** vs IKJ) | 232.89 GF | `1.29e-05` |
| `f32 1024x1024x1024` | — | 81.52 GF | 166.17–168.73 GF | 264.29 GF | `2.69e-05` |
| `f32 4x1024x1024` (Thin-M) | — | — | **36.71 GF (1T)** | 25.53 GF | `1.23e-05` |
| `f64 1024x1024x1024` | — | — | 76.85 GF | 134.46 GF | `5.19e-14` |

### Generalized BLAS C ABI v3.0 (`nvxk_sgemm_ex`, `512x512x512`, 2T)
- `ColMajor NoTrans/NoTrans`: `73.45 GFLOPS` (`MaxRelErr = 3.58e-05`)
- `RowMajor NoTrans/Trans (alpha=1.5, beta=-0.5)`: `68.29 GFLOPS` (`MaxRelErr = 5.24e-05`)
- `RowMajor Trans/NoTrans (alpha=1.5, beta=0.5)`: `67.79 GFLOPS` (`MaxRelErr = 7.60e-05`)

*Note*: On Xeon 8481C, OpenBLAS uses AVX-512 (512-bit FMA), whereas NVXK uses AVX2 (256-bit FMA). On AVX2-only CPUs such as Intel Core i7-7700, NVXK's relative ratio to OpenBLAS is expected to be higher; direct Windows i7-7700 verification should be run using `scripts/run_experiments.ps1`.