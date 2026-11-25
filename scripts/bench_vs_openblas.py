#!/usr/bin/env python3
"""
Controlled benchmark comparing NVXK v2 (via C ABI libnvxk.so) against
OpenBLAS 0.3.27 (via NumPy np.matmul) on identical matrices and thread counts.
"""
import ctypes
import csv
import os
import time
import platform
import numpy as np


def resolve_nvxk_lib():
    sys_name = platform.system()
    candidates = (
        ["nvxk.dll", "build/nvxk.dll", "build/Release/nvxk.dll"]
        if sys_name == "Windows"
        else (["libnvxk.dylib", "build/libnvxk.dylib"] if sys_name == "Darwin" else ["libnvxk.so", "build/libnvxk.so"])
    )
    for c in candidates:
        if os.path.exists(c):
            return os.path.abspath(c)
    return os.path.abspath(candidates[0])

LIB_PATH = os.path.abspath(resolve_nvxk_lib())
lib = ctypes.CDLL(LIB_PATH)

lib.nvxk_calibrate.argtypes = [ctypes.c_double]
lib.nvxk_calibrate.restype = ctypes.c_int

lib.nvxk_sgemm.argtypes = [
    ctypes.POINTER(ctypes.c_float),
    ctypes.POINTER(ctypes.c_float),
    ctypes.POINTER(ctypes.c_float),
    ctypes.c_size_t, ctypes.c_size_t, ctypes.c_size_t,
    ctypes.POINTER(ctypes.c_int),
]
lib.nvxk_sgemm.restype = ctypes.c_int

lib.nvxk_dgemm.argtypes = [
    ctypes.POINTER(ctypes.c_double),
    ctypes.POINTER(ctypes.c_double),
    ctypes.POINTER(ctypes.c_double),
    ctypes.c_size_t, ctypes.c_size_t, ctypes.c_size_t,
    ctypes.POINTER(ctypes.c_int),
]
lib.nvxk_dgemm.restype = ctypes.c_int


def measure(fn, runs=9):
    # Warm-up
    fn()
    # Adaptive repeat count for short kernels
    iters = 1
    while True:
        t0 = time.perf_counter()
        for _ in range(iters):
            fn()
        dt = time.perf_counter() - t0
        if dt >= 0.04 or iters >= 2048:
            break
        iters *= 2
    samples = []
    for _ in range(runs):
        t0 = time.perf_counter()
        for _ in range(iters):
            fn()
        samples.append((time.perf_counter() - t0) / iters)
    arr = np.array(samples, dtype=np.float64)
    return {
        "min": float(np.min(arr)),
        "median": float(np.median(arr)),
        "mean": float(np.mean(arr)),
        "std": float(np.std(arr, ddof=1)) if len(arr) > 1 else 0.0,
        "p95": float(np.percentile(arr, 95)),
    }


def main():
    assert lib.nvxk_calibrate(200.0) == 0, "nvxk_calibrate failed"
    rng = np.random.default_rng(42)

    shapes = [
        (128, 128, 128),
        (256, 256, 256),
        (512, 512, 512),
        (1024, 1024, 1024),
        (1, 1024, 1024),
        (4, 1024, 1024),
        (64, 1024, 1024),
        (512, 256, 512),
    ]

    os.makedirs("results", exist_ok=True)
    rows = []

    print(f"{'Dtype':<6} {'Shape':<15} {'NVXK(GF)':>10} {'OBLAS(GF)':>10} {'Ratio':>8} {'Threads':>8} {'MaxRelErr':>11}")
    print("-" * 74)

    for dtype, tag in [(np.float32, "f32"), (np.float64, "f64")]:
        for (M, K, N) in shapes:
            A = np.ascontiguousarray(rng.uniform(-1.0, 1.0, size=(M, K)).astype(dtype))
            B = np.ascontiguousarray(rng.uniform(-1.0, 1.0, size=(K, N)).astype(dtype))
            C_nvxk = np.empty((M, N), dtype=dtype)
            C_blas = np.empty((M, N), dtype=dtype)
            used_t = ctypes.c_int(0)
            ops = 2.0 * M * K * N

            if dtype == np.float32:
                ap = A.ctypes.data_as(ctypes.POINTER(ctypes.c_float))
                bp = B.ctypes.data_as(ctypes.POINTER(ctypes.c_float))
                cp = C_nvxk.ctypes.data_as(ctypes.POINTER(ctypes.c_float))
                st_nvxk = measure(lambda: lib.nvxk_sgemm(ap, bp, cp, M, K, N, ctypes.byref(used_t)))
            else:
                ap = A.ctypes.data_as(ctypes.POINTER(ctypes.c_double))
                bp = B.ctypes.data_as(ctypes.POINTER(ctypes.c_double))
                cp = C_nvxk.ctypes.data_as(ctypes.POINTER(ctypes.c_double))
                st_nvxk = measure(lambda: lib.nvxk_dgemm(ap, bp, cp, M, K, N, ctypes.byref(used_t)))

            st_blas = measure(lambda: np.matmul(A, B, out=C_blas))

            denom = np.maximum(1.0, np.abs(C_blas.astype(np.float64)))
            max_rel = float(np.max(np.abs(C_nvxk.astype(np.float64) - C_blas.astype(np.float64)) / denom))
            tol = 1e-4 if dtype == np.float32 else 1e-11
            assert max_rel < tol, f"Accuracy mismatch on {tag} {M}x{K}x{N}: {max_rel}"

            gf_nvxk = (ops / st_nvxk["median"]) * 1e-9
            gf_blas = (ops / st_blas["median"]) * 1e-9
            ratio = gf_nvxk / gf_blas if gf_blas > 0 else 0.0

            shape_str = f"{M}x{K}x{N}"
            print(f"{tag:<6} {shape_str:<15} {gf_nvxk:>10.2f} {gf_blas:>10.2f} {ratio*100:>7.1f}% {used_t.value:>8d} {max_rel:>11.2e}")
            rows.append({
                "dtype": tag, "M": M, "K": K, "N": N,
                "nvxk_threads": used_t.value,
                "nvxk_median_s": st_nvxk["median"],
                "nvxk_mean_s": st_nvxk["mean"],
                "nvxk_std_s": st_nvxk["std"],
                "nvxk_p95_s": st_nvxk["p95"],
                "nvxk_gflops": gf_nvxk,
                "openblas_median_s": st_blas["median"],
                "openblas_gflops": gf_blas,
                "nvxk_vs_openblas_pct": ratio * 100.0,
                "max_rel_err": max_rel,
            })

    out_csv = "results/nvxk_vs_openblas.csv"
    with open(out_csv, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
        w.writeheader()
        w.writerows(rows)
    print(f"\nSaved comparison to {out_csv}")


if __name__ == "__main__":
    main()