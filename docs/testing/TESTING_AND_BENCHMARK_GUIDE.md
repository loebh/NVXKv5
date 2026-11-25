# Phần VIII: Kiểm thử, Kiểm chứng và Đánh giá Hiệu năng (Testing, Validation & Benchmark Guide)

## 1. Hệ thống Kiểm thử Toàn diện (10 Loại Kiểm thử)

NVXK không phụ thuộc framework kiểm thử ngoài; toàn bộ kiểm thử viết trực tiếp bằng C++11 (`tests/nvxk_test.cpp`) và C++17 (`tests/workbench_test.cpp`) cùng các cờ biên dịch chuyên biệt.

| Loại kiểm thử | Cách chạy thực tế | Ý nghĩa & Phạm vi kiểm tra | Điều kiện PASS / FAIL |
| :--- | :--- | :--- | :--- |
| **1. Unit Testing** | `make test` (`./nvxk-test.exe`) | Kiểm tra `vector_add_avx2` (`0..257`), hình dạng lẻ `1×1×1`, `7×13×19`, `131×263×149`, `i32→i64`, null pointer, tràn `size_t`. | Exit code `0`, in `[PASS] All NVXK unit, differential, C ABI, and hardening tests succeeded.` |
| **2. Integration Testing** | `make test-workbench` (`./workbench-test.exe`) | Kiểm tra 8 lớp tích hợp: Backend Registry, 5-stage ISA matrix, Plan Inspector, Matrix Lab `2×2` tùy chỉnh, Async Runner + chặn chạy trùng, Insights, Report MD/CSV, REST `GET /api/state` & chặn path traversal `400`. | Exit code `0`, in `[PASS] All NVXK Engineering Workbench C++17 integration tests succeeded.` |
| **3. Correctness Testing** | Tự động chạy trong `nvxk-test.exe`, `nvxk.exe` và nút `Run 7-Shape Suite` | Đối chiếu từng phần tử đầu ra `C` với tham chiếu độc lập tính bằng `double` (`compute_ref_f64` / `f64_ikj`). | **PASS** nếu 0 giá trị `NaN`/`Inf` và `|ref - got| <= abs_tol + rel_tol * max(1, |ref|)` với `f32`: `abs_tol = 1e-5*sqrt(K)`, `rel_tol = 1e-4`; `f64`: `1e-11`. |
| **4. Regression Testing** | Tích hợp cuối `main()` của `tests/nvxk_test.cpp` | Kiểm tra lại 4 lỗi lịch sử: C2 (watermark `XK_Workspace` không vượt ngân sách), C3/L2 (co khối mềm xuống 16 KiB và 64 B), C6 (`XK_gemm_blocked` với `block_size=8192`), L1 (từ chối tệp profile hỏng chứa `nan`/số âm). | 100% `XK_CHECK` đạt, không ném ngoại lệ bất thường. |
| **5. Differential / Randomized Testing** | `test_differential_randomized_shapes()` trong `nvxk-test.exe` | Sinh 25 ma trận ngẫu nhiên (`M, K, N ∈ [1..140]`, `seed=20261124`) đối chiếu `auto` và `auto_asm` dưới ngân sách 2 MiB. | Sai số mọi phần tử nằm trong ngưỡng IEEE-754. |
| **6. Memory & Sanitizer Testing** | `make test-san` (Linux) & `python3 scripts/bench_memory_budget.py` | Biên dịch với `-fsanitize=address,undefined` phát hiện out-of-bounds, use-after-free, memory leak, UB; kiểm tra co khối từ 64 MiB xuống 128 KiB. | 0 báo cáo ASan/UBSan; `allocated_bytes() <= budget`. |
| **7. Multithreading Testing** | `test_thin_m_streaming_and_n_axis()` + `XK_gemm_omp` / `XK_gemm_f64_ex` | Kiểm tra song song hóa trục `M`, trục `N` (`1, 2, 4` luồng), `Avx2ThinM` đa luồng và khóa đồng bộ `WorkbenchEngine` (reject concurrent run). | Kết quả khớp tham chiếu `f64`, không race condition. *(Lưu ý: chưa tự động hóa ThreadSanitizer do false-positive với `libgomp`).* |
| **8. Hardware Compatibility Testing** | `make test-scalar` | Biên dịch với `-DXK_FORCE_SCALAR_FALLBACK` để mô phỏng CPU không có AVX2+FMA. | Toàn bộ test lõi PASS 100% trên nhánh scalar. |
| **9. Stress / Long-Run Testing** | `./nvxk.exe --sizes 512,1024 --runs 20 --algorithm all` | Chạy lặp nhiều mẫu ở kích thước lớn để phát hiện rò rỉ bộ nhớ, quá nhiệt/hạ xung hoặc tăng độ lệch chuẩn bất thường. | Hoàn tất không crash, `CV = stddev/mean` ổn định, 0 lỗi kiểm chứng. |
| **10. Performance & Cold-Cache Testing** | `./nvxk.exe --calibrate --runs 7 --algorithm all` và cờ `--cold-cache` | Đo thông lượng thực tế trong chế độ warm-cache (lặp thích ứng ≥40 ms) và cold-cache (xóa L3 ngoài vùng bấm giờ). | In `[SUCCESS] All benchmarks passed numerical verification.` |

### Đọc Log và Xác nhận Bản sửa lỗi
- Khi `nvxk-test.exe` gặp lỗi, log in chính xác vị trí: `[FAIL] tests/nvxk_test.cpp:<dòng> — Check failed: <biểu thức>`.
- Khi `nvxk.exe` gặp sai lệch số học, log in: `[FAILED] <algorithm> at index <i>: ref=<giá trị>, got=<giá trị>, diff=<độ lệch>` và dừng ngay với `EXIT_FAILURE`.
- **Quy trình xác nhận một bản sửa lỗi**: (1) Tái hiện lỗi bằng test case cụ thể; (2) Sửa mã nguồn; (3) Chạy đủ 3 lệnh `make test`, `make test-scalar`, `make test-san`; (4) Nếu đụng tới hiệu năng, chạy benchmark ≥7 mẫu và xác nhận mức chênh lệch vượt 2× tổng độ lệch chuẩn (`stddev`).

## 2. Đọc Chỉ số Benchmark & Cấu trúc Tệp CSV

Các tệp CSV xuất từ `nvxk.exe --csv <file>` có phần đầu chú thích `# CPU`, `# PhysicalCores`, `# CacheMode` và 15 cột dữ liệu chuẩn:
`M, K, N, algorithm, threads_req, threads_used, min_sec, median_sec, mean_sec, stddev_sec, p95_sec, p99_sec, gflops, workspace_kb, max_rel_err`.

### Công thức tính toán thực tế trong mã nguồn (`benchmarks/main.cpp` & `workbench_engine.cpp`)
- **Tổng số phép toán (FLOPs)**: $\text{FLOPs} = 2 \times M \times K \times N$.
- **Thông lượng (Throughput — GFLOPS)**: $\text{GFLOPS} = \frac{2 \cdot M \cdot K \cdot N}{\text{median\_sec} \times 10^9}$.
- **Độ trễ (Latency)**: Dùng `median_ms` làm chỉ số trung tâm (ít nhiễu bởi spike OS hơn `mean_ms`); `P95`/`P99` phản ánh độ trễ đuôi (Workbench cảnh báo `P99: low n` nếu `sample_count < 20`).
- **Tăng tốc (Speedup)**: $\text{Speedup} = \frac{\text{median\_ms}_{\text{scalar\_baseline}}}{\text{median\_ms}_{\text{backend}}}$ (chỉ tính khi cùng phiên có đo baseline `ikj`/`naive`).
- **Hệ số biến thiên (Jitter / CV)**: $\text{CV} = \frac{\text{stddev}}{\text{mean}} \times 100\%$. Nếu `CV > 10%`, Insights Engine tự gắn cảnh báo nghi ngờ nhiễu lịch trình OS hoặc thay đổi xung CPU.

## 3. Kết quả Thực nghiệm Đã Xác minh (Số liệu Thật từ `results/*.csv`)

> **Minh bạch tuyệt đối**: Toàn bộ số liệu dưới đây trích nguyên trạng từ thư mục `results/` đo trên **Intel Xeon Platinum 8481C** (2 nhân vật lý / 2 luồng logic, L1d 48 KiB, L2 2 MiB, L3 105 MiB, Linux 6.12, GCC 12.2.0 `-O3 -fopenmp`, không dùng `-mavx2` toàn cục). **Không có bất kỳ số liệu giả lập nào cho i7-7700** — để đo trên i7-7700 Windows, hãy chạy `.\scripts\run_experiments.ps1`.

### 3.1. Hiệu năng Microkernel (`f32`, 7 mẫu/điểm — `results/v2_opt_*.csv` & `v2_thin4.csv`)

| Hình dạng (`M×K×N`) | Baseline `v0` (1T) | NVXK `v2` Intrinsics (1T) | NVXK `v2` ASM (1T) | NVXK `v2` Auto-ASM (1T/2T) | Sai số cực đại |
| :--- | ---: | ---: | ---: | ---: | ---: |
| `128×128×128` | 80.92 GF | 78.35 GF | 79.16 GF | 80.55 GF (1T) | `2.96e-06` |
| `256×256×256` | 87.64 GF | 82.88 GF | 86.97 GF | 147.97 GF (2T) | `6.50e-06` |
| `512×512×512` | 87.71 GF | 85.13 GF | 85.38 GF | 164.07 GF (2T) | `1.34e-05` |
| `1024×1024×1024` | 85.85 GF | 84.63 GF | **90.88 GF** | **165.94 GF** (2T) | `2.57e-05` |
| `4×1024×1024` (Thin-M) | 32.51 GF (2T) | — | — | **63.10 GF** (2T, `0 KiB` WS) | `1.11e-05` |

### 3.2. Đối chiếu NVXK v2 (C ABI, 256-bit AVX2) vs OpenBLAS 0.3.27 (512-bit trên Xeon, 2 luồng)

| Kiểu | Hình dạng | NVXK `v2` | OpenBLAS 0.3.27 | Tỷ lệ NVXK / OpenBLAS | Sai số cực đại |
| :--- | :--- | ---: | ---: | ---: | ---: |
| `f32` | `128×128×128` | 77.97 GF (1T) | 94.63 GF | 82.4% | `0.00e+00` |
| `f32` | `1024×1024×1024` | 167.90 GF (2T) | 280.09 GF | 59.9% | `2.91e-05` |
| `f32` | `1×1024×1024` | 22.43 GF (2T) | 26.17 GF | 85.7% | `1.08e-05` |
| `f32` | **`4×1024×1024`** | **58.82 GF** (2T) | 22.25 GF | **264.3%** | `1.23e-05` |
| `f64` | `1024×1024×1024` | 76.55 GF (2T) | 137.36 GF | 55.7% | `5.19e-14` |

### 3.3. Đánh đổi Ngân sách Bộ nhớ (`1024³`, `f32`, 2T — `results/memory_budget_tradeoff.csv`)

| Ngân sách (`KiB`) | Thực cấp phát (`KiB`) | Khối `(MC, KC, NC)` | Co khối | Thông lượng | Tỷ lệ giữ thông lượng |
| ---: | ---: | :---: | :---: | ---: | ---: |
| 65,536 | 2,256 | `(240, 384, 2048)` | Không | 165.05 GF | 100.0% |
| 1,024 | 912 (-59.6%) | `(240, 384, 128)` | Có | 156.45 GF | 94.8% |
| 512 | 408 (-81.9%) | `(120, 384, 32)` | Có | 126.00 GF | 76.3% |
| 128 | **84 (-96.3%)** | `(12, 384, 32)` | Có | **125.20 GF** | **75.9%** |