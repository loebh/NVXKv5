# NVXK v2 Reproducible Experiment Suite for Windows (MSYS2 UCRT64 / Intel Core i7-7700)
$ErrorActionPreference = "Stop"

Write-Host "=== Building NVXK v2 (MSYS2 GCC, Runtime Dispatch, No Global -mavx2) ==="
mingw32-make clean
mingw32-make all
mingw32-make test
mingw32-make test-scalar

New-Item -ItemType Directory -Force -Path results | Out-Null

Write-Host "=== Running Calibrated Benchmarks on Local Hardware ==="
.\nvxk.exe --calibrate --profile results\i7_7700_profile.cfg --explain `
    --sizes 128,256,512,1024 --threads 1,2,4,8 --runs 7 --algorithm all `
    --csv results\i7_7700_square_warm.csv

.\nvxk.exe --profile results\i7_7700_profile.cfg --cold-cache `
    --sizes 256,512,1024 --threads 1,2,4,8 --runs 5 --algorithm auto `
    --csv results\i7_7700_square_cold.csv

.\nvxk.exe --profile results\i7_7700_profile.cfg --explain `
    --shape 1,1024,1024 --threads 1,2,4,8 --runs 7 --algorithm auto `
    --csv results\i7_7700_thin1.csv

.\nvxk.exe --profile results\i7_7700_profile.cfg --explain `
    --shape 4,1024,1024 --threads 1,2,4,8 --runs 7 --algorithm auto `
    --csv results\i7_7700_thin4.csv

Write-Host "=== Done! Results saved in results\ ==="