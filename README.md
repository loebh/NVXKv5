# NVXK Unified Computing Engine (v5.0.0)

**NVXK** (*Native Vector eXecution Kernels*) là bộ máy tính toán số học trên CPU viết bằng **C++17**, được thiết kế theo kiến trúc phân tầng rõ ràng. **NVXK Core** đảm nhiệm toàn bộ API công khai, lập kế hoạch thực thi (Adaptive Planner), quản lý bộ nhớ tạm (`XK_Workspace`), điều phối backend, kiểm định độ chính xác số học và chẩn đoán phần cứng — đồng thời tích hợp có chọn lọc 3 thư viện tùy chọn:

- **Google Highway (`1.4.0`)** — Hệ thống SIMD đa kiến trúc tự động chọn tập lệnh lúc chạy (`AVX3_SPR`, `AVX3`, `AVX2`, `SSE4`, `NEON`, `SVE`, `Scalar`) cho các phép toán vector, rút gọn (`dot`, `sum`, `max`), hàm kích hoạt AI (`relu`, `exp`, `sigmoid`, `gelu`, `softmax`, `layernorm`) và `gemv` (`include/nvxk/simd.hpp`).
- **BLIS (`0.8.1-195`)** — Thư viện đại số tuyến tính tùy chọn (`bli_sgemm`, `bli_dgemm`, `bli_syrk`, `bli_trsm`) hỗ trợ stride tổng quát zero-copy cho ma trận lớn (`include/nvxk/blas_backend.hpp`).
- **hwloc (`3.0.0a1-git`)** — Thư viện dò topology phần cứng tùy chọn (socket, nhân vật lý, luồng logic, cache L1/L2/L3, NUMA và ghim luồng vào nhân CPU) trong `include/nvxk/topology.hpp`; tự động chuyển sang bộ dò CPUID/OS gốc nếu không cài `libhwloc`.

Toàn bộ giao diện lập trình **C++ (`include/nvxk.h`)** và **C ABI (`include/nvxk_c.h`)** từ bản v3.0.0, các kernel AVX2+FMA `6×16` / `Thin-M`, Web Workbench và Desktop ImGui UI đều giữ nguyên khả năng tương thích ngược 100%.

---

## Mục lục
1. [Kiến thức nền cho người mới (Từ con số 0)](#1-kiến-thức-nền-cho-người-mới-từ-con-số-0)
2. [Kiến trúc phân tầng & Chính sách điều phối](#2-kiến-thức-kiến-trúc-phân-tầng--chính-sách-điều-phối)
3. [Cài đặt môi trường & Biên dịch (Copy-Paste)](#3-cài-đặt-môi-trường--biên-dịch-copy-paste)
4. [Hướng dẫn sử dụng CLI (`nvxk_cli` và `nvxk`)](#4-hướng-dẫn-sử-dụng-cli-nvxk_cli-và-nvxk)
5. [Mở giao diện Web UI & Desktop UI](#5-mở-giao-diện-web-ui--desktop-ui)
6. [Tích hợp vào dự án C / C++ khác](#6-tích-hợp-vào-dự-án-c--c-khác)
7. [Phát triển thêm Backend mới](#7-phát-triển-thêm-backend-mới)
8. [Tài liệu chi tiết, Kiểm định & Trạng thái tính năng](#8-tài-liệu-chi-tiết-kiểm-định--trạng-thái-tính-năng)
$dist = "NVXK_final"
Remove-Item $dist, "$dist.zip" -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path "$dist\ui\web", "$dist\results" | Out-Null

# 1. Copy các file chạy, DLL thư viện, giao diện Web và README
Copy-Item nvxk.exe, nvxk_cli.exe, nvxk_workbench.exe, my_app.exe, nvxk.dll, README.md -Destination $dist -ErrorAction SilentlyContinue
Copy-Item ui\web\index.html -Destination "$dist\ui\web\"
Copy-Item results\*.csv -Destination "$dist\results\" -ErrorAction SilentlyContinue

# 2. Copy kèm DLL runtime của MSYS2 UCRT64 để chạy được trên máy không cài MSYS2
$msysBin = "C:\msys64\ucrt64\bin"
@("libgomp-1.dll", "libstdc++-6.dll", "libgcc_s_seh-1.dll", "libwinpthread-1.dll") | ForEach-Object {
    $dll = Join-Path $msysBin $_
    if (Test-Path $dll) { Copy-Item $dll -Destination $dist }
}

# 3. Nén thành 1 file NVXK_final.zip
Compress-Archive -Path "$dist\*" -DestinationPath "$dist.zip" -Force
Write-Host "Da dong goi xong: $dist.zip"
---

## 1. Kiến thức nền cho người mới (Từ con số 0)

Nếu bạn chưa từng biên dịch dự án C++ trước đây, chỉ cần nắm 4 khái niệm ngắn gọn sau:

- **Compiler (Trình biên dịch — `g++` / `clang++` / `MSVC`)**: Công cụ dịch mã nguồn `.cpp` (con người đọc) thành file chạy `.exe` (Windows) hoặc file thực thi (Linux/macOS).
- **Build System (`CMake` hoặc `mingw32-make` / `make`)**: Thay vì phải gõ tay từng lệnh `g++` dài hàng trăm chữ, file `CMakeLists.txt` và `makefile` đã viết sẵn toàn bộ công thức biên dịch để bạn chạy chỉ bằng 1–2 dòng lệnh.
- **SIMD (`AVX2`, `FMA`, `AVX-512`)**: Cơ chế giúp 1 nhân CPU xử lý cùng lúc 8 hoặc 16 số thực (`float`) trong một nhịp lệnh thay vì tính từng số một. NVXK tự kiểm tra CPU lúc chạy để chọn đường chạy nhanh nhất mà không làm treo máy đời cũ.
- **Thư mục `benchmark/` và `benchmarks/` khác nhau thế nào?**
  - `benchmark/` (số ít): Thư viện lõi của Workbench UI (chạy đo đạc, phân tích kết quả, xuất báo cáo).
  - `benchmarks/` (số nhiều): Chương trình CLI đo GEMM độc lập (`nvxk.exe`). Không xóa nhầm một trong hai thư mục này vì cả hai đều được biên dịch độc lập.

---

## 2. Kiến trúc phân tầng & Chính sách điều phối

```text
Layer 5 — Giao diện người dùng  : nvxk_cli | nvxk | nvxk_workbench (Web UI) | nvxk_desktop (ImGui)
Layer 4 — Kiểm định & Đo lường  : nvxk-test | nvxk-unified-test | workbench-test | Benchmark Engine
Layer 1 — NVXK Core             : Public C/C++ API | RuntimeConfig | Planner | Dispatcher | Workspace
Layer 2 — Trí tuệ phần cứng     : nvxk::topology (hwloc 3.0.0a1 -> fallback CPUID/OS gốc)
Layer 3 — Backend tính toán     : NVXK Native AVX2/ASM | Google Highway SIMD | BLIS Matrix | Scalar Ref
```

- **Đồ thị phụ thuộc 1 chiều (Strict DAG)**: Không phụ thuộc vòng tròn; không để lọt bất kỳ header bên thứ ba nào (`hwy/*`, `blis/*`, `hwloc.h`) ra thư mục `include/`.
- **Chính sách điều phối có thể giải thích (`src/core/nvxk_dispatcher.cpp`)**:
  - Ma trận rất nhỏ (`2*M*N*K < 8192` FLOPs) → `Scalar` (tránh tốn chi phí chia khối và tạo luồng OpenMP).
  - Ma trận mỏng `Thin-M` kiểu `f32` (`M <= 4, N >= 16, K >= 8`) → `NVXK Native Thin-M` (streaming trực tiếp, `0 KiB` workspace).
  - Ma trận lớn (`2*M*N*K >= 2e7`, khi có `libblis`) → `BLIS` (số luồng giới hạn theo topology thực tế).
  - Ma trận tổng quát trên CPU có AVX2+FMA → `NVXK Native 6×16` (`f32`) / `6×8` (`f64`).
  - Phép toán vector (`n >= 16`, khi bật Highway) → `Google Highway` (`HWY_DYNAMIC_DISPATCH`).
  - Máy không có AVX2/FMA hoặc thiếu thư viện ngoài → Tự động hạ cấp an toàn về `Scalar` / `NVXK Native`.

---

## 3. Cài đặt môi trường & Biên dịch (Copy-Paste)

### A. Trên Windows 10/11 (MSYS2 UCRT64 + PowerShell)

1. **Cài MSYS2**: Tải và cài đặt tại `https://www.msys2.org/` (mặc định vào `C:\msys64`).
2. **Mở terminal `MSYS2 UCRT64`** (từ Start Menu) và chạy lệnh cài bộ công cụ GCC, Make, CMake:
   ```bash
   pacman -Syu --noconfirm
   pacman -S --needed --noconfirm mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-make mingw-w64-ucrt-x86_64-cmake
   ```
3. **Mở PowerShell ngay tại thư mục gốc dự án NVXK**, nạp `PATH` và biên dịch + chạy kiểm thử:
   ```powershell
   $env:PATH = "C:\msys64\ucrt64\bin;C:\msys64\usr\bin;$env:PATH"

   # Dọn bản build cũ và biên dịch toàn bộ (CLI, DLL, Web UI, Test)
   mingw32-make clean
   mingw32-make -j4 all

   # Chạy đủ 3 bộ test + kiểm định chéo CLI
   mingw32-make test
   .\nvxk_cli.exe --verify
   ```

> **Mẹo**: Nếu muốn dùng **CMake** thay cho `mingw32-make`:
> ```powershell
> $env:PATH = "C:\msys64\ucrt64\bin;C:\msys64\usr\bin;$env:PATH"
> cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
> cmake --build build -j
> ctest --test-dir build --output-on-failure
> ```

### B. Trên Linux (Ubuntu / Debian / WSL)

1. **Cài gói biên dịch cơ bản**:
   ```bash
   sudo apt-get update
   sudo apt-get install -y build-essential cmake libomp-dev
   ```
   *(Tùy chọn: cài thêm `libhwloc-dev` và `libblis-dev` nếu muốn bật sẵn hwloc/BLIS hệ thống).*
2. **Biên dịch và chạy kiểm thử**:
   ```bash
   # Cách 1: Dùng Makefile (nhanh, mặc định bật Google Highway)
   make clean && make -j$(nproc) all
   make test
   ./nvxk_cli --verify

   # Cách 2: Chạy kiểm tra bộ nhớ với ASan + UBSan
   make test-san

   # Cách 3: Dùng CMake
   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DNVXK_ENABLE_HIGHWAY=ON -DNVXK_ENABLE_HWLOC=ON -DNVXK_ENABLE_BLIS=OFF
   cmake --build build --parallel
   ctest --test-dir build --output-on-failure
   ```

### C. Các cờ tùy chỉnh khi biên dịch bằng `make` / `mingw32-make`

| Lệnh | Ý nghĩa |
| :--- | :--- |
| `make all ENABLE_HIGHWAY=1 ENABLE_HWLOC=1 ENABLE_BLIS=1` | Bật đủ cả Google Highway + `libhwloc` + `libblis` |
| `make all ENABLE_HIGHWAY=0 ENABLE_HWLOC=0 ENABLE_BLIS=0` | Build thuần C++17 không phụ thuộc bất kỳ thư viện ngoài nào |
| `make all NO_OMP=1` | Tắt OpenMP (chạy đơn luồng hoàn toàn) |
| `make clean` | Xóa toàn bộ file thực thi và thư viện đã biên dịch |

---

## 4. Hướng dẫn sử dụng CLI (`nvxk_cli` và `nvxk`)

### A. `nvxk_cli` — CLI hợp nhất v5.0 (Kiểm tra phần cứng, SIMD, Verify & Benchmark)

```powershell
# 1. Xem thông tin CPU, tập lệnh ISA, topology cache và các backend đang bật
.\nvxk_cli.exe --info --topology --simd --backends

# 2. Kiểm định độ chính xác số học (GEMM, Dot, GELU, Softmax, LayerNorm vs Scalar) + in log điều phối
.\nvxk_cli.exe --verify --trace

# 3. Chạy bộ benchmark hợp nhất và xuất ra CSV / JSON
.\nvxk_cli.exe --benchmark --csv=results\unified_bench.csv --json=results\unified_bench.json

# 4. Ép chạy backend và số luồng cụ thể
.\nvxk_cli.exe --verify --backend=native --vec-backend=highway --threads=4 --no-pin --trace
```

*(Trên Linux/macOS, thay `.\nvxk_cli.exe` bằng `./nvxk_cli` và dấu `\` thành `/`.)*

**Hoặc điều khiển nhanh bằng biến môi trường:**
- PowerShell: `$env:NVXK_BACKEND="native"; $env:NVXK_VEC_BACKEND="highway"; $env:NVXK_THREADS="4"; $env:NVXK_TRACE="1"; .\nvxk_cli.exe --verify`
- Linux/Bash: `NVXK_BACKEND=native NVXK_VEC_BACKEND=highway NVXK_THREADS=4 NVXK_TRACE=1 ./nvxk_cli --verify`

### B. `nvxk` — CLI đo hiệu năng GEMM chuyên sâu (v2/v3)

```powershell
# Hiệu chuẩn phần cứng, đo nhiều kích thước và nhiều mức luồng, giải thích lý do Planner chọn kernel
.\nvxk.exe --calibrate --profile results\hw.cfg --sizes 128,256,512 --threads 1,4 --algorithm auto_asm --explain --csv results\gemm.csv

# Đo ma trận chữ nhật Thin-M (4x1024x1024) ở chế độ Cold-Cache (xả L3 trước mỗi lần đo)
.\nvxk.exe --shape 4,1024,1024 --threads 1,2,4 --algorithm auto --cold-cache --runs 7
```

---

## 5. Mở giao diện Web UI & Desktop UI

### A. Web Workbench UI (Chạy offline qua trình duyệt — Khuyên dùng)

1. Chạy lệnh sau trong PowerShell (hoặc Terminal Linux):
   ```powershell
   .\nvxk_workbench.exe
   ```
2. Trình duyệt sẽ tự mở tại **`http://127.0.0.1:8765/`** (nếu không tự mở, hãy tự gõ địa chỉ này vào trình duyệt).
3. Các tùy chọn thêm:
   - Đổi cổng nếu cổng `8765` đang bận: `.\nvxk_workbench.exe --port 8766`
   - Chạy server không tự mở trình duyệt: `.\nvxk_workbench.exe --no-browser`
   - Nhấn **`Ctrl + C`** trong terminal để tắt server khi dùng xong.
4. Trên giao diện Web UI, bấm nút **English / Tiếng Việt** (hoặc phím tắt **`Alt + L`**) ở thanh trên cùng để chuyển đổi ngôn ngữ tức thì. Xem thêm `docs/web/WEB_UI_GUIDE.md`.

### B. Native Desktop UI (Dear ImGui + ImPlot + GLFW + OpenGL 3.3)

Trên Windows (MSYS2 UCRT64), chạy script dựng sẵn (lần đầu cần kết nối mạng để CMake tải `glfw`, `imgui`, `implot`):

```powershell
$env:PATH = "C:\msys64\ucrt64\bin;C:\msys64\usr\bin;$env:PATH"
.\scripts\build_desktop_windows.ps1
.\build_desktop\nvxk_desktop.exe
```

Xem chi tiết tại `docs/desktop/DESKTOP_UI_GUIDE.md`.

---

## 6. Tích hợp vào dự án C / C++ khác

### C++17 (Dùng `include/nvxk/unified.hpp`)

```cpp
#include <nvxk/unified.hpp>
#include <vector>

int main() {
    nvxk::apply_env_overrides();
    const auto& topo = nvxk::topology::cached_topology();
    const auto simd  = nvxk::simd::query_info();

    // Tích vô hướng (dot product) tự động chọn SIMD tốt nhất qua Google Highway
    std::vector<float> x(4096, 1.0f), y(4096, 2.0f);
    float d = nvxk::simd::dot(x.data(), y.data(), x.size());

    // Nhân ma trận C = A * B qua bộ điều phối thống nhất NVXK
    const size_t M = 256, K = 256, N = 256;
    std::vector<float> A(M * K, 0.5f), B(K * N, 0.25f), C(M * N, 0.0f);
    auto decision = nvxk::dispatch::gemm(A.data(), B.data(), C.data(), M, K, N);

    (void)topo;
    (void)simd;
    return (d > 0.0f && decision.threads >= 1) ? 0 : 1;
}
```

### C / FFI (Python, Rust, C#, v.v.)
Include `include/nvxk_c.h` và liên kết với `nvxk.dll` (Windows) hoặc `libnvxk.so` (Linux) — không làm rò rỉ bất kỳ symbol nào của thư viện bên thứ ba.

---

## 7. Phát triển thêm Backend mới

1. Thêm phần tử mới vào `nvxk::BackendKind` trong `include/nvxk/config.hpp`.
2. Tạo thư mục cài đặt tách biệt trong `src/backends/<tên_backend>/` (tuyệt đối không `#include` thư viện bên thứ ba trong `include/`).
3. Đăng ký luật điều phối và đường fallback an toàn trong `src/core/nvxk_dispatcher.cpp`.
4. Bổ sung test đối chiếu sai số với `BackendKind::Scalar` trong `tests/unified_integration_test.cpp`.

---

## 8. Tài liệu chi tiết, Kiểm định & Trạng thái tính năng

### Hệ thống tài liệu trong `docs/`
- **Bắt đầu & Cài đặt**: `docs/INSTALLATION.md`, `docs/installation/INSTALLATION_GUIDE.md`, `docs/BUILD_GUIDE.md`, `docs/BUILD_MATRIX.md`, `docs/user-manual/COMPLETE_USER_MANUAL.md`
- **Kiến trúc & API**: `docs/ARCHITECTURE.md`, `docs/api/API_REFERENCE.md`, `docs/cli/CLI_COMMAND_REFERENCE.md`, `docs/INTEGRATION.md`
- **Giao diện & Kiểm thử**: `docs/web/WEB_UI_GUIDE.md`, `docs/desktop/DESKTOP_UI_GUIDE.md`, `docs/testing/TESTING_AND_BENCHMARK_GUIDE.md`, `docs/troubleshooting/TROUBLESHOOTING_MASTER_DB.md`
- **Báo cáo & Kiểm toán**: `docs/PERFORMANCE_REPORT.md`, `docs/QA_REPORT.md`, `docs/NVXK_KHKT_Research_Paper.md`, `docs/audit/`
- **Giấy phép & SBOM**: `LICENSE`, `THIRD_PARTY_NOTICES.md`, `licenses/`, `sbom.spdx.json`

### Phân biệt rõ trạng thái tính năng (Trung thực kỹ thuật)
- **Đã triển khai & kiểm chứng trên Linux GCC 12.2.0 (`nvxk-test`, `nvxk-unified-test`, `workbench-test`, `nvxk_cli --verify`)**: Core v3 BLAS GEMM (`Row/ColMajor`, `NoTrans/Trans`, `alpha/beta`), Highway v5 SIMD, Adaptive Planner có giới hạn `memory_budget_bytes`, `calibrate_blis_crossover()`, Web Workbench (`127.0.0.1:8765`), và các bản vá an toàn bộ nhớ/API.
- **Đang chờ kiểm chứng trực tiếp trên phần cứng đích / cấu hình mở rộng**:
  - Chạy thực nghiệm đầy đủ trên Windows MSYS2 UCRT64 (Intel Core i7-7700) với `NVXK_ENABLE_HWLOC=1`, `NVXK_ENABLE_BLIS=1`, `nvxk.dll` và `nvxk_desktop.exe`.
  - Cấp phát bộ nhớ đa-NUMA (`hwloc_alloc_membind` đã được cài đặt trong `nvxk_topology.cpp` nhưng hiện mới kiểm chứng trên hệ thống 1 NUMA node).
  - Tự động hiệu chỉnh ngưỡng chuyển giao `BLIS` theo từng phần cứng trong `calibrate_blis_crossover()` cần chạy đo thực tế khi liên kết `libblis`.
