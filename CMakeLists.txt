cmake_minimum_required(VERSION 3.16)
project(NVXK VERSION 5.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# ============================================================================
# Unified Subsystem & Build Options (NVXK Core always enabled)
# ============================================================================
option(NVXK_ENABLE_HIGHWAY "Enable Google Highway portable runtime-dispatched SIMD subsystem" ON)
option(NVXK_ENABLE_BLIS "Enable BLIS high-performance matrix subsystem (requires libblis)" OFF)
option(NVXK_ENABLE_HWLOC "Enable hwloc hardware topology & affinity subsystem" ON)
option(NVXK_ENABLE_OPENMP "Enable OpenMP multi-threading" ON)
option(NVXK_ENABLE_TESTS "Build unit, numerical, and cross-backend tests" ON)
option(NVXK_ENABLE_BENCHMARKS "Build CLI & benchmark executables" ON)
option(NVXK_BUILD_SHARED "Build shared C ABI library (libnvxk)" ON)
option(NVXK_BUILD_WORKBENCH "Build NVXK Engineering Workbench Server & Engine" ON)
option(NVXK_BUILD_DESKTOP_IMGUI "Build native Dear ImGui + ImPlot + GLFW Desktop App" OFF)
option(NVXK_FORCE_SCALAR_FALLBACK "Force portable scalar fallback (disable AVX2 dispatch)" OFF)
option(NVXK_PORTABLE_BINARY "Avoid host-specific -march=native so binaries run on older CPUs" ON)
option(NVXK_USE_OPENBLAS "Enable optional OpenBLAS comparison baseline" OFF)
option(NVXK_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(NVXK_ENABLE_SANITIZERS "Enable AddressSanitizer + UndefinedBehaviorSanitizer" OFF)
option(NVXK_ENABLE_LTO "Enable Link-Time Optimization (IPO/LTO)" OFF)

find_package(Threads REQUIRED)
if(NVXK_ENABLE_OPENMP)
    find_package(OpenMP)
endif()

if(MSVC)
    set(NVXK_OPT_FLAGS /O2 /W4)
    set(NVXK_TEST_FLAGS /O2 /W4)
    if(NVXK_WARNINGS_AS_ERRORS)
        list(APPEND NVXK_OPT_FLAGS /WX)
    endif()
else()
    set(NVXK_OPT_FLAGS -O3 -Wall -Wextra)
    set(NVXK_TEST_FLAGS -O2 -Wall -Wextra)
    if(NVXK_WARNINGS_AS_ERRORS)
        list(APPEND NVXK_OPT_FLAGS -Werror)
    endif()
    if(NVXK_ENABLE_SANITIZERS)
        list(APPEND NVXK_OPT_FLAGS -O1 -g -fsanitize=address,undefined)
        list(APPEND NVXK_TEST_FLAGS -O1 -g -fsanitize=address,undefined)
        add_link_options(-fsanitize=address,undefined)
    endif()
endif()

if(NVXK_ENABLE_LTO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT NVXK_IPO_SUPPORTED)
    if(NVXK_IPO_SUPPORTED)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
    endif()
endif()

# ============================================================================
# Optional Subsystems: Highway, BLIS, hwloc (Never mandatory for NVXK Core)
# ============================================================================
set(NVXK_SUBSYSTEM_LIBS "")
set(NVXK_SUBSYSTEM_DEFS "")
set(NVXK_SUBSYSTEM_INCS "")

if(NVXK_ENABLE_HIGHWAY)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/highway/CMakeLists.txt")
        set(HWY_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
        set(HWY_ENABLE_EXAMPLES OFF CACHE BOOL "" FORCE)
        set(HWY_ENABLE_CONTRIB OFF CACHE BOOL "" FORCE)
        add_subdirectory(third_party/highway EXCLUDE_FROM_ALL)
        list(APPEND NVXK_SUBSYSTEM_LIBS hwy)
        list(APPEND NVXK_SUBSYSTEM_DEFS NVXK_ENABLE_HIGHWAY=1)
        list(APPEND NVXK_SUBSYSTEM_INCS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/highway")
    else()
        find_package(hwy QUIET)
        if(hwy_FOUND)
            list(APPEND NVXK_SUBSYSTEM_LIBS hwy::hwy)
            list(APPEND NVXK_SUBSYSTEM_DEFS NVXK_ENABLE_HIGHWAY=1)
        else()
            message(WARNING "NVXK_ENABLE_HIGHWAY=ON but Highway not found; building with scalar vector fallback.")
        endif()
    endif()
endif()

if(NVXK_ENABLE_BLIS)
    find_package(BLIS QUIET)
    if(BLIS_FOUND)
        list(APPEND NVXK_SUBSYSTEM_LIBS BLIS::BLIS)
        list(APPEND NVXK_SUBSYSTEM_DEFS NVXK_ENABLE_BLIS=1)
    else()
        message(WARNING "NVXK_ENABLE_BLIS=ON but libblis not found (set NVXK_BLIS_ROOT); falling back to NVXK native kernels.")
    endif()
endif()

if(NVXK_ENABLE_HWLOC)
    find_package(HWLOC QUIET)
    if(HWLOC_FOUND)
        list(APPEND NVXK_SUBSYSTEM_LIBS HWLOC::HWLOC)
        list(APPEND NVXK_SUBSYSTEM_DEFS NVXK_ENABLE_HWLOC=1)
    else()
        message(STATUS "System libhwloc not found; NVXK will use native CPUID/OS topology fallback.")
    endif()
endif()

# ============================================================================
# Layer 1-3: NVXK Core + Hardware Intelligence + Computational Backends
# ============================================================================
set(NVXK_CORE_SRCS
    src/nvxk_api.cpp
    src/nvxk_hw.cpp
    src/nvxk_kernels_avx2.cpp
    src/nvxk_kernels_scalar.cpp
    src/nvxk_planner.cpp
    src/nvxk_workspace.cpp
    src/core/nvxk_config.cpp
    src/core/nvxk_dispatcher.cpp
    src/topology/nvxk_topology.cpp
    src/backends/highway/nvxk_highway_backend.cpp
    src/backends/blis/nvxk_blis_backend.cpp
)

add_library(nvxk_core STATIC ${NVXK_CORE_SRCS})
target_include_directories(nvxk_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${NVXK_SUBSYSTEM_INCS}
)
target_compile_options(nvxk_core PRIVATE ${NVXK_OPT_FLAGS})
target_compile_definitions(nvxk_core PRIVATE ${NVXK_SUBSYSTEM_DEFS})
target_link_libraries(nvxk_core PUBLIC Threads::Threads PRIVATE ${NVXK_SUBSYSTEM_LIBS})

if(OpenMP_CXX_FOUND)
    target_link_libraries(nvxk_core PUBLIC OpenMP::OpenMP_CXX)
endif()
if(NVXK_FORCE_SCALAR_FALLBACK)
    target_compile_definitions(nvxk_core PUBLIC XK_FORCE_SCALAR_FALLBACK)
endif()
if(NVXK_USE_OPENBLAS)
    find_package(BLAS REQUIRED)
    target_compile_definitions(nvxk_core PUBLIC NVXK_USE_OPENBLAS)
    target_link_libraries(nvxk_core PUBLIC ${BLAS_LIBRARIES})
endif()

if(NVXK_BUILD_SHARED)
    add_library(nvxk_shared SHARED ${NVXK_CORE_SRCS})
    set_target_properties(nvxk_shared PROPERTIES OUTPUT_NAME nvxk)
    target_include_directories(nvxk_shared PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${NVXK_SUBSYSTEM_INCS})
    target_compile_options(nvxk_shared PRIVATE ${NVXK_OPT_FLAGS})
    target_compile_definitions(nvxk_shared PRIVATE NVXK_BUILDING_DLL ${NVXK_SUBSYSTEM_DEFS})
    target_link_libraries(nvxk_shared PUBLIC Threads::Threads PRIVATE ${NVXK_SUBSYSTEM_LIBS})
    if(OpenMP_CXX_FOUND)
        target_link_libraries(nvxk_shared PUBLIC OpenMP::OpenMP_CXX)
    endif()
    if(NVXK_FORCE_SCALAR_FALLBACK)
        target_compile_definitions(nvxk_shared PUBLIC XK_FORCE_SCALAR_FALLBACK)
    endif()
endif()

# ============================================================================
# Layer 4 & 5: CLI, Benchmarks, Tests, Workbench & Desktop UI
# ============================================================================
if(NVXK_ENABLE_BENCHMARKS)
    add_executable(nvxk benchmarks/main.cpp)
    target_compile_options(nvxk PRIVATE ${NVXK_OPT_FLAGS})
    target_link_libraries(nvxk PRIVATE nvxk_core)

    add_executable(nvxk_cli src/cli/nvxk_cli.cpp)
    target_compile_options(nvxk_cli PRIVATE ${NVXK_OPT_FLAGS})
    target_link_libraries(nvxk_cli PRIVATE nvxk_core)

    add_executable(my_app apps/my_app.cpp)
    target_compile_options(my_app PRIVATE ${NVXK_OPT_FLAGS})
    target_link_libraries(my_app PRIVATE nvxk_core)
endif()

if(NVXK_ENABLE_TESTS)
    enable_testing()
    add_executable(nvxk-test tests/nvxk_test.cpp)
    target_compile_options(nvxk-test PRIVATE ${NVXK_TEST_FLAGS})
    target_link_libraries(nvxk-test PRIVATE nvxk_core)
    add_test(NAME nvxk_core_tests COMMAND nvxk-test)

    add_executable(nvxk-unified-test tests/unified_integration_test.cpp)
    target_compile_options(nvxk-unified-test PRIVATE ${NVXK_TEST_FLAGS})
    target_link_libraries(nvxk-unified-test PRIVATE nvxk_core)
    add_test(NAME nvxk_unified_integration_tests COMMAND nvxk-unified-test)
endif()

if(NVXK_BUILD_WORKBENCH)
    set(NVXK_WORKBENCH_ENGINE_SRCS
        adapters/core_adapter/nvxk_adapter.cpp
        diagnostics/cpu/cpu_diagnostics.cpp
        benchmark/runner/workbench_engine.cpp
        benchmark/insights/insights_engine.cpp
        benchmark/reports/report_exporter.cpp
        ui/server/http_server.cpp
    )
    add_library(nvxk_workbench_engine STATIC ${NVXK_WORKBENCH_ENGINE_SRCS})
    target_compile_options(nvxk_workbench_engine PRIVATE ${NVXK_OPT_FLAGS})
    target_link_libraries(nvxk_workbench_engine PUBLIC nvxk_core Threads::Threads)
    if(WIN32)
        target_link_libraries(nvxk_workbench_engine PUBLIC ws2_32 psapi)
    endif()

    add_executable(nvxk_workbench ui/server/main.cpp)
    target_compile_options(nvxk_workbench PRIVATE ${NVXK_OPT_FLAGS})
    target_link_libraries(nvxk_workbench PRIVATE nvxk_workbench_engine)

    if(NVXK_ENABLE_TESTS)
        add_executable(workbench-test tests/workbench_test.cpp)
        target_compile_options(workbench-test PRIVATE ${NVXK_TEST_FLAGS})
        target_link_libraries(workbench-test PRIVATE nvxk_workbench_engine)
        add_test(NAME nvxk_workbench_tests COMMAND workbench-test)
    endif()
endif()

if(NVXK_BUILD_DESKTOP_IMGUI AND NVXK_BUILD_WORKBENCH)
    include(FetchContent)
    find_package(OpenGL REQUIRED)
    set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(glfw GIT_REPOSITORY https://github.com/glfw/glfw.git GIT_TAG 3.4)
    FetchContent_Declare(imgui GIT_REPOSITORY https://github.com/ocornut/imgui.git GIT_TAG docking)
    FetchContent_Declare(implot GIT_REPOSITORY https://github.com/epezent/implot.git GIT_TAG v0.16)
    FetchContent_MakeAvailable(glfw imgui implot)

    add_library(imgui_implot STATIC
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
        ${implot_SOURCE_DIR}/implot.cpp
        ${implot_SOURCE_DIR}/implot_items.cpp
    )
    target_include_directories(imgui_implot PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends ${implot_SOURCE_DIR})
    target_link_libraries(imgui_implot PUBLIC glfw OpenGL::GL)

    add_executable(nvxk_desktop ui/imgui/main_imgui.cpp ui/imgui/theme.cpp ui/imgui/workbench_app.cpp)
    target_compile_options(nvxk_desktop PRIVATE ${NVXK_OPT_FLAGS})
    target_link_libraries(nvxk_desktop PRIVATE nvxk_workbench_engine imgui_implot)
    if(WIN32)
        target_link_libraries(nvxk_desktop PRIVATE opengl32 gdi32)
    endif()
endif()