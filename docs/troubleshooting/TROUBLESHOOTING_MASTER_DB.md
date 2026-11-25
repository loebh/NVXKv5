# Phần IX: Cơ sở Dữ liệu Xử lý Sự cố Toàn diện (Troubleshooting Master Database)

Cơ sở dữ liệu này bao phủ đủ **17 danh mục lỗi** của hệ thống NVXK, chuẩn hóa theo 10 trường cố định và gắn nhãn minh bạch trạng thái xác minh:
- **[ĐÃ TÁI HIỆN / REPRODUCED]**: Đã gặp và kiểm chứng trực tiếp trong quá trình phát triển/test của kho mã nguồn.
- **[ĐÃ BIẾT / KNOWN]**: Ghi nhận rõ từ mã nguồn và tài liệu kỹ thuật (`docs/TROUBLESHOOTING.md`, `CMakeLists.txt`).
- **[TIỀM ẨN / POTENTIAL]**: Có thể xảy ra dựa trên cấu trúc mã nguồn (ví dụ: ranh giới giới hạn, đường dẫn Windows).
- **[GIẢ ĐỊNH CẦN KIỂM CHỨNG / HYPOTHETICAL]**: Tình huống môi trường đặc thù chưa chạy trực tiếp trong sandbox.

Mọi phương án xử lý đều **ưu tiên không phá hủy dữ liệu và cấu hình người dùng** (không xóa `results/`, không ghi đè mã nguồn lõi).

---

### 1. Installation Errors

**ERROR ID**: `ERR-INST-001` | **Trạng thái**: `[ĐÃ BIẾT / KNOWN]`
- **ERROR NAME**: Thiếu công cụ `g++`, `mingw32-make` hoặc `cmake` sau khi cài MSYS2
- **SEVERITY**: High
- **SYMPTOMS**: PowerShell báo `'g++' is not recognized as the name of a cmdlet`.
- **POSSIBLE CAUSES**: Chưa cài gói trong MSYS2 UCRT64 hoặc chưa thêm `C:\msys64\ucrt64\bin` vào `PATH` Windows.
- **DIAGNOSTIC STEPS**: Mở PowerShell mới, kiểm tra biến `$env:Path` và vị trí thực thi `g++`.
- **COMMANDS**: `where.exe g++` và `$env:Path -split ';' | Select-String msys64`
- **SOLUTION**: Cài gói trong MSYS2 UCRT64 bằng `pacman -S --needed mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-make mingw-w64-ucrt-x86_64-cmake`, sau đó thêm `C:\msys64\ucrt64\bin` vào User `Path` và mở lại PowerShell.
- **VERIFICATION**: Chạy `g++ --version` và `mingw32-make --version` trả về phiên bản MSYS2 UCRT64.
- **PREVENTION**: Luôn dùng đúng shell **MSYS2 UCRT64** khi cài gói `pacman`.

### 2. Environment Errors

**ERROR ID**: `ERR-ENV-001` | **Trạng thái**: `[ĐÃ BIẾT / KNOWN]`
- **ERROR NAME**: Cảnh báo `libgomp: Affinity not supported` trên Windows MinGW
- **SEVERITY**: Low
- **SYMPTOMS**: Terminal in cảnh báo từ `libgomp` khi khởi chạy `nvxk.exe` hoặc `nvxk_workbench.exe`.
- **POSSIBLE CAUSES**: Người dùng đặt biến môi trường `OMP_PROC_BIND=true` hoặc `OMP_PLACES=cores` trên MinGW/MSYS2 Windows.
- **DIAGNOSTIC STEPS**: Kiểm tra các biến môi trường `OMP_*` đang hoạt động.
- **COMMANDS**: `Get-ChildItem Env:OMP*` (PowerShell) hoặc `env | grep OMP` (Bash).
- **SOLUTION**: Xóa tạm hai biến này trong phiên làm việc: `Remove-Item Env:OMP_PROC_BIND, Env:OMP_PLACES -ErrorAction SilentlyContinue`.
- **VERIFICATION**: Chạy lại `.\nvxk.exe --size 64 --runs 1 --algorithm auto`, không còn cảnh báo.
- **PREVENTION**: Không đặt `OMP_PROC_BIND` toàn cục trên Windows MinGW; NVXK đã tự gọi `omp_set_dynamic(0)`.

### 3. Compiler Errors

**ERROR ID**: `ERR-COMP-001` | **Trạng thái**: `[ĐÃ BIẾT / KNOWN]`
- **ERROR NAME**: Thiếu OpenMP (`fatal error: omp.h: No such file or directory` hoặc lỗi link `-lgomp`)
- **SEVERITY**: High
- **SYMPTOMS**: `make all` dừng ngay ở bước biên dịch `nvxk_hw.cpp` hoặc bước liên kết.
- **POSSIBLE CAUSES**: Hệ thống Linux thiếu gói `libgomp1`, hoặc Windows dùng bộ MinGW rút gọn không kèm OpenMP.
- **DIAGNOSTIC STEPS**: Kiểm tra trình biên dịch có hỗ trợ `-fopenmp` không.
- **COMMANDS**: `echo '#include <omp.h>' | g++ -x c++ -fopenmp -fsyntax-only -`
- **SOLUTION**: Linux: `sudo apt-get install -y g++ libgomp1`; Windows: cài đúng `mingw-w64-ucrt-x86_64-gcc` qua MSYS2 UCRT64.
- **VERIFICATION**: Lệnh kiểm tra cú pháp trả exit code `0` và `make all` chạy sạch.
- **PREVENTION**: Sử dụng đúng toolchain GCC 11+ được khuyến nghị trong Phần II.

### 4. Build Errors

**ERROR ID**: `ERR-BLD-001` | **Trạng thái**: `[ĐÃ BIẾT / KNOWN]`
- **ERROR NAME**: CMake `FetchContent` lỗi khi build `nvxk_desktop.exe` trong môi trường không có mạng
- **SEVERITY**: High
- **SYMPTOMS**: CMake báo lỗi `Failed to clone repository: 'https://github.com/glfw/glfw.git'` khi bật `-DNVXK_BUILD_DESKTOP_IMGUI=ON`.
- **POSSIBLE CAUSES**: Máy không có Internet, bị tường lửa chặn GitHub, hoặc chưa cài `git`.
- **DIAGNOSTIC STEPS**: Kiểm tra `git --version` và kết nối HTTPS tới `github.com`.
- **COMMANDS**: `git ls-remote https://github.com/glfw/glfw.git HEAD`
- **SOLUTION**: Kết nối Internet (hoặc cấu hình proxy Git) cho lần cấu hình đầu tiên. Nếu máy hoàn toàn offline, dùng **Web Workbench** (`mingw32-make workbench`) vì không cần tải bất kỳ thư viện ngoài nào.
- **VERIFICATION**: `cmake --build build_desktop -j` tạo thành công `build_desktop\nvxk_desktop.exe`.
- **PREVENTION**: Lưu giữ thư mục `build_desktop/_deps` sau lần tải đầu để tái sử dụng offline.

### 5. Runtime Errors

**ERROR ID**: `ERR-RUN-001` | **Trạng thái**: `[ĐÃ TÁI HIỆN / REPRODUCED]`
- **ERROR NAME**: `Permission denied` khi chạy tệp thực thi đặt trong `/tmp` (Linux/Container)
- **SEVERITY**: Medium
- **SYMPTOMS**: Biên dịch thành công nhưng chạy `./file` trong `/tmp` báo `Permission denied`.
- **POSSIBLE CAUSES**: Phân vùng `/tmp` được mount với cờ bảo mật `noexec`.
- **DIAGNOSTIC STEPS**: Kiểm tra cờ mount của `/tmp`.
- **COMMANDS**: `mount | grep ' /tmp '`
- **SOLUTION**: Biên dịch và chạy toàn bộ tệp thực thi ngay trong thư mục dự án (`./nvxk.exe`, `./build/nvxk`).
- **VERIFICATION**: Chạy `./nvxk-test.exe` tại thư mục gốc trả về `[PASS]`.
- **PREVENTION**: Không đặt thư mục build CMake vào `/tmp`.

### 6. Desktop UI Errors

**ERROR ID**: `ERR-DESK-001` | **Trạng thái**: `[TIỀM ẨN / POTENTIAL]`
- **ERROR NAME**: Cửa sổ `nvxk_desktop.exe` không mở được do thiếu DLL runtime MinGW hoặc lỗi OpenGL 3.3
- **SEVERITY**: High
- **SYMPTOMS**: Hộp thoại Windows báo thiếu `libgomp-1.dll` / `libstdc++-6.dll`, hoặc đóng ngay kèm `GLFW Error: Failed to create context`.
- **POSSIBLE CAUSES**: `C:\msys64\ucrt64\bin` chưa có trong `PATH`, hoặc driver Intel HD 630 quá cũ / đang chạy qua RDP.
- **DIAGNOSTIC STEPS**: Chạy `.\build_desktop\nvxk_desktop.exe` trực tiếp trong PowerShell để đọc `stderr`.
- **COMMANDS**: `.\build_desktop\nvxk_desktop.exe`
- **SOLUTION**: Thêm `C:\msys64\ucrt64\bin` vào `PATH` (hoặc copy 4 DLL runtime vào `build_desktop\`); cập nhật driver đồ họa Intel HD Graphics 630; chạy trực tiếp trên màn hình cục bộ.
- **VERIFICATION**: Cửa sổ 1440×900 hiển thị đầy đủ Top Bar, Sidebar và 5 thẻ KPI.
- **PREVENTION**: Giữ `C:\msys64\ucrt64\bin` trong `PATH` hệ thống.

### 7. Web UI Errors

**ERROR ID**: `ERR-WEB-001` | **Trạng thái**: `[ĐÃ BIẾT / KNOWN]`
- **ERROR NAME**: Trình duyệt hiển thị `UI file not found at ui/web/index.html` (HTTP 404)
- **SEVERITY**: Medium
- **SYMPTOMS**: Mở `http://127.0.0.1:8765/` nhận trang văn bản trắng báo 404.
- **POSSIBLE CAUSES**: Khởi chạy `nvxk_workbench.exe` từ thư mục con (ví dụ `cd build && ./nvxk_workbench`) thay vì thư mục gốc chứa `ui/web/index.html`.
- **DIAGNOSTIC STEPS**: Kiểm tra thư mục hiện tại có chứa `ui/web/index.html` không.
- **COMMANDS**: `ls ui/web/index.html` (Bash) hoặc `Test-Path ui\web\index.html` (PowerShell).
- **SOLUTION**: Chạy từ thư mục gốc repository hoặc truyền rõ `--ui-dir <đường_dẫn_tới_ui/web>`.
- **VERIFICATION**: Tải lại `http://127.0.0.1:8765/` hiển thị giao diện Dark-Graphite.
- **PREVENTION**: Luôn chạy kèm tham số tường minh `--ui-dir ui/web --results-dir results`.

### 8. Backend Errors

**ERROR ID**: `ERR-BE-001` | **Trạng thái**: `[ĐÃ BIẾT / KNOWN]`
- **ERROR NAME**: `bind() failed on 127.0.0.1:8765` (Cổng TCP đang bị chiếm)
- **SEVERITY**: Medium
- **SYMPTOMS**: `nvxk_workbench.exe` thoát ngay với mã lỗi `1` và thông báo `bind() failed`.
- **POSSIBLE CAUSES**: Một tiến trình `nvxk_workbench.exe` cũ vẫn đang chạy nền hoặc phần mềm khác chiếm cổng `8765`.
- **DIAGNOSTIC STEPS**: Kiểm tra tiến trình đang giữ cổng `8765`.
- **COMMANDS**: Windows: `netstat -ano | findstr :8765` | Linux: `ss -ltnp | grep 8765`
- **SOLUTION**: Tắt tiến trình cũ hoặc đổi cổng khởi chạy sang cổng trống: `.\nvxk_workbench.exe --port 8766`.
- **VERIFICATION**: Terminal in `Listening on http://127.0.0.1:8766/`.
- **PREVENTION**: Luôn bấm `Ctrl+C` trong terminal để đóng sạch server trước khi tắt cửa sổ.

### 9. API Errors

**ERROR ID**: `ERR-API-001` | **Trạng thái**: `[ĐÃ TÁI HIỆN / REPRODUCED]`
- **ERROR NAME**: `std::invalid_argument: output matrix C must not alias A or B` / `NVXK_ERR_INVALID_ARG (-1)`
- **SEVERITY**: High
- **SYMPTOMS**: Gọi `XK_gemm(A, B, A, ...)` ném ngoại lệ hoặc `nvxk_sgemm` trả `-1`.
- **POSSIBLE CAUSES**: Con trỏ đầu ra `C` trùng hoàn toàn hoặc chồng lấn một phần vùng nhớ với `A` hoặc `B`.
- **DIAGNOSTIC STEPS**: Kiểm tra địa chỉ con trỏ và kích thước byte của 3 bộ đệm; đọc `nvxk_last_error()` nếu dùng C ABI.
- **COMMANDS**: `./nvxk-test.exe` (xác minh `test_aliasing_rejection` hoạt động đúng).
- **SOLUTION**: Cấp phát mảng `C` ở vùng nhớ hoàn toàn tách biệt với `A` và `B`.
- **VERIFICATION**: Hàm thực thi trả `NVXK_OK (0)` và không ném ngoại lệ.
- **PREVENTION**: Không bao giờ nhân ma trận tại chỗ (in-place) bằng API GEMM của NVXK.

### 10. CPU Compatibility Errors

**ERROR ID**: `ERR-CPU-001` | **Trạng thái**: `[ĐÃ BIẾT / KNOWN]`
- **ERROR NAME**: Biên dịch bằng MSVC (`cl.exe`) khiến hiệu năng chậm bất thường (chạy Scalar Fallback)
- **SEVERITY**: High
- **SYMPTOMS**: Thông lượng `1024³` chỉ đạt vài GFLOPS dù CPU hỗ trợ AVX2+FMA; tab **CPU Architecture** báo `AVX2: Compiler Supports = NO`.
- **POSSIBLE CAUSES**: Macro `XK_ENABLE_X86_AVX2_DISPATCH` trong `src/nvxk_kernels_avx2.cpp` chỉ bật cho `__GNUC__ || __clang__`.
- **DIAGNOSTIC STEPS**: Chạy `./nvxk.exe --size 128 --runs 1 --algorithm auto` xem dòng `ISA: AVX2+FMA=No`.
- **COMMANDS**: `cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release`
- **SOLUTION**: Chuyển sang biên dịch bằng **MSYS2 UCRT64 GCC** (`mingw32-make all`) hoặc Clang trên Windows.
- **VERIFICATION**: Dòng `ISA` hiển thị `AVX2+FMA=Yes, x86-64 ASM=Yes`.
- **PREVENTION**: Luôn chỉ định `-G "MinGW Makefiles"` khi dùng CMake trên Windows.

### 11. SIMD Errors

**ERROR ID**: `ERR-SIMD-001` | **Trạng thái**: `[TIỀM ẨN / POTENTIAL]`
- **ERROR NAME**: Chết tiến trình `SIGILL` (Illegal Instruction) nếu người dùng tự thêm `-mavx2` toàn cục
- **SEVERITY**: Critical
- **SYMPTOMS**: Chương trình crash ngay khi khởi động trên CPU không có AVX2.
- **POSSIBLE CAUSES**: Chỉnh sửa `makefile`/`CMakeLists.txt` thêm `-mavx2` hoặc `-march=native` vào `CXXFLAGS` làm phá vỡ cơ chế runtime dispatch.
- **DIAGNOSTIC STEPS**: Kiểm tra cờ biên dịch trong `makefile` và `CMakeLists.txt`.
- **COMMANDS**: `grep -rn "mavx2\|march=native" makefile CMakeLists.txt`
- **SOLUTION**: Xóa ngay các cờ toàn cục `-mavx2`, `-mfma`, `-march=native`; giữ nguyên cơ chế `__attribute__((target("avx2,fma")))` mặc định của NVXK.
- **VERIFICATION**: `make test-scalar` chạy thành công và binary chạy an toàn trên CPU không AVX2.
- **PREVENTION**: Tuân thủ quy tắc số 1 trong `CONTRIBUTING.md`.

### 12. Memory Errors

**ERROR ID**: `ERR-MEM-001` | **Trạng thái**: `[ĐÃ TÁI HIỆN / REPRODUCED]`
- **ERROR NAME**: `std::length_error: XK_Workspace: required scratchpad exceeds memory budget`
- **SEVERITY**: Medium
- **SYMPTOMS**: Ném ngoại lệ `std::length_error` khi cấp phát workspace.
- **POSSIBLE CAUSES**: Gọi trực tiếp `ws.prepare_f32(...)` vượt ngân sách thay vì đi qua `XK_gemm_ex`, hoặc gọi `XK_Workspace` quá nhỏ.
- **DIAGNOSTIC STEPS**: Kiểm tra giá trị `ws.memory_budget_bytes()` và lời gọi API.
- **COMMANDS**: `./nvxk-test.exe` (xem `test_graceful_memory_budget_clamping`).
- **SOLUTION**: Truyền `XK_Workspace` vào `XK_gemm_ex` / `XK_gemm_f64_ex` để Adaptive Planner tự động co khối `(NC → MC → KC)` hoặc suy giảm mềm về `ScalarIkj` (`0 KiB`) mà không ném lỗi.
- **VERIFICATION**: `ws.allocated_bytes() <= ws.memory_budget_bytes()` và không ném ngoại lệ.
- **PREVENTION**: Không gọi trực tiếp `ws.prepare_f32` từ mã ứng dụng ngoài.

### 13. Multithreading Errors

**ERROR ID**: `ERR-MT-001` | **Trạng thái**: `[TIỀM ẨN / POTENTIAL]`
- **ERROR NAME**: Data race / sai kết quả khi nhiều luồng ngoài dùng chung một đối tượng `XK_Workspace`
- **SEVERITY**: Critical
- **SYMPTOMS**: Kết quả ma trận sai ngẫu nhiên hoặc crash khi nhiều `std::thread` gọi `XK_gemm_ex(..., &shared_ws)` đồng thời.
- **POSSIBLE CAUSES**: `XK_Workspace` không được thiết kế để chia sẻ đồng thời giữa nhiều lời gọi `XK_gemm_ex` song song từ các luồng ngoài (nó chỉ chia khe nội bộ cho OpenMP của *một* lời gọi).
- **DIAGNOSTIC STEPS**: Rà soát các luồng gọi `XK_gemm_ex` xem có dùng chung con trỏ `XK_Workspace*` hay không.
- **COMMANDS**: Chạy kiểm thử ASan (`make test-san`).
- **SOLUTION**: Cấp mỗi luồng ngoài một `XK_Workspace` riêng, hoặc truyền `nullptr` để NVXK tự dùng `thread_local XK_Workspace`.
- **VERIFICATION**: Chạy lặp đa luồng cho kết quả khớp 100% tham chiếu `f64`.
- **PREVENTION**: Đọc mục 3 trong `docs/RELIABILITY.md` trước khi tích hợp đa luồng.

### 14. Testing Errors

**ERROR ID**: `ERR-TEST-001` | **Trạng thái**: `[GIẢ ĐỊNH CẦN KIỂM CHỨNG / HYPOTHETICAL]`
- **ERROR NAME**: `make test-san` thất bại trên Windows MSYS2 MinGW
- **SEVERITY**: Low
- **SYMPTOMS**: Trình biên dịch MinGW báo không tìm thấy thư viện `libasan` / `libubsan` khi chạy `mingw32-make test-san`.
- **POSSIBLE CAUSES**: Gói MinGW GCC trên Windows thường không hỗ trợ đầy đủ `-fsanitize=address,undefined` như GCC trên Linux.
- **DIAGNOSTIC STEPS**: Kiểm tra lỗi link từ `g++ -fsanitize=address`.
- **COMMANDS**: `mingw32-make test && mingw32-make test-scalar`
- **SOLUTION**: Trên Windows, chạy `mingw32-make test` và `mingw32-make test-scalar`; dùng WSL2 (Ubuntu) hoặc môi trường Linux để chạy `make test-san`.
- **VERIFICATION**: Cả `nvxk-test.exe` và `workbench-test.exe` trả `[PASS]`.
- **PREVENTION**: Sử dụng Linux/WSL2 cho bước kiểm tra Sanitizer trước khi phát hành.

### 15. Benchmark Errors

**ERROR ID**: `ERR-BENCH-001` | **Trạng thái**: `[ĐÃ BIẾT / KNOWN]`
- **ERROR NAME**: Độ lệch chuẩn cao (`CV > 10%`) hoặc `HTTP 409 Conflict` khi chạy benchmark
- **SEVERITY**: Low
- **SYMPTOMS**: Insights báo `Elevated latency variance`, hoặc bấm `Run Benchmark` báo lỗi 409.
- **POSSIBLE CAUSES**: Chạy trong `Normal Mode` khi máy đang mở nhiều ứng dụng nặng, số `runs` quá ít (`< 5`), hoặc bấm chạy trong khi phiên trước chưa kết thúc.
- **DIAGNOSTIC STEPS**: Xem cột `stddev_ms` và trạng thái `BUSY` ở đáy Sidebar.
- **COMMANDS**: `./nvxk.exe --calibrate --runs 9 --sizes 256,512,1024 --algorithm auto_asm`
- **SOLUTION**: Đóng ứng dụng nền, bật **Measurement Mode**, tăng `runs >= 7` (hoặc `9`), và chờ job hiện tại kết thúc trước khi chạy phiên mới.
- **VERIFICATION**: Hệ số biến thiên `CV < 5%` ở chế độ warm-cache.
- **PREVENTION**: Cắm sạc laptop (chế độ High Performance) khi đo benchmark.

### 16. Integration Errors

**ERROR ID**: `ERR-INT-001` | **Trạng thái**: `[ĐÃ TÁI HIỆN / REPRODUCED]`
- **ERROR NAME**: `libnvxk.so: cannot open shared object file` (Linux) hoặc `OSError` khi chạy `bench_vs_openblas.py` (Windows)
- **SEVERITY**: Medium
- **SYMPTOMS**: Chương trình C/Python không nạp được thư viện chia sẻ NVXK.
- **POSSIBLE CAUSES**: Chưa chạy `make all`, thiếu `LD_LIBRARY_PATH=.` trên Linux, hoặc `scripts/bench_vs_openblas.py` hardcode `libnvxk.so` trên Windows (`INC-08`).
- **DIAGNOSTIC STEPS**: Kiểm tra file thư viện tồn tại tại thư mục gốc (`ls libnvxk.so` hoặc `dir nvxk.dll`).
- **COMMANDS**: Linux: `LD_LIBRARY_PATH=. ./demo` | Windows Python: đổi `LIB_PATH` sang `os.path.abspath("nvxk.dll")`.
- **SOLUTION**: Chạy `make all` / `mingw32-make all`; link C trên Linux với `-Wl,-rpath,.`; trên Windows dùng `nvxk.dll` và thêm `os.add_dll_directory(r"C:\msys64\ucrt64\bin")` trong Python 3.8+.
- **VERIFICATION**: `python3 -c "import ctypes; print(ctypes.CDLL('./libnvxk.so').nvxk_has_avx2_fma())"` in `1` hoặc `0`.
- **PREVENTION**: Dùng logic chọn tên thư viện theo `sys.platform` như ví dụ trong Phần VII.

### 17. Data and Export Errors

**ERROR ID**: `ERR-DATA-001` | **Trạng thái**: `[ĐÃ TÁI HIỆN / REPRODUCED]`
- **ERROR NAME**: Import CSV trả `400 Bad Request` hoặc tệp hồ sơ `.cfg` bị từ chối khi nạp
- **SEVERITY**: Low
- **SYMPTOMS**: `/api/benchmark/import_csv` báo `Invalid filename`, hoặc `XK_load_hardware_profile` trả `false`.
- **POSSIBLE CAUSES**: Tên tệp CSV chứa ký tự đường dẫn (`..`, `/`, `\`) bị chặn bảo mật path traversal; tệp `.cfg` thiếu `nvxk_profile_version=1` hoặc chứa giá trị không hợp lệ (`<= 0`, `nan`).
- **DIAGNOSTIC STEPS**: Kiểm tra nội dung tệp `.cfg` và vị trí tệp CSV trong thư mục `results/`.
- **COMMANDS**: `head -n 20 results/hw_profile.cfg`
- **SOLUTION**: Chỉ truyền tên tệp thuần (ví dụ `v2_opt_auto_asm.csv`) nằm trong `--results-dir`; tạo lại tệp profile sạch bằng `./nvxk.exe --calibrate --profile results/hw_profile.cfg --size 64 --runs 1 --algorithm auto`.
- **VERIFICATION**: `XK_load_hardware_profile` trả `true` và Import CSV báo `imported > 0`.
- **PREVENTION**: Không chỉnh sửa thủ công tệp `.cfg` bằng trình soạn thảo làm hỏng định dạng `key=value`.