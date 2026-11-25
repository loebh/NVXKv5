# Hệ thống Tài liệu Kỹ thuật Hoàn chỉnh NVXK (Documentation Hub)

Bộ tài liệu này được xây dựng dựa trên kiểm toán toàn bộ 43 tệp mã nguồn của dự án **NVXK v2** (*Native Vector eXecution Kernels* + *NVXK Engineering Intelligence Workbench*).

## Cấu trúc Thư mục `docs/`
```text
docs/
├── README.md                                 # Chỉ mục trung tâm này
├── CHANGELOG.md                              # Lịch sử cập nhật hệ thống tài liệu
├── QA_REPORT.md                              # Báo cáo Đảm bảo Chất lượng (Phần XIV)
├── audit/SOURCE_CODE_AUDIT.md                # Kiểm toán mã nguồn & 10 điểm không thống nhất (Phần I)
├── user-manual/COMPLETE_USER_MANUAL.md       # Cẩm nang tổng thể, 12 Workflows & Quick Start (Phần X–XI)
├── installation/INSTALLATION_GUIDE.md        # Cài đặt từ con số 0 & Tương thích i7-7700 (Phần II)
├── desktop/DESKTOP_UI_GUIDE.md               # Hướng dẫn Desktop UI ImGui & 17 lỗi (Phần IV)
├── web/WEB_UI_GUIDE.md                       # Hướng dẫn Web UI Offline & 23 lỗi (Phần V)
├── cli/CLI_COMMAND_REFERENCE.md              # Sổ tay toàn bộ lệnh CLI 12 nhóm (Phần III)
├── api/API_REFERENCE.md                      # Kiến trúc Backend, REST, C++11 & C ABI (Phần VI–VII)
├── testing/TESTING_AND_BENCHMARK_GUIDE.md    # 10 loại kiểm thử & Kết quả thực nghiệm (Phần VIII)
├── troubleshooting/TROUBLESHOOTING_MASTER_DB.md # Cơ sở dữ liệu 17 danh mục lỗi (Phần IX)
├── assets/                                   # 3 biểu đồ hiệu năng PNG 220 DPI từ results/*.csv
└── scripts/                                  # Script sinh biểu đồ & biên dịch 8 tệp PDF
```

## Tái sinh Toàn bộ 8 Tệp PDF từ Mã nguồn Markdown
```bash
python3 docs/scripts/generate_charts.py
python3 docs/scripts/build_pdfs.py
```