#!/usr/bin/env python3
"""
Measures throughput vs. workspace memory budget on 1024x1024x1024 SGEMM
to empirically answer Research Question 1 (memory-footprint vs throughput trade-off).
"""
import subprocess
import os
import csv

SRC = r"""
#include <nvxk.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <random>
#include <vector>
#if defined(_OPENMP)
#include <omp.h>
#endif

int main() {
#if defined(_OPENMP)
    omp_set_dynamic(0);
#endif
    XK_HardwareProfile prof = XK_calibrate_hardware(200.0);
    XK_set_active_profile(prof);

    const size_t N = 1024;
    std::mt19937 rng(42);
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
    std::vector<float> A(N * N), B(N * N), C(N * N);
    for (float& x : A) x = dist(rng);
    for (float& x : B) x = dist(rng);

    const size_t budgets_kb[] = {65536, 4096, 1024, 512, 256, 128};
    const double ops = 2.0 * N * N * N;

    std::cout << "budget_kb,allocated_kb,mc,kc,nc,threads,clamped,median_s,gflops\n";
    for (size_t bkb : budgets_kb) {
        XK_Workspace ws(bkb * 1024);
        XK_WorkloadDesc wd(N, N, N, sizeof(float));
        wd.mode = XK_ExecMode::Auto;
        int used = 0;

        // Warm-up
        XK_ExecutionPlan p = XK_gemm_ex(A.data(), B.data(), C.data(), wd, &ws, &prof, &used);

        std::vector<double> times;
        for (int r = 0; r < 7; ++r) {
            auto t0 = std::chrono::steady_clock::now();
            XK_gemm_ex(A.data(), B.data(), C.data(), wd, &ws, &prof, &used);
            auto t1 = std::chrono::steady_clock::now();
            times.push_back(std::chrono::duration<double>(t1 - t0).count());
        }
        std::sort(times.begin(), times.end());
        double med = times[times.size() / 2];
        double gf = (ops / med) * 1e-9;

        std::cout << bkb << "," << (ws.allocated_bytes() / 1024.0) << ","
                  << p.mc << "," << p.kc << "," << p.nc << "," << used << ","
                  << (p.explanation.budget_clamped ? 1 : 0) << ","
                  << std::fixed << std::setprecision(6) << med << ","
                  << std::setprecision(2) << gf << "\n";
    }
    return 0;
}
"""

os.makedirs("build", exist_ok=True)
os.makedirs("results", exist_ok=True)
with open("build/bench_budget.cpp", "w") as f:
    f.write(SRC)

cmd = [
    "g++", "-std=c++11", "-O3", "-fopenmp", "-Iinclude",
    "build/bench_budget.cpp",
    "src/nvxk_api.cpp", "src/nvxk_hw.cpp", "src/nvxk_kernels_avx2.cpp",
    "src/nvxk_kernels_scalar.cpp", "src/nvxk_planner.cpp", "src/nvxk_workspace.cpp",
    "-o", "build/bench_budget.exe"
]
subprocess.run(cmd, check=True)
res = subprocess.run(["./build/bench_budget.exe"], capture_output=True, text=True, check=True)
print(res.stdout)
with open("results/memory_budget_tradeoff.csv", "w") as f:
    f.write(res.stdout)