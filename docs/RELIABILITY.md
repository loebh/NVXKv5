# Kỹ thuật Độ tin cậy và An toàn Hệ thống (Reliability Engineering)

Tài liệu này tóm tắt các cơ chế đảm bảo độ tin cậy đã được triển khai và kiểm chứng trong mã nguồn **NVXK v2**, cùng những giới hạn cần lưu ý khi đưa vào sử dụng thực tế.

## 1. Các Cơ chế Đã Triển khai và Xác minh

| Cơ chế | Vị trí cài đặt | Bảo vệ chống lỗi gì? |
| :--- | :--- | :--- |
| **Kiểm tra con trỏ & kích thước `0`** | `xk_detail::validate_ptrs` (`include/nvxk.h`) | Ngăn dereference con trỏ `nullptr` khi `M, K, N > 0`; xử lý an toàn trường hợp `M == 0`, `N == 0` hoặc `K == 0` (điền `0` vào `C`). |
| **Chống tràn số nguyên kích thước** | `xk_detail::checked_element_count`, `checked_round_up` | Ngăn integer overflow khi tính `M * K`, `K * N`, `M * N` hoặc làm tròn căn lề trước khi cấp phát bộ nhớ; ném `std::length_error`. |
| **Chống chồng lấn bộ nhớ (Aliasing)** | `xk_detail::validate_no_aliasing`, `ranges_overlap` | Ngăn lỗi sai kết quả hoặc data race khi vùng nhớ đầu ra `C` trùng hoàn toàn hoặc một phần với `A` hoặc `B`; ném `std::invalid_argument` (C ABI trả `NVXK_ERR_INVALID_ARG`). |
| **Kiểm tra tràn số học `int64_t`** | `xk_detail::add_checked`, `store_result` | Phát hiện tràn cộng tích lũy trong `XK_gemm_i32_i64` và các bản template số nguyên có dấu; ném `std::overflow_error` (C ABI trả `NVXK_ERR_OVERFLOW`). |
| **Cấp phát ngoài vùng OpenMP** | `XK_Workspace::prepare_f32` / `prepare_f64` | Mọi lệnh cấp phát heap căn lề 64-byte đều thực hiện *trước* `#pragma omp parallel`, đảm bảo không có ngoại lệ C++ nào thoát khỏi vùng OpenMP gây `std::terminate()`. |
| **Chống False Sharing** | `XK_Workspace::prepare_raw` (`src/nvxk_workspace.cpp`) | Đệm bước nhảy `a_slot_stride_` của từng luồng theo bội số 64 byte (1 cache line) để hai luồng không ghi chung cùng một dòng cache. |
| **Suy giảm mềm theo ngân sách bộ nhớ** | `resolve_effective_budget` (`src/nvxk_api.cpp`) + `compute_cache_blocking` (`src/nvxk_planner.cpp`) | Khi `XK_Workspace` có ngân sách nhỏ (xuống tới `128 KiB`), Planner tự động co `NC → MC → KC` thay vì ném lỗi OOM/length_error. |
| **An toàn tập lệnh (No `SIGILL`)** | `__attribute__((target("avx2,fma")))` + `XK_has_avx2_fma()` | Tệp nhị phân không chứa lệnh AVX2 ở đường dẫn khởi tạo toàn cục; chạy an toàn qua fallback scalar trên CPU đời cũ. |
| **Cô lập ngoại lệ qua C ABI** | `extern "C"` trong `src/nvxk_api.cpp` | Toàn bộ hàm trong `include/nvxk_c.h` bọc `try/catch`, tuyệt đối không để ngoại lệ C++ vượt ranh giới FFI. |

## 2. Xác minh Độ an toàn Đã Chạy

* **AddressSanitizer + UndefinedBehaviorSanitizer (`make test-san`)**: 0 lỗi đọc/ghi ngoài biên, 0 rò rỉ bộ nhớ, 0 hành vi chưa xác định (UB).
* **Kiểm thử sai phân (Differential Testing)**: 25 hình dạng ngẫu nhiên (`1..140`) đối chiếu với tham chiếu `f64`, sai số tương đối cực đại `f32 ≤ 2.91e-05`, `f64 ≤ 5.19e-14`.
* **Fallback Scalar (`make test-scalar`)**: Toàn bộ bộ kiểm thử đạt 100% khi tắt AVX2 ở lúc biên dịch.

## 3. Giả định & Giới hạn An toàn Cần Biết

* **Thread-safety của `XK_Workspace`**: Đối tượng `XK_Workspace` là bộ đệm tái sử dụng cho *một* lệnh gọi `XK_gemm_ex` tại một thời điểm (bên trong hàm tự chia khe cho các luồng OpenMP). Nếu ứng dụng của bạn có nhiều luồng ngoài gọi `XK_gemm_ex` song song, hãy cấp mỗi luồng ngoài một `XK_Workspace` riêng hoặc truyền `nullptr` để dùng `thread_local` workspace mặc định.
* **Giá trị `NaN` / `Inf` đầu vào**: Các hàm GEMM cấp thấp không quét trước từng phần tử của `A`/`B` để kiểm tra `NaN`/`Inf` nhằm tránh giảm hiệu năng; việc kiểm tra đầu vào hữu hạn hiện nằm ở tầng ứng dụng (`apps/my_app.cpp`) và tầng kiểm thử/benchmark.
* **Tính tất định dấu phẩy động**: Trên cùng số luồng và cùng hình dạng, kết quả phép nhân là tất định; tuy nhiên khi đổi số luồng hoặc đổi trục song song, thứ tự cộng có thể khác nhẹ trong sai số làm tròn IEEE-754 (`~1e-6` với `f32`).