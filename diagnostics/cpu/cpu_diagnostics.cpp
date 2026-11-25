#include "cpu_diagnostics.hpp"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <mutex>
#include <set>
#include <sstream>
#include <thread>

#if defined(_OPENMP)
#include <omp.h>
#endif

#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#endif

namespace nvxk_wb {

namespace {

std::mutex& exec_mutex() {
    static std::mutex m;
    return m;
}

std::set<std::string>& executed_features() {
    static std::set<std::string> s;
    return s;
}

bool is_feature_executed(const std::string& name) {
    std::lock_guard<std::mutex> lock(exec_mutex());
    return executed_features().count(name) > 0;
}

#if defined(__linux__)
struct CpuTimes {
    uint64_t idle = 0;
    uint64_t total = 0;
};

bool read_linux_proc_stat(CpuTimes& agg, std::vector<CpuTimes>& cores) {
    std::ifstream in("/proc/stat");
    if (!in) return false;
    cores.clear();
    std::string line;
    bool got_agg = false;
    while (std::getline(in, line)) {
        if (line.rfind("cpu", 0) != 0) break;
        std::istringstream iss(line);
        std::string label;
        iss >> label;
        uint64_t user = 0, nice = 0, sys = 0, idle = 0, iowait = 0, irq = 0, softirq = 0, steal = 0;
        iss >> user >> nice >> sys >> idle >> iowait >> irq >> softirq >> steal;
        CpuTimes ct;
        ct.idle = idle + iowait;
        ct.total = user + nice + sys + idle + iowait + irq + softirq + steal;
        if (label == "cpu") {
            agg = ct;
            got_agg = true;
        } else if (label.size() > 3 && std::isdigit(static_cast<unsigned char>(label[3]))) {
            cores.push_back(ct);
        }
    }
    return got_agg;
}

void read_linux_memory(SystemTelemetrySnapshot& snap) {
    std::ifstream st("/proc/self/status");
    std::string line;
    while (std::getline(st, line)) {
        if (line.rfind("VmRSS:", 0) == 0) {
            std::istringstream iss(line.substr(6));
            size_t kb = 0;
            iss >> kb;
            snap.process_rss_bytes = kb * 1024;
            snap.memory_available = true;
        } else if (line.rfind("VmHWM:", 0) == 0) {
            std::istringstream iss(line.substr(6));
            size_t kb = 0;
            iss >> kb;
            snap.process_peak_rss_bytes = kb * 1024;
        }
    }
    std::ifstream mi("/proc/meminfo");
    while (std::getline(mi, line)) {
        if (line.rfind("MemTotal:", 0) == 0) {
            std::istringstream iss(line.substr(9));
            size_t kb = 0;
            iss >> kb;
            snap.system_total_ram_bytes = kb * 1024;
        } else if (line.rfind("MemAvailable:", 0) == 0) {
            std::istringstream iss(line.substr(13));
            size_t kb = 0;
            iss >> kb;
            snap.system_avail_ram_bytes = kb * 1024;
        }
    }
}
#endif

}  // namespace

void mark_feature_executed(const std::string& feature_name) {
    std::lock_guard<std::mutex> lock(exec_mutex());
    executed_features().insert(feature_name);
}

SystemTelemetrySnapshot sample_system_telemetry() {
    SystemTelemetrySnapshot snap;

#if defined(__linux__)
    read_linux_memory(snap);

    static std::mutex stat_mtx;
    static bool has_prev = false;
    static CpuTimes prev_agg{};
    static std::vector<CpuTimes> prev_cores;
    static auto prev_time = std::chrono::steady_clock::now();

    std::lock_guard<std::mutex> lock(stat_mtx);
    CpuTimes cur_agg{};
    std::vector<CpuTimes> cur_cores;
    const auto now = std::chrono::steady_clock::now();
    if (read_linux_proc_stat(cur_agg, cur_cores)) {
        if (!has_prev) {
            // Brief 25 ms delta on first sample so we report a real measured value immediately
            prev_agg = cur_agg;
            prev_cores = cur_cores;
            prev_time = now;
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
            read_linux_proc_stat(cur_agg, cur_cores);
            has_prev = true;
        }
        const auto cur_now = std::chrono::steady_clock::now();
        const double win_ms = std::chrono::duration<double, std::milli>(cur_now - prev_time).count();
        const uint64_t d_total = cur_agg.total - prev_agg.total;
        const uint64_t d_idle = cur_agg.idle - prev_agg.idle;
        if (d_total > 0) {
            snap.cpu_util_available = true;
            snap.cpu_util_percent = 100.0 * static_cast<double>(d_total - d_idle) / static_cast<double>(d_total);
            snap.sampling_window_ms = win_ms;
        }
        if (cur_cores.size() == prev_cores.size()) {
            for (size_t i = 0; i < cur_cores.size(); ++i) {
                const uint64_t dt = cur_cores[i].total - prev_cores[i].total;
                const uint64_t di = cur_cores[i].idle - prev_cores[i].idle;
                const double pct = (dt > 0) ? (100.0 * static_cast<double>(dt - di) / static_cast<double>(dt)) : 0.0;
                snap.per_core_util_percent.push_back(pct);
            }
        }
        prev_agg = cur_agg;
        prev_cores = cur_cores;
        prev_time = cur_now;
    }
#elif defined(_WIN32)
    PROCESS_MEMORY_COUNTERS pmc{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        snap.memory_available = true;
        snap.process_rss_bytes = static_cast<size_t>(pmc.WorkingSetSize);
        snap.process_peak_rss_bytes = static_cast<size_t>(pmc.PeakWorkingSetSize);
    }
    MEMORYSTATUSEX ms{};
    ms.dwLength = sizeof(ms);
    if (GlobalMemoryStatusEx(&ms)) {
        snap.system_total_ram_bytes = static_cast<size_t>(ms.ullTotalPhys);
        snap.system_avail_ram_bytes = static_cast<size_t>(ms.ullAvailPhys);
    }
    static std::mutex win_mtx;
    static bool has_prev = false;
    static ULONGLONG prev_idle = 0, prev_kern = 0, prev_user = 0;
    static auto prev_time = std::chrono::steady_clock::now();

    std::lock_guard<std::mutex> lock(win_mtx);
    auto to_u64 = [](const FILETIME& ft) -> ULONGLONG {
        return (static_cast<ULONGLONG>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
    };
    FILETIME ft_i{}, ft_k{}, ft_u{};
    if (GetSystemTimes(&ft_i, &ft_k, &ft_u)) {
        ULONGLONG ci = to_u64(ft_i), ck = to_u64(ft_k), cu = to_u64(ft_u);
        if (!has_prev) {
            prev_idle = ci; prev_kern = ck; prev_user = cu;
            prev_time = std::chrono::steady_clock::now();
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
            GetSystemTimes(&ft_i, &ft_k, &ft_u);
            ci = to_u64(ft_i); ck = to_u64(ft_k); cu = to_u64(ft_u);
            has_prev = true;
        }
        const auto now = std::chrono::steady_clock::now();
        const ULONGLONG di = ci - prev_idle;
        const ULONGLONG dt = (ck - prev_kern) + (cu - prev_user);
        if (dt > 0) {
            snap.cpu_util_available = true;
            snap.cpu_util_percent = 100.0 * static_cast<double>(dt - di) / static_cast<double>(dt);
            snap.sampling_window_ms = std::chrono::duration<double, std::milli>(now - prev_time).count();
        }
        prev_idle = ci; prev_kern = ck; prev_user = cu;
        prev_time = now;
    }
#endif
    return snap;
}

DiagnosticsReport collect_diagnostics_report() {
    DiagnosticsReport rep;
    rep.profile = XK_get_active_profile();

#if defined(_WIN32)
    rep.os_name = "Windows";
#elif defined(__linux__)
    rep.os_name = "Linux";
#elif defined(__APPLE__)
    rep.os_name = "macOS";
#else
    rep.os_name = "Unknown OS";
#endif

#if defined(__clang__)
    rep.compiler_name = "Clang " + std::to_string(__clang_major__) + "." +
                        std::to_string(__clang_minor__) + "." +
                        std::to_string(__clang_patchlevel__);
#elif defined(__GNUC__)
    rep.compiler_name = "GCC " + std::to_string(__GNUC__) + "." +
                        std::to_string(__GNUC_MINOR__) + "." +
                        std::to_string(__GNUC_PATCHLEVEL__);
#elif defined(_MSC_VER)
    rep.compiler_name = "MSVC " + std::to_string(_MSC_VER);
#else
    rep.compiler_name = "Unknown Compiler";
#endif

    rep.cpp_standard = "C++" + std::to_string(__cplusplus / 100 % 100);

#if defined(_OPENMP)
    rep.openmp_enabled = true;
    rep.openmp_max_threads = omp_get_max_threads();
#else
    rep.openmp_enabled = false;
    rep.openmp_max_threads = 1;
#endif

    const bool compiler_x86_gnu =
#if (defined(__GNUC__) || defined(__clang__)) && (defined(__i386__) || defined(__x86_64__))
        true;
#else
        false;
#endif

    const bool forced_scalar =
#if defined(XK_FORCE_SCALAR_FALLBACK)
        true;
#else
        false;
#endif

    const bool binary_avx2 = compiler_x86_gnu && !forced_scalar;
    const bool binary_asm =
#if (defined(__GNUC__) || defined(__clang__)) && defined(__x86_64__) && !defined(XK_FORCE_SCALAR_FALLBACK)
        true;
#else
        false;
#endif

    const bool rt_avx2 = XK_has_avx2_fma();
    const bool rt_asm = XK_has_avx2_asm_kernel();

    // Build 5-stage ISA matrix strictly without collapsing stages
    rep.isa_matrix.push_back({
        "AVX2 (256-bit SIMD)",
        rep.profile.hw.has_avx2,
        compiler_x86_gnu,
        binary_avx2,
        rt_avx2,
        is_feature_executed("AVX2"),
        forced_scalar ? "Disabled at compile time via XK_FORCE_SCALAR_FALLBACK"
                      : "Runtime-dispatched via __attribute__((target(\"avx2,fma\")))"
    });

    rep.isa_matrix.push_back({
        "FMA3 (Fused Multiply-Add)",
        rep.profile.hw.has_fma,
        compiler_x86_gnu,
        binary_avx2,
        rt_avx2,
        is_feature_executed("FMA"),
        "Paired with AVX2 in BLIS 6x16 / 6x8 and Avx2ThinM kernels"
    });

    rep.isa_matrix.push_back({
        "x86-64 Inline ASM 6x16",
        rep.profile.hw.has_avx2 && rep.profile.hw.has_fma,
        binary_asm,
        binary_asm,
        rt_asm,
        is_feature_executed("ASM_6x16"),
        "Explicit ymm4..ymm15 accumulator allocation, 4x K-unrolled"
    });

    rep.isa_matrix.push_back({
        "AVX-512F (512-bit SIMD)",
        rep.profile.hw.has_avx512f,
        compiler_x86_gnu,
        false,
        false,
        false,
        "Not implemented in NVXK v2 (intentionally scoped to AVX2+FMA for i7-7700 parity)"
    });

    rep.isa_matrix.push_back({
        "OpenMP Threading",
        rep.profile.hw.logical_threads > 1,
        rep.openmp_enabled,
        rep.openmp_enabled,
        rep.openmp_enabled && rep.openmp_max_threads > 1,
        is_feature_executed("OpenMP"),
        rep.openmp_enabled ? ("OpenMP runtime active (max_threads=" + std::to_string(rep.openmp_max_threads) + ")")
                           : "Compiled without -fopenmp; single-threaded fallback"
    });

    rep.isa_matrix.push_back({
        "Portable Scalar IKJ",
        true,
        true,
        true,
        !rt_avx2,
        is_feature_executed("Scalar"),
        "Always available as fallback and reference kernel"
    });

    rep.telemetry = sample_system_telemetry();
    return rep;
}

}  // namespace nvxk_wb