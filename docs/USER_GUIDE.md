# NVXK v3.0 User Guide

## 1. CLI Benchmark Suite (`nvxk.exe`)
```bash
# Calibrate hardware and benchmark adaptive AVX2+ASM kernel
./nvxk.exe --calibrate --algorithm auto_asm --sizes 64,128,256,512,1024 --runs 5 --csv results/my_run.csv

# Rectangular / Thin-M shape with planner explanation
./nvxk.exe --algorithm auto_asm --shape 4,1024,1024 --explain

# Cold-cache benchmark (L3 flushed outside timed window)
./nvxk.exe --algorithm auto_asm --sizes 256,512 --cold-cache --runs 5
```

## 2. Interactive Matrix CLI (`my_app.exe`)
Run `./my_app.exe` and enter matrix dimensions `M K N` and values interactively to multiply matrices using `XK_gemm`.

## 3. C++11 Library Usage
```cpp
#include <nvxk.h>
#include <vector>

int main() {
    size_t M = 256, K = 256, N = 256;
    std::vector<float> A(M * K, 1.0f), B(K * N, 2.0f), C(M * N, 0.0f);

    // Simple adaptive GEMM: C = A * B
    XK_gemm(A.data(), B.data(), C.data(), M, K, N);

    // Generalized BLAS GEMM (v3.0+): C = alpha * op(A) * op(B) + beta * C
    XK_gemm_blas(XK_Layout::RowMajor, XK_Transpose::NoTrans, XK_Transpose::Trans,
                 M, N, K, 1.5f, A.data(), K, B.data(), K, 0.5f, C.data(), N);
}
```

## 4. Python FFI Usage (`ctypes`)
```python
import ctypes, numpy as np
lib = ctypes.CDLL("./libnvxk.so")  # or "./nvxk.dll" on Windows
lib.nvxk_version_string.restype = ctypes.c_char_p
print("NVXK version:", lib.nvxk_version_string().decode())
```
See `scripts/bench_vs_openblas.py` for a complete cross-platform Python example.