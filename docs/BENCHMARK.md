# Hướng dẫn Benchmark và Đánh giá Hiệu năng (Benchmark Guide)

Tài liệu này mô tả giao thức đo đạc, toàn bộ cờ dòng lệnh của `nvxk.exe`, các kết quả đã đo thực tế và cách tái lập trên Linux cũng như Windows.

## 1. Giao thức Đo đạc (`benchmarks/main.cpp`)

* **Khởi động & làm nóng**: Gọi `omp_set_dynamic(0)`, chạy 1 lượt warm-up trước khi bấm giờ.
* **Lặp thích ứng (warm-cache)**: Tự động nhân đôi số vòng lặp cho tới khi mỗi mẫu đo kéo dài ít nhất `40 ms` để giảm nhiễu độ phân giải đồng hồ; đặt compiler memory barrier (`asm volatile("" : : : "memory")`) sau mỗi lần gọi.
* **Chế độ Cold-Cache (`--cold-cache`)**: Quét bộ đệm lớn gấp `max(2 × L3, 16 MiB)` hoàn toàn **bên ngoài** vùng bấm giờ `steady_clock::now()` trước mỗi mẫu.
* **Kiểm chứng số học bắt buộc**: Mọi phép đo đều đối chiếu kết quả `C` với tham chiếu tính bằng `double` (`compute_ref_f64`); nếu vượt ngưỡng `1e-5·sqrt(K) + 1e-4·|ref|` hoặc có `NaN`/`Inf`, chương trình dừng ngay với `EXIT_FAILURE`.
* **Chỉ số thống kê xuất ra CSV**: `min_sec`, `median_sec`, `mean_sec`, `stddev_sec`, `p95_sec`, `p99_sec`, `gflops`, `workspace_kb`, `max_rel_err`.

## 2. Tham số Dòng lệnh của `nvxk.exe`

| Cờ CLI | Mô tả | Mặc định |
| :--- | :--- | :--- |
| `--size N` | Chạy 1 kích thước ma trận vuông `N×N×N` | `128,256,512,1024` |
| `--sizes N,N,...` | Danh sách kích thước vuông | `128,256,512,1024` |
| `--shape M,K,N` | Chạy ma trận chữ nhật `A(M×K) · B(K×N)` | *(tắt)* |
| `--runs N` | Số mẫu bấm giờ độc lập mỗi cấu hình | `7` |
| `--threads N,N,...` | Danh sách số luồng (chỉ áp dụng cho `--algorithm omp`) | `1,2,4,8` |
| `--algorithm NAME` | `all`, `naive`, `ikj`, `blocked`, `avx2`, `avx2_asm`, `omp`, `auto`, `auto_asm` | `all` |
| `--calibrate` | Chạy hiệu chuẩn phần cứng (`200 ms`) trước khi đo | *(tắt)* |
| `--profile FILE` | Nạp hồ sơ phần cứng (hoặc lưu nếu đi kèm `--calibrate`) | *(trống)* |
| `--explain` | In chuỗi giải trình `XK_PlanExplanation` cho từng hình dạng | *(tắt)* |
| `--cold-cache` | Xóa cache L3 bên ngoài vùng bấm giờ trước mỗi mẫu | *(warm-cache)* |
| `--csv FILE` | Ghi bảng số liệu thống kê đầy đủ ra tệp CSV | *(không ghi)* |

## 3. Lệnh Tái lập Thực nghiệm

### 3.1. Chạy nhanh 1 cấu hình *(đã xác minh)*

```bash
./nvxk.exe --calibrate --explain --size 128 --runs 3 --algorithm auto
```

* **Điều kiện thành công**: In bảng cấu hình phần cứng, dòng `[PLAN 128x128x128] ...`, bảng số liệu và kết thúc bằng `[SUCCESS] All benchmarks passed numerical verification.`.

### 3.2. Chạy toàn bộ chuỗi thực nghiệm Linux *(đã xác minh từng thành phần)*

```bash
./scripts/run_experiments.sh
```

Script sẽ lần lượt build, chạy đủ 3 bộ test, đo ma trận vuông (warm & cold), ma trận mỏng `4×1024×1024`, đường cong ngân sách bộ nhớ (`scripts/bench_memory_budget.py`), đối chiếu OpenBLAS (`scripts/bench_vs_openblas.py`) và vẽ 3 biểu đồ vào `docs/figures/`.

### 3.3. Chạy trên máy đích Windows Intel Core i7-7700 *(chưa xác minh trong sandbox)*

```powershell
.\scripts\run_experiments.ps1
```

## 4. Kết quả Thực nghiệm Đã Xác minh (Sandbox Linux)

* **Môi trường đo**: `Intel(R) Xeon(R) Platinum 8481C` (2 nhân vật lý / 2 luồng logic, L1d = 48 KiB, L2 = 2 MiB, L3 = 105 MiB, RAM 7.8 GiB), Linux 6.12 x86-64, GCC 12.2.0 (`-O3 -fopenmp`, không dùng `-mavx2` toàn cục).
* **Tệp dữ liệu gốc**: Lưu sẵn trong thư mục `results/` (`v2_opt_*.csv`, `v2_thin*.csv`, `v2_cold_auto.csv`, `nvxk_vs_openblas.csv`, `memory_budget_tradeoff.csv`).

### 4.1. So sánh Microkernel (`f32`, 7 mẫu/điểm)

| Hình dạng | Baseline `v0` 1T | NVXK `v2` Intrinsics 1T | NVXK `v2` ASM 1T | NVXK `v2` Auto-ASM (1T/2T) |
| :--- | ---: | ---: | ---: | ---: |
| `128×128×128` | 80.92 GF | 78.35 GF | 79.16 GF | 80.55 GF (1T) |
| `256×256×256` | 87.64 GF | 82.88 GF | 86.97 GF | 147.97 GF (2T) |
| `512×512×512` | 87.71 GF | 85.13 GF | 85.38 GF | 164.07 GF (2T) |
| `1024×1024×1024` | 85.85 GF | 84.63 GF | **90.88 GF** | 165.94 GF (2T) |
| `4×1024×1024` (Thin-M) | 32.51 GF (2T) | — | — | **63.10 GF** (2T, `0 KiB` WS) |

### 4.2. Đối chiếu NVXK v2 (C ABI, AVX2 256-bit) vs OpenBLAS 0.3.27 (512-bit trên Xeon, 2 luồng)

| Kiểu | Hình dạng | NVXK `v2` | OpenBLAS 0.3.27 | Tỷ lệ | Sai số cực đại |
| :--- | :--- | ---: | ---: | ---: | ---: |
| `f32` | `128×128×128` | 77.97 GF (1T) | 94.63 GF | 82.4% | `0.00e+00` |
| `f32` | `1024×1024×1024` | 167.90 GF (2T) | 280.09 GF | 59.9% | `2.91e-05` |
| `f32` | `1×1024×1024` | 22.43 GF (2T) | 26.17 GF | 85.7% | `1.08e-05` |
| `f32` | **`4×1024×1024`** | **58.82 GF** (2T) | 22.25 GF | **264.3%** | `1.23e-05` |
| `f64` | `1024×1024×1024` | 76.55 GF (2T) | 137.36 GF | 55.7% | `5.19e-14` |

### 4.3. Đường cong Đánh đổi Ngân sách Bộ nhớ (`1024³`, `f32`, 2T)

* Ngân sách `64 MiB` / `4 MiB`: cấp phát `2,256 KiB` → `165.05–167.62 GFLOPS` (`100%`).
* Ngân sách `1,024 KiB`: cấp phát `912 KiB` (**-59.6%**) → `156.45 GFLOPS` (**94.8%**).
* Ngân sách `128 KiB`: cấp phát **`84 KiB`** (**-96.3%**) → **`125.20 GFLOPS`** (**75.9%**, 0 ngoại lệ).