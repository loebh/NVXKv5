# Phần II: Cài đặt Môi trường từ Con số 0 (Environment Setup & Hardware Compatibility)

Hướng dẫn này dành cho cả người mới chưa từng cài môi trường lập trình C/C++, giúp thiết lập hoàn chỉnh máy tính Windows 10/11 từ đầu để biên dịch và vận hành đầy đủ **NVXK v2 Core**, **Web Workbench** và **Native Desktop App**.

## 1. Thiết lập Môi trường Windows từ Con số 0

### Bước 1.1: Kiểm tra Phiên bản Windows và Kiến trúc Hệ thống
- **Mục đích**: Đảm bảo máy chạy Windows 10 (build 1903+) hoặc Windows 11 bản **64-bit (x64)** để hỗ trợ chuỗi công cụ MSYS2 UCRT64 và tập lệnh x86-64 AVX2+FMA.
- **Thao tác**: Nhấn phím `Win + X`, chọn **Terminal** hoặc **Windows PowerShell**, sau đó chạy:

```powershell
Get-CimInstance Win32_OperatingSystem | Select-Object Caption, Version, OSArchitecture
```

- **Kết quả mong đợi**: `OSArchitecture` hiển thị `64-bit`, `Caption` là `Microsoft Windows 10` hoặc `Microsoft Windows 11`.
- **Lỗi có thể gặp & Cách sửa**: Nếu máy là 32-bit (`x86`), microkernel hợp ngữ x86-64 (`micro_6x16_full_asm_f32`) sẽ không kích hoạt; cần hệ điều hành 64-bit để khai thác hiệu năng đầy đủ của NVXK.

### Bước 1.2: Cài đặt Git for Windows
- **Công dụng & Tại sao cần**: Tải mã nguồn NVXK về máy, quản lý phiên bản và cung cấp `git` cho CMake `FetchContent` khi biên dịch ứng dụng Desktop (`nvxk_desktop.exe`).
- **Link tải chính thức**: [https://git-scm.com/download/win](https://git-scm.com/download/win)
- **Phiên bản phù hợp**: Git for Windows 64-bit phiên bản `2.40+`.
- **Các bước cài đặt**:
  1. Tải tệp `Git-x.xx.x-64-bit.exe` và chạy trình cài đặt.
  2. Giữ nguyên cấu hình mặc định ở hầu hết các bước; tại màn hình **Adjusting your PATH environment**, chọn **Git from the command line and also from 3rd-party software**.
  3. Hoàn tất cài đặt và mở lại cửa sổ PowerShell mới.
- **Lệnh xác nhận**:

```powershell
git --version
```

- **Kết quả mong đợi**: `git version 2.4x.x.windows.1`.
- **Lỗi phổ biến & Cách sửa**: Báo `'git' is not recognized` → Đóng và mở lại PowerShell; nếu vẫn lỗi, thêm `C:\Program Files\Git\cmd` vào biến môi trường `Path` của hệ thống.

### Bước 1.3: Cài đặt MSYS2 và Trình biên dịch GCC/MinGW (UCRT64)
- **Công dụng & Tại sao cần**: Cung cấp trình biên dịch `g++` (GCC hỗ trợ C++11/C++17, OpenMP `libgomp`, và thuộc tính `__attribute__((target("avx2,fma")))`), công cụ `mingw32-make` và `cmake`. **Lưu ý quan trọng**: Không nên dùng MSVC (`cl.exe`) làm trình biên dịch chính cho NVXK vì macro `XK_ENABLE_X86_AVX2_DISPATCH` trong `src/nvxk_kernels_avx2.cpp` chỉ bật phân phát động AVX2+FMA trên GCC/Clang (trên MSVC, NVXK sẽ tự động hạ về nhánh scalar chậm hơn nhiều).
- **Link tải chính thức**: [https://www.msys2.org/](https://www.msys2.org/)
- **Phiên bản phù hợp**: MSYS2 mới nhất với gói `mingw-w64-ucrt-x86_64-gcc` (GCC 12+ hoặc mới hơn).
- **Các bước cài đặt**:
  1. Tải `msys2-x86_64-YYYYMMDD.exe` từ trang chủ MSYS2 và cài vào thư mục mặc định `C:\msys64`.
  2. Mở ứng dụng **MSYS2 UCRT64** từ Start Menu (chú ý chọn đúng biểu tượng **UCRT64**, không chọn MSYS thường).
  3. Cập nhật hệ thống gói bằng lệnh:

```bash
pacman -Syu
```

  4. Nếu cửa sổ tự đóng sau khi cập nhật lõi, mở lại **MSYS2 UCRT64** và cài trọn bộ GCC, Make, CMake:

```bash
pacman -S --needed mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-make mingw-w64-ucrt-x86_64-cmake
```

- **Cấu hình biến môi trường `PATH` trên Windows** (để dùng `g++`, `mingw32-make`, `cmake` trực tiếp từ PowerShell/CMD/VS Code):
  1. Nhấn `Win`, gõ `Environment Variables` → chọn **Edit the system environment variables**.
  2. Nhấn nút **Environment Variables...**.
  3. Trong mục **User variables** (hoặc **System variables**), chọn `Path` → **Edit** → **New**.
  4. Thêm chính xác đường dẫn: `C:\msys64\ucrt64\bin` → nhấn **OK** trên tất cả cửa sổ.
- **Lệnh xác nhận** (mở cửa sổ **PowerShell mới**):

```powershell
g++ --version
mingw32-make --version
cmake --version
```

- **Kết quả mong đợi**:
  - `g++.exe (Rev..., Built by MSYS2 project) 13.x / 14.x / 16.x`
  - `GNU Make 4.4.x Built for Windows32`
  - `cmake version 3.2x.x` (yêu cầu `>= 3.16`).
- **Lỗi phổ biến & Cách sửa**:
  - `'g++' is not recognized`: Kiểm tra lại `C:\msys64\ucrt64\bin` trong `Path` và khởi động lại terminal.
  - Xung đột nhiều bản MinGW: Đẩy `C:\msys64\ucrt64\bin` lên **trên cùng** danh sách `Path` và kiểm tra bằng `where.exe g++`.

### Bước 1.4: Cài đặt Python 3 (Tùy chọn — chỉ cho Script Đối chiếu & Biểu đồ)
- **Công dụng & Tại sao cần**: Chạy `scripts/bench_memory_budget.py`, `scripts/bench_vs_openblas.py` và `scripts/generate_charts.py`. **Không bắt buộc** để biên dịch hay chạy lõi NVXK, Web Workbench hoặc Desktop App.
- **Link tải chính thức**: [https://www.python.org/downloads/windows/](https://www.python.org/downloads/windows/) (Phiên bản `3.10+`).
- **Cài đặt & Xác nhận**: Khi cài, đánh dấu chọn **Add python.exe to PATH**. Sau đó cài thư viện phụ trợ:

```powershell
python --version
python -m pip install numpy matplotlib
```

- **Kết quả mong đợi**: `Python 3.1x.x` và cài đặt thành công `numpy`, `matplotlib`.

### Bước 1.5: Kiểm tra Tập lệnh CPU, AVX2/FMA và Số Luồng
- **Mục đích**: Xác định trước phần cứng máy tính hỗ trợ những tập lệnh nào trước khi chạy NVXK.
- **Kiểm tra số nhân vật lý & luồng logic trong PowerShell**:

```powershell
Get-CimInstance Win32_Processor | Select-Object Name, NumberOfCores, NumberOfLogicalProcessors, L2CacheSize, L3CacheSize
```

- **Kiểm tra cờ AVX2 và FMA**: Trên Windows không có lệnh `lscpu` mặc định, bạn có thể kiểm tra bằng chính bộ công cụ NVXK sau khi biên dịch (`./nvxk.exe --size 64 --runs 1 --algorithm auto`) hoặc dùng tiện ích chính thức **Sysinternals Coreinfo** ([https://learn.microsoft.com/en-us/sysinternals/downloads/coreinfo](https://learn.microsoft.com/en-us/sysinternals/downloads/coreinfo)):

```powershell
coreinfo64.exe -f | Select-String -Pattern "AVX2|FMA|AVX512"
```

- **Kết quả mong đợi trên máy tham chiếu i7-7700**: `AVX2 * Supports AVX2`, `FMA * Supports FMA`, `AVX512F -` (không có dấu `*`).

## 2. Thiết lập Môi trường Phát triển (Developer Environment)

### 2.1. Phân biệt 4 loại Terminal trên Windows

| Terminal | Đặc điểm cốt lõi | Nên dùng cho việc gì trong NVXK? |
| :--- | :--- | :--- |
| **PowerShell** (khuyến nghị) | Shell mặc định Windows 10/11, hỗ trợ chạy script `.ps1`, nhận `PATH` Windows. | Chạy `mingw32-make`, `cmake`, `.\nvxk.exe`, `.\scripts\run_experiments.ps1`, `.\scripts\build_desktop_windows.ps1`. |
| **CMD (Command Prompt)** | Shell truyền thống Windows, cú pháp biến `%VAR%`, không chạy `.ps1` trực tiếp. | Chỉ dùng cho lệnh đơn giản (`mingw32-make all`, `nvxk.exe`); không hỗ trợ cú pháp Linux. |
| **Git Bash** | Mô phỏng POSIX (`bash`, `printf`, `grep`), đi kèm Git for Windows. | Chạy các câu lệnh dạng pipe POSIX như `printf "2\n2\n2\n..." \| ./my_app.exe` hoặc `git`. |
| **MSYS2 UCRT64** | Môi trường POSIX gắn liền trình quản lý gói `pacman` và toolchain UCRT64. | Cài đặt/cập nhật gói (`pacman -S ...`), biên dịch bằng `mingw32-make` hoặc `make` (nếu đã cài `make`). |

### 2.2. Quy trình Từ Clone đến Build, Run, Test, Clean, Rebuild
Mở **PowerShell** tại thư mục chứa dự án (ví dụ `C:\dev`):

```powershell
# 1. Clone repository (thay URL bằng địa chỉ kho git thực tế của nhóm bạn)
git clone <NVXK_REPO_URL> NVXK
cd NVXK

# 2. Biên dịch toàn bộ lõi, ứng dụng mẫu, C ABI DLL và Web Workbench
mingw32-make all

# 3. Chạy thử ứng dụng mẫu tương tác
.\my_app.exe

# 4. Chạy toàn bộ bộ kiểm thử lõi và Workbench
mingw32-make test

# 5. Chạy kiểm thử nhánh fallback scalar (đảm bảo di động)
mingw32-make test-scalar

# 6. Dọn sạch tệp biên dịch và build lại từ đầu
mingw32-make clean
mingw32-make all
```

### 2.3. Cấu hình IDE (VS Code) và Debugger (`gdb`)
1. Cài **Visual Studio Code** ([https://code.visualstudio.com/](https://code.visualstudio.com/)) cùng 2 extension: **C/C++** (`ms-vscode.cpptools`) và **CMake Tools** (`ms-vscode.cmake-tools`).
2. Mở thư mục gốc `NVXK` trong VS Code (`File → Open Folder`).
3. Chọn Kit là **GCC x.x.x x86_64-w64-mingw32 (ucrt64)** trong thanh trạng thái CMake Tools.
4. Cài đặt debugger `gdb` trong **MSYS2 UCRT64** nếu chưa có:

```bash
pacman -S --needed mingw-w64-ucrt-x86_64-gdb
```

5. Để debug mã nguồn có ký hiệu gỡ lỗi (`-g`), biên dịch với cờ debug:

```powershell
cmake -S . -B build_debug -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Debug
cmake --build build_debug -j
gdb .\build_debug\nvxk-test.exe
```

## 3. Tương thích Phần cứng (Hardware Compatibility)

### 3.1. Cấu hình Tham chiếu: Intel Core i7-7700 / RAM 16 GB / Intel HD Graphics 630
Toàn bộ thiết kế vi kiến trúc của NVXK v2 được định chuẩn trực tiếp theo cấu hình phần cứng phổ thông này:

| Thông số phần cứng | Giá trị trên i7-7700 (Kaby Lake) | Cách NVXK v2 thích ứng tự động |
| :--- | :--- | :--- |
| **Tập lệnh SIMD** | SSE4.2, AVX, **AVX2, FMA3** (không có AVX-512) | Runtime dispatch kích hoạt BLIS `6×16` (`f32`), ASM `6×16`, `Avx2ThinM` và `6×8` (`f64`). |
| **Số nhân / Luồng** | **4 nhân vật lý / 8 luồng logic** (Hyper-Threading/SMT) | `nvxk_hw.cpp` đọc chính xác 4C/8T qua Win32 `GetLogicalProcessorInformationEx`. Khi dùng >4 luồng, Planner tự động chia đôi ngân sách L2 (`smt_l2_shared = true`) và đo `smt_efficiency` thực tế khi hiệu chuẩn. |
| **Bộ nhớ đệm** | L1d = 32 KiB/nhân, L2 = 256 KiB/nhân, L3 = 8 MiB dùng chung | Planner tự tính `KC` (≤70% L1d), `MC` (≤55% L2 hiệu dụng), `NC` (≤25% L3) dựa trên CPUID Leaf `0x04`. |
| **RAM hệ thống** | 16 GB DDR4 | Mặc định `XK_Workspace` dùng tối đa 64 MiB/luồng; với ngân sách chặt (128 KiB–1 MiB), Planner tự co khối chống OOM. Ma trận tối đa trong Interactive Matrix Lab giới hạn ở `2048×2048` (~48 MiB `f32`) để bảo vệ UI. |
| **Đồ họa tích hợp** | Intel HD Graphics 630 (hỗ trợ OpenGL 4.5 / 3.3 Core) | **Không cần GPU rời**. Web UI chạy bằng Canvas 2D; Desktop App (`nvxk_desktop.exe`) chỉ dùng **OpenGL 3.3 Core** với VSync (`glfwSwapInterval(1)`), hoạt động mượt trên HD 630. |
| **Trình biên dịch phù hợp** | MSYS2 UCRT64 GCC (hoặc Clang) | Bắt buộc dùng GCC/Clang để kích hoạt `__attribute__((target("avx2,fma")))` và inline ASM x86-64. |

### 3.2. Tương thích với các CPU khác (Không mặc định AVX2)
NVXK biên dịch ở mức **x86-64 cơ bản** (tuyệt đối không gắn cờ toàn cục `-mavx2` hay `-mfma`), nhờ đó tệp nhị phân chạy an toàn trên mọi CPU mà không gây lỗi `SIGILL` (Illegal Instruction):

- **CPU Intel đời cũ (Sandy Bridge, Ivy Bridge, Celeron/Pentium không có AVX2/FMA)**: `XK_has_avx2_fma()` trả về `false` ở runtime → Tự động chuyển sang nhánh **Scalar Fallback** (`src/nvxk_kernels_scalar.cpp`, vòng lặp IKJ thuần C++11 có hỗ trợ đa luồng OpenMP). Kết quả số học vẫn chính xác 100%, chỉ khác về thông lượng.
- **CPU AMD Ryzen / EPYC (Zen 1–5)**: Hỗ trợ đầy đủ AVX2+FMA; `detect_x86_caches` tự đọc CPUID mở rộng `0x8000001D` cho vendor `AuthenticAMD` để lấy đúng dung lượng L1d/L2/L3.
- **CPU Intel có AVX-512 (Xeon Scalable, Core Gen 11/12+)**: NVXK phát hiện cờ `has_avx512f = true` nhưng chủ đích giữ độ rộng 256-bit AVX2+FMA (không sinh lệnh 512-bit để giữ tương thích i7-7700 và tránh hạ xung AVX-512).
- **CPU ARM64 (Apple Silicon, Raspberry Pi, ARM Server)**: Biên dịch sạch qua nhánh scalar fallback (`XK_ENABLE_X86_AVX2_DISPATCH = 0`); nhánh tối ưu ARM NEON hiện ở trạng thái `PLANNED`.