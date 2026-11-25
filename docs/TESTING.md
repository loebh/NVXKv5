# Hướng dẫn Kiểm thử (Testing Guide)

Bộ kiểm thử nằm tại `tests/nvxk_test.cpp` và không yêu cầu framework ngoài. Mọi chế độ dưới đây đều **đã chạy xác minh thành công** trên Linux x86-64 (GCC 12.2.0).

## 1. Các Lệnh Chạy Kiểm thử

### 1.1. Kiểm thử Tiêu chuẩn (Runtime Dispatch AVX2+FMA + ASM + OpenMP)

```bash
make test
```

* **Mục đích**: Biên dịch `nvxk-test.exe` (`-O2 -fopenmp -Wall -Wextra`) và chạy toàn bộ 10 nhóm kiểm thử.
* **Output dự kiến**:
  ```text
  [PASS] All NVXK unit, differential, C ABI, and hardening tests succeeded.
  ```
* **Điều kiện thành công**: Exit code `0`, không có dòng `[FAIL]`.

### 1.2. Kiểm thử Nhánh Fallback Scalar Thuần C++11

```bash
make test-scalar
```

* **Mục đích**: Biên dịch với `-DXK_FORCE_SCALAR_FALLBACK` để vô hiệu hóa hoàn toàn đường dẫn AVX2+FMA/ASM, xác minh mọi API và kiểm thử vẫn chạy đúng trên CPU không có AVX2.
* **Điều kiện thành công**: In `[PASS] ...` và exit code `0`.

### 1.3. Kiểm thử An toàn Bộ nhớ (ASan + UBSan)

```bash
make test-san
```

* **Mục đích**: Biên dịch với `-O1 -g -fsanitize=address,undefined` để phát hiện đọc/ghi ngoài biên, use-after-free, memory leak và undefined behavior.
* **Điều kiện thành công**: In `[PASS] ...`, không có báo cáo lỗi từ AddressSanitizer hoặc UndefinedBehaviorSanitizer.

## 2. Phạm vi Kiểm thử trong `tests/nvxk_test.cpp`

| Nhóm kiểm thử | Nội dung xác minh |
| :--- | :--- |
| `test_vector_add<float/double>` | Các kích thước `0, 1, 3, 4, 7, 8, 9, 17, 257`, độ chính xác `4·eps`, từ chối con trỏ null. |
| `test_odd_and_large_k_gemm_f32` | Hình dạng lẻ và lớn hơn `KC` (`1×1×1`, `3×5×11`, `6×16×16`, `7×13×19`, `13×17×23`, `131×263×149`) trên `naive`, `ikj`, `avx2`, `avx2_asm`, `auto`, `blocked(7/64)`, `omp(2T)`. |
| `test_odd_and_large_k_gemm_f64` | Độ chính xác kép (`tol <= 1e-11 + 1e-13·K`) trên đơn luồng và `XK_gemm_f64_ex` 2 luồng OpenMP. |
| `test_thin_m_streaming_and_n_axis` | Xác nhận `Avx2ThinM` được chọn tự động cho `M ∈ {1..4}` với `required_workspace_bytes == 0`, đồng thời kiểm thử đường dẫn `AxisN` thủ công ở `1, 2, 4` luồng. |
| `test_differential_randomized_shapes` | 25 hình dạng ngẫu nhiên (`M, K, N ∈ [1, 140]`, hạt giống cố định `20261124`) đối chiếu `auto` và `auto_asm` với tham chiếu `f64` dưới workspace giới hạn `2 MiB`. |
| `test_graceful_memory_budget_clamping` | Chạy `256³` với `XK_Workspace(128 KiB)`, xác nhận không ném ngoại lệ, `allocated_bytes() <= 128 KiB` và `budget_clamped == true`. |
| `test_c_abi` | Kiểm tra `nvxk_sgemm`, `nvxk_dgemm`, phát hiện aliasing trả về `NVXK_ERR_INVALID_ARG` + `nvxk_last_error()`, và tràn số `nvxk_gemm_i32_i64` trả về `NVXK_ERR_OVERFLOW`. |
| `test_aliasing_rejection` | Xác nhận từ chối trùng hoàn toàn và trùng một phần (`C = A + 4`) trên mọi hàm GEMM. |
| `test_integer_gemm_and_overflow` | Kiểm tra kết quả đúng và ném `std::overflow_error` khi tràn `int64_t` và `int16_t`. |
| `test_phase5_hardware_planner_workspace_profile` | Kiểm tra CPUID, giả lập hồ sơ i7-7700 (`128³` giữ 1T, `4×1024×1024` chọn `AxisN`), lưu/nạp hồ sơ, ngân sách `1 KiB` ném `std::length_error` khi gọi `prepare_f32` trực tiếp, và `XK_calibrate_hardware(100.0)`. |
| `test_empty_and_null_checks` | Kiểm tra kích thước `0`, con trỏ null, `block_size == 0`, và tràn `size_t`. |

## 3. Kiểm thử Chưa có (Giới hạn Hiện tại)

* Chưa có kiểm thử ThreadSanitizer (`-fsanitize=thread`) tự động trong `makefile` (do `libgomp` thường tạo false-positive với TSan nếu không biên dịch lại `libgomp`).
* Chưa có fuzzing liên tục (libFuzzer/AFL) cho giao diện C ABI.
* Chưa chạy kiểm thử tự động trên Windows MSVC và kiến trúc ARM64.