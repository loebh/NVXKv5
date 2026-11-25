# Phần III: Sổ tay Toàn bộ Lệnh CLI (CLI Command Reference)

Tài liệu này liệt kê **đầy đủ mọi lệnh thực tế** tồn tại trong kho mã nguồn NVXK (`makefile`, `CMakeLists.txt`, `benchmarks/main.cpp`, `apps/my_app.cpp`, `ui/server/main.cpp`, `scripts/`). Không sử dụng dấu ba chấm thay cho phần lệnh bắt buộc.

## 1. Nhóm Lệnh Cài đặt (Installation)

### 1.1. Cài chuỗi công cụ trên Windows MSYS2 UCRT64
- **Mục đích**: Cài đặt trình biên dịch GCC 64-bit, GNU Make, CMake và GDB.
- **Terminal & Thư mục**: `MSYS2 UCRT64` | Bất kỳ thư mục nào.
- **Cú pháp đầy đủ**:

```bash
pacman -S --needed mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-make mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-gdb
```

- **Giải thích tham số**: `-S` đồng bộ và cài gói; `--needed` bỏ qua gói đã cài phiên bản mới nhất.
- **Output dự kiến**: Danh sách gói kèm tiến trình tải và giải nén kết thúc không báo lỗi.
- **Xác nhận thành công**: Chạy `g++ --version` và `mingw32-make --version` trả về phiên bản hợp lệ.
- **Lỗi phổ biến & Cách sửa**: Lỗi khóa cơ sở dữ liệu `unable to lock database` → Đóng các tiến trình `pacman` khác; nếu vẫn kẹt, xóa `/var/lib/pacman/db.lck`.

### 1.2. Cài chuỗi công cụ trên Linux (Debian / Ubuntu)
- **Mục đích**: Cài GCC (OpenMP), GNU Make, CMake và thư viện Python phân tích.
- **Terminal & Thư mục**: `Bash` | Bất kỳ thư mục nào.
- **Cú pháp đầy đủ**:

```bash
sudo apt-get update && sudo apt-get install -y g++ make cmake libgomp1 python3 python3-numpy python3-matplotlib
```

- **Xác nhận thành công**: `g++ --version` hiển thị GCC 9+ (đã kiểm chứng trên GCC 12.2.0).

## 2. Nhóm Lệnh Cấu hình (Configuration)

### 2.1. Cấu hình CMake tiêu chuẩn (Core + Shared C ABI + Web Workbench)
- **Mục đích**: Tạo tệp build Release với thư viện tĩnh `nvxk_core`, thư viện động `nvxk` và Web Workbench `nvxk_workbench`.
- **Terminal & Thư mục**: `PowerShell` (Windows) hoặc `Bash` (Linux) | Thư mục gốc repository `NVXK`.
- **Cú pháp đầy đủ (Windows MSYS2)**:

```powershell
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release -DNVXK_BUILD_SHARED=ON -DNVXK_BUILD_WORKBENCH=ON
```

- **Cú pháp đầy đủ (Linux)**:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DNVXK_BUILD_SHARED=ON -DNVXK_BUILD_WORKBENCH=ON
```

- **Giải thích tham số**: `-S .` thư mục chứa `CMakeLists.txt`; `-B build` thư mục build đầu ra; `-G "MinGW Makefiles"` chọn generator cho MinGW; `-DCMAKE_BUILD_TYPE=Release` bật `-O3`; `-DNVXK_BUILD_SHARED=ON` tạo `nvxk.dll`/`libnvxk.so`; `-DNVXK_BUILD_WORKBENCH=ON` bật C++17 Workbench.
- **Output dự kiến**: `-- Configuring done`, `-- Generating done`, `-- Build files have been written to: .../build`.
- **Lỗi phổ biến & Cách sửa**: CMake tự chọn Visual Studio MSVC thay vì MinGW → Bắt buộc thêm `-G "MinGW Makefiles"` và đảm bảo `C:\msys64\ucrt64\bin` ở đầu `PATH`.

### 2.2. Cấu hình CMake bật Ứng dụng Desktop gốc (`nvxk_desktop.exe`)
- **Mục đích**: Cấu hình tải tự động GLFW 3.4, Dear ImGui (`docking`), ImPlot v0.16 qua `FetchContent` và bật target `nvxk_desktop`.
- **Terminal & Thư mục**: `PowerShell` | Thư mục gốc `NVXK` (cần Internet ở lần cấu hình đầu tiên).
- **Cú pháp đầy đủ**:

```powershell
cmake -S . -B build_desktop -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release -DNVXK_BUILD_WORKBENCH=ON -DNVXK_BUILD_DESKTOP_IMGUI=ON
```

- **Xác nhận thành công**: Xuất hiện thư mục `build_desktop/_deps/imgui-src`, `glfw-src`, `implot-src` và kết thúc bằng `-- Build files have been written to: .../build_desktop`.

## 3. Nhóm Lệnh Biên dịch (Build)

### 3.1. Build toàn bộ bằng GNU Make (`make all` / `mingw32-make all`)
- **Mục đích**: Biên dịch 4 sản phẩm: `nvxk.exe`, `my_app.exe`, `libnvxk.so` (hoặc `nvxk.dll`), `nvxk_workbench.exe`.
- **Terminal & Thư mục**: `PowerShell` / `MSYS2 UCRT64` (Windows) hoặc `Bash` (Linux) | Thư mục gốc `NVXK`.
- **Cú pháp đầy đủ**:

```powershell
# Windows (PowerShell / MSYS2)
mingw32-make all

# Linux / macOS
make all
```

- **Tùy chọn mở rộng (OpenBLAS)**: `make all OPENBLAS=1` (yêu cầu đã cài `cblas.h` và `libopenblas`).
- **Tùy chọn build riêng Web Workbench**: `make workbench` hoặc `mingw32-make workbench`.
- **Output dự kiến**: Các dòng lệnh `g++ ...` chạy sạch với **0 cảnh báo** `-Wall -Wextra`, exit code `0`.

### 3.2. Build bằng CMake
- **Terminal & Thư mục**: `PowerShell` / `Bash` | Thư mục gốc `NVXK`.
- **Cú pháp đầy đủ**:

```powershell
cmake --build build -j
```

- **Build Desktop App bằng script PowerShell**:

```powershell
.\scripts\build_desktop_windows.ps1
```

## 4. Nhóm Lệnh Khởi chạy (Run)

### 4.1. Chạy Ứng dụng Tương tác Mẫu (`my_app.exe`)
- **Mục đích**: Nhập kích thước `M, K, N` và phần tử hai ma trận `A`, `B`, in kết quả `A × B`.
- **Terminal & Thư mục**: `PowerShell` / `Git Bash` / `Bash` | Thư mục gốc `NVXK`.
- **Ví dụ thực tế (Linux / Git Bash)**:

```bash
printf "2\n2\n2\n1 2\n3 4\n5 6\n7 8\n" | ./my_app.exe
```

- **Output dự kiến**:
```text
Ket qua A x B (2 x 2):
19 22
43 50
```

### 4.2. Khởi chạy Web Workbench Server (`nvxk_workbench.exe`)
- **Mục đích**: Mở máy chủ HTTP/REST trên `127.0.0.1` phục vụ giao diện Web UI tại `ui/web/index.html`.
- **Terminal & Thư mục**: `PowerShell` / `Bash` | Thư mục gốc `NVXK`.
- **Cú pháp đầy đủ**:

```powershell
.\nvxk_workbench.exe --port 8765 --ui-dir ui/web --results-dir results
```

- **Tham số**:
  - `--port N`: Cổng TCP lắng nghe trên `127.0.0.1` (mặc định `8765`).
  - `--ui-dir DIR`: Thư mục chứa `index.html` (mặc định `ui/web`).
  - `--results-dir DIR`: Thư mục chứa các tệp `.csv` lịch sử để import (mặc định `results`).
  - `--no-browser`: Không tự động mở trình duyệt trên Windows (hữu ích khi chạy headless).
  - `--help`: In hướng dẫn tham số rồi thoát.
- **Output dự kiến**:
```text
 NVXK Engineering Intelligence Workbench (C++17 / NVXK v2)
 Listening on http://127.0.0.1:8765/
 Press Ctrl+C to stop.
```
- **Lỗi phổ biến & Cách sửa**: `bind() failed on 127.0.0.1:8765` → Cổng 8765 đang bị chiếm; đổi sang `--port 9000`.

### 4.3. Khởi chạy Ứng dụng Desktop gốc (`nvxk_desktop.exe`)
- **Terminal & Thư mục**: `PowerShell` | Thư mục gốc `NVXK` (sau khi build bằng CMake).
- **Cú pháp đầy đủ**:

```powershell
.\build_desktop\nvxk_desktop.exe
```

- **Xác nhận thành công**: Cửa sổ đồ họa `NVXK Engineering Intelligence Workbench` (1440×900) mở ra với thanh trên cùng, sidebar 12 mục và 5 thẻ KPI.

## 5. Nhóm Lệnh Kiểm thử (Testing)

| Lệnh đầy đủ | Mục đích | Kết quả mong đợi |
| :--- | :--- | :--- |
| `make test` (Windows: `mingw32-make test`) | Build và chạy cả `nvxk-test.exe` và `workbench-test.exe`. | In cả 2 dòng `[PASS] All NVXK ... succeeded.` và `[PASS] All NVXK Engineering Workbench ... succeeded.` |
| `make test-workbench` | Chỉ chạy kiểm thử tích hợp Workbench (`workbench-test.exe`). | Exit code `0`, in `[PASS] All NVXK Engineering Workbench C++17 integration tests succeeded.` |
| `make test-scalar` | Biên dịch lõi với `-DXK_FORCE_SCALAR_FALLBACK` và chạy `nvxk-test-scalar.exe`. | Exit code `0`, chứng minh 100% chức năng đúng khi tắt AVX2. |
| `make test-san` | Biên dịch với `-O1 -g -fsanitize=address,undefined` và chạy `nvxk-test-san.exe` (Linux GCC/Clang). | Exit code `0`, 0 lỗi ASan/UBSan. |
| `ctest --test-dir build --output-on-failure` | Chạy bộ kiểm thử đã đăng ký trong CMake (`nvxk_unit_tests`, `nvxk_workbench_tests`). | `100% tests passed, 0 tests failed out of 2`. |

## 6. Nhóm Lệnh Benchmark (Benchmark)

### 6.1. Benchmark CLI đầy đủ (`nvxk.exe`)
- **Mục đích**: Đo thời gian thống kê (min, median, mean, stddev, P95, P99), thông lượng GFLOPS, dung lượng workspace và tự động kiểm chứng sai số so với tham chiếu `f64`.
- **Terminal & Thư mục**: `PowerShell` / `Bash` | Thư mục gốc `NVXK`.
- **Ví dụ chạy đầy đủ có hiệu chuẩn, giải trình kế hoạch và xuất CSV**:

```bash
./nvxk.exe --calibrate --profile results/hw_profile.cfg --explain --sizes 128,256,512,1024 --threads 1,2,4,8 --runs 7 --algorithm all --csv results/square_warm.csv
```

- **Ví dụ đo ma trận mỏng Thin-M (`4×1024×1024`)**:

```bash
./nvxk.exe --profile results/hw_profile.cfg --explain --shape 4,1024,1024 --runs 7 --algorithm auto --csv results/thin4.csv
```

- **Ví dụ đo Cold-Cache (xóa L3 bên ngoài vùng bấm giờ)**:

```bash
./nvxk.exe --profile results/hw_profile.cfg --cold-cache --sizes 256,512,1024 --runs 5 --algorithm auto --csv results/square_cold.csv
```

- **Giải thích toàn bộ tham số `nvxk.exe`**:
  - `--size N`: Đo 1 kích thước vuông `N×N×N`.
  - `--sizes N,N,...`: Danh sách kích thước vuông (mặc định `128,256,512,1024`).
  - `--shape M,K,N`: Đo ma trận chữ nhật `A(M×K) · B(K×N)`.
  - `--runs N`: Số mẫu đo độc lập (mặc định `7`).
  - `--threads N,N,...`: Số luồng OpenMP cho thuật toán `omp` (mặc định `1,2,4,8`).
  - `--algorithm NAME`: Một trong 9 giá trị: `all`, `naive`, `ikj`, `blocked`, `avx2`, `avx2_asm`, `omp`, `auto`, `auto_asm`.
  - `--calibrate`: Chạy hiệu chuẩn phần cứng 200 ms trước khi đo.
  - `--profile FILE`: Nạp hồ sơ từ `FILE` (hoặc lưu ra `FILE` nếu đi kèm `--calibrate`).
  - `--explain`: In chuỗi giải trình `XK_PlanExplanation` của Adaptive Planner.
  - `--cold-cache`: Xóa cache L3 (quét bộ đệm `max(2×L3, 16 MiB)`) bên ngoài vùng bấm giờ.
  - `--csv FILE`: Xuất bảng số liệu thống kê đầy đủ ra tệp CSV.
- **Xác nhận thành công**: Dòng cuối in `[SUCCESS] All benchmarks passed numerical verification.` và exit code `0`.

### 6.2. Chạy trọn bộ Thực nghiệm Nghiên cứu
```bash
# Linux
./scripts/run_experiments.sh

# Windows PowerShell (máy tham chiếu i7-7700)
.\scripts\run_experiments.ps1

# Đo đường cong ngân sách bộ nhớ (1024^3 từ 64 MiB xuống 128 KiB)
python3 scripts/bench_memory_budget.py

# Đối chiếu NVXK C ABI vs OpenBLAS (Linux)
python3 scripts/bench_vs_openblas.py

# Sinh 3 biểu đồ PNG vào docs/figures/
python3 scripts/generate_charts.py
```

## 7. Nhóm Lệnh Profiling, Phát hiện CPU & Gỡ lỗi (Profiling, CPU Detection, Debugging)

### 7.1. Phát hiện CPU, Cache và Giải trình Lập lịch (CPU Detection)
```bash
./nvxk.exe --calibrate --explain --size 128 --runs 1 --algorithm auto
```
- **Output dự kiến**: In đầy đủ `CPU`, `Topology` (physical/logical), `Caches` (L1d/L2/L3), `ISA` (`AVX2+FMA`, `x86-64 ASM`), `Profile` (1T GFLOPS, Memory BW, OMP fork overhead, SMT efficiency) và dòng `[PLAN 128x128x128] ...`.

### 7.2. Kiểm tra Hợp ngữ sinh ra (Microarchitectural Profiling / Disassembly)
- **Mục đích**: Kiểm tra microkernel không có lệnh AVX2 lọt ra ngoài hàm `target("avx2,fma")` và kiểm tra register spill.
```bash
objdump -d -C nvxk.exe | grep -A 60 "micro_6x16_full_asm_f32"
```

### 7.3. Gỡ lỗi bộ nhớ & GDB (Debugging)
```bash
# Linux: AddressSanitizer + UndefinedBehaviorSanitizer
make test-san

# Windows / Linux: Chạy GDB trên bản build Debug
gdb ./build_debug/nvxk-test.exe -ex "run" -ex "bt" -ex "quit"
```

## 8. Nhóm Lệnh Clean, Rebuild, Integration & Maintenance

```bash
# Dọn sạch toàn bộ tệp thực thi & thư viện chia sẻ (Make)
make clean
mingw32-make clean

# Dọn sạch thư mục build CMake và build lại từ đầu
rm -rf build && cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j

# Liên kết tích hợp chương trình C99 với C ABI (Linux)
gcc -std=c99 -O2 -Iinclude demo.c -L. -lnvxk -Wl,-rpath,. -o demo

# Liên kết tích hợp chương trình C99 với C ABI (Windows MSYS2 UCRT64)
gcc -std=c99 -O2 -Iinclude demo.c -L. -lnvxk -o demo.exe

# Bảo trì: Kiểm tra trạng thái sạch kho mã nguồn Git
git status
```