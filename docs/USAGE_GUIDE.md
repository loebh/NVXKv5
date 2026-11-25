# Hướng dẫn Sử dụng (Usage Guide)

Tài liệu này hướng dẫn các kịch bản sử dụng thực tế từ cơ bản đến nâng cao trong **NVXK v2**.

## 1. Chạy Ứng dụng Tương tác Mẫu (`my_app.exe`)

Sau khi chạy `make all`, bạn có thể nhân nhanh hai ma trận nhập từ bàn phím hoặc qua pipe:

```bash
printf "2\n2\n2\n1 2\n3 4\n5 6\n7 8\n" | ./my_app.exe
```

* **Mục đích**: Kiểm tra hoạt động cơ bản của `XK_gemm` trên dữ liệu thực.
* **Output dự kiến**: In ma trận kết quả `2 × 2`: `19 22` và `43 50`.
* **Xử lý lỗi**: `my_app.exe` tự kiểm tra kích thước dương và giá trị hữu hạn (`isfinite`); nếu nhập chữ hoặc `NaN`/`Inf`, chương trình in thông báo lỗi và thoát với mã `1`.

## 2. Chế độ Tự động (Auto Mode) và Hiệu chuẩn một lần

Khi ứng dụng thực hiện nhiều phép nhân ma trận liên tiếp, nên chạy hiệu chuẩn một lần ở lúc khởi động (`XK_calibrate_hardware`) hoặc nạp hồ sơ đã lưu:

```cpp
#include <nvxk.h>
#include <iostream>
#include <vector>

int main() {
    XK_HardwareProfile prof;
    if (!XK_load_hardware_profile("hw_profile.cfg", prof)) {
        prof = XK_calibrate_hardware(200.0);  // giới hạn tối đa ~200 ms
        XK_save_hardware_profile(prof, "hw_profile.cfg");
    }
    XK_set_active_profile(prof);

    const size_t M = 512, K = 256, N = 512;
    std::vector<float> A(M * K, 0.5f), B(K * N, 0.25f), C(M * N, 0.0f);

    XK_WorkloadDesc wd(M, K, N);
    wd.mode = XK_ExecMode::Auto;
    int threads_used = 0;

    XK_ExecutionPlan plan = XK_gemm_ex(A.data(), B.data(), C.data(), wd,
                                       nullptr, nullptr, &threads_used);
    std::cout << "Plan: " << plan.explanation.reason << "\n";
    std::cout << "Actual threads: " << threads_used << "\n";
    return 0;
}
```

## 3. Chế độ Thủ công (Manual Mode) và So sánh ASM vs Intrinsics

Khi cần cố định số luồng, trục song song hoặc kích thước khối để phục vụ thí nghiệm:

```cpp
XK_WorkloadDesc wd(1024, 1024, 1024);
wd.mode = XK_ExecMode::Manual;
wd.manual_threads = 2;
wd.manual_axis = XK_ParallelAxis::AxisM;
wd.manual_mc = 120;
wd.manual_kc = 256;
wd.manual_nc = 1024;
wd.prefer_asm_kernel = false;  // đổi sang true để dùng microkernel inline ASM x86-64

XK_gemm_ex(A.data(), B.data(), C.data(), wd);
```

## 4. Tái sử dụng `XK_Workspace` dưới Ngân sách Bộ nhớ Chặt

Trong hệ thống hạn chế RAM, bạn có thể tạo một `XK_Workspace` với ngân sách nhỏ (ví dụ `512 KiB` hoặc `128 KiB`) và truyền vào `XK_gemm_ex` / `XK_gemm_f64_ex`. Planner sẽ tự động co `(NC → MC → KC)` để tổng bộ nhớ tạm không vượt ngân sách:

```cpp
XK_Workspace tight_ws(256 * 1024);  // giới hạn 256 KiB
XK_WorkloadDesc wd(1024, 1024, 1024);
wd.mode = XK_ExecMode::Auto;

XK_ExecutionPlan plan = XK_gemm_ex(A.data(), B.data(), C.data(), wd, &tight_ws);
// Kiểm tra trạng thái co khối và dung lượng thực cấp phát:
// plan.explanation.budget_clamped == true
// tight_ws.allocated_bytes() <= 256 * 1024
```

*Lưu ý quan trọng*: Một đối tượng `XK_Workspace` **không dùng đồng thời** giữa nhiều luồng ứng dụng gọi `XK_gemm_ex` song song. Mỗi luồng gọi ngoài cần một `XK_Workspace` riêng (hoặc truyền `nullptr` để dùng `thread_local` workspace mặc định 64 MiB của NVXK).

## 5. Nhân Ma trận Độ chính xác Kép (`f64`) và Số nguyên (`i32 → i64`)

```cpp
// Double precision (tự động dùng BLIS 6x8 + OpenMP + XK_Workspace::prepare_f64)
XK_gemm(A_f64.data(), B_f64.data(), C_f64.data(), M, K, N);

// Số nguyên 32-bit tích lũy 64-bit có kiểm tra tràn số học
XK_gemm_i32_i64(A_i32.data(), B_i32.data(), C_i64.data(), M, K, N);
```