# Hướng dẫn Đóng góp cho NVXK (Contributing Guide)

NVXK được phát triển theo tiêu chuẩn kỹ thuật hệ thống hiệu năng cao và nghiên cứu khoa học thực nghiệm. Mọi thay đổi mã nguồn hoặc tài liệu cần tuân thủ các quy tắc dưới đây.

## 1. Nguyên tắc Kỹ thuật Bắt buộc

1. **Không phá vỡ phân phát tập lệnh động (Runtime Dispatch)**:
   * Không bao giờ thêm cờ toàn cục `-mavx2`, `-mfma`, `-march=native` vào `CXXFLAGS` trong `makefile` hoặc `CMakeLists.txt`.
   * Mọi hàm dùng intrinsics hoặc inline Assembly AVX2+FMA phải được gắn `__attribute__((target("avx2,fma")))` và nằm sau kiểm tra runtime `XK_has_avx2_fma()`.
2. **Bắt buộc duy trì nhánh Fallback Di động**:
   * Mọi kernel mới phải có nhánh fallback thuần C++11 tương đương và vượt qua `make test-scalar`.
3. **Không đánh đổi độ chính xác và độ an toàn bộ nhớ**:
   * Mọi hàm công khai phải kiểm tra con trỏ null (`validate_ptrs`), tràn kích thước (`checked_element_count`), và chống chồng lấn bộ nhớ (`validate_no_aliasing`).
   * Không cấp phát bộ nhớ động hoặc ném ngoại lệ C++ bên trong vùng `#pragma omp parallel`.
4. **Chứng minh mọi tối ưu hóa bằng dữ liệu**:
   * Không giữ lại mã phức tạp (đặc biệt là Assembly) nếu không có số liệu benchmark có kiểm soát chứng minh cải thiện vượt độ lệch chuẩn.

## 2. Quy trình Kiểm tra Trước khi Gửi Thay đổi

Chạy đủ 4 lệnh sau và đảm bảo không có cảnh báo `-Wall -Wextra`:

```bash
make clean
make all
make test
make test-scalar
make test-san
```

Nếu thay đổi ảnh hưởng đến hiệu năng, chạy benchmark so sánh trước và sau tối thiểu 7 mẫu (`--runs 7`) ở cả chế độ mặc định và `--cold-cache`, đồng thời ghi rõ phần cứng CPU, trình biên dịch và cờ biên dịch.

## 3. Quy chuẩn Mã nguồn và Tài liệu

* Chuẩn ngôn ngữ lõi: C++11 (`-std=c++11`), không dùng tiện ích mở rộng phụ thuộc trình biên dịch ngoài các khối `#if defined(...)` đã bao bọc rõ ràng.
* Giao diện C ABI (`include/nvxk_c.h`): Phải biên dịch sạch bằng trình biên dịch C99, không để rò rỉ ngoại lệ C++ qua ranh giới `extern "C"`.
* Cập nhật tài liệu: Khi thay đổi chữ ký API, cờ CLI hoặc quy trình build, phải cập nhật đồng bộ `README.md`, `docs/API_REFERENCE.md` và `CHANGELOG.md`.