# NVXK v3.0 Desktop & Web Workbench Guide

NVXK provides two user interfaces backed by the exact same C++17 `nvxk_workbench_engine`:

## 1. Offline Web Workbench (`nvxk_workbench.exe`)
- **Zero external dependencies**: Single-file offline HTML5/CSS3/JS (`ui/web/index.html`) served over loopback `http://127.0.0.1:8765`.
- **Launch**: `./nvxk_workbench.exe` (use `--port 8766` to pick another port). On Windows and Linux desktops with `DISPLAY`, it opens automatically in your default browser.
- **Features**:
  - **Main Dashboard**: Real CPU brand, physical/logical cores, L1/L2/L3 cache, ISA capabilities, active backend, and calibration status.
  - **Matrix Lab & Generalized BLAS Quick-Tester**: Run `FP32`/`FP64`/`INT32->INT64` GEMM and `RowMajor`/`ColMajor`, `NoTrans`/`Trans`, `alpha`/`beta` tests with `f64` reference verification.
  - **Benchmark Center**: Multi-backend async execution with progress, safe cancellation, Measurement Mode, CSV import, and CSV/Markdown report export.
  - **Truthful System Monitor**: Displays real OS memory/CPU metrics; unsupported hardware PMU/thermal sensors are explicitly marked `Unavailable` (never fabricated).
  - **UX & Persistence**: Dark/Light theme toggle (`localStorage`), keyboard shortcuts (`Ctrl+B` Benchmark, `Ctrl+K` Calibrate, `Alt+T` Theme, `?` Help, `Esc` Close), and live engine logs (`/api/logs`).

## 2. Native Desktop GUI (`nvxk_desktop.exe`)
- Built with Dear ImGui (Docking) + ImPlot v0.16 + GLFW 3.4 + OpenGL 3.3 via `-DNVXK_BUILD_DESKTOP_IMGUI=ON`.
- Calls `WorkbenchEngine` directly in-process (no HTTP overhead).
- Supports Dark/Light themes (`apply_dark_theme` / `apply_light_theme`), dynamic backend flags, and `nvxk_workbench.ini` config persistence.