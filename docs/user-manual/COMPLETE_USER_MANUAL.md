# Cẩm nang Người dùng Hoàn chỉnh NVXK (Complete User Manual)

Tài liệu tổng hợp này đóng vai trò điểm khởi đầu trung tâm cho toàn bộ hệ thống tài liệu **NVXK v2** (*Native Vector eXecution Kernels*) và **NVXK Engineering Intelligence Workbench**. Tài liệu được thiết kế để một tổ chức, nhóm nghiên cứu hoặc người mới hoàn toàn có thể tự cài đặt, vận hành, kiểm thử, xử lý sự cố và tích hợp NVXK mà không cần tác giả trực tiếp hướng dẫn.

## 1. Tổng quan Hệ thống & Kiến trúc 6+4 Module

NVXK là động cơ tính toán ma trận hiệu năng cao, tiết kiệm bộ nhớ tạm và có độ tin cậy cao cho **CPU phổ thông không có GPU rời** (định chuẩn tham chiếu trên Intel Core i7-7700, 16 GB RAM, Intel HD Graphics 630).

- **Tầng Lõi Tính toán (`src/`, C++11 + C ABI)**: Gồm 6 module tách biệt (`nvxk_api`, `nvxk_hw`, `nvxk_planner`, `nvxk_workspace`, `nvxk_kernels_avx2`, `nvxk_kernels_scalar`), biên dịch an toàn không gắn cờ `-mavx2` toàn cục, tự động phân phát AVX2+FMA/ASM ở runtime hoặc fallback scalar.
- **Tầng Workbench & Giao diện (`adapters/`, `diagnostics/`, `benchmark/`, `ui/`, C++17)**: Cung cấp bộ phân tích ISA 5 tầng, đo benchmark bất đồng bộ có hủy an toàn, phòng thí nghiệm ma trận tương tác, kiểm chứng sai số so với `f64`, xuất báo cáo Markdown/CSV qua hai giao diện:
  - **Web Workbench (`nvxk_workbench.exe` + `ui/web/index.html`)**: 100% offline, **không cần Node.js/npm**, chạy tại `http://127.0.0.1:8765/`.
  - **Desktop App gốc (`nvxk_desktop.exe`, `ui/imgui/`)**: Cửa sổ đồ họa Dear ImGui Docking + ImPlot + GLFW + OpenGL 3.3 Core (biên dịch từ mã nguồn qua CMake).

## 2. Hướng dẫn Nhanh theo 3 Nhóm Đối tượng (Phần XI — Quick Start)

### 2.1. Nhóm Beginner (Chưa biết lập trình, máy Windows trắng)
1. Cài **Git for Windows** ([git-scm.com](https://git-scm.com/download/win)) giữ nguyên mặc định.
2. Cài **MSYS2** ([msys2.org](https://www.msys2.org/)) vào `C:\msys64`, mở **MSYS2 UCRT64** và chạy:
   `pacman -S --needed mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-make mingw-w64-ucrt-x86_64-cmake`
3. Thêm `C:\msys64\ucrt64\bin` vào biến môi trường `Path` của Windows rồi mở **PowerShell mới**.
4. Vào thư mục mã nguồn NVXK (`cd C:\dev\NVXK`) và biên dịch: `mingw32-make all`.
5. Mở giao diện trực quan ngay lập tức: `.\nvxk_workbench.exe` — trình duyệt sẽ tự mở tại `http://127.0.0.1:8765/`.
6. Bấm nút **Calibrate** (~200 ms) rồi bấm **Benchmark** trên thanh trên cùng để xem tốc độ GFLOPS thực tế của máy tính.

### 2.2. Nhóm Developer (Tích hợp API, Debug & Phát triển)
1. Clone kho mã nguồn, cấu hình CMake release:
   `cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release && cmake --build build -j`
2. Xác minh toàn bộ test lõi & Workbench: `ctest --test-dir build --output-on-failure` (kỳ vọng `2/2 passed`).
3. Liên kết tĩnh C++ bằng `add_subdirectory(third_party/nvxk)` + `target_link_libraries(app PRIVATE nvxk_core)`, hoặc gọi C ABI (`include/nvxk_c.h` + `nvxk.dll`/`libnvxk.so`) từ C99/Python/Rust.
4. Tuân thủ 3 quy tắc an toàn: không aliasing (`C` tách biệt `A`/`B`), không chia sẻ chung 1 `XK_Workspace` giữa nhiều luồng ngoài, không gắn `-mavx2` toàn cục.

### 2.3. Nhóm Researcher (Benchmark, Kiểm chứng & Tái lập Khoa học)
1. Đóng ứng dụng nền, cắm sạc, chạy chuỗi thực nghiệm chuẩn hóa:
   - Windows (i7-7700): `.\scripts\run_experiments.ps1`
   - Linux: `./scripts/run_experiments.sh`
2. Thu thập các tệp CSV trong `results/` (chứa đủ `min`, `median`, `mean`, `stddev`, `p95`, `p99`, `gflops`, `workspace_kb`, `max_rel_err`).
3. Phân tích đường cong ngân sách bộ nhớ (`python3 scripts/bench_memory_budget.py`) và đối chiếu OpenBLAS (`python3 scripts/bench_vs_openblas.py`), sau đó sinh biểu đồ khoa học (`python3 scripts/generate_charts.py`).

## 3. 12 Quy trình Vận hành Chuẩn (Phần X — Operational Workflows)

Mỗi quy trình đều đi kèm chuỗi bước tuyến tính và checklist xác nhận trước khi kết thúc:

| # | Quy trình | Chuỗi thực thi chuẩn | Checklist Xác nhận (Done Criteria) |
| :--- | :--- | :--- | :--- |
| **W01** | **Cài NVXK lần đầu** | Kiểm tra Win x64 → Cài Git & MSYS2 UCRT64 → Thêm `PATH` → `mingw32-make all` → `mingw32-make test`. | `g++ --version` OK; tạo đủ 4 binary; 2/2 bộ test in `[PASS]`. |
| **W02** | **Khởi động NVXK** | Đứng tại thư mục gốc `NVXK` → Chọn CLI (`.\nvxk.exe`), Web (`.\nvxk_workbench.exe`), hoặc Desktop (`.\build_desktop\nvxk_desktop.exe`). | Không báo thiếu DLL; cổng `8765` lắng nghe hoặc cửa sổ 1440×900 hiển thị. |
| **W03** | **Sử dụng Desktop UI** | Mở `nvxk_desktop.exe` → Bấm `Calibrate (~200ms)` → Kiểm tra tab **CPU Architecture** → Bấm `Run Benchmark` → Xem **Overview** & **Reports**. | Trạng thái chuyển `Calibrated`; 5 thẻ KPI có số đo thực; nút `Save nvxk_report.md` ghi file thành công. |
| **W04** | **Sử dụng Web UI** | Chạy `nvxk_workbench.exe` → Mở `http://127.0.0.1:8765/` → Chọn **Measurement Mode** → Chạy **Matrix Lab** / **Benchmark** → Tải Markdown/CSV. | Chấm xanh `Idle`; biểu đồ Canvas có dữ liệu; tải về tệp `.md`/`.csv` không rỗng. |
| **W05** | **Chạy Benchmark chuẩn** | Đóng ứng dụng nặng → Hiệu chuẩn `--calibrate --profile results/hw.cfg` → Chạy `--runs 7` (warm) và `--cold-cache --runs 5` → Xuất `--csv`. | Dòng cuối in `[SUCCESS] All benchmarks passed numerical verification.`; `CV < 5%` ở warm-cache. |
| **W06** | **Kiểm tra CPU & ISA** | Chạy `./nvxk.exe --calibrate --explain --size 128 --runs 1 --algorithm auto` hoặc mở tab **CPU Architecture**. | Xác định đủ 5 tầng ISA (`CPU → Compiler → Binary → Dispatch → Executed`) và dung lượng L1d/L2/L3. |
| **W07** | **Chạy toàn bộ Test** | Chạy lần lượt `make test` → `make test-scalar` → `make test-san` (trên Linux/WSL2). | Tất cả trả exit code `0`, 0 cảnh báo compiler, 0 lỗi ASan/UBSan. |
| **W08** | **Xuất Kết quả & Báo cáo** | Từ CLI dùng `--csv results/out.csv`; từ Web/Desktop UI mở tab **Reports** xuất `.md` và `.csv`, hoặc bấm **Export PNG** trên biểu đồ Web. | Tệp CSV đọc sạch bằng Excel/Python; báo cáo Markdown đủ 4 phần (Hardware, ISA Matrix, Results, Insights). |
| **W09** | **Tích hợp vào Dự án mới** | Chọn C++ (`add_subdirectory` + `nvxk_core`) hoặc C ABI (`include/nvxk_c.h` + `libnvxk.so`/`nvxk.dll`) → Viết hàm gọi thử `2×2` → Biên dịch & chạy. | Ma trận `[[1,2],[3,4]] × [[5,6],[7,8]]` trả chính xác `[[19,22],[43,50]]`. |
| **W10** | **Cập nhật NVXK** | Lưu dự phòng thư mục `results/` → `git pull` → `mingw32-make clean && mingw32-make all && mingw32-make test`. | Không mất dữ liệu CSV cũ; toàn bộ test pass sau cập nhật. |
| **W11** | **Khôi phục khi Cấu hình lỗi** | Không xóa `results/` → Xóa tệp profile lỗi (`rm results/hw_profile.cfg`) → Chạy `make clean` / xóa `build/` → Build lại và chạy `--calibrate`. | Hệ thống nạp lại hồ sơ mặc định rồi hiệu chuẩn sạch, không mất dữ liệu đo cũ. |
| **W12** | **Chẩn đoán Toàn hệ thống** | Tách lớp kiểm tra từ dưới lên: (1) `g++ --version` → (2) `make test-scalar` → (3) `make test` → (4) `curl http://127.0.0.1:8765/api/state` → (5) Tra `ERR-*` trong Troubleshooting Handbook. | Khoanh vùng chính xác lớp lỗi (Toolchain / Lõi / Server / UI) và xác minh sau khi sửa. |

## 4. Bản đồ Bộ 8 Tài liệu PDF Độc lập

| STT | Tệp PDF | Nội dung chuyên sâu |
| :---: | :--- | :--- |
| 1 | `NVXK_Complete_User_Manual.pdf` | Cẩm nang tổng thể, Quick Start 3 nhóm, Kiến trúc, 12 Workflows & Kiểm toán mã nguồn. |
| 2 | `NVXK_Installation_Guide.pdf` | Cài đặt từ con số 0 trên Windows/Linux, PATH, 4 Terminal, Tương thích phần cứng i7-7700. |
| 3 | `NVXK_Desktop_UI_Guide.pdf` | Cài đặt & hướng dẫn từng nút ứng dụng Desktop gốc (`nvxk_desktop.exe`, ImGui) + 17 lỗi Desktop. |
| 4 | `NVXK_Web_UI_Guide.pdf` | Triển khai & hướng dẫn 12 trang Web Workbench (`ui/web/index.html`, 0 Node.js) + 23 lỗi Web. |
| 5 | `NVXK_CLI_Command_Reference.pdf` | Tra cứu toàn bộ lệnh CLI thực tế thuộc 12 danh mục (đầy đủ cú pháp, tham số, ví dụ, lỗi). |
| 6 | `NVXK_API_Reference.pdf` | Tra cứu C++11 API, C ABI, REST API 12 endpoints & ví dụ tích hợp C++/C99/Python. |
| 7 | `NVXK_Troubleshooting_Handbook.pdf` | Cơ sở dữ liệu xử lý sự cố 17 danh mục theo chuẩn 10 trường an toàn không mất dữ liệu. |
| 8 | `NVXK_Testing_Benchmark_Guide.pdf` | 10 loại kiểm thử, công thức đọc CSV, kết quả xác minh từ `results/` & Báo cáo QA. |