#!/usr/bin/env bash
set -euo pipefail

echo "=== NVXK v2 Reproducible Experiment Suite (Linux) ==="
make clean
make all
make test
make test-scalar
make test-san

mkdir -p results
./nvxk.exe --calibrate --profile results/hw_profile.cfg --explain \
    --sizes 128,256,512,1024 --runs 7 --algorithm all --csv results/square_warm.csv
./nvxk.exe --profile results/hw_profile.cfg --cold-cache \
    --sizes 256,512,1024 --runs 5 --algorithm auto --csv results/square_cold.csv
./nvxk.exe --profile results/hw_profile.cfg --explain \
    --shape 4,1024,1024 --runs 7 --algorithm auto --csv results/thinm_4x1024x1024.csv

python3 scripts/bench_memory_budget.py
python3 scripts/bench_vs_openblas.py
python3 scripts/generate_charts.py
echo "=== All reproducible experiments completed ==="