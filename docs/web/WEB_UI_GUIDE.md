# Phần V: Hướng dẫn Sử dụng Web UI (NVXK Web Workbench)

> **Xác minh từ Mã nguồn (`ui/web/index.html` & `ui/server/`)**: Giao diện Web UI của NVXK được viết hoàn toàn bằng **HTML5 + CSS3 + Vanilla JavaScript thuần** trong **duy nhất 1 tệp `ui/web/index.html`** và phục vụ trực tiếp bởi máy chủ nhúng C++17 `nvxk_workbench.exe`. Hệ thống **KHÔNG sử dụng Node.js, npm, React, Vite hay bất kỳ thư viện CDN bên ngoài nào** — hoạt động **100% offline** ngay sau khi biên dịch C++.

## 1. Triển khai Web Workbench (Web Deployment)

### 1.1. Biên dịch và Khởi chạy
Vì không cần Node.js hay cài gói npm, quy trình triển khai chỉ gồm biên dịch C++17 và chạy thực thi:

```powershell
# Windows (PowerShell / MSYS2 UCRT64)
mingw32-make workbench
.\nvxk_workbench.exe --port 8765 --ui-dir ui/web --results-dir results

# Linux
make workbench
./nvxk_workbench.exe --port 8765 --ui-dir ui/web --results-dir results
```

- **Trình duyệt**: Trên Windows, `nvxk_workbench.exe` tự gọi `ShellExecuteA` mở `http://127.0.0.1:8765/`. Trên Linux (hoặc khi dùng `--no-browser`), mở trình duyệt (Edge, Chrome, Firefox) và truy cập thủ công `http://127.0.0.1:8765/`.
- **Kiểm tra deployment**: Gọi kiểm tra API trạng thái:

```bash
curl -s http://127.0.0.1:8765/api/state | head -c 200
```
Kết quả mong đợi: Chuỗi JSON bắt đầu bằng `{"diagnostics":{"os_name":...`.

### 1.2. Phân biệt các Môi trường Triển khai

| Môi trường | Cách vận hành thực tế trong NVXK | Lưu ý kỹ thuật bắt buộc |
| :--- | :--- | :--- |
| **Development** | Chạy `nvxk_workbench.exe` trực tiếp tại thư mục gốc; chỉnh sửa `ui/web/index.html` và bấm `F5` (tải lại ngay vì server đọc tệp từ đĩa ở mỗi request `GET /` kèm header `Cache-Control: no-store`). | Không cần build lại C++ khi chỉ sửa HTML/CSS/JS. |
| **Production (Local)** | Biên dịch Release (`-O3`), chạy `nvxk_workbench.exe --port 8765 --ui-dir ui/web --results-dir results`. | Tắt các ứng dụng nặng khác và bật **Measurement Mode** khi đo hiệu năng. |
| **Localhost (`127.0.0.1`)** | Chế độ mặc định duy nhất được hỗ trợ trực tiếp (`INADDR_LOOPBACK` trong `ui/server/http_server.cpp`). | An toàn tuyệt đối trên máy cá nhân, không mở cổng ra mạng ngoài. |
| **LAN (Mạng nội bộ)** | **Không hỗ trợ trực tiếp** do `http_server.cpp` cố định `htonl(INADDR_LOOPBACK)` và không có lớp Authentication. | Nếu cần xem từ máy khác trong LAN (chỉ trong mạng tin cậy), dùng Reverse Proxy cục bộ (Nginx/Caddy) có mật khẩu trỏ về `127.0.0.1:8765`. |
| **Remote Server** | Chạy `./nvxk_workbench.exe --no-browser --port 8765` trên máy chủ Linux và chuyển tiếp cổng an toàn qua SSH Tunnel. | Trên máy cá nhân chạy: `ssh -L 8765:127.0.0.1:8765 user@remote-host`, sau đó mở `http://127.0.0.1:8765/`. |

## 2. Tham quan Giao diện Web (`ui/web/index.html`)

Giao diện gồm **Top Bar** (chọn Session, Backend, Precision, Mode, nút `Calibrate`, `Benchmark`, `Refresh`, `Menu`), **Sidebar 12 mục** (có thể thu gọn), **Job Banner** (thanh tiến trình + nút `Cancel`) và **12 trang chức năng thực tế**:

1. **Overview (`#view-overview`)**: 5 thẻ KPI có biểu đồ tia (sparkline) gồm *CPU Utilization*, *Throughput*, *Execution Latency*, *Memory Performance*, *Numerical Accuracy*; biểu đồ Canvas tương tác (di chuột xem tooltip, chọn 6 chỉ số, nút **Export PNG**); danh sách **Engineering Insights** theo mức độ tin cậy; bảng **Top Functions** có sắp xếp cột, ô lọc tìm kiếm, nút **Import Historical CSV** và hộp **Inspector** chi tiết khi click dòng.
2. **Vector & Matrix Lab (`#view-matrix_lab`)**: Chọn `GEMM` hoặc `Vector Add`, chọn `FP32` / `FP64` / `INT32 → INT64`, chọn Backend, nhập `M, K, N, Threads, Seed`. Với ma trận nhỏ `<= 6×6`, giao diện tự sinh **lưới ô nhập số trực tiếp** cho `A` và `B` (có nút `Randomize Small Grid`). Bấm `Execute Operation` để xem ma trận màu `C` (tối đa `16×16` downsampled) và báo cáo kiểm chứng `f64`.
3. **Execution Intelligence (`#view-execution`)**: Hiển thị cảnh báo minh bạch `[CONCEPTUAL + INSTRUMENTED]`, sơ đồ thanh ghi AVX2 256-bit minh họa hình học `micro_6x16_full_asm_f32` (khái niệm) bên cạnh công cụ truy vấn sống `XK_plan_gemm()` (`POST /api/plan`).
4. **CPU Architecture (`#view-cpu_arch`)**: 3 thẻ tóm tắt CPU/Cache/Roofline Profile và bảng **Ma trận 5 tầng ISA** (*CPU Supports → Compiler Supports → Binary Contains → Runtime Selected → Execution Verified*).
5. **Performance Benchmark (`#view-benchmark`)**: Preset 6 hình dạng sẵn (`128³`, `256³`, `512³`, `4×1024²`, `1×1024²`, `64×512²`) hoặc tùy chỉnh (`M, K, N`), chọn Precision, Runs, Manual OMP Threads, Budget KiB, checkboxes 8 backend, checkboxes Measurement Mode / Cold-Cache / Validate, nút `Launch Benchmark Session`, `Cancel Running`, `Clear Session History` và `Import Historical CSV`.
6. **Top Functions (`#view-top_functions`)**: Bảng đầy đủ 13 cột kèm bộ lọc theo Backend và ô tìm kiếm.
7. **Kernel Analysis (`#view-kernel_analysis`)**: Bảng danh mục 8 backend thực tế trong NVXK kèm tình trạng `AVAILABLE`/`UNAVAILABLE`.
8. **Memory Analysis (`#view-memory`)**: 3 thẻ RSS/Bandwidth/Workspace, thanh so sánh Working Set với dung lượng L1d/L2/L3 và ghi chú trung thực `HW CACHE COUNTERS: UNAVAILABLE`.
9. **Numerical Reliability (`#view-reliability`)**: Chọn Precision + Backend, bấm `Run 7-Shape Edge-Case Suite` (`POST /api/reliability/run`), xem bảng 7 hình dạng biên và bản đồ nhiệt sai số (`Error Heatmap`).
10. **Diagnostics (`#view-diagnostics`)**: Thông tin OS, Compiler, C++ Standard, OpenMP, CPUID và kiến trúc hệ thống.
11. **Reports (`#view-reports`)**: Nút `Generate Markdown Preview`, `Download Markdown` (`/api/report/markdown`), `Download CSV` (`/api/report/csv`).
12. **Settings (`#view-settings`)**: Cài đặt chu kỳ poll telemetry (`500..30000 ms`, mặc định `2500 ms`) + nút `Apply Settings`.
- **Responsive Layout**: CSS sử dụng Grid media queries (`@media (max-width: 1280px)` chuyển KPI từ 5 cột xuống 3 cột; `@media (max-width: 1120px)` xếp chồng biểu đồ và Insights; `@media (max-width: 1000px)` chuyển lưới 2/3 cột thành 1 cột).

## 3. Xử lý Sự cố Web UI (23 Tình huống Chuẩn hóa)

| # | Tình huống | Dấu hiệu | Nguyên nhân | Cách kiểm tra & Khắc phục | Xác nhận đã sửa |
| :--- | :--- | :--- | :--- | :--- | :--- |
| 1 | **Không truy cập được localhost** | Trình duyệt báo `ERR_CONNECTION_REFUSED`. | `nvxk_workbench.exe` chưa chạy hoặc đã tắt. | Kiểm tra terminal chạy server; khởi chạy lại `.\nvxk_workbench.exe --port 8765`. | Terminal in `Listening on http://127.0.0.1:8765/` và trang mở thành công. |
| 2 | **Trang không tải (treo mãi)** | Tab trình duyệt xoay vòng mãi. | Proxy hệ thống chặn `127.0.0.1` hoặc tường lửa chặn loopback. | Thử `http://127.0.0.1:8765/` (không dùng `localhost`), tắt proxy cho `127.0.0.1`. | `curl -I http://127.0.0.1:8765/` trả về `HTTP/1.1 200 OK`. |
| 3 | **Màn hình trắng** | Trang trống trơn, nền trắng. | Trình duyệt quá cũ không hỗ trợ ES6 `async/await` hoặc file `index.html` hỏng. | Mở DevTools (`F12` → Console) xem lỗi cú pháp; khôi phục `ui/web/index.html` từ Git (`git checkout ui/web/index.html`). | Hiện giao diện nền tối `#111315` và Top Bar `NVXK`. |
| 4 | **HTTP 404** | Trình duyệt hiện `UI file not found at ui/web/index.html`. | Chạy `nvxk_workbench.exe` từ thư mục con (ví dụ `build/`) làm sai đường dẫn tương đối `ui/web`. | Đứng tại thư mục gốc `NVXK` hoặc truyền đường dẫn tuyệt đối `--ui-dir C:\dev\NVXK\ui\web`. | Trang chủ tải trả mã `200 OK`. |
| 5 | **HTTP 403** | Trình duyệt báo Forbidden (nếu qua Reverse Proxy). | Cấu hình proxy chặn truy cập hoặc sai quyền đọc file trên Linux. | Kiểm tra quyền file `chmod 644 ui/web/index.html` và cấu hình proxy. | `GET /` trả `200 OK`. |
| 6 | **HTTP 500** | Request trả lỗi server (hoặc đóng kết nối đột ngột). | Ngoại lệ không bắt được hoặc cạn RAM khi cấp phát ma trận quá lớn. | Giảm kích thước `M, K, N` xuống `<= 1024`; kiểm tra log terminal server. | `/api/state` trả JSON hợp lệ với mã `200`. |
| 7 | **API request thất bại (`409 Conflict` / `400 Bad Request`)** | Hộp thoại `alert(...)` báo lỗi benchmark hoặc Matrix Lab. | `409`: Đang có benchmark khác chạy; `400`: Kích thước `<= 0` hoặc vượt giới hạn (`> 2048` ở Lab, `> 4096` ở Benchmark). | Chờ job xong (hoặc bấm `Cancel`); nhập lại kích thước hợp lệ. | Request trả `{"ok":true}`. |
| 8 | **CORS error** | Console báo `blocked by CORS policy`. | Mở `index.html` trực tiếp bằng `file://` hoặc gọi từ trang web ở port khác (`http_server.cpp` không gửi header CORS). | Luôn truy cập đúng qua `http://127.0.0.1:8765/` (cùng nguồn với API). | Không còn lỗi CORS trong DevTools Console. |
| 9 | **Backend offline** | Giao diện đang mở nhưng bấm nút không có tác dụng, `fetch` lỗi mạng. | Cửa sổ terminal chứa `nvxk_workbench.exe` đã bị đóng hoặc bấm `Ctrl+C`. | Khởi động lại `nvxk_workbench.exe` rồi bấm `Refresh`. | Chấm xanh `Idle • NVXK Core Ready` hoạt động lại. |
| 10 | **WebSocket disconnected** | Người dùng tìm lỗi WebSocket trong Network tab. | **Không áp dụng**: NVXK Web UI dùng **HTTP Polling** (`/api/telemetry` mỗi 2.5 s), **không dùng WebSocket**. | Kiểm tra tab Network lọc `telemetry`; đảm bảo trạng thái `200`. | `/api/telemetry` trả về định kỳ `200 OK`. |
| 11 | **JavaScript runtime error** | Một nút hoặc biểu đồ ngừng hoạt động, Console hiện `TypeError`. | Extension trình duyệt can thiệp DOM hoặc JSON trả về bị cắt cụt. | Thử mở chế độ Ẩn danh (Incognito) không có extension; kiểm tra phản hồi `/api/state`. | Console không còn lỗi đỏ. |
| 12 | **CSS không tải / vỡ bố cục** | Chữ dính sát, mất màu nền tối. | Tệp `ui/web/index.html` bị sửa lỗi thẻ `<style>` (CSS nằm nội tuyến trong `index.html`). | Kiểm tra `git diff ui/web/index.html` và khôi phục bản gốc. | Giao diện Dark-Graphite hiển thị đúng chuẩn. |
| 13 | **Chart không hiển thị** | Khung *Performance Telemetry* trống hoặc chỉ có chữ *"Awaiting benchmark..."*. | Chưa có phiên benchmark nào chạy hoặc `canvas` có kích thước 0 khi đổi tab. | Bấm nút `Benchmark` ở Top Bar; nếu vừa thu gọn cửa sổ, bấm `Refresh`. | Biểu đồ cột GFLOPS hiển thị kèm giá trị số trên đỉnh cột. |
| 14 | **Dữ liệu không cập nhật** | Số liệu CPU% đứng yên trong lúc benchmark chạy. | **Hành vi chủ đích của Measurement Mode**: cả frontend và backend tạm ngưng lấy mẫu `/api/telemetry` khi đang đo. | Đợi benchmark hoàn tất (dữ liệu sẽ tự làm mới) hoặc chuyển Mode sang `Normal (Live)` nếu chỉ cần quan sát. | Khi job kết thúc, bảng và biểu đồ tự cập nhật. |
| 15 | **Loading vô hạn** | Banner tiến trình kẹt ở một mức `%` rất lâu. | Chạy ma trận quá lớn (`4096³`) với `naive` hoặc `ikj` mất hàng phút. | Bấm nút `Cancel` (màu đỏ) trên banner; thử lại với kích thước nhỏ hơn (`256³` hoặc `512³`) và bỏ chọn `naive`. | Banner biến mất, trạng thái về `Idle`. |
| 16 | **Form không gửi được** | Bấm `Launch Benchmark Session` báo lỗi *"At least one backend must be selected"*. | Tất cả ô checkbox backend đã bị bỏ chọn hoặc bị disabled. | Đánh dấu ít nhất 1 backend khả dụng (ví dụ `ikj`, `auto_asm`). | Banner tiến trình xuất hiện ngay khi bấm. |
| 17 | **Không xuất được CSV / Markdown** | Bấm `Download CSV` tải về tệp chỉ có dòng tiêu đề. | Lịch sử benchmark hiện đang trống (`history` rỗng). | Chạy ít nhất 1 phiên benchmark (hoặc bấm `Import Historical CSV`) trước khi tải. | Tệp `nvxk_benchmark_results.csv` có đầy đủ dòng số liệu. |
| 18 | **Không lưu được Settings** | Đổi `Poll Interval` xong tải lại trang (`F5`) bị quay về `2500 ms`. | Thiết kế Web UI lưu `state.pollMs` trong biến JS bộ nhớ tạm (không ghi `localStorage`). | Chấp nhận nhập lại sau khi tải lại trang, hoặc giữ nguyên tab không `F5`. | Kiểm tra Network tab thấy chu kỳ `/api/telemetry` đổi theo giá trị vừa bấm `Apply Settings`. |
| 19 | **Browser không tương thích** | Lỗi `Optional chaining` hoặc Canvas mờ. | Trình duyệt quá cũ (IE11, Chrome < 80). | Dùng Chrome 100+, Microsoft Edge 100+ hoặc Firefox 100+. | Toàn bộ 12 tab và biểu đồ hoạt động bình thường. |
| 20 | **Sai tham số dòng lệnh / đường dẫn** | Import CSV trả về `imported: 0`. | Sai `--results-dir` hoặc thư mục `results/` không chứa tệp CSV tương ứng. | Khởi chạy từ thư mục gốc với `--results-dir results` và kiểm tra tệp tồn tại trong `results/`. | Nút `Import Historical CSV` nạp được các dòng vào bảng Top Functions. |
| 21 | **Port bị chiếm** | Server báo `bind() failed on 127.0.0.1:8765`. | Tiến trình `nvxk_workbench.exe` cũ chưa tắt hoặc ứng dụng khác dùng port 8765. | Windows: `netstat -ano \| findstr 8765` để kiểm tra; chạy lại với `--port 8766`. | Server lắng nghe trên `http://127.0.0.1:8766/`. |
| 22 | **Cache cũ của trình duyệt** | Sửa `index.html` nhưng trình duyệt vẫn hiện giao diện cũ. | Dù server gửi `Cache-Control: no-store`, một số trình duyệt vẫn giữ bộ nhớ đệm bộ nhớ. | Nhấn `Ctrl + Shift + R` (Hard Reload). | Giao diện mới hiển thị ngay. |
| 23 | **Frontend và Backend không cùng phiên bản** | Một số trường hiện `undefined` trong bảng. | `nvxk_workbench.exe` là bản biên dịch cũ nhưng `--ui-dir` trỏ tới `index.html` mới (hoặc ngược lại). | Chạy `mingw32-make clean && mingw32-make workbench` để đồng bộ binary với mã nguồn hiện tại. | `/api/state` khớp toàn bộ trường JSON và hiển thị sạch `undefined`. |