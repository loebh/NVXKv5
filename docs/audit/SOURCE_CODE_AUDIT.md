# Phần I: Kiểm toán Mã nguồn NVXK (Source Code Audit)

Báo cáo kiểm toán này được thực hiện trực tiếp trên toàn bộ 43 tệp tin của kho mã nguồn NVXK (`nvxk-audit.txt`, phiên bản lõi `v2.0.0` kèm nền tảng *NVXK Engineering Intelligence Workbench*). Mọi trạng thái chức năng và điểm không thống nhất dưới đây đều đối chiếu trực tiếp từng tệp nguồn, không suy đoán hay bổ sung tính năng chưa tồn tại.

## 1. Kiểm kê Toàn bộ Dự án (Project Inventory)

### 1.1. Cấu trúc Thư mục và Phân lớp
Kho mã nguồn được chia thành hai tầng rõ ràng: **Lõi tính toán NVXK v2 (C++11 + C ABI)** và **Nền tảng Workbench & Giao diện (C++17 + HTML5/JS + Dear ImGui)**.

| Thư mục / Tệp | Ngôn ngữ | Vai trò kỹ thuật thực tế |
| :--- | :--- | :--- |
| `include/nvxk.h`, `include/nvxk_c.h` | C++11 / C99 | Header API công khai C++ và giao diện C ABI ổn định (`extern "C"`). |
| `src/` (6 `.cpp` + `nvxk_internal.h`) | C++11 + x86-64 ASM | 6 module lõi: API, Hardware Discovery, Planner, Workspace, AVX2+FMA Kernels, Scalar Fallback. |
| `apps/my_app.cpp` | C++11 | Ứng dụng CLI tương tác mẫu nhập ma trận `A`, `B` và gọi `XK_gemm`. |
| `benchmarks/main.cpp` | C++11 | Bộ benchmark CLI (`nvxk.exe`) hỗ trợ 8 thuật toán, hiệu chuẩn, cold-cache, xuất CSV. |
| `tests/nvxk_test.cpp` | C++11 | Kiểm thử lõi (11 nhóm test + 4 regression tests, không dùng framework ngoài). |
| `adapters/core_adapter/` | C++17 | Bộ chuyển đổi bọc lõi NVXK (`AlignedBuffer`, 8 backend, Matrix Lab, bộ kiểm chứng `f64`). |
| `diagnostics/cpu/` | C++17 | Ma trận năng lực ISA 5 tầng và đọc telemetry hệ điều hành (Linux `/proc`, Win32 API). |
| `benchmark/runner/` | C++17 | Động cơ chạy benchmark bất đồng bộ (`std::thread`), hủy an toàn, Measurement Mode, đọc CSV. |
| `benchmark/insights/` | C++17 | Bộ sinh nhận định kỹ thuật dựa trên bằng chứng đo đạc (`CPU`, `Performance`, `Memory`, `Reliability`). |
| `benchmark/reports/` | C++17 | Bộ xuất báo cáo Markdown và CSV (`export_report_markdown`, `export_report_csv`). |
| `ui/server/` | C++17 | Máy chủ HTTP/REST loopback `127.0.0.1:8765` (POSIX sockets / WinSock2), 12 routes. |
| `ui/web/index.html` | HTML5 / CSS3 / JS | Giao diện Web UI đơn tệp (12 trang), chạy 100% offline, **không dùng Node.js/npm**. |
| `ui/imgui/` | C++17 | Ứng dụng Desktop gốc (Dear ImGui Docking + ImPlot v0.16 + GLFW 3.4 + OpenGL 3.3 Core). |
| `tests/workbench_test.cpp` | C++17 | Kiểm thử tích hợp 8 phần cho Adapter, Diagnostics, Engine, Insights, Report, REST API. |
| `scripts/` (6 scripts) | Python 3 / Bash / PS1 | Tự động hóa thực nghiệm Linux/Windows, benchmark OpenBLAS/Memory, vẽ biểu đồ, build Desktop. |
| `results/` (13 tệp CSV) | CSV | Số liệu thực nghiệm đo trên Intel Xeon Platinum 8481C (2C/2T, Linux GCC 12.2.0). |
| `makefile`, `CMakeLists.txt` | Make / CMake >= 3.16 | Hệ thống build kép cho Linux, macOS và Windows (MSYS2 MinGW/GCC, MSVC). |

### 1.2. Entry Points, Thư viện Phụ thuộc & Giao tiếp
- **Entry Points thực tế (6 tệp `main()`)**:
  1. `benchmarks/main.cpp` → `nvxk.exe` (CLI Benchmark Suite).
  2. `apps/my_app.cpp` → `my_app.exe` (Interactive Matrix CLI).
  3. `ui/server/main.cpp` → `nvxk_workbench.exe` (Loopback HTTP Server + Web UI).
  4. `ui/imgui/main_imgui.cpp` → `nvxk_desktop.exe` (Native Desktop App via CMake `-DNVXK_BUILD_DESKTOP_IMGUI=ON`).
  5. `tests/nvxk_test.cpp` → `nvxk-test.exe` (Core Unit/Differential/C ABI Tests).
  6. `tests/workbench_test.cpp` → `workbench-test.exe` (Workbench Integration Tests).
- **Thư viện phụ thuộc**:
  - Lõi & Web Workbench: Thư viện chuẩn C++11/C++17, OpenMP (`libgomp`, tùy chọn nhưng khuyến nghị), `pthread` (Linux/macOS) hoặc `ws2_32` + `psapi` (Windows). **Không có phụ thuộc bên thứ ba bắt buộc**.
  - Desktop ImGui (tùy chọn): CMake `FetchContent` tự tải GLFW `3.4`, Dear ImGui (`docking`), ImPlot `v0.16`, liên kết `OpenGL::GL` (`opengl32`, `gdi32` trên Windows).
  - Tùy chọn đối chứng/biểu đồ: OpenBLAS (`-DNVXK_USE_OPENBLAS`), Python 3 (`numpy`, `matplotlib`).
- **Cơ sở dữ liệu (Database)**: **Không có database** (SQL/NoSQL). Lịch sử benchmark lưu trong RAM (`std::vector<BenchmarkResultRow>` trong `WorkbenchEngine`), lưu trữ lâu dài qua tệp xuất `.csv`, `.md` và hồ sơ phần cứng `.cfg`.
- **Giao thức giao tiếp**:
  - Desktop App (`nvxk_desktop.exe`) gọi trực tiếp C++ API của `WorkbenchEngine` trong cùng tiến trình (không qua mạng).
  - Web UI (`ui/web/index.html`) giao tiếp với `nvxk_workbench.exe` qua **HTTP/1.1 REST + JSON** trên `http://127.0.0.1:8765` (không dùng WebSocket, không dùng Authentication vì chỉ bind `INADDR_LOOPBACK`).

## 2. Phân loại Trạng thái Thực tế từng Chức năng

| Chức năng / Thành phần | Trạng thái | Bằng chứng trong mã nguồn & Ghi chú trung thực |
| :--- | :--- | :--- |
| Lõi GEMM `f32` / `f64` / `i32→i64`, `vector_add_avx2` | **VERIFIED** | `src/nvxk_*.cpp`, `tests/nvxk_test.cpp` (pass trên Linux GCC 12.2.0 + ASan/UBSan + Scalar). |
| Microkernel x86-64 Inline ASM `6×16` (`micro_6x16_full_asm_f32`) | **EXPERIMENTAL / VERIFIED** | Khai báo `prefer_asm_kernel` (mặc định `true` trong `v2.0.0`); đã xác minh nhanh hơn Intrinsics +7.4% (1T `1024³`) trên Xeon 8481C. |
| Kernel `Avx2ThinM` không đóng gói (`M ≤ 4`, `0 KiB` WS) | **VERIFIED** | `src/nvxk_kernels_avx2.cpp`, vượt OpenBLAS 2.64× trên `4×1024×1024` (`results/nvxk_vs_openblas.csv`). |
| Suy giảm mềm theo ngân sách bộ nhớ (`XK_Workspace`) | **VERIFIED** | `src/nvxk_planner.cpp` + `src/nvxk_workspace.cpp`; cắt 96.3% RAM giữ 75.9% GFLOPS (`results/memory_budget_tradeoff.csv`). |
| C ABI (`include/nvxk_c.h`, `libnvxk.so`) & Python `ctypes` | **VERIFIED** | Kiểm chứng trực tiếp qua `test_c_abi()` và `scripts/bench_vs_openblas.py`. |
| Web UI (`ui/web/index.html`) + HTTP Server (`nvxk_workbench.exe`) | **VERIFIED** | Kiểm chứng qua `tests/workbench_test.cpp` (8 phần test) trên Linux; chưa chạy test thủ công trên trình duyệt Windows i7-7700. |
| Desktop UI gốc (`nvxk_desktop.exe`, Dear ImGui + ImPlot + GLFW) | **IMPLEMENTED** | Đã cài đặt đầy đủ mã nguồn (`ui/imgui/*`, `CMakeLists.txt`, `scripts/build_desktop_windows.ps1`); **chưa biên dịch/chạy trong sandbox offline** do `FetchContent` cần Internet. |
| Hiệu chuẩn SMT (4T vs 8T) & Build Windows MSYS2 UCRT64 (`nvxk.dll`) | **IMPLEMENTED** | Mã nguồn Win32 API (`GetLogicalProcessorInformationEx`, `GetSystemTimes`, `ws2_32`) và `scripts/run_experiments.ps1` đã có, **chờ chạy xác minh trên Windows i7-7700**. |
| Nhập ma trận tùy chỉnh & Lọc phiên trong Desktop ImGui | **PARTIALLY IMPLEMENTED** | `lab_custom_a_`, `lab_custom_b_`, `selected_session_idx_` đã khai báo trong `workbench_app.hpp` nhưng chưa có widget nhập liệu trong `workbench_app.cpp`; chưa có nút Import CSV như Web UI. |
| Đối chứng OpenBLAS trên Windows (`scripts/bench_vs_openblas.py`) | **PARTIALLY IMPLEMENTED** | Hoạt động trên Linux (`libnvxk.so`), nhưng hardcode tên tệp `libnvxk.so` nên chưa chạy trực tiếp với `nvxk.dll` trên Windows nếu không sửa đường dẫn. |
| `TransA/TransB`, `beta != 0`, INT8/FP16, Sparse, AVX-512, ARM NEON, GPU | **PLANNED** | Chưa triển khai; đã ghi rõ trong `docs/NVXK_KHKT_Research_Paper.md` mục 14 & 16. |
| Bộ đếm PMU phần cứng (Cache Hit Rate, IPC) & Đọc thanh ghi YMM sống | **PLANNED** | Báo cáo trung thực là `Unavailable` trong `cpu_diagnostics.hpp` và gắn nhãn `[CONCEPTUAL]` trong UI. |
| Trình cài đặt Windows Installer (`.msi` / `Setup.exe`) | **PLANNED** | Chưa có installer; người dùng chạy trực tiếp tệp `.exe` biên dịch từ mã nguồn. |
| Truy cập Web UI qua mạng LAN / Remote | **BROKEN / RESTRICTED** | `ui/server/http_server.cpp` cố định `INADDR_LOOPBACK` (`127.0.0.1`), không cho phép kết nối từ máy khác trong LAN trừ khi sửa mã nguồn hoặc dùng SSH tunnel. |

## 3. Báo cáo Điểm Không Thống Nhất (Inconsistency Audit)

Qua đối chiếu chéo toàn bộ tài liệu hiện có với mã nguồn thực tế, phát hiện **10 điểm không thống nhất** cần lưu ý:

| Mã | Mức độ | Vị trí đối chiếu | Mô tả điểm không thống nhất thực tế |
| :--- | :--- | :--- | :--- |
| **INC-01** | Cao | `README.md` vs `makefile` | `README.md` chỉ mô tả 3 sản phẩm lõi (`nvxk.exe`, `my_app.exe`, `libnvxk.so`), hoàn toàn không đề cập `nvxk_workbench.exe`, `nvxk_desktop.exe`, `workbench-test.exe` và `docs/WORKBENCH_MANUAL.md` dù `make all` và `make test` đều build/chạy Workbench. |
| **INC-02** | Trung bình | `docs/INSTALLATION.md` vs `makefile` & `CMakeLists.txt` | `INSTALLATION.md` ghi `make all` tạo 3 sản phẩm và CTest chạy `1` test (`out of 1`), nhưng thực tế `make all` tạo **4** sản phẩm và CMake đăng ký **2** CTest (`nvxk_unit_tests` + `nvxk_workbench_tests`). |
| **INC-03** | Cao | `docs/WORKBENCH_MANUAL.md` vs `ui/imgui/` & `CMakeLists.txt` | Mục 6 của `WORKBENCH_MANUAL.md` ghi Dear ImGui/ImPlot/GLFW *"Not bundled / evaluated and deferred"*, trong khi thực tế `ui/imgui/`, cờ `NVXK_BUILD_DESKTOP_IMGUI` và `scripts/build_desktop_windows.ps1` đã được triển khai đầy đủ. |
| **INC-04** | Trung bình | `ui/imgui/workbench_app.hpp` vs `.cpp` | `lab_custom_a_`, `lab_custom_b_` và `selected_session_idx_` được khai báo và khởi tạo nhưng không được nối ra giao diện Desktop (khác với Web UI có lưới nhập ma trận `<= 6×6` và bộ lọc Session). |
| **INC-05** | Thấp | `ui/imgui/` vs `ui/web/index.html` | Web UI có nút `Import Historical CSV` gọi `POST /api/benchmark/import_csv`, nhưng Desktop ImGui chưa có nút gọi `engine_.import_csv_results()`. |
| **INC-06** | Trung bình | `ui/server/http_server.cpp` vs chuẩn REST | Trình phân tích JSON tự viết chỉ hỗ trợ JSON phẳng (không hỗ trợ object lồng nhau) và không trả header CORS (`Access-Control-Allow-Origin`), chỉ an toàn khi truy cập cùng nguồn `127.0.0.1:8765`. |
| **INC-07** | Thấp | `ui/server/main.cpp` (Windows vs Linux) | Hàm `open_desktop_window()` chỉ tự mở trình duyệt trên Windows (`ShellExecuteA`); trên Linux là hàm rỗng `(void)url;`, người dùng phải mở thủ công `http://127.0.0.1:8765/`. |
| **INC-08** | Trung bình | `scripts/bench_vs_openblas.py` vs Windows | Script hardcode `libnvxk.so`, sẽ lỗi `OSError` nếu chạy nguyên bản trên Windows (nơi build sinh ra `nvxk.dll`). |
| **INC-09** | Thấp | `ui/imgui/workbench_app.cpp` vs `nvxk_adapter.cpp` | Mảng `bench_bk_flags_[8]` trong ImGui phụ thuộc cứng vào thứ tự 8 phần tử trả về từ `list_implemented_backends()`. |
| **INC-10** | Thấp | `ui/web/index.html` vs `ui/imgui/workbench_app.cpp` | Nút chạy nhanh `Benchmark` trên Web UI hardcode `threads: 2`, trong khi ImGui tự động lấy `diag_.profile.hw.physical_cores`. |