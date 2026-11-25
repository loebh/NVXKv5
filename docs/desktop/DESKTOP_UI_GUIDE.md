# Phần IV: Hướng dẫn Sử dụng Desktop UI (NVXK Native Desktop Workbench)

> **Trạng thái trung thực từ Kiểm toán Mã nguồn**: Ứng dụng Desktop gốc (`nvxk_desktop.exe`, xây dựng bằng **C++17 + Dear ImGui Docking + ImPlot v0.16 + GLFW 3.4 + OpenGL 3.3 Core**) đã được **triển khai mã nguồn đầy đủ** trong thư mục `ui/imgui/` (`main_imgui.cpp`, `theme.cpp`, `workbench_app.cpp`) và `CMakeLists.txt`. Tuy nhiên, dự án **chưa có bộ cài đặt đóng gói sẵn (`.msi` / `Setup.exe`)** và chưa được biên dịch thử trong sandbox offline (do CMake `FetchContent` cần kết nối Internet để tải 3 thư viện GUI từ GitHub). Do đó, phần hướng dẫn dưới đây bám sát 100% mã nguồn thực tế trong `ui/imgui/` và hướng dẫn người dùng biên dịch/chạy trực tiếp từ build artifact.

## 1. Cài đặt và Khởi chạy Desktop App từ Mã nguồn

### 1.1. Biên dịch `nvxk_desktop.exe` trên Windows (i7-7700 + Intel HD 630)
Yêu cầu: Đã cài Git, MSYS2 UCRT64 GCC/CMake (theo Phần II) và có kết nối Internet ở lần build đầu tiên.

```powershell
# Chạy script tự động cấu hình, tải GLFW/ImGui/ImPlot, biên dịch và chạy test
.\scripts\build_desktop_windows.ps1
```

Hoặc chạy thủ công từng bước:
```powershell
cmake -S . -B build_desktop -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release -DNVXK_BUILD_WORKBENCH=ON -DNVXK_BUILD_DESKTOP_IMGUI=ON
cmake --build build_desktop -j
```

### 1.2. Khởi chạy, Tạo Shortcut, Cập nhật, Gỡ cài đặt và Kiểm tra Log
- **Khởi chạy**: Chạy trực tiếp `.\build_desktop\nvxk_desktop.exe` từ PowerShell hoặc nhấp đúp trong File Explorer (lưu ý: cần đảm bảo `C:\msys64\ucrt64\bin` có trong `PATH` hoặc copy các DLL runtime `libstdc++-6.dll`, `libgcc_s_seh-1.dll`, `libgomp-1.dll`, `libwinpthread-1.dll` vào cùng thư mục `build_desktop\`).
- **Tạo Shortcut trên Desktop Windows**: Nhấp chuột phải vào `build_desktop\nvxk_desktop.exe` → **Send to → Desktop (create shortcut)**; nhấp chuột phải vào shortcut → **Properties** → đặt **Start in** là thư mục gốc `NVXK` để các tệp xuất báo cáo (`nvxk_report.md`, `nvxk_results.csv`) lưu đúng chỗ.
- **Cập nhật phiên bản**: Chạy `git pull` → chạy lại `.\scripts\build_desktop_windows.ps1`.
- **Gỡ cài đặt**: Xóa thư mục `build_desktop\` và xóa shortcut (ứng dụng hoàn toàn portable, không ghi Registry).
- **Kiểm tra Log**: `nvxk_desktop.exe` in lỗi GLFW/OpenGL ra `stderr` (nếu chạy từ PowerShell sẽ thấy trực tiếp dòng `GLFW Error <mã>: <mô tả>`). Thông báo trạng thái người dùng (như lỗi chạy trùng benchmark hoặc lưu file thành công) hiển thị trên thanh thông báo màu hổ phách (`status_toast_`) ngay đầu vùng nội dung chính kèm nút **Dismiss**.

## 2. Tham quan Giao diện Desktop (`ui/imgui/workbench_app.cpp`)

Ứng dụng mở cửa sổ **1440×900**, tông màu tối Dark-Graphite (`#111315`, `#17191C`, `#1B1E22`), bật hỗ trợ **ImGui Docking** và **VSync** (`glfwSwapInterval(1)`). Bố cục gồm 4 vùng thực tế:
1. **Top Bar (`##TopBar`, cao 44 px)**: Huy hiệu `XK`, tiêu đề `NVXK Engineering Workbench`, dropdown chọn Backend, dropdown Precision (`FP32`/`FP64`), dropdown Mode (`Measurement Mode`/`Normal Mode`), nút `Calibrate (~200ms)`, nút `Run Benchmark`, nút `Refresh`, nút thu gọn Sidebar (`<<` / `>>`).
2. **Left Sidebar (`##SidebarPane`, rộng 215 px / thu gọn 58 px)**: 4 nhóm điều hướng chứa **12 trang làm việc** + vùng trạng thái dưới cùng (`READY` xanh lá hoặc `BUSY` vàng kèm số nhân/luồng và ISA).
3. **Job Banner (`##JobBanner`)**: Chỉ xuất hiện khi benchmark bất đồng bộ đang chạy; hiển thị huy hiệu `MEASUREMENT`/`NORMAL`, tên bước hiện tại, thanh tiến trình `ProgressBar` và nút `Cancel`.
4. **Main Content Pane (`##MainContentPane`)**: Hiển thị 1 trong 12 trang làm việc:
   - *Nhóm INTELLIGENCE*: **Overview**, **Vector & Matrix Lab**, **Execution Intelligence**.
   - *Nhóm PERFORMANCE*: **CPU Architecture**, **Performance Benchmark**, **Top Functions**, **Kernel Analysis**.
   - *Nhóm SYSTEM & VERIFICATION*: **Memory Analysis**, **Numerical Reliability**, **Diagnostics**.
   - *Nhóm WORKSPACE*: **Reports**, **Settings**.

> **Lưu ý tính năng chưa có trên Desktop ImGui (khác với Web UI)**: Theo Kiểm toán Mã nguồn (`INC-04`, `INC-05`), Desktop ImGui hiện **chưa có** bộ lọc Session trên Top Bar, **chưa có** lưới nhập từng ô ma trận tùy chỉnh trong Matrix Lab (luôn sinh dữ liệu ngẫu nhiên theo `Seed`), và **chưa có** nút Import CSV lịch sử. Nếu cần các tính năng này, hãy dùng Web Workbench (`nvxk_workbench.exe`).

## 3. Hướng dẫn Từng Nút bấm và Thành phần Điều khiển Thực tế

### 3.1. Thanh Công cụ Trên cùng (Top Bar)

| Thành phần | Vị trí | Chức năng & Khi nào dùng | Thao tác & Kết quả mong đợi | Nếu không hoạt động |
| :--- | :--- | :--- | :--- | :--- |
| **Dropdown Backend (`##TopBackend`)** | Top Bar, trái | Chọn backend mặc định dùng cho Matrix Lab, Execution Intelligence và Reliability Suite. | Nhấn để chọn 1 trong 8 backend (`auto_asm`, `auto`, `avx2_asm`, `avx2`, `omp`, `blocked`, `ikj`, `naive`). Backend không khả dụng tự bị làm mờ (disabled). | Kiểm tra tab **CPU Architecture** xem CPU/compiler có hỗ trợ AVX2+FMA hay không. |
| **Dropdown Precision (`##TopPrec`)** | Top Bar, giữa | Chuyển đổi độ chính xác giữa `FP32 (float)` và `FP64 (double)`. | Chọn precision trước khi bấm `Run Benchmark`, `Inspect Plan` hoặc `Run 7-Shape Suite`. | Lưu ý `avx2_asm` và `blocked` tự bỏ qua khi chạy `FP64`. |
| **Dropdown Mode (`##TopMode`)** | Top Bar, giữa | Chuyển giữa `Measurement Mode` (tạm dừng poll telemetry OS khi đo để chống nhiễu) và `Normal Mode`. | Giữ `Measurement Mode` khi benchmark nghiên cứu để giảm jitter. | Không thay đổi được giữa lúc job đang chạy; chờ xong hoặc bấm `Cancel`. |
| **BUTTON: `Calibrate (~200ms)`** | Top Bar, phải | Chạy `XK_calibrate_hardware(200.0)` đo tốc độ 1T thực, băng thông RAM, chi phí OpenMP và SMT. | Bấm 1 lần sau khi mở app. Hồ sơ chuyển từ `Default` sang `Calibrated` trong tab **CPU Architecture**. | Nút bị khóa khi đang có benchmark chạy. Chờ benchmark xong rồi thử lại. |
| **BUTTON: `Run Benchmark`** | Top Bar, phải (màu xanh) | Khởi chạy nhanh phiên benchmark `256×256×256` trên 5 backend (`ikj`, `avx2`, `avx2_asm`, `auto`, `auto_asm`), 5 runs, `threads = physical_cores`, bật kiểm chứng `f64`. | Bấm 1 lần. Banner tiến trình xuất hiện, sau vài giây 5 thẻ KPI, biểu đồ ImPlot và bảng Top Functions tự cập nhật. | Nếu hiện toast màu vàng *"A benchmark session is already running"*, bấm `Cancel` trên banner rồi chạy lại. |
| **BUTTON: `Refresh`** | Top Bar, phải | Ép làm mới ngay telemetry OS, lịch sử và insights. | Bấm khi muốn lấy mẫu CPU/RSS tức thời. | Không có tác dụng đo lại benchmark; chỉ cập nhật hiển thị. |
| **BUTTON: `<<` / `>>`** | Top Bar, ngoài cùng phải | Thu gọn/mở rộng Sidebar (215 px ↔ 58 px với ký hiệu 2 chữ cái + tooltip). | Bấm khi cần thêm không gian cho biểu đồ hoặc bảng rộng. | — |

### 3.2. Các Nút và Điều khiển theo từng Trang làm việc

- **BUTTON: `Cancel`** (trong Job Banner): Gửi cờ hủy nguyên tử (`cancel_benchmark()`) để dừng phiên benchmark đang chạy ở cuối mẫu hiện tại mà không làm treo UI.
- **Overview — Dropdown `##ChartMetric`**: Chuyển đổi 6 chỉ số trên biểu đồ ImPlot (`Throughput (GFLOPS)`, `Median Latency (ms)`, `Bandwidth (GB/s)`, `Workspace (KiB)`, `Max Rel Error`, `Live CPU (%)`).
- **Overview / Top Functions — Ô lọc `##TblSearch` & Click dòng bảng**: Lọc theo tên hàm/backend không phân biệt hoa thường; click một dòng để mở thanh **Inspector** ở đáy bảng xem `Kernel`, `Threads`, `(MC, KC, NC)`, `Workspace` và chuỗi giải trình `Planner`.
- **Vector & Matrix Lab — `Operation`, `Precision`, `M`, `K`, `N`, `Threads`, `Seed`, BUTTON: `Execute Operation`**:
  - Chức năng: Chạy thực tế phép nhân ma trận (`C = A × B`) hoặc cộng vector (`c = a + b`) với giới hạn an toàn `1..2048`.
  - Kết quả mong đợi: Hiện thời gian `ms`, `GFLOPS`, trạng thái căn lề 64B, kết quả kiểm chứng `PASS/FAIL` so với `f64` và biểu đồ nhiệt (Heatmap `<= 16×16`) bằng ImPlot.
  - Nếu lỗi: Nếu đang chạy benchmark, sẽ báo *"Cannot run Matrix Lab while a benchmark session is active"*.
- **Execution Intelligence — `M`, `K`, `N`, `Budget KiB`, BUTTON: `Inspect Plan`**:
  - Gọi trực tiếp `XK_plan_gemm()` không cần chạy nhân ma trận, in `Kernel`, `Axis`, `Threads`, `(MC, KC, NC)`, `Workspace`, `Clamped`, `SMT L2-Halved` và chuỗi `Rationale`.
- **Performance Benchmark — Form cấu hình + Checkboxes 8 backend + BUTTON: `Launch Benchmark Session` + BUTTON: `Clear History`**:
  - Cho phép nhập kích thước tùy chỉnh (`1..4096`), số `Runs` (`1..100`), `Manual OMP Threads`, `Workspace Budget (KiB)`, tick `Measurement Mode`, `Cold-Cache Mode`, `Validate every backend vs f64 reference`.
  - Bấm `Launch Benchmark Session` để chạy bất đồng bộ; bấm `Clear History` để xóa toàn bộ kết quả trong RAM của phiên hiện tại.
- **Numerical Reliability — BUTTON: `Run 7-Shape Edge-Case Verification Suite`**:
  - Chạy tự động 7 hình dạng biên (`1×1×1`, `1×64×64`, `4×128×128`, `6×16×16`, `7×13×19`, `63×65×67`, `128×128×128`), in bảng `MaxAbsErr`, `MaxRelErr`, `RMSE`, `NaN/Inf`, `PASS/FAIL` và vẽ Heatmap sai số của ma trận `128³`.
- **Reports — BUTTON: `Generate Markdown Preview`, `Save nvxk_report.md`, `Save nvxk_results.csv`**:
  - Xem trước báo cáo Markdown hoặc ghi trực tiếp ra thư mục làm việc hiện tại.
- **Settings — Slider `Telemetry Poll Interval (ms)` (`500..10000`), Checkboxes `Default Measurement Mode`, `Compact Sidebar`**:
  - Điều chỉnh chu kỳ lấy mẫu OS và hành vi mặc định (lưu trong phiên chạy hiện tại).

## 4. Xử lý Sự cố Desktop UI (Desktop Troubleshooting — 17 Tình huống)

| # | Tình huống lỗi | Chẩn đoán nhanh | Quy trình khắc phục an toàn |
| :--- | :--- | :--- | :--- |
| 1 | **Không mở được ứng dụng** | Chạy `.\build_desktop\nvxk_desktop.exe` trong PowerShell để đọc mã thoát và thông báo lỗi. | Kiểm tra xem đã build target `nvxk_desktop` chưa; kiểm tra quyền thực thi và phần mềm diệt virus có cách ly file `.exe` mới biên dịch không. |
| 2 | **Ứng dụng mở rồi tự đóng ngay** | Xem `stderr` trong PowerShell (`GLFW Error 65543: GLX/WGL: Failed to create context`). | Driver GPU chưa hỗ trợ OpenGL 3.3 Core. Cập nhật driver Intel HD Graphics 630 mới nhất từ Intel; tránh chạy qua phiên Remote Desktop (RDP) cũ chỉ hỗ trợ OpenGL 1.1. |
| 3 | **Màn hình trắng / đen kịt** | Kiểm tra tải GPU và log `ImGui_ImplOpenGL3_Init`. | Tắt phần mềm overlay (RivaTuner, MSI Afterburner, Discord Overlay) gây xung đột OpenGL context; chạy lại ứng dụng. |
| 4 | **UI bị treo (Not Responding)** | Kiểm tra xem người dùng có vừa bấm `Execute Operation` (Matrix Lab) hay `Run 7-Shape Suite` với kích thước quá lớn không (hai thao tác này chạy đồng bộ trên luồng UI). | Chờ phép tính hoàn tất (tối đa vài giây với `<= 2048`). Với ma trận lớn (`> 512`), luôn dùng tab **Performance Benchmark** (chạy bất đồng bộ trên `std::thread`). |
| 5 | **Nút bấm không phản hồi / bị làm mờ** | Quan sát trạng thái `BUSY` ở đáy Sidebar hoặc thanh Job Banner. | Các nút `Calibrate`, `Run Benchmark`, `Launch Benchmark Session` cố tình bị vô hiệu hóa khi đang có job chạy để tránh tranh chấp dữ liệu. Bấm `Cancel` hoặc chờ xong. |
| 6 | **Dropdown không hiển thị hoặc mục bị mờ** | Mở tab **Kernel Analysis** xem cột `Available`. | `avx2_asm` và `avx2` tự động bị khóa nếu CPU không có AVX2+FMA hoặc bản build bật `-DXK_FORCE_SCALAR_FALLBACK`. |
| 7 | **Không tải được dữ liệu (lịch sử trống)** | Desktop ImGui không lưu database giữa các lần tắt app. | Bấm `Run Benchmark` để sinh dữ liệu mới; nếu cần nạp CSV cũ từ `results/`, hãy dùng Web UI (`nvxk_workbench.exe`). |
| 8 | **Không kết nối được backend** | Desktop ImGui liên kết tĩnh trực tiếp với `nvxk_workbench_engine` (không dùng mạng). | Nếu thấy lỗi không có backend nào, kiểm tra bản build có bị hỏng không và build lại bằng `.\scripts\build_desktop_windows.ps1`. |
| 9 | **Không chạy được benchmark** | Đọc dòng toast màu vàng ngay dưới Top Bar. | Sửa tham số vi phạm ràng buộc: `M, K, N` phải trong `1..4096`, `Runs` trong `1..100`, chọn ít nhất 1 backend khả dụng. |
| 10 | **Không hiển thị CPU Information / CPU% là `Unavailable`** | Kiểm tra tab **Diagnostics**. | Trên Windows, đảm bảo liên kết `-lpsapi`; trên Linux, kiểm tra quyền đọc `/proc/stat` và `/proc/cpuinfo`. |
| 11 | **Không xuất được báo cáo (`nvxk_report.md` / `.csv`)** | Kiểm tra thư mục làm việc hiện tại (`pwd`) có quyền ghi hay không. | Không chạy từ `C:\Program Files\`; đặt thư mục làm việc (Start in) về thư mục người dùng có quyền ghi (ví dụ `C:\dev\NVXK`). |
| 12 | **Cấu hình không được lưu sau khi đóng app** | Thiết kế hiện tại của `WorkbenchApp` lưu `Settings` trong RAM. | Đây là giới hạn hiện tại của mã nguồn (`ui/imgui/workbench_app.hpp`), chưa có tệp cấu hình `.ini` cho settings ứng dụng (chỉ lưu bố cục cửa sổ `imgui.ini`). |
| 13 | **Ứng dụng bị crash khi chạy ma trận lớn** | Kiểm tra RAM còn trống trong Task Manager hoặc tab **Memory Analysis**. | Giảm kích thước `M, K, N` hoặc giảm `Workspace Budget (KiB)` xuống `1024` hoặc `512` để tránh `std::bad_alloc` khi hệ thống thiếu RAM. |
| 14 | **Thiếu DLL (`libstdc++-6.dll`, `libgomp-1.dll`, `libwinpthread-1.dll`)** | Hộp thoại Windows báo *"The code execution cannot proceed because libgomp-1.dll was not found"*. | Thêm `C:\msys64\ucrt64\bin` vào biến môi trường `PATH` hệ thống, hoặc copy 4 tệp DLL trên từ `C:\msys64\ucrt64\bin` vào thư mục chứa `nvxk_desktop.exe`. |
| 15 | **Lỗi quyền truy cập (Access Denied)** | Windows Defender / SmartScreen chặn tệp `.exe` tự biên dịch hoặc không ghi được file báo cáo. | Thêm thư mục build vào ngoại lệ quét cục bộ (nếu cần) và chạy trong thư mục thuộc quyền User (không cần quyền Administrator). |
| 16 | **Lỗi đường dẫn (có dấu tiếng Việt / khoảng trắng)** | CMake `FetchContent` hoặc MinGW gặp lỗi khi đường dẫn chứa ký tự Unicode. | Di chuyển kho mã nguồn NVXK về đường dẫn ngắn, không dấu, không khoảng trắng như `C:\dev\NVXK`. |
| 17 | **Lỗi DPI hoặc chữ bị mờ / quá nhỏ trên màn hình HiDPI** | Windows Scaling 125%–150% làm cửa sổ GLFW bị phóng mờ. | Nhấp chuột phải `nvxk_desktop.exe` → **Properties → Compatibility → Change high DPI settings** → đánh dấu **Override high DPI scaling behavior** → chọn **Application**. |