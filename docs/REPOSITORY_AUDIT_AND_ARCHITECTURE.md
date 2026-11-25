# NVXK — Báo cáo Kiểm toán Mã nguồn (Phase 1), Chiến lược Nghiên cứu (Phase 2) và Thiết kế Kiến trúc (Phase 3)

## 1. Báo cáo Kiểm toán Repository (Phase 1 Audit)

Toàn bộ mã nguồn gốc (`v0-baseline`, gắn thẻ Git trước mọi chỉnh sửa) gồm 10 tệp cốt lõi (`apps/my_app.cpp`, `benchmarks/main.cpp`, `include/nvxk.h`, `src/nvxk.cpp`, `src/nvxk_hw.cpp`, `src/nvxk_planner.cpp`, `src/nvxk_workspace.cpp`, `tests/nvxk_test.cpp`, `CMakeLists.txt`, `makefile`) và 1 tệp dư thừa (`nvxk-dit.txt` — bản sao Repomix trùng lặp 100%).

### Bảng phân loại phát hiện theo mức độ nghiêm trọng

| Mức độ | Vị trí | Nguyên nhân gốc | Hệ quả | Giải pháp đã triển khai | Kiểm chứng |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **CRITICAL** | `benchmarks/main.cpp:365-397` (`v0`) | Hàm `maybe_flush()` (quét 16+ MiB RAM xóa L3) đặt **bên trong** lambda đo thời gian `measure_time()`. | Mọi số liệu `--cold-cache` bị cộng dồn thời gian xóa cache, sai lệch hoàn toàn phép đo. | Tách `pre_hook` ra **ngoài** vùng bấm giờ `steady_clock::now()`. | Chạy `--cold-cache` trên `256³–1024³`, cho thấy thông lượng thực `92.64–151.24 GFLOPS`. |
| **HIGH** | `src/nvxk.cpp:173-250` (`v0`) | Microkernel `XK_micro_6x16_packed_f32` không unroll vòng `k` (`k += 1`), gộp nhánh biên `mr/nr` làm phát sinh stack frame `448 B` (`sub rsp, 0x1c0`) trên mọi ô nội tâm. | Lãng phí ~25% băng thông decode front-end và phát sinh register spill khi compiler tối ưu mạnh. | Tách `micro_6x16_full_f32` và `micro_6x16_edge_f32`, unroll `k += 4`, đồng thời triển khai `micro_6x16_full_asm_f32` cấp phát thanh ghi tường minh (`ymm4..ymm15`). | `objdump` xác nhận 0 spill; 1T `1024³` tăng từ `85.85` → **`90.88 GFLOPS`** (+5.9%). |
| **HIGH** | `src/nvxk.cpp:419-527` (`v0`) | Chỉ có 1 hình học microkernel `6×16`. Với ma trận mỏng `M ∈ {1..4}` (GEMV / LLM decode), phải đệm 2–5 hàng số 0 vào `A_packed`. | Lãng phí 33%–83% năng lực FMA và tốn 100% chi phí đóng gói `A`, `B`. | Bổ sung kernel streaming không đóng gói `Avx2ThinM` (`1×32`, `2×32`, `3×24`, `4×24`), workspace `0 KiB`, OpenMP trục N không barrier nội. | `4×1024×1024` tăng từ `32.51` → **`63.10 GFLOPS`** (+94.1%), đạt **264.3%** so với OpenBLAS 0.3.27. |
| **HIGH** | `src/nvxk_planner.cpp` + `XK_gemm_ex` (`v0`) | Khi `workload.memory_budget_bytes == 0` nhưng `XK_Workspace` tùy chỉnh có ngân sách nhỏ hơn, Planner vẫn lập lịch theo 64 MiB khiến `prepare_f32()` ném `std::length_error`. | Vi phạm nguyên tắc suy giảm mềm (graceful degradation) khi chạy trong môi trường hạn chế RAM. | Tự động lan truyền `min(workload.budget, workspace.budget)` vào Planner để co `NC/MC/KC` phù hợp. | Test ngân sách `128 KiB` trên `1024³`: cấp phát `84 KiB`, 0 ngoại lệ, giữ **75.9%** thông lượng. |
| **HIGH** | `src/nvxk.cpp:635` (`v0`) | `XK_gemm(double*)` chạy đơn luồng cố định, cấp phát/giải phóng heap mới (`XK_AlignedF64Buf`) ở mọi lần gọi và không đi qua Planner. | Thiếu khả năng mở rộng đa luồng cho tính toán khoa học `f64` và gây phân mảnh bộ nhớ. | Nâng cấp `XK_Workspace::prepare_f64`, bổ sung `XK_gemm_f64_ex` và `gemm_avx2_blis_f64_omp` unroll `k += 4`. | `f64 1024³` đạt **`76.55 GFLOPS`** (2T), sai số tương đối `≤ 5.19e-14`. |
| **HIGH** | `src/nvxk_hw.cpp` (`v0`) | `XK_calibrate_hardware` gán cứng `smt_efficiency = 0.12`, không đo thực nghiệm hiệu suất SMT. | Dự báo chi phí 4T vs 8T trên CPU có Hyper-Threading (như i7-7700) thiếu độ tin cậy thực nghiệm. | Bổ sung bước đo đối chiếu `physical_cores` vs `logical_threads` vào quy trình hiệu chuẩn có giới hạn thời gian. | Kiểm chứng biên dịch và chạy an toàn trên cả CPU không SMT (2C/2T) và cấu hình giả lập 4C/8T. |
| **MEDIUM** | `src/nvxk.cpp`, `nvxk-dit.txt` | Mã nguồn đơn khối >1000 dòng, dư 1 `#pragma omp barrier` ở ô `(jc, pc)` cuối cùng trong `AxisM`, enum chết `Avx2DoubleRow`, không có C ABI cho FFI. | Khó bảo trì, khó kiểm chứng đa ngôn ngữ. | Tách 6 module rõ ràng, bỏ barrier cuối, xóa `nvxk-dit.txt`, bổ sung `include/nvxk_c.h` (`libnvxk.so`). | Build 0 cảnh báo `-Wall -Wextra`, test C ABI + Python `ctypes` thành công. |

---

## 2. Đánh giá Chiến lược Nghiên cứu và Công nghệ (Phase 2)

### Bảng so sánh các phương án kiến trúc & đa ngôn ngữ

| Phương án | Hiệu năng kỳ vọng | Tác động bộ nhớ | Độ tin cậy | Độ phức tạp | Khả năng di động | Chi phí triển khai | Quyết định |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **Kiến trúc gốc `v0` (BLIS 6×16 đơn khối)** | Trung bình–Cao (vuông), Kém (thin-M) | Cố định ~2.2 MiB, ném lỗi khi thiếu ngân sách | Khá (nhưng lỗi cold-cache & f64) | Trung bình | Cao | 0 | **Làm Baseline** |
| **C++11 Modular + Intrinsics 4× Unroll + Thin-M + Planner v2** | Cao (đặc biệt Thin-M +94%) | 0 KiB (Thin-M), co giãn xuống 84 KiB | Rất cao (ASan/UBSan + differential) | Trung bình | Rất cao (Windows/Linux/ARM fallback) | Trung bình | **Chọn làm lõi mặc định di động** |
| **Bổ sung x86-64 Inline ASM 6×16 Microkernel (Runtime Dispatch)** | **Cao nhất (+5.9% vs v0, +7.4% vs Intrinsics 1T)** | Không tăng | Cao (giới hạn trong 1 hàm nội tâm duy nhất) | Cao (chỉ 120 dòng ASM) | Tự động fallback về C++ trên non-x86_64 | Thấp–Trung bình | **Giữ lại vì thực nghiệm chứng minh vượt trội** |
| **Viết lại lõi bằng Rust** | Tương đương C++ (cùng LLVM backend) | Tương đương | Rất cao | Cao (FFI + OpenMP/Rayon) | Cần `rustc` (không có trong sandbox) | Cao | **Không chọn** (không chứng minh được lợi ích đo lường vượt C++ + ASan/UBSan) |
| **Tích hợp OpenBLAS / BLIS / Eigen / oneDNN** | Rất cao (OpenBLAS dùng AVX-512 trên Xeon) | Lớn (nhiều buffer nội bộ) | Rất cao | Phụ thuộc ngoài lớn | Cao | Thấp | **Dùng OpenBLAS 0.3.27 làm chuẩn đối chiếu khoa học** |

### Khảo sát dự án mã nguồn mở (Open-Source Research)
1. **BLIS (BSD-3-Clause)**: Đóng góp khung phân rã 5 vòng lặp (`JC → PC → IC → JR → IR`) tách biệt hoàn toàn microkernel với hạ tầng đóng gói `A`/`B`. NVXK học hỏi cấu trúc phân tầng cache L1/L2/L3 của BLIS nhưng đơn giản hóa để tích hợp chặt với bộ lập kế hoạch thích ứng có ngân sách bộ nhớ.
2. **OpenBLAS (BSD-3-Clause)**: Sử dụng các microkernel hợp ngữ viết tay cho từng vi kiến trúc (`Haswell`, `SkylakeX`, `SapphireRapids`). NVXK sử dụng OpenBLAS 0.3.27 làm đối chứng định lượng và chứng minh bằng thực nghiệm rằng trên ma trận mỏng `4×1024×1024`, chiến lược zero-pack streaming có thể vượt OpenBLAS 2.64×.
3. **Eigen (MPL2)**: Sử dụng biểu thức mẫu (expression templates) và GEBP kernel; mạnh về tổng quát nhưng khó giải thích trực quan quyết định chọn luồng/khối như `XK_PlanExplanation` của NVXK.
4. **oneDNN (Apache-2.0)** & **Google Highway (Apache-2.0)**: oneDNN sử dụng JIT (`Xbyak`) sinh mã theo từng shape; Highway cung cấp SIMD trừu tượng đa kiến trúc. Với phạm vi KHKT và yêu cầu tối giản phụ thuộc, NVXK chọn runtime dispatch bằng `__attribute__((target("avx2,fma")))` + CPUID/XGETBV thay cho JIT nặng nề.

---

## 3. Thiết kế Kiến trúc NVXK v2 (Phase 3)

```text
+-------------------------------------------------------------------------+
|                    Public API Layer (src/nvxk_api.cpp)                  |
|   C++ API (include/nvxk.h)           |   Stable C ABI (include/nvxk_c.h)|
|   XK_gemm / XK_gemm_ex / f64 / i32   |   nvxk_sgemm / nvxk_dgemm (FFI)  |
+-------------------+------------------+-----------------+----------------+
                    | Input Validation (Null, Overflow, Aliasing)
                    v
+-------------------------------------------------------------------------+
|         Hardware Discovery & Calibration (src/nvxk_hw.cpp)              |
|   CPUID/XGETBV (AVX2/FMA) | L1d/L2/L3 | Physical vs SMT | Peak & BW    |
+-------------------+-----------------------------------------------------+
                    v
+-------------------------------------------------------------------------+
|        Workload- & Budget-Aware Planner (src/nvxk_planner.cpp)          |
|   Roofline AI Model | Thread & Axis (M/N) Selection | Graceful Clamping |
|   Emits XK_ExecutionPlan + Human-Readable XK_PlanExplanation            |
+-------+-------------------------+--------------------------+------------+
        |                         |                          |
        v (M <= 4, N >= 16)       v (General Compute-Bound)  v (No AVX2)
+-------------------+   +---------------------------+  +------------------+
| Avx2ThinM Kernel  |   | BLIS 6x16 (f32) / 6x8(f64)|  | Portable Scalar  |
| Zero-Pack Stream  |   | - x86-64 ASM (default f32)|  | Fallback IKJ     |
| 0 KiB Workspace   |   | - C++ Intrinsics fallback |  | (nvxk_kernels_   |
| Barrier-Free OMP  |   | - Unified XK_Workspace    |  |  scalar.cpp)     |
+-------------------+   +---------------------------+  +------------------+
```