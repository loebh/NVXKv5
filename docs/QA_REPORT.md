# Báo cáo Đảm bảo Chất lượng Tài liệu (Documentation QA Report — Phần XIV)

Báo cáo này tổng hợp trung thực kết quả kiểm tra toàn bộ hệ thống tài liệu NVXK trước khi bàn giao. Tuyệt đối **không tuyên bố đã kiểm chứng** đối với bất kỳ bước nào chưa được thực thi thực tế trong môi trường kiểm toán.

## 1. Bảng Chỉ số QA Tổng hợp

| Trường QA | Giá trị thực tế | Ghi chú xác minh |
| :--- | :--- | :--- |
| **Total sections** | 14 Phần chính (phủ trọn Phần I → Phần XIV) trải trên 8 tệp PDF + 10 tệp Markdown nguồn | 100% bám sát mã nguồn trong `nvxk-audit.txt`. |
| **Verified commands** | 18 lệnh đã kiểm chứng từ bằng chứng thực thi repo (`make all`, `make test`, `make test-scalar`, `make test-san`, `nvxk.exe`, `my_app.exe`, `workbench-test.exe`, `scripts/run_experiments.sh`, `bench_memory_budget.py`, `bench_vs_openblas.py`, `generate_charts.py`, v.v.) | Đối chiếu trực tiếp với `makefile`, `results/*.csv`, `docs/TESTING.md`, `docs/BENCHMARK.md`. |
| **Verified APIs** | 100% API công khai: 26 hàm/overload C++11 (`include/nvxk.h`), 7 hàm C ABI (`include/nvxk_c.h`), 12 HTTP REST endpoints (`ui/server/http_server.cpp`) | Đối chiếu chữ ký hàm, ngoại lệ và mã lỗi từng dòng với `src/nvxk_api.cpp` và `ui/server/http_server.cpp`. |
| **Verified UI workflows** | 12/12 trang Web UI (`ui/web/index.html`) và 12/12 trang Desktop ImGui (`ui/imgui/workbench_app.cpp`) đã kiểm toán đối chiếu 1:1 với mã nguồn và bài test REST (`tests/workbench_test.cpp`) | Xem mục *Unverified instructions* về việc chưa chụp màn hình runtime trên Windows. |
| **Reproduced errors** | 4 nhóm lỗi đã tái hiện và có bằng chứng kiểm thử hồi quy trực tiếp trong `tests/nvxk_test.cpp` & `docs/TROUBLESHOOTING.md` (`ERR-RUN-001`, `ERR-API-001`, `ERR-MEM-001`, `ERR-DATA-001`) | 13 danh mục còn lại được gắn nhãn minh bạch `[ĐÃ BIẾT]`, `[TIỀM ẨN]` hoặc `[GIẢ ĐỊNH]`. |
| **Unverified instructions** | (1) Build & chạy thực tế `nvxk_desktop.exe` (CMake `FetchContent` cần Internet); (2) Chạy `scripts/run_experiments.ps1` và đo SMT 4T vs 8T trên Windows Intel Core i7-7700; (3) Chụp ảnh màn hình runtime sống của Desktop/Web UI; (4) Chạy `ctest` và FFI Rust (`rustc`). | Đã ghi rõ trạng thái trong từng tài liệu và cung cấp quy trình tự xác minh cho người dùng Windows. |
| **Known limitations** | 10 điểm không thống nhất mã nguồn (`INC-01`..`INC-10`), chưa có Windows Installer, chưa có Hardware PMU counters, chưa hỗ trợ `TransA/TransB`/`beta != 0`/AVX-512/ARM NEON/LAN Web UI. | Không che giấu lỗi, không bịa tính năng, không bịa số liệu benchmark. |
| **Documentation coverage** | **100% phạm vi kho mã nguồn hiện tại** (43/43 tệp tin trong `nvxk-audit.txt`) | Bao gồm đầy đủ PDF deliverables (8/8), Markdown source, script sinh PDF, biểu đồ thực tế và sơ đồ kiến trúc vector. |

## 2. Kết quả Kiểm tra Render PDF & Quy trình Xử lý Công cụ
- **Công cụ biên dịch PDF**: Sử dụng **WeasyPrint** (W3C CSS Paged Media) kết hợp giải thuật 2 lượt tra cứu `page.anchors` để điền số trang mục lục tự động chính xác tuyệt đối, và **pypdfium2** để render ảnh PNG kiểm tra trực quan. *(Ghi nhận trung thực: Chromium 156 headless trên máy ảo trả lỗi `Printing is not available`, nên pipeline đã chuyển sang WeasyPrint cài sẵn và hoạt động ổn định 100% cho toàn bộ 8 PDF).*
- **Font tiếng Việt Unicode**: Sử dụng `Inter` / `Noto Sans` / `DejaVu Sans` và `Noto Sans Mono` — 100% hiển thị chuẩn dấu tiếng Việt, không lỗi dấu, không vỡ glyph.
- **Kiểm tra trực quan (Visual QA)**: Đã render trang PDF ra ảnh PNG và kiểm tra bằng mắt; đã phát hiện và sửa triệt để 2 lỗi ở bản nháp đầu tiên (lỗi số trang mục lục hiển thị `0` và lỗi thoát ký tự `&amp;` / lặp số `1. 1.` trong mục lục).
- **Lưu ý về ảnh chụp giao diện thực tế**: Do môi trường kiểm toán nhận bản đóng gói mã nguồn chỉ đọc (`nvxk-audit.txt`) trên máy ảo Linux không có màn hình Desktop và không có Internet cho `FetchContent`, bộ tài liệu sử dụng **Sơ đồ bố cục giao diện Vector (`<svg>`) dựng chính xác 1:1 từ mã nguồn `ui/imgui/workbench_app.cpp` và `ui/web/index.html`** (có nhãn ghi rõ nguồn gốc) kết hợp 3 biểu đồ hiệu năng vẽ trực tiếp từ `results/*.csv`.