#include <nvxk.h>
#include <nvxk/topology.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#endif

#if defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64))
#include <intrin.h>
#define XK_X86_CPUID 1
#elif (defined(__GNUC__) || defined(__clang__)) && \
    (defined(__i386__) || defined(__x86_64__))
#include <cpuid.h>
#define XK_X86_CPUID 1
#else
#define XK_X86_CPUID 0
#endif

#if defined(_OPENMP)
#include <omp.h>
#endif

namespace {

#if XK_X86_CPUID
void xk_cpuid_ex(uint32_t leaf, uint32_t subleaf, uint32_t regs[4]) {
#if defined(_MSC_VER)
    int tmp[4] = {0, 0, 0, 0};
    __cpuidex(tmp, static_cast<int>(leaf), static_cast<int>(subleaf));
    for (int i = 0; i < 4; ++i) regs[i] = static_cast<uint32_t>(tmp[i]);
#else
    unsigned int a = 0, b = 0, c = 0, d = 0;
    __cpuid_count(leaf, subleaf, a, b, c, d);
    regs[0] = a;
    regs[1] = b;
    regs[2] = c;
    regs[3] = d;
#endif
}

uint64_t xk_xgetbv(uint32_t index) {
#if defined(_MSC_VER)
    return _xgetbv(index);
#else
    uint32_t eax = 0, edx = 0;
    __asm__ __volatile__("xgetbv" : "=a"(eax), "=d"(edx) : "c"(index));
    return (static_cast<uint64_t>(edx) << 32) | eax;
#endif
}
#endif

std::string trim_spaces(std::string s) {
    const size_t first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    const size_t last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
}

int detect_physical_cores(int logical_threads) {
#if defined(_WIN32)
    DWORD len = 0;
    GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &len);
    if (len > 0) {
        std::vector<char> buf(len);
        if (GetLogicalProcessorInformationEx(
                RelationProcessorCore,
                reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buf.data()),
                &len)) {
            int cores = 0;
            size_t offset = 0;
            while (offset < len) {
                const auto* info =
                    reinterpret_cast<const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(
                        buf.data() + offset);
                if (info->Relationship == RelationProcessorCore) ++cores;
                offset += info->Size;
            }
            if (cores > 0) return cores;
        }
    }
#elif defined(__linux__)
    std::ifstream cpuinfo("/proc/cpuinfo");
    if (cpuinfo) {
        std::vector<std::pair<int, int>> pairs;
        int phys_id = -1;
        int core_id = -1;
        std::string line;
        while (std::getline(cpuinfo, line)) {
            if (line.empty()) {
                if (phys_id >= 0 && core_id >= 0) {
                    const std::pair<int, int> key(phys_id, core_id);
                    if (std::find(pairs.begin(), pairs.end(), key) == pairs.end()) {
                        pairs.push_back(key);
                    }
                }
                phys_id = -1;
                core_id = -1;
            } else if (line.find("physical id") == 0) {
                const size_t colon = line.find(':');
                if (colon != std::string::npos) {
                    phys_id = std::atoi(line.c_str() + colon + 1);
                }
            } else if (line.find("core id") == 0) {
                const size_t colon = line.find(':');
                if (colon != std::string::npos) {
                    core_id = std::atoi(line.c_str() + colon + 1);
                }
            }
        }
        if (phys_id >= 0 && core_id >= 0) {
            const std::pair<int, int> key(phys_id, core_id);
            if (std::find(pairs.begin(), pairs.end(), key) == pairs.end()) {
                pairs.push_back(key);
            }
        }
        if (!pairs.empty()) return static_cast<int>(pairs.size());
    }
#endif
    if (logical_threads >= 4 && (logical_threads % 2 == 0)) {
        return logical_threads / 2;
    }
    return std::max(1, logical_threads);
}

#if XK_X86_CPUID
void detect_x86_caches(XK_HardwareInfo& info, uint32_t max_basic_leaf,
                       uint32_t max_ext_leaf) {
    auto parse_cache_leaf = [&](uint32_t leaf) {
        for (uint32_t sub = 0; sub < 16; ++sub) {
            uint32_t r[4] = {0, 0, 0, 0};
            xk_cpuid_ex(leaf, sub, r);
            const uint32_t cache_type = r[0] & 0x1Fu;
            if (cache_type == 0) break;
            if (cache_type != 1 && cache_type != 3) continue;
            const uint32_t level = (r[0] >> 5) & 0x7u;
            const size_t line_size = static_cast<size_t>((r[1] & 0xFFFu) + 1u);
            const size_t partitions = static_cast<size_t>(((r[1] >> 12) & 0x3FFu) + 1u);
            const size_t ways = static_cast<size_t>(((r[1] >> 22) & 0x3FFu) + 1u);
            const size_t sets = static_cast<size_t>(r[2]) + 1u;
            const size_t total = ways * partitions * line_size * sets;
            if (total == 0) continue;
            if (level == 1 && cache_type == 1) info.l1d_bytes = total;
            else if (level == 2) info.l2_bytes = total;
            else if (level == 3) info.l3_bytes = total;
        }
    };

    if (info.vendor == "AuthenticAMD" && max_ext_leaf >= 0x8000001Du) {
        parse_cache_leaf(0x8000001Du);
    } else if (max_basic_leaf >= 0x04u) {
        parse_cache_leaf(0x04u);
    }
}
#endif

std::mutex& active_profile_mutex() {
    static std::mutex m;
    return m;
}

XK_HardwareProfile& active_profile_storage() {
    static XK_HardwareProfile profile = XK_default_hardware_profile();
    return profile;
}

}  // namespace

XK_HardwareInfo XK_detect_hardware() {
    XK_HardwareInfo info;

    int hw_threads = static_cast<int>(std::thread::hardware_concurrency());
#if defined(_OPENMP)
    const int omp_max = omp_get_max_threads();
    if (omp_max > 0) {
        hw_threads = (hw_threads > 0) ? std::min(hw_threads, omp_max) : omp_max;
    }
#endif
    if (hw_threads < 1) hw_threads = 1;
    info.logical_threads = hw_threads;
    info.physical_cores = std::min(hw_threads, detect_physical_cores(hw_threads));

#if defined(__ARM_NEON) || defined(__aarch64__)
    info.has_neon = true;
#endif

#if XK_X86_CPUID
    uint32_t r0[4] = {0, 0, 0, 0};
    xk_cpuid_ex(0, 0, r0);
    const uint32_t max_basic = r0[0];

    char vendor_buf[13] = {};
    std::memcpy(vendor_buf + 0, &r0[1], 4);
    std::memcpy(vendor_buf + 4, &r0[3], 4);
    std::memcpy(vendor_buf + 8, &r0[2], 4);
    info.vendor = trim_spaces(std::string(vendor_buf));

    uint32_t rext[4] = {0, 0, 0, 0};
    xk_cpuid_ex(0x80000000u, 0, rext);
    const uint32_t max_ext = rext[0];
    if (max_ext >= 0x80000004u) {
        char brand_buf[49] = {};
        for (uint32_t i = 0; i < 3; ++i) {
            uint32_t rb[4] = {0, 0, 0, 0};
            xk_cpuid_ex(0x80000002u + i, 0, rb);
            std::memcpy(brand_buf + i * 16, rb, 16);
        }
        info.brand = trim_spaces(std::string(brand_buf));
    }

    if (max_basic >= 1u) {
        uint32_t r1[4] = {0, 0, 0, 0};
        xk_cpuid_ex(1, 0, r1);
        const bool osxsave = (r1[2] & (1u << 27)) != 0;
        const bool avx = (r1[2] & (1u << 28)) != 0;
        const bool fma = (r1[2] & (1u << 12)) != 0;
        bool ymm_enabled = false;
        bool zmm_enabled = false;
        if (osxsave && avx) {
            const uint64_t xcr0 = xk_xgetbv(0);
            ymm_enabled = ((xcr0 & 0x6u) == 0x6u);
            zmm_enabled = ymm_enabled && ((xcr0 & 0xE0u) == 0xE0u);
        }
        if (max_basic >= 7u && ymm_enabled) {
            uint32_t r7[4] = {0, 0, 0, 0};
            xk_cpuid_ex(7, 0, r7);
            info.has_avx2 = (r7[1] & (1u << 5)) != 0;
            info.has_fma = info.has_avx2 && fma;
            info.has_avx512f = zmm_enabled && ((r7[1] & (1u << 16)) != 0);
        }
    }

    detect_x86_caches(info, max_basic, max_ext);
#endif

    if (!XK_has_avx2_fma()) {
        info.has_avx2 = false;
        info.has_fma = false;
    }

    // v5.0: Prefer hwloc-discovered physical cores & cache hierarchy when hwloc is active
    {
        int h_phys = info.physical_cores;
        int h_log = info.logical_threads;
        size_t h_l1 = info.l1d_bytes;
        size_t h_l2 = info.l2_bytes;
        size_t h_l3 = info.l3_bytes;
        if (nvxk::topology::query_hwloc_overrides(h_phys, h_log, h_l1, h_l2, h_l3)) {
            info.physical_cores = h_phys;
            info.logical_threads = h_log;
            info.l1d_bytes = h_l1;
            info.l2_bytes = h_l2;
            info.l3_bytes = h_l3;
        }
    }

    return info;
}

XK_HardwareProfile XK_default_hardware_profile() {
    XK_HardwareProfile profile;
    profile.hw = XK_detect_hardware();
    profile.calibrated = false;
    profile.peak_gflops_1t = profile.hw.has_fma ? 90.0 : 12.0;
    profile.mem_bandwidth_gbs = 25.0;
    profile.omp_fork_overhead_us = 35.0;
    profile.omp_barrier_overhead_us = 6.0;
    profile.smt_efficiency = 0.12;
    return profile;
}

XK_HardwareProfile XK_calibrate_hardware(double budget_ms) {
    using namespace std::chrono;
    XK_HardwareProfile profile = XK_default_hardware_profile();
    const auto t_start = steady_clock::now();
    const double budget_sec = std::max(0.02, budget_ms * 1e-3);

    // 1. Measure single-thread GEMM peak throughput (256^3) using default kernel path
    {
        const size_t N = 256;
        const bool use_asm = XK_has_avx2_asm_kernel();
        std::vector<float> A(N * N, 0.5f), B(N * N, 0.25f), C(N * N, 0.0f);
        if (use_asm) XK_gemm_avx2_asm(A.data(), B.data(), C.data(), N, N, N);
        else XK_gemm_avx2(A.data(), B.data(), C.data(), N, N, N);  // warm-up

        double best_sec = 1e9;
        for (int r = 0; r < 5; ++r) {
            const auto s = steady_clock::now();
            if (use_asm) XK_gemm_avx2_asm(A.data(), B.data(), C.data(), N, N, N);
            else XK_gemm_avx2(A.data(), B.data(), C.data(), N, N, N);
            const auto e = steady_clock::now();
            const double dt = duration<double>(e - s).count();
            if (dt > 0.0 && dt < best_sec) best_sec = dt;
            if (duration<double>(e - t_start).count() > budget_sec * 0.35) break;
        }
        if (best_sec < 1e8 && best_sec > 0.0) {
            const double flops = 2.0 * N * N * N;
            profile.peak_gflops_1t = (flops / best_sec) * 1e-9;
        }
    }

    // 2. Measure streaming memory bandwidth (> 2x L3, capped at 8M floats)
    if (duration<double>(steady_clock::now() - t_start).count() < budget_sec * 0.65) {
        const size_t target_bytes = std::max(profile.hw.l3_bytes * 2,
                                             static_cast<size_t>(16 * 1024 * 1024));
        const size_t count = std::min(target_bytes / sizeof(float),
                                      static_cast<size_t>(8 * 1024 * 1024));
        std::vector<float> a(count, 1.0f), b(count, 2.0f), c(count, 0.0f);
        vector_add_avx2(a.data(), b.data(), c.data(), count);  // warm-up
        double best_sec = 1e9;
        for (int r = 0; r < 3; ++r) {
            const auto s = steady_clock::now();
            vector_add_avx2(a.data(), b.data(), c.data(), count);
            const auto e = steady_clock::now();
            const double dt = duration<double>(e - s).count();
            if (dt > 0.0 && dt < best_sec) best_sec = dt;
            if (duration<double>(e - t_start).count() > budget_sec * 0.72) break;
        }
        if (best_sec < 1e8 && best_sec > 0.0) {
            profile.mem_bandwidth_gbs =
                (3.0 * static_cast<double>(count) * sizeof(float) / best_sec) * 1e-9;
        }
    }

    // 3. Measure OpenMP fork/join + barrier overhead
#if defined(_OPENMP)
    if (profile.hw.logical_threads >= 2 &&
        duration<double>(steady_clock::now() - t_start).count() < budget_sec * 0.85) {
        const int test_threads = std::min(profile.hw.physical_cores >= 2
                                              ? profile.hw.physical_cores
                                              : 2,
                                          profile.hw.logical_threads);
        volatile int sink = 0;
        const int iters = 40;
        #pragma omp parallel num_threads(test_threads)
        {
            #pragma omp atomic
            sink += 1;
        }

        const auto s1 = steady_clock::now();
        for (int it = 0; it < iters; ++it) {
            #pragma omp parallel num_threads(test_threads)
            {
                #pragma omp barrier
            }
        }
        const auto e1 = steady_clock::now();
        const double per_call_us =
            (duration<double>(e1 - s1).count() / static_cast<double>(iters)) * 1e6;

        const auto s2 = steady_clock::now();
        #pragma omp parallel num_threads(test_threads)
        {
            for (int it = 0; it < iters; ++it) {
                #pragma omp barrier
            }
        }
        const auto e2 = steady_clock::now();
        const double per_barrier_us =
            (duration<double>(e2 - s2).count() / static_cast<double>(iters)) * 1e6;

        profile.omp_barrier_overhead_us = std::max(1.0, per_barrier_us);
        profile.omp_fork_overhead_us =
            std::max(5.0, per_call_us - profile.omp_barrier_overhead_us);
        (void)sink;
    }

    // 4. Empirically calibrate SMT scaling efficiency when logical_threads > physical_cores
    if (profile.hw.logical_threads > profile.hw.physical_cores &&
        profile.hw.physical_cores >= 2 &&
        duration<double>(steady_clock::now() - t_start).count() < budget_sec) {
        const size_t N = XK_has_avx2_fma() ? 512 : 128;
        std::vector<float> A(N * N, 0.5f), B(N * N, 0.25f), C(N * N, 0.0f);
        const int phys = profile.hw.physical_cores;
        const int logt = profile.hw.logical_threads;

        XK_gemm_omp(A.data(), B.data(), C.data(), N, N, N, phys);
        const auto sp = steady_clock::now();
        XK_gemm_omp(A.data(), B.data(), C.data(), N, N, N, phys);
        const auto ep = steady_clock::now();
        const double t_phys = duration<double>(ep - sp).count();

        XK_gemm_omp(A.data(), B.data(), C.data(), N, N, N, logt);
        const auto sl = steady_clock::now();
        XK_gemm_omp(A.data(), B.data(), C.data(), N, N, N, logt);
        const auto el = steady_clock::now();
        const double t_log = duration<double>(el - sl).count();

        if (t_phys > 0.0 && t_log > 0.0) {
            const double extra_ratio = (t_phys / t_log) - 1.0;
            const double smt_per_thread =
                (extra_ratio * static_cast<double>(phys)) /
                static_cast<double>(logt - phys);
            profile.smt_efficiency = std::max(0.0, std::min(0.45, smt_per_thread));
        }
    }
#endif

    profile.calibrated = true;
    return profile;
}

bool XK_save_hardware_profile(const XK_HardwareProfile& profile,
                              const std::string& path) {
    std::ofstream out(path.c_str());
    if (!out) return false;
    out << "nvxk_profile_version=1\n";
    out << "vendor=" << profile.hw.vendor << "\n";
    out << "brand=" << profile.hw.brand << "\n";
    out << "physical_cores=" << profile.hw.physical_cores << "\n";
    out << "logical_threads=" << profile.hw.logical_threads << "\n";
    out << "l1d_bytes=" << profile.hw.l1d_bytes << "\n";
    out << "l2_bytes=" << profile.hw.l2_bytes << "\n";
    out << "l3_bytes=" << profile.hw.l3_bytes << "\n";
    out << "has_avx2=" << (profile.hw.has_avx2 ? 1 : 0) << "\n";
    out << "has_fma=" << (profile.hw.has_fma ? 1 : 0) << "\n";
    out << "calibrated=" << (profile.calibrated ? 1 : 0) << "\n";
    out << "peak_gflops_1t=" << profile.peak_gflops_1t << "\n";
    out << "mem_bandwidth_gbs=" << profile.mem_bandwidth_gbs << "\n";
    out << "omp_fork_overhead_us=" << profile.omp_fork_overhead_us << "\n";
    out << "omp_barrier_overhead_us=" << profile.omp_barrier_overhead_us << "\n";
    out << "smt_efficiency=" << profile.smt_efficiency << "\n";
    return static_cast<bool>(out.flush());
}

bool XK_load_hardware_profile(const std::string& path,
                              XK_HardwareProfile& out_profile) {
    std::ifstream in(path.c_str());
    if (!in) return false;

    XK_HardwareProfile loaded = XK_default_hardware_profile();
    bool saw_version = false;
    std::string line;
    while (std::getline(in, line)) {
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = trim_spaces(line.substr(0, eq));
        const std::string val = trim_spaces(line.substr(eq + 1));
        if (key == "nvxk_profile_version") saw_version = (val == "1");
        else if (key == "vendor") loaded.hw.vendor = val;
        else if (key == "brand") loaded.hw.brand = val;
        else if (key == "physical_cores") loaded.hw.physical_cores = std::atoi(val.c_str());
        else if (key == "logical_threads") loaded.hw.logical_threads = std::atoi(val.c_str());
        else if (key == "l1d_bytes") loaded.hw.l1d_bytes = static_cast<size_t>(std::strtoull(val.c_str(), nullptr, 10));
        else if (key == "l2_bytes") loaded.hw.l2_bytes = static_cast<size_t>(std::strtoull(val.c_str(), nullptr, 10));
        else if (key == "l3_bytes") loaded.hw.l3_bytes = static_cast<size_t>(std::strtoull(val.c_str(), nullptr, 10));
        else if (key == "has_avx2") loaded.hw.has_avx2 = (val == "1");
        else if (key == "has_fma") loaded.hw.has_fma = (val == "1");
        else if (key == "calibrated") loaded.calibrated = (val == "1");
        else if (key == "peak_gflops_1t") loaded.peak_gflops_1t = std::atof(val.c_str());
        else if (key == "mem_bandwidth_gbs") loaded.mem_bandwidth_gbs = std::atof(val.c_str());
        else if (key == "omp_fork_overhead_us") loaded.omp_fork_overhead_us = std::atof(val.c_str());
        else if (key == "omp_barrier_overhead_us") loaded.omp_barrier_overhead_us = std::atof(val.c_str());
        else if (key == "smt_efficiency") loaded.smt_efficiency = std::atof(val.c_str());
    }
    if (!saw_version) return false;
    if (loaded.hw.physical_cores < 1 ||
        loaded.hw.logical_threads < loaded.hw.physical_cores ||
        loaded.hw.l1d_bytes < 4096 ||
        loaded.hw.l2_bytes < 16384 ||
        loaded.hw.l3_bytes < 65536 ||
        !std::isfinite(loaded.peak_gflops_1t) || loaded.peak_gflops_1t <= 0.0 ||
        !std::isfinite(loaded.mem_bandwidth_gbs) || loaded.mem_bandwidth_gbs <= 0.0 ||
        !std::isfinite(loaded.omp_fork_overhead_us) || loaded.omp_fork_overhead_us < 0.0 ||
        !std::isfinite(loaded.omp_barrier_overhead_us) || loaded.omp_barrier_overhead_us < 0.0 ||
        !std::isfinite(loaded.smt_efficiency) ||
        loaded.smt_efficiency < 0.0 || loaded.smt_efficiency > 1.0) {
        return false;
    }
    out_profile = loaded;
    return true;
}

void XK_set_active_profile(const XK_HardwareProfile& profile) {
    std::lock_guard<std::mutex> lock(active_profile_mutex());
    active_profile_storage() = profile;
}

XK_HardwareProfile XK_get_active_profile() {
    std::lock_guard<std::mutex> lock(active_profile_mutex());
    return active_profile_storage();
}