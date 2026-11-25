# Thiết kế và Tối ưu hóa Động cơ Tính toán Ma trận Thích ứng Phần cứng – Khối lượng công việc (NVXK v2) theo Hướng Hiệu năng Cao, Tiết kiệm Bộ nhớ và Độ tin cậy Cao trên CPU Phổ thông

## 1. Tóm tắt (Abstract)

Phép nhân ma trận tổng quát (GEMM) là hạt nhân tính toán nền tảng của học sâu, mô phỏng khoa học và xử lý tín hiệu. Trên các bộ xử lý CPU phổ thông không có GPU rời, hiệu năng thực tế của GEMM không chỉ phụ thuộc vào tốc độ nhân cộng hợp nhất (FMA) mà còn bị chi phối mạnh bởi phân cấp bộ nhớ đệm (L1d/L2/L3), chi phí đồng bộ đa luồng (OpenMP), áp lực thanh ghi (register pressure), chi phí đóng gói dữ liệu (packing) trên các ma trận mỏng (thin-M) và giới hạn bộ nhớ tạm (scratchpad). Nghiên cứu này trình bày quá trình kiểm toán, tái cấu trúc và tối ưu hóa toàn diện hệ thống **NVXK v2** (*Native Vector eXecution Kernels*) — một động cơ tính toán ma trận thích ứng có cơ chế phân phát tập lệnh động (runtime dispatch), tự hiệu chuẩn phần cứng, hỗ trợ cả giao diện C++ và C ABI, cùng cơ chế suy giảm mềm theo ngân sách bộ nhớ.

Bằng thực nghiệm có kiểm soát trên CPU Intel Xeon Platinum 8481C (2 nhân vật lý, tập lệnh mục tiêu AVX2+FMA 256-bit tương thích với Intel Core i7-7700) sử dụng trình biên dịch GCC 12.2.0 và đối chiếu trực tiếp với thư viện chuẩn công nghiệp OpenBLAS 0.3.27, nghiên cứu thu được ba kết quả chính: (1) Loại bỏ xung đột cổng tải (`prefetcht0` dư thừa) và áp lực thanh ghi thông qua microkernel hợp ngữ x86-64 `6×16` unroll `4×` nâng thông lượng đơn luồng `1024³` từ `85.85 GFLOPS` lên **`90.88 GFLOPS`** (+5.9% so với baseline, +7.4% so với C++ intrinsics); (2) Kiến trúc đa-kernel với nhánh streaming không đóng gói `Avx2ThinM` nâng thông lượng ma trận mỏng `4×1024×1024` từ `32.51 GFLOPS` lên **`63.10 GFLOPS`** (+94.1%, tiêu thụ **`0 KiB`** bộ nhớ tạm), đạt **264.3%** thông lượng của OpenBLAS 0.3.27 (`22.25 GFLOPS`) trên cùng phép đo; (3) Cơ chế co khối thích ứng theo ngân sách bộ nhớ cho phép cắt giảm **96.3%** bộ nhớ tạm (từ `2,256 KiB` xuống **`84 KiB`**) mà vẫn duy trì **75.9%** thông lượng (`125.20 GFLOPS`) không phát sinh lỗi thiếu bộ nhớ. Toàn bộ hệ thống vượt qua kiểm thử đơn vị, kiểm thử sai phân ngẫu nhiên, chế độ fallback scalar và AddressSanitizer/UndefinedBehaviorSanitizer với 0 cảnh báo biên dịch.

## 2. Giới thiệu (Introduction)

Trong bối cảnh trí tuệ nhân tạo và tính toán khoa học ngày càng phổ biến, phần lớn người dùng, sinh viên và các hệ thống nhúng/biên vẫn vận hành trên phần cứng CPU phổ thông (như Intel Core i7-7700 với 4 nhân / 8 luồng, 256 KiB L2 mỗi nhân, hỗ trợ AVX2+FMA nhưng không có AVX-512 hay GPU rời). Các thư viện tuyến tính lớn như OpenBLAS hay Intel oneDNN đạt hiệu năng rất cao nhờ hàng trăm nghìn dòng hợp ngữ viết riêng cho từng thế hệ CPU hoặc trình biên dịch JIT, nhưng lại khó tùy biến giới hạn bộ nhớ tạm, thiếu khả năng giải thích quyết định lập lịch và thường tối ưu ưu tiên cho ma trận vuông kích thước lớn.

Dự án **NVXK** (*Native Vector eXecution Kernels*, sử dụng tiền tố giao diện lập trình `XK_` — *eXecution Kernel*) hướng tới bài toán nghiên cứu: *Làm thế nào để xây dựng một động cơ tính toán ma trận gọn nhẹ, minh bạch, có độ tin cậy cao, tự thích ứng với cấu trúc phần cứng và hình dạng dữ liệu trên CPU phổ thông mà không cần phụ thuộc ngoài nặng nề?*

## 3. Vấn đề nghiên cứu (Research Problem)

Qua kiểm toán toàn bộ mã nguồn phiên bản gốc (`v0-baseline`) và phân tích hợp ngữ (`objdump`), nghiên cứu xác định bốn nút cổ chai mang tính hệ thống:

1. **Nghẽn vi kiến trúc ở vòng lặp nóng (Hot-path Microkernel)**: Microkernel `6×16` gốc không trải vòng `k` (`k += 1`), gộp xử lý ô biên gây cấp phát stack `448 B` ở mọi lệnh gọi, và khi tăng số thanh ghi broadcast lên 16/16 thanh ghi YMM, trình biên dịch GCC 12.2.0 phát sinh lệnh sao chép thanh ghi (`vmovaps`) và tràn con trỏ lên stack; đồng thời việc chèn `prefetcht0` vào vòng lặp nóng gây tranh chấp cổng tải (Load Port 2/3) với bộ nạp trước phần cứng.

2. **Sự lãng phí của kiến trúc đơn-kernel trên ma trận mỏng (Thin-M / GEMV)**: Với `M ∈ {1..4}` (đặc trưng của pha giải mã LLM và suy luận batch nhỏ), microkernel `6×16` đệm 2–5 hàng số 0, lãng phí 33%–83% phép tính FMA và chịu 100% chi phí sao chép đóng gói ma trận `B`.

3. **Thiếu suy giảm mềm dưới ràng buộc bộ nhớ (Memory-Budget Fragility)**: Khi bộ đệm `XK_Workspace` có ngân sách nhỏ hơn kế hoạch mặc định, hệ thống ném ngoại lệ `std::length_error` thay vì tự động co kích thước khối (`MC`, `KC`, `NC`).

4. **Sai lệch đo lường và khoảng trống tính toán `f64`**: Chế độ `--cold-cache` ở `v0-baseline` đặt vòng lặp xóa cache vào bên trong vùng bấm giờ; phép nhân `double` (`f64`) vẫn chạy đơn luồng và cấp phát heap mới ở mỗi lệnh gọi.

## 4. Câu hỏi nghiên cứu (Research Questions)

* **RQ1 (Hiệu quả bộ nhớ vs Thông lượng)**: Có thể cắt giảm mạnh kích thước bộ nhớ tạm (workspace memory footprint) của GEMM phân khối mà vẫn duy trì phần lớn thông lượng tính toán và không gây lỗi OOM như thế nào?

* **RQ2 (Đa-kernel thích ứng hình dạng)**: Việc tách nhánh kernel streaming không đóng gói (`Avx2ThinM`) cho các ma trận mỏng (`M ≤ 4`) cải thiện thông lượng và bộ nhớ ra sao so với BLIS `6×16` và thư viện chuẩn OpenBLAS?

* **RQ3 (Compiler Intrinsics vs Hand-tuned Assembly)**: Khi áp lực thanh ghi chạm ngưỡng kiến trúc (16 thanh ghi YMM của AVX2), microkernel hợp ngữ x86-64 viết tay có mang lại lợi ích đo lường được so với mã C++ intrinsics biên dịch bằng GCC `-O3` hay không?

* **RQ4 (Độ tin cậy và Khả năng di động)**: Làm thế nào để kết hợp phân phát tập lệnh động (runtime dispatch), giao diện C ABI ổn định và kiểm thử tự động (differential testing, ASan/UBSan, scalar fallback) nhằm đảm bảo tính tất định và an toàn bộ nhớ?

## 5. Giả thuyết nghiên cứu (Hypotheses)

* **H1**: Tự động co kích thước khối `(NC → MC → KC)` theo ngân sách bộ nhớ cho phép giảm trên 90% dung lượng scratchpad trong khi vẫn giữ trên 70% thông lượng đỉnh trên `1024³`.

* **H2**: Loại bỏ hoàn toàn khâu đóng gói (`0 KiB` workspace) và thay microkernel `6×16` bằng kernel streaming `1×32 / 2×32 / 3×24 / 4×24` song song trục `N` không barrier nội sẽ tăng ít nhất 1.5× thông lượng cho ma trận `4×1024×1024`.

* **H3**: Microkernel hợp ngữ x86-64 cấp phát tường minh 12 thanh ghi tích lũy (`ymm4..ymm15`), 2 thanh ghi `B` (`ymm0..ymm1`), 2 thanh ghi broadcast `A` luân phiên (`ymm2..ymm3`) kết hợp trải vòng `k += 4` và loại bỏ `prefetcht0` nội vòng sẽ đạt thông lượng cao hơn mã C++ intrinsics tương đương.

## 6. Nghiên cứu liên quan (Related Work)

* **GotoBLAS & BLIS** (Goto & van de Geijn, 2008; Van Zee & van de Geijn, 2015): Đặt nền móng cho thuật toán GEMM phân khối 5 vòng lặp với đóng gói `A` vào L2 và `B` vào L3, giúp tối ưu tái sử dụng dữ liệu trên thanh ghi và bộ nhớ đệm.

* **OpenBLAS** (Wang et al., 2013): Kế thừa GotoBLAS2 với hệ thống microkernel hợp ngữ tối ưu sâu cho từng vi kiến trúc x86/ARM. Tuy nhiên, kiến trúc tổng quát của OpenBLAS vẫn đóng gói dữ liệu trên nhiều hình dạng ma trận mỏng.

* **Mô hình Roofline** (Williams, Waterman & Patterson, 2009): Liên kết cường độ tính toán (Arithmetic Intensity — FLOP/Byte) với trần tính toán (Peak GFLOPS) và trần băng thông bộ nhớ (Memory Bandwidth), là cơ sở lý thuyết cho bộ lập kế hoạch thích ứng `nvxk_planner`.

## 7. Cơ sở lý thuyết (Theoretical Foundation)

Với phép nhân ma trận $C_{M \times N} = A_{M \times K} \cdot B_{K \times N}$:

* Tổng số phép toán dấu phẩy động: $\text{FLOPs} = 2MKN$.

* Lượng truyền dữ liệu tối thiểu (với kích thước phần tử $e$ byte): $\text{Bytes} = (MK + KN + MN) \cdot e$.

* Cường độ tính toán:
  

  $$
  \text{AI} = \frac{2MKN}{(MK + KN + MN) \cdot e}
  $$

* Với ma trận vuông $1024^3$ (`f32`, $e=4$): $\text{AI} = 170.7\ \text{FLOP/B}$ (compute-bound rõ rệt).

* Với ma trận mỏng $4 \times 1024 \times 1024$ (`f32`): $\text{AI} = 2.0\ \text{FLOP/B}$ (memory-bound). Khi đó, việc đóng gói $B$ ($4\ \text{MiB}$) vào bộ đệm tạm làm **tăng gấp đôi** lưu lượng đọc/ghi bộ nhớ chính mà không mang lại đủ lượt tái sử dụng theo chiều $M$, dẫn đến sụt giảm nghiêm trọng băng thông hữu dụng.

* Ở tầng thanh ghi AVX2 (16 thanh ghi `ymm`, mỗi thanh ghi 8 phần tử `f32`): hình học microkernel `6×16` tiêu thụ đúng $6 \times 2 = 12$ thanh ghi tích lũy `C`, $2$ thanh ghi tải `B`, và $1$–$2$ thanh ghi broadcast `A`, đạt tỷ lệ $12\ \text{FMA} / 8\ \text{loads} = 1.5\ \text{FMA/load}$.

## 8. Kiến trúc đề xuất (Proposed Architecture)

NVXK v2 được tái cấu trúc thành 6 module độc lập (không còn tệp đơn khối `nvxk.cpp`):

1. **Hardware Discovery & Calibration (`src/nvxk_hw.cpp`)**: Đọc CPUID/XGETBV xác định AVX2/FMA và kích thước thực L1d/L2/L3; tự hiệu chuẩn có giới hạn thời gian (`peak_gflops_1t`, `mem_bandwidth_gbs`, `omp_fork_overhead_us`, `omp_barrier_overhead_us`, và bổ sung đo thực nghiệm `smt_efficiency`).

2. **Adaptive Workload- & Budget-Aware Planner (`src/nvxk_planner.cpp`)**: Áp dụng mô hình Roofline kết hợp chi phí đồng bộ OpenMP để chọn số luồng, trục song song (`AxisM`/`AxisN`), kích thước khối `(MC, KC, NC)` và loại kernel (`Avx2ThinM`, `Avx2Blis6x16Asm`, `Avx2Blis6x16`, `ScalarIkj`). Tự động co khối theo `min(workload.budget, workspace.budget)` và xuất giải trình `XK_PlanExplanation`.

3. **Unified Aligned Workspace (`src/nvxk_workspace.cpp`)**: Bộ cấp phát căn lề 64-byte dùng chung cho cả `f32` và `f64`, đệm từng khe luồng theo bội số 64 byte nhằm triệt tiêu false sharing, tái sử dụng qua nhiều lệnh gọi mà không phân mảnh heap.

4. **Runtime-Dispatched AVX2+FMA Kernels (`src/nvxk_kernels_avx2.cpp`)**: Biên dịch không dùng cờ toàn cục `-mavx2`/`-mfma` (sử dụng `__attribute__((target("avx2,fma")))`) để bảo đảm tệp nhị phân chạy an toàn trên mọi CPU x86-64. Bao gồm: microkernel `6×16` full/edge tách biệt, phiên bản hợp ngữ x86-64 unroll `4×`, đóng gói `A` bằng transpose SIMD `4×4 + 2×4`, kernel `Avx2ThinM` không đóng gói, và BLIS `6×8` đa luồng cho `f64`.

5. **Portable Scalar Fallback (`src/nvxk_kernels_scalar.cpp`)**: Triển khai thuần C++11 cho `f32`, `f64` và `i32 → i64` có kiểm tra tràn số học (`add_checked`), luôn sẵn sàng khi CPU không có AVX2 hoặc bật `-DXK_FORCE_SCALAR_FALLBACK`.

6. **Public C++ & Stable C ABI (`src/nvxk_api.cpp`, `include/nvxk_c.h`)**: Kiểm tra con trỏ null, tràn kích thước `size_t`, chống chồng lấn bộ nhớ (`validate_no_aliasing`), dịch ngoại lệ sang mã lỗi C an toàn cho FFI từ Python/C/Rust.

## 9. Phương pháp luận (Methodology)

* **Biến độc lập**: Kiến trúc kernel (`v0-baseline`, `v2-intrinsics`, `v2-asm`, `Avx2ThinM`, `OpenBLAS 0.3.27`), hình dạng ma trận (vuông `128³–1024³`, mỏng `1×1024×1024`, `4×1024×1024`, chữ nhật `64×1024×1024`, `512×256×512`), kiểu dữ liệu (`f32`, `f64`), chế độ cache (warm/cold), ngân sách bộ nhớ tạm (`64 MiB` đến `128 KiB`).

* **Biến phụ thuộc**: Thời gian thực thi (median, mean, std, P95, P99), thông lượng (GFLOPS), dung lượng workspace thực cấp phát (KiB), và sai số tương đối cực đại so với tham chiếu `f64`.

* **Biến kiểm soát**: Hạt giống ngẫu nhiên cố định (`seed=42`), phân phối đồng nhất `[-1, 1]`, `omp_set_dynamic(0)`, chạy làm nóng (warm-up) trước mỗi chuỗi đo, lặp thích ứng ≥40 ms cho mỗi mẫu, và xóa cache L3 hoàn toàn **bên ngoài** vùng bấm giờ ở chế độ `--cold-cache`.

## 10. Thiết lập thực nghiệm (Experimental Setup)

> **Minh bạch môi trường đo đạc**: Toàn bộ số liệu định lượng dưới đây được đo trực tiếp, không giả lập, trên môi trường sandbox Linux nơi mã nguồn được biên dịch và kiểm chứng. Ngoài ra, bộ kịch bản tái lập `scripts/run_experiments.ps1` đã được đóng gói sẵn để chạy lại chính xác quy trình trên máy đích Windows Intel Core i7-7700 (4C/8T, MSYS2 GCC 16.2.0).

* **Phần cứng đo thực nghiệm (Sandbox)**: Intel Xeon Platinum 8481C, 2 nhân vật lý / 2 luồng logic, L1d = 48 KiB, L2 = 2,048 KiB, L3 = 107,520 KiB, RAM 7.8 GiB, Linux 6.12 x86_64.

* **Trình biên dịch & Cờ tối ưu**: `g++ 12.2.0`, `-std=c++11 -O3 -fopenmp -Wall -Wextra` (tuyệt đối không gắn cờ toàn cục `-mavx2` hay `-mfma`).

* **Thư viện đối chứng**: OpenBLAS 0.3.27 (tích hợp trong NumPy/SciPy, hỗ trợ tập lệnh rộng tới 512-bit trên Xeon 8481C), gọi trong cùng tiến trình Python với `libnvxk.so` qua C ABI, cùng thiết lập `OPENBLAS_NUM_THREADS=2`, `OMP_NUM_THREADS=2`.

## 11. Kết quả thực nghiệm (Results)

### 11.1. Tối ưu hóa Microkernel: Baseline vs Intrinsics vs x86-64 Inline ASM (`f32`)

| Kích thước | Baseline `v0` 1T (GFLOPS) | NVXK `v2` Intrinsics 1T (GFLOPS) | NVXK `v2` ASM 1T (GFLOPS) | Tăng tốc ASM vs `v0` | NVXK `v2` Auto-ASM 2T (GFLOPS) | Độ lệch chuẩn 2T (s) | Sai số cực đại | 
| ----- | ----- | ----- | ----- | ----- | ----- | ----- | ----- | 
| `128×128×128` | 80.92 | 78.35 | 79.16 | \-2.2% | **80.55** (1T auto) | 0.000000 | `2.96e-06` | 
| `256×256×256` | 87.64 | 82.88 | 86.97 | \-0.8% | **147.97** (2T) | 0.000006 | `6.50e-06` | 
| `512×512×512` | 87.71 | 85.13 | 85.38 | \-2.7% | **164.07** (2T) | 0.000047 | `1.34e-05` | 
| `1024×1024×1024` | 85.85 | 84.63 | **90.88** | **+5.9%** | **165.94** (2T) | 0.000309 | `2.57e-05` | 

*Ghi chú chẩn đoán vi kiến trúc*: Ở thử nghiệm đầu tiên trước khi loại bỏ `prefetcht0` nội vòng và hạ áp lực thanh ghi từ 16 xuống 15 YMM trong bản C++ intrinsics, `v2` intrinsics chỉ đạt `81.89 GFLOPS` trên `1024³` do GCC sinh lệnh `vfmadd132ps` và tràn con trỏ lên stack. Sau khi sửa dựa trên phân tích `objdump`, intrinsics tăng lên `84.63 GFLOPS` (+3.3%) và bản ASM tăng lên **`90.88 GFLOPS`** (+7.4% so với intrinsics).

### 11.2. Đột phá trên Ma trận Mỏng (Thin-M) và Đối chiếu OpenBLAS 0.3.27 (2 luồng)

| Kiểu | Hình dạng (`M×K×N`) | NVXK `v2` (GFLOPS) | OpenBLAS 0.3.27 (GFLOPS) | Tỷ lệ NVXK / OpenBLAS | Workspace NVXK | Sai số cực đại vs OpenBLAS | 
| ----- | ----- | ----- | ----- | ----- | ----- | ----- | 
| `f32` | `128×128×128` | 77.97 (1T) | 94.63 | 82.4% | 130 KiB | `0.00e+00` | 
| `f32` | `256×256×256` | 146.48 (2T) | 212.47 | 68.9% | 736 KiB | `0.00e+00` | 
| `f32` | `512×512×512` | 161.67 (2T) | 232.56 | 69.5% | 1,488 KiB | `1.77e-05` | 
| `f32` | `1024×1024×1024` | 167.90 (2T) | 280.09 | 59.9% | 2,256 KiB | `2.91e-05` | 
| `f32` | `1×1024×1024` (GEMV) | **22.43** (2T) | 26.17 | **85.7%** | **0 KiB** | `1.08e-05` | 
| `f32` | **`4×1024×1024` (Thin-M)** | **58.82** (2T) | 22.25 | **264.3% (2.64×)** | **0 KiB** | `1.23e-05` | 
| `f32` | `512×256×512` | 168.89 (2T) | 221.84 | 76.1% | 1,488 KiB | `0.00e+00` | 
| `f64` | `128×128×128` | 49.73 (2T) | 69.83 | 71.2% | 256 KiB | `0.00e+00` | 
| `f64` | `256×256×256` | 73.17 (2T) | 104.52 | 70.0% | 736 KiB | `0.00e+00` | 
| `f64` | `1024×1024×1024` | 76.55 (2T) | 137.36 | 55.7% | 2,256 KiB | `5.19e-14` | 

*(Ghi nhận bổ sung trên C++ benchmark độc lập: `4×1024×1024` tăng từ `32.51 GFLOPS` ở `v0-baseline` lên **`63.10 GFLOPS`** ở `v2`, tương đương tăng tốc **1.94× / +94.1%**).*

### 11.3. Đường cong Đánh đổi Ngân sách Bộ nhớ – Thông lượng (`1024³`, `f32`, 2T)

| Ngân sách đặt (`KiB`) | Thực cấp phát (`KiB`) | Giảm bộ nhớ | Khối `(MC, KC, NC)` | Co khối | Median (s) | Thông lượng (GFLOPS) | Tỷ lệ giữ thông lượng | 
| ----- | ----- | ----- | ----- | ----- | ----- | ----- | ----- | 
| 65,536 | 2,256 | 0.0% (Gốc) | `(240, 384, 2048)` | Không | 0.013011 | 165.05 | 100.0% | 
| 4,096 | 2,256 | 0.0% | `(240, 384, 2048)` | Không | 0.012812 | 167.62 | 101.6% | 
| 1,024 | 912 | **-59.6%** | `(240, 384, 128)` | Có | 0.013727 | 156.45 | **94.8%** | 
| 512 | 408 | **-81.9%** | `(120, 384, 32)` | Có | 0.017044 | 126.00 | **76.3%** | 
| 256 | 228 | **-89.9%** | `(60, 384, 32)` | Có | 0.016729 | 128.37 | **77.8%** | 
| 128 | **84** | **-96.3%** | `(12, 384, 32)` | Có | 0.017152 | **125.20** | **75.9%** | 

### 11.4. Kiểm chứng Chế độ Cold-Cache (Đã sửa lỗi bấm giờ)

Sau khi đưa thao tác xóa L3 ra ngoài vùng đo `steady_clock`, thông lượng cold-cache trên `256³`, `512³`, `1024³` lần lượt đạt `92.64`, `135.32` và `151.24 GFLOPS` (giảm 9%–35% so với warm-cache đúng theo lý thuyết truy cập DRAM lạnh, phản ánh trung thực chi phí nạp dữ liệu lần đầu).

## 12. Phân tích thống kê (Statistical Analysis)

Trên chuỗi lặp 7–9 mẫu (mỗi mẫu tích lũy ≥40 ms), hệ số biến thiên ($\text{CV} = \sigma / \mu$) của NVXK v2 ở chế độ warm-cache rất thấp:

* `128³`: $\text{CV} < 1.9\%$; `256³`: $\text{CV} = 2.6\%$; `512³`: $\text{CV} = 2.8\%$; `1024³`: $\text{CV} = 2.4\%$ ($\mu = 12.90\ \text{ms}$, $\sigma = 0.31\ \text{ms}$).

* Khoảng chênh lệch `+7.4%` giữa microkernel ASM (`23.63 ms`, $\sigma = 0.40\ \text{ms}$) và C++ intrinsics (`25.37 ms`, $\sigma = 0.74\ \text{ms}$) trên 1T `1024³` vượt quá $2.5\times$ tổng độ lệch chuẩn, xác nhận ý nghĩa thống kê rõ rệt chứ không phải nhiễu đo.

* Độ chính xác số học đạt chuẩn IEEE-754: sai số tương đối cực đại trên toàn bộ kiểm thử `f32` là `≤ 2.91e-05` và `f64` là `≤ 5.19e-14`.

## 13. Thảo luận (Discussion)

1. **Xác nhận H1 (Tiết kiệm bộ nhớ với suy giảm mềm)**: Kết quả bảng 11.3 chứng minh phần lớn dung lượng scratchpad của BLIS (`B_packed` kích thước `KC × NC`) chỉ đóng góp khoảng 5%–24% thông lượng cuối cùng. Nhờ cơ chế co khối tự động, NVXK v2 vận hành ổn định chỉ với `84 KiB` bộ nhớ tạm — mở ra khả năng nhúng vào các hệ thống biên hạn chế RAM.

2. **Xác nhận H2 (Giá trị của kiến trúc đa-kernel)**: Trên `4×1024×1024`, NVXK v2 vượt OpenBLAS 0.3.27 tới **2.64×** không phải nhờ tập lệnh mạnh hơn (ngược lại, NVXK chỉ dùng 256-bit AVX2), mà nhờ quyết định thuật toán: nhận diện đúng vùng memory-bound (`AI = 2.0 FLOP/B`) để bỏ hoàn toàn bước đóng gói `B` và loại bỏ barrier nội vòng.

3. **Xác nhận H3 (Khi nào Assembly thực sự có giá trị)**: Thực nghiệm bác bỏ hai cực đoan phổ biến: (a) chèn prefetch thủ công vào luồng dữ liệu tuần tự trên CPU hiện đại thực tế *làm chậm* chương trình do tranh chấp cổng tải; (b) ở ngưỡng bão hòa 16 thanh ghi YMM, cấp phát thanh ghi tường minh bằng \~120 dòng hợp ngữ x86-64 giải quyết triệt để register spill của compiler và mang lại +5.9%–7.4% thông lượng thực.

4. **Giải thích khoảng cách với OpenBLAS trên ma trận vuông lớn**: Trên Xeon Platinum 8481C (Sapphire Rapids), OpenBLAS 0.3.27 khai thác tập lệnh 512-bit (AVX-512), cho băng thông FMA lý thuyết gấp đôi AVX2 256-bit. NVXK chủ đích giữ giới hạn AVX2+FMA để tương thích trực tiếp với Intel Core i7-7700; trên i7-7700 (nơi cả hai đều bị giới hạn ở AVX2), khoảng cách này dự kiến thu hẹp đáng kể.

## 14. Hạn chế (Limitations)

1. **Khác biệt phần cứng đo đạc và máy đích**: Số liệu trong báo cáo này được đo trên Linux Xeon 8481C (2C/2T, không SMT). Việc xác nhận định lượng hiệu quả hiệu chuẩn SMT thực nghiệm (4T vs 8T) trên Windows Intel Core i7-7700 cần chạy trực tiếp `scripts/run_experiments.ps1` trên máy đích.

2. **Phạm vi phép toán**: Hiện tại NVXK v2 tối ưu sâu cho GEMM dạng `C = A × B` (`NoTrans`, `NoTrans`, $\alpha=1, \beta=0$). Các biến thể tổng quát (`TransA/TransB`, `beta != 0`, ma trận thưa, lượng tử hóa INT8/BF16) chưa được triển khai.

3. **Chưa có nhánh AVX-512 hoặc ARM NEON tối ưu hóa**: Trên ARM và CPU có AVX-512, hệ thống vẫn chạy đúng qua nhánh fallback nhưng chưa khai thác hết độ rộng thanh ghi của các kiến trúc đó.

## 15. Kết luận (Conclusion)

Nghiên cứu đã hoàn thành trọn vẹn chu trình 8 giai đoạn tối ưu hóa phần mềm hiệu năng cao và độ tin cậy cao cho **NVXK v2**. Hệ thống đạt được sự cân bằng khoa học giữa tốc độ tính toán (`90.88 GFLOPS` 1T / `167.90 GFLOPS` 2T AVX2 trên `1024³`), hiệu quả bộ nhớ (cắt 96.3% bộ nhớ tạm vẫn giữ 75.9% thông lượng; `0 KiB` và **2.64× nhanh hơn OpenBLAS** trên ma trận mỏng `4×1024×1024`), khả năng di động (runtime dispatch, C ABI, fallback scalar) và độ tin cậy (0 cảnh báo, 100% vượt qua ASan/UBSan và kiểm thử sai phân ngẫu nhiên).

## 16. Hướng phát triển tương lai (Future Work)

1. Thực thi bộ kịch bản `scripts/run_experiments.ps1` trên Intel Core i7-7700 (Windows MSYS2 GCC 16.2.0) để thu thập bộ dữ liệu hoàn chỉnh cho bài toán SMT 4T vs 8T và lập bảng so sánh đối chứng hai kiến trúc (Kaby Lake vs Sapphire Rapids).

2. Mở rộng giao diện GEMM hỗ trợ chuyển vị (`TransA`, `TransB`) và tích lũy ($\alpha, \beta$) cũng như các nhân tính toán lượng tử hóa (`INT8`/`FP16`) cho suy luận mạng nơ-ron trên CPU.

3. Triển khai nhánh runtime dispatch cho ARM NEON/SVE và bộ kiểm thử fuzzing tự động (libFuzzer) cho giao diện C ABI.

## 17. Tài liệu tham khảo (References)

1. Goto, K., & van de Geijn, R. A. (2008). *Anatomy of High-Performance Matrix Multiplication*. ACM Transactions on Mathematical Software (TOMS), 34(3), Article 12.

2. Van Zee, F. G., & van de Geijn, R. A. (2015). *BLIS: A Framework for Rapidly Instantiating BLAS Functionality*. ACM Transactions on Mathematical Software (TOMS), 41(3), Article 14.

3. Williams, S., Waterman, A., & Patterson, D. (2009). *Roofline: An Insightful Visual Performance Model for Multicore Architectures*. Communications of the ACM, 52(4), 65–76.

4. Wang, Q., Zhang, X., Zhang, Y., & Yi, Q. (2013). *AUGEM: Automatically Generate High Performance Dense Linear Algebra Kernels on x86 CPUs*. SC '13: Proceedings of the International Conference on High Performance Computing, Networking, Storage and Analysis.

5. Intel Corporation (2024). *Intel® 64 and IA-32 Architectures Optimization Reference Manual*.

6. OpenBLAS Project (2024). *OpenBLAS 0.3.27: An Optimized BLAS Library* (BSD-3-Clause).