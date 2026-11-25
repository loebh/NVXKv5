# Build NVXK Native Desktop App (Dear ImGui Docking + ImPlot + GLFW + OpenGL3)
# Target: Windows 10/11 + MSYS2 UCRT64 MinGW/GCC (Intel Core i7-7700 + Intel HD Graphics 630)
$ErrorActionPreference = "Stop"

Write-Host "=== Configuring NVXK Native Desktop Workbench (C++17 + Dear ImGui + ImPlot + GLFW) ==="
cmake -S . -B build_desktop -G "MinGW Makefiles" `
    -DCMAKE_BUILD_TYPE=Release `
    -DNVXK_BUILD_WORKBENCH=ON `
    -DNVXK_BUILD_DESKTOP_IMGUI=ON

Write-Host "=== Building nvxk_desktop.exe and tests ==="
cmake --build build_desktop -j

Write-Host "=== Running NVXK Core & Workbench Integration Tests ==="
ctest --test-dir build_desktop --output-on-failure

Write-Host "=== Done! Launch native desktop app with: .\build_desktop\nvxk_desktop.exe ==="