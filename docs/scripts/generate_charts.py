#!/usr/bin/env python3
"""
Generate publication-grade Vietnamese benchmark charts strictly from verified
NVXK v2 CSV measurements in results/ (Intel Xeon Platinum 8481C, 2C/2T, GCC 12.2.0).
"""
import os
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

OUT_DIR = "/working_dir/c_03e52d0f508725d4/artifacts/file_generation/ttl=63d/intermediate/docs/assets"
os.makedirs(OUT_DIR, exist_ok=True)

plt.rcParams.update({
    "font.family": "DejaVu Sans",
    "font.size": 10,
    "axes.titlesize": 11,
    "axes.titleweight": "bold",
    "axes.grid": True,
    "grid.alpha": 0.25,
    "axes.spines.top": False,
    "axes.spines.right": False,
    "figure.dpi": 220,
})

# Chart 1: Microkernel Comparison (f32, 1T & Auto 2T) from results/v2_opt_*.csv
sizes = ["128³", "256³", "512³", "1024³"]
x = np.arange(len(sizes))
w = 0.26

base_1t = [80.92, 87.64, 87.71, 85.85]
int_1t  = [78.35, 82.88, 85.13, 84.63]
asm_1t  = [79.16, 86.97, 85.38, 90.88]
auto_int = [74.63, 142.84, 157.98, 161.81]
auto_asm = [80.55, 147.97, 164.07, 165.94]

fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(7.0, 3.6), constrained_layout=True)
ax1.bar(x - w, base_1t, w, label="Baseline v0 (1T)", color="#94A3B8")
ax1.bar(x,     int_1t,  w, label="v2 Intrinsics (1T)", color="#2563EB")
ax1.bar(x + w, asm_1t,  w, label="v2 x86-64 ASM (1T)", color="#0F172A")
for i, v in enumerate(asm_1t):
    ax1.text(x[i] + w, v + 1.8, f"{v:.1f}", ha="center", va="bottom", fontsize=8, fontweight="bold")
ax1.set_xticks(x)
ax1.set_xticklabels(sizes)
ax1.set_ylabel("Thông lượng (GFLOPS)")
ax1.set_title("Đơn luồng (1T) SGEMM f32")
ax1.set_ylim(0, 108)
ax1.legend(fontsize=7.5, loc="lower right", frameon=True)

w2 = 0.34
ax2.bar(x - w2/2, auto_int, w2, label="v2 Auto Intrinsics", color="#2563EB")
ax2.bar(x + w2/2, auto_asm, w2, label="v2 Auto ASM", color="#0D9488")
for i, v in enumerate(auto_asm):
    ax2.text(x[i] + w2/2, v + 2.5, f"{v:.1f}", ha="center", va="bottom", fontsize=8, fontweight="bold")
ax2.set_xticks(x)
ax2.set_xticklabels(sizes)
ax2.set_ylabel("Thông lượng (GFLOPS)")
ax2.set_title("Chế độ Auto (1T/2T) SGEMM f32")
ax2.set_ylim(0, 195)
ax2.legend(fontsize=7.5, loc="upper left", frameon=True)

fig.suptitle("Hiệu năng Microkernel NVXK v2 (Xeon 8481C, 2C/2T, AVX2+FMA)", fontsize=11, fontweight="bold")
fig.savefig(os.path.join(OUT_DIR, "chart_microkernel_perf.png"), bbox_inches="tight")
plt.close(fig)

# Chart 2: NVXK v2 vs OpenBLAS 0.3.27 (f32) from results/nvxk_vs_openblas.csv
shapes_lbl = ["128³", "256³", "512³", "1024³", "1×1024²", "4×1024²", "64×1024²", "512×256×512"]
nvxk_f32   = [77.97, 146.48, 161.67, 167.90, 22.43, 58.82, 101.48, 168.89]
oblas_f32  = [94.63, 212.47, 232.56, 280.09, 26.17, 22.25, 165.93, 221.84]

x3 = np.arange(len(shapes_lbl))
fig, ax = plt.subplots(figsize=(7.0, 3.6), constrained_layout=True)
ax.bar(x3 - 0.19, nvxk_f32, 0.38, label="NVXK v2 (C ABI, AVX2 256-bit)", color="#0D9488")
ax.bar(x3 + 0.19, oblas_f32, 0.38, label="OpenBLAS 0.3.27 (512-bit trên Xeon)", color="#64748B")
ax.annotate("2.64× vs OpenBLAS\n(Avx2ThinM, 0 KiB WS)", xy=(5 - 0.19, 58.82),
            xytext=(3.7, 130), arrowprops=dict(arrowstyle="->", color="#B91C1C", lw=1.3),
            fontsize=8.2, fontweight="bold", color="#B91C1C",
            bbox=dict(boxstyle="round,pad=0.3", fc="#FEF2F2", ec="#DC2626"))
ax.set_xticks(x3)
ax.set_xticklabels(shapes_lbl, rotation=22, ha="right", fontsize=8.5)
ax.set_ylabel("Thông lượng (GFLOPS)")
ax.set_title("Đối chiếu NVXK v2 vs OpenBLAS 0.3.27 (SGEMM f32, 2 luồng)")
ax.set_ylim(0, 315)
ax.legend(fontsize=8.5, loc="upper left", frameon=True)
fig.savefig(os.path.join(OUT_DIR, "chart_vs_openblas.png"), bbox_inches="tight")
plt.close(fig)

# Chart 3: Memory-Budget Graceful Degradation Curve from results/memory_budget_tradeoff.csv
alloc_kb = [84.0, 228.0, 408.0, 912.0, 2256.0]
gf_mb    = [125.20, 128.37, 126.00, 156.45, 165.05]

fig, ax = plt.subplots(figsize=(6.8, 3.4), constrained_layout=True)
ax.plot(alloc_kb, gf_mb, marker="o", linewidth=2.2, markersize=7, color="#0F766E")
ax.fill_between(alloc_kb, gf_mb, alpha=0.12, color="#0F766E")
offsets = [(0, 10), (0, -24), (0, -24), (0, 10), (-12, 10)]
for akb, g, (ox, oy) in zip(alloc_kb, gf_mb, offsets):
    ax.annotate(f"{akb:.0f} KiB\n({g:.1f} GF)", (akb, g),
                textcoords="offset points", xytext=(ox, oy),
                ha="center", fontsize=8.2, fontweight="bold", color="#0F172A")
ax.set_xscale("log", base=2)
ax.set_xlabel("Bộ nhớ tạm thực cấp phát (KiB, thang log2)")
ax.set_ylabel("Thông lượng 1024³ (GFLOPS)")
ax.set_title("Đường cong Suy giảm mềm theo Ngân sách Bộ nhớ (-96.3% RAM giữ 75.9% GFLOPS)")
ax.set_ylim(95, 192)
fig.savefig(os.path.join(OUT_DIR, "chart_memory_budget.png"), bbox_inches="tight")
plt.close(fig)

print("Generated 3 Vietnamese benchmark charts in", OUT_DIR)