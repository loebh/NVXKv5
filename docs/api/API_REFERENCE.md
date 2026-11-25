# Phần VI & VII: Kiến trúc Backend, Hệ thống Tích hợp & Tra cứu API (API & Code Reference)

## 1. Kiến trúc Thực tế và Luồng Dữ liệu (Backend & System Integration)

Theo kiểm toán mã nguồn thực tế, NVXK vận hành theo hai luồng kiến trúc song song cùng chia sẻ một lõi duy nhất:
- **Luồng 1 — Desktop App gốc (`nvxk_desktop.exe`)**: `User → Dear ImGui / ImPlot UI (ui/imgui/) → WorkbenchEngine (C++17) → Core Adapter & Diagnostics → NVXK v2 Core (C++11) → CPU (AVX2+FMA / Scalar)`.
- **Luồng 2 — Web Workbench (`nvxk_workbench.exe`)**: `User → Browser (ui/web/index.html) → HTTP/1.1 REST JSON (127.0.0.1:8765) → WorkbenchHttpServer (ui/server/) → WorkbenchEngine (C++17) → Core Adapter & Diagnostics → NVXK v2 Core (C++11) → CPU`.

### Giải thích khái niệm nền tảng cho người mới
- **Frontend**: Giao diện người dùng (`ui/web/index.html` trên trình duyệt hoặc `ui/imgui/` trên cửa sổ Desktop), chỉ hiển thị dữ liệu và nhận thao tác, **không bao giờ tính toán ma trận trực tiếp**.
- **Backend**: Tiến trình C++17 (`nvxk_workbench.exe`) chạy `WorkbenchEngine` trên luồng nền (`std::thread`) và giao tiếp qua `WorkbenchHttpServer` (POSIX sockets / WinSock2).
- **API, HTTP, JSON, Request/Response**: Giao diện Web gửi yêu cầu HTTP (`GET`/`POST`) chứa dữ liệu cấu trúc dạng **JSON** (ví dụ `{"M":256,"K":256,"N":256}`) tới máy chủ cục bộ `127.0.0.1` cổng `8765`; máy chủ thực thi lõi NVXK rồi trả về phản hồi JSON kèm mã trạng thái HTTP (`200 OK`, `400 Bad Request`, `404 Not Found`, `409 Conflict`).
- **WebSocket & Authentication**: **Không sử dụng**. Giao diện Web dùng cơ chế HTTP Polling định kỳ (`/api/telemetry`, mặc định 2.5 giây) và giới hạn nghiêm ngặt chỉ lắng nghe địa chỉ nội bộ `127.0.0.1` (`INADDR_LOOPBACK`) để bảo mật mà không cần đăng nhập.

### Kiểm tra từng lớp khi xảy ra sự cố
1. **Lớp CPU / Lõi NVXK**: Chạy `./nvxk-test.exe` để xác minh tính toán ma trận độc lập với UI.
2. **Lớp Workbench Engine / REST Handler**: Chạy `./workbench-test.exe` (gọi trực tiếp `handle_api_request` không qua socket).
3. **Lớp HTTP Server**: Gọi `curl -i http://127.0.0.1:8765/api/state` để kiểm tra cổng và JSON trả về.
4. **Lớp Giao diện Web**: Mở DevTools (`F12` → Network & Console) để xem request/response thực tế.

### Danh mục 12 Endpoint HTTP REST (`ui/server/http_server.cpp`)

| Method | Đường dẫn | Chức năng | Mã trả về chính |
| :--- | :--- | :--- | :--- |
| `GET` | `/` hoặc `/index.html` | Phục vụ tệp giao diện `ui/web/index.html` (`Cache-Control: no-store`). | `200`, `404` |
| `GET` | `/api/state` | Trả toàn bộ trạng thái: `diagnostics`, `status`, `backends`, `history`, `insights`. | `200` |
| `GET` | `/api/telemetry` | Lấy mẫu CPU/RSS hệ điều hành (tự tạm dừng nếu đang ở Measurement Mode). | `200` |
| `POST` | `/api/calibrate` | Chạy `XK_calibrate_hardware(budget_ms)` (mặc định `200.0`). | `200`, `409` |
| `POST` | `/api/plan` | Gọi `inspect_gemm_plan(...)` trả kế hoạch `XK_ExecutionPlan` mà không chạy kernel. | `200`, `400` |
| `POST` | `/api/benchmark/start` | Khởi chạy benchmark bất đồng bộ trên `std::thread`. | `200`, `409` |
| `POST` | `/api/benchmark/cancel` | Đặt cờ hủy nguyên tử dừng phiên benchmark đang chạy. | `200` |
| `POST` | `/api/benchmark/clear` | Xóa toàn bộ lịch sử benchmark trong RAM phiên hiện tại. | `200` |
| `POST` | `/api/benchmark/import_csv` | Nạp tệp CSV lịch sử từ `--results-dir` (chặn path traversal `..`, `/`, `\`). | `200`, `400` |
| `POST` | `/api/matrix_lab/run` | Chạy đồng bộ 1 phép toán `gemm`/`vector_add` (tối đa `2048`) kèm kiểm chứng `f64`. | `200`, `400` |
| `POST` | `/api/reliability/run` | Chạy bộ kiểm chứng 7 hình dạng biên và trả về error heatmap. | `200`, `409` |
| `GET` | `/api/report/markdown` & `/api/report/csv` | Xuất báo cáo Markdown (`text/markdown`) hoặc CSV (`text/csv`). | `200` |

## 2. Tra cứu API Công khai C++11 (`include/nvxk.h`)

Toàn bộ ma trận đầu vào/đầu ra dùng bố cục **row-major** liên tục (`A[i*K + k]`, `B[k*N + j]`, `C[i*N + j]`).

### 2.1. Phân loại Độ ổn định API
- **Ổn định (STABLE)**: `XK_gemm`, `XK_gemm_ex`, `XK_gemm_f64_ex`, `XK_gemm_i32_i64`, `XK_gemm_naive`, `XK_gemm_ikj`, `XK_gemm_blocked`, `XK_gemm_avx2`, `XK_gemm_omp`, `vector_add_avx2`, `XK_Workspace`, `XK_detect_hardware`, `XK_calibrate_hardware`, `XK_plan_gemm`, toàn bộ C ABI trong `include/nvxk_c.h`.
- **Thử nghiệm / Đặc thù kiến trúc (EXPERIMENTAL)**: `XK_gemm_avx2_asm`, trường `XK_WorkloadDesc::prefer_asm_kernel` (chỉ có trên x86-64 GCC/Clang, tự động fallback sang intrinsics/scalar trên nền tảng khác; tuy được ghi chú là experimental trong header nhưng đã kiểm chứng thực nghiệm và đặt mặc định `true` trong v2.0.0).

### 2.2. Các Kiểu dữ liệu & Cấu trúc Chính
- `enum class XK_ExecMode { Auto = 0, Manual = 1 };`
- `enum class XK_ParallelAxis { Auto = 0, None = 1, AxisM = 2, AxisN = 3 };`
- `enum class XK_KernelKind { ScalarIkj = 0, Avx2Blis6x16 = 1, Avx2ThinM = 2, Avx2Blis6x16Asm = 3 };`
- `struct XK_HardwareInfo`: `vendor`, `brand`, `physical_cores`, `logical_threads`, `l1d_bytes`, `l2_bytes`, `l3_bytes`, `has_avx2`, `has_fma`, `has_avx512f`, `has_neon`.
- `struct XK_HardwareProfile`: `hw`, `calibrated`, `peak_gflops_1t`, `mem_bandwidth_gbs`, `omp_fork_overhead_us`, `omp_barrier_overhead_us`, `smt_efficiency`.
- `struct XK_WorkloadDesc(M, K, N, elem_bytes = sizeof(float))`: `memory_budget_bytes` (`0` = kế thừa từ workspace), `mode`, `manual_threads`, `manual_mc/kc/nc`, `manual_axis`, `prefer_asm_kernel`.
- `struct XK_ExecutionPlan`: `kernel`, `axis`, `threads`, `mc`, `kc`, `nc`, `required_workspace_bytes`, `explanation` (`XK_PlanExplanation`).
- `class XK_Workspace(size_t memory_budget_bytes = 64 * 1024 * 1024)`: Bộ đệm tạm căn lề 64-byte (không copyable), tái sử dụng cho cả `f32` và `f64`, tránh false sharing giữa các luồng.

### 2.3. Chi tiết Hàm Cốt lõi

#### `XK_gemm_ex` / `XK_gemm_f64_ex` *(STABLE — Khuyến nghị cho ứng dụng chuyên sâu)*
- **Cú pháp**:
```cpp
XK_ExecutionPlan XK_gemm_ex(const float* A, const float* B, float* C,
                            const XK_WorkloadDesc& workload,
                            XK_Workspace* workspace = nullptr,
                            const XK_HardwareProfile* profile = nullptr,
                            int* actual_thread_count = nullptr);
```
- **Input / Output**: Ma trận `A(M×K)`, `B(K×N)`, đầu ra `C(M×N)` (không chồng lấn vùng nhớ với `A`/`B`), mô tả `workload`, bộ đệm `workspace` (truyền `nullptr` để dùng `thread_local` 64 MiB), hồ sơ `profile` (`nullptr` = dùng hồ sơ toàn cục). Trả về `XK_ExecutionPlan` kèm chuỗi giải trình.
- **Điều kiện tiền đề & Lỗi**: `A, B, C` không được `nullptr` khi kích thước dương (`std::invalid_argument`); `C` không được trùng vùng nhớ với `A`/`B` (`std::invalid_argument`); tích kích thước không tràn `size_t` (`std::length_error`); hết RAM (`std::bad_alloc`).
- **Hiệu năng & Khi nào dùng**: Dùng khi cần tái sử dụng `XK_Workspace`, áp đặt ngân sách RAM chặt (128 KiB–4 MiB), cố định số luồng/khối (Manual Mode), hoặc lấy chuỗi giải trình kế hoạch. **Không nên dùng** cùng một con trỏ `XK_Workspace` đồng thời từ nhiều luồng ứng dụng ngoài (mỗi luồng ngoài cần `XK_Workspace` riêng hoặc truyền `nullptr`).

#### `XK_gemm` *(STABLE — Điểm vào nhanh mặc định)*
- **Cú pháp**: `void XK_gemm(const float* A, const float* B, float* C, size_t M, size_t K, size_t N);` (và bản `double`, cùng bản template `XK_gemm<T>` cho số thực và số nguyên có dấu kích thước `<= sizeof(int)`).
- **Khi nào dùng**: Mọi phép nhân `C = A × B` thông thường theo chế độ `Auto` mặc định.

#### `XK_gemm_i32_i64` *(STABLE)*
- **Cú pháp**: `void XK_gemm_i32_i64(const int32_t* A, const int32_t* B, int64_t* C, size_t M, size_t K, size_t N);`
- **Đặc điểm**: Tính chính xác tuyệt đối bằng tích lũy `int64_t` có kiểm tra tràn (`xk_detail::add_checked`); ném `std::overflow_error` nếu vượt giới hạn `int64_t`. Chạy trên nhánh scalar.

#### `XK_calibrate_hardware`, `XK_save_hardware_profile`, `XK_load_hardware_profile`, `XK_set_active_profile` *(STABLE)*
- **Chức năng**: Đo thực tế `peak_gflops_1t`, `mem_bandwidth_gbs`, `omp_fork_overhead_us`, `omp_barrier_overhead_us` và `smt_efficiency` trong thời gian giới hạn (mặc định `200 ms`), lưu/nạp dạng `key=value` (`nvxk_profile_version=1`) và kích hoạt an toàn đa luồng qua `std::mutex`.

#### `vector_add_avx2` *(STABLE)*
- **Cú pháp**: `void vector_add_avx2(const float* a, const float* b, float* c, size_t n);` (và bản `double`).
- **Đặc điểm**: Cộng mảng `c[i] = a[i] + b[i]` trải vòng 4× AVX2 (32 float hoặc 16 double mỗi vòng lặp), tự fallback scalar nếu CPU không hỗ trợ AVX2.

### 2.4. Ví dụ Code C++11 Hoàn chỉnh (Hiệu chuẩn + Ngân sách Bộ nhớ + Giải trình)
```cpp
#include <nvxk.h>
#include <iostream>
#include <vector>

int main() {
    XK_HardwareProfile prof;
    if (!XK_load_hardware_profile("hw_profile.cfg", prof)) {
        prof = XK_calibrate_hardware(200.0);
        XK_save_hardware_profile(prof, "hw_profile.cfg");
    }
    XK_set_active_profile(prof);

    const size_t M = 512, K = 256, N = 512;
    std::vector<float> A(M * K, 0.5f), B(K * N, 0.25f), C(M * N, 0.0f);

    XK_Workspace ws(512 * 1024); // Giới hạn bộ nhớ tạm 512 KiB
    XK_WorkloadDesc wd(M, K, N, sizeof(float));
    wd.mode = XK_ExecMode::Auto;

    int threads_used = 0;
    XK_ExecutionPlan plan = XK_gemm_ex(A.data(), B.data(), C.data(), wd, &ws, nullptr, &threads_used);
    std::cout << "Plan: " << plan.explanation.reason << "\n";
    std::cout << "Threads: " << threads_used << " | Workspace: " << ws.allocated_bytes() << " bytes\n";
    return 0;
}
```

## 3. Giao diện C ABI Ổn định (`include/nvxk_c.h`) & Tích hợp Đa ngôn ngữ

Mọi hàm C ABI bọc `try/catch`, tuyệt đối không để ngoại lệ C++ thoát ra ngoài, trả mã lỗi nguyên (`NVXK_OK = 0`, `NVXK_ERR_INVALID_ARG = -1`, `NVXK_ERR_LENGTH = -2`, `NVXK_ERR_OOM = -3`, `NVXK_ERR_OVERFLOW = -4`, `NVXK_ERR_INTERNAL = -5`) và lưu thông báo lỗi theo luồng đọc qua `nvxk_last_error()`.

| Hàm C ABI | Mô tả |
| :--- | :--- |
| `int nvxk_has_avx2_fma(void)` | Trả `1` nếu hỗ trợ AVX2+FMA ở runtime, `0` nếu chạy scalar. |
| `int nvxk_calibrate(double budget_ms)` | Hiệu chuẩn phần cứng và cập nhật hồ sơ toàn cục. |
| `int nvxk_sgemm(const float* A, const float* B, float* C, size_t M, size_t K, size_t N, int* out_threads_used)` | SGEMM Auto Mode. |
| `int nvxk_sgemm_manual(..., int threads, int* out_threads_used)` | SGEMM cố định số luồng. |
| `int nvxk_dgemm(const double* A, const double* B, double* C, size_t M, size_t K, size_t N, int* out_threads_used)` | DGEMM Auto Mode. |
| `int nvxk_gemm_i32_i64(const int32_t* A, const int32_t* B, int64_t* C, size_t M, size_t K, size_t N)` | GEMM số nguyên kiểm tra tràn `int64_t`. |
| `const char* nvxk_last_error(void)` | Chuỗi lỗi gần nhất trên luồng gọi hiện tại. |

### Ví dụ Tích hợp Python (`ctypes` + `NumPy`, tương thích cả Linux và Windows)
```python
import ctypes, os, sys
import numpy as np

lib_name = "nvxk.dll" if sys.platform.startswith("win") else "libnvxk.so"
lib = ctypes.CDLL(os.path.abspath(lib_name))
lib.nvxk_sgemm.argtypes = [
    ctypes.POINTER(ctypes.c_float), ctypes.POINTER(ctypes.c_float), ctypes.POINTER(ctypes.c_float),
    ctypes.c_size_t, ctypes.c_size_t, ctypes.c_size_t, ctypes.POINTER(ctypes.c_int),
]
lib.nvxk_sgemm.restype = ctypes.c_int

A = np.ascontiguousarray(np.ones((64, 128), dtype=np.float32))
B = np.ascontiguousarray(np.full((128, 64), 0.5, dtype=np.float32))
C = np.empty((64, 64), dtype=np.float32)
used = ctypes.c_int(0)
rc = lib.nvxk_sgemm(A.ctypes.data_as(ctypes.POINTER(ctypes.c_float)),
                    B.ctypes.data_as(ctypes.POINTER(ctypes.c_float)),
                    C.ctypes.data_as(ctypes.POINTER(ctypes.c_float)),
                    64, 128, 64, ctypes.byref(used))
assert rc == 0, f"NVXK failed with code {rc}"
```