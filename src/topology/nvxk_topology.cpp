#include <nvxk/config.hpp>
#include <nvxk/topology.hpp>
#include <nvxk.h>

#include <algorithm>
#include <cstdlib>
#include <mutex>
#include <sstream>
#include <thread>
#include <unordered_set>

#if defined(_OPENMP)
#include <omp.h>
#endif

#if defined(NVXK_ENABLE_HWLOC)
#include <hwloc.h>
#endif

#if defined(_WIN32)
#include <malloc.h>
#elif defined(__linux__)
#include <pthread.h>
#include <sched.h>
#endif

namespace nvxk {
namespace topology {

namespace {

TopologyInfo discover_native_fallback() {
    TopologyInfo info;
    const XK_HardwareInfo hw = XK_detect_hardware();
    info.backend_name = "native-fallback";
    info.hwloc_compiled = (NVXK_HAS_HWLOC != 0);
    info.hwloc_active = false;
    info.packages = 1;
    info.physical_cores = std::max(1, hw.physical_cores);
    info.logical_pus = std::max(1, hw.logical_threads);
    info.numa_nodes = 1;
    info.l1d_bytes = hw.l1d_bytes;
    info.l2_bytes = hw.l2_bytes;
    info.l3_bytes = hw.l3_bytes;
    info.cache_line_bytes = 64;
    info.smt_ratio = static_cast<double>(info.logical_pus) /
                     static_cast<double>(info.physical_cores);
    info.numa_supported = false;
#if defined(__linux__)
    info.affinity_supported = true;
#else
    info.affinity_supported = false;
#endif
    for (int i = 0; i < info.physical_cores; ++i) {
        info.physical_core_os_ids.push_back(i);
    }

    CacheLevelInfo l1;
    l1.level = 1; l1.type = "Data"; l1.size_bytes = info.l1d_bytes; l1.line_size_bytes = 64;
    CacheLevelInfo l2;
    l2.level = 2; l2.type = "Unified"; l2.size_bytes = info.l2_bytes; l2.line_size_bytes = 64;
    CacheLevelInfo l3;
    l3.level = 3; l3.type = "Unified"; l3.size_bytes = info.l3_bytes; l3.line_size_bytes = 64;
    info.caches.push_back(l1);
    info.caches.push_back(l2);
    info.caches.push_back(l3);
    return info;
}

#if defined(NVXK_ENABLE_HWLOC)
bool discover_with_hwloc_raw(TopologyInfo& out) {
    hwloc_topology_t topo = nullptr;
    if (hwloc_topology_init(&topo) != 0) {
        return false;
    }
    if (hwloc_topology_load(topo) != 0) {
        hwloc_topology_destroy(topo);
        return false;
    }

    out = TopologyInfo();
    out.hwloc_compiled = true;
    out.hwloc_active = true;
    out.backend_name = std::string("hwloc ") + HWLOC_VERSION;

    const int n_pkg = hwloc_get_nbobjs_by_type(topo, HWLOC_OBJ_PACKAGE);
    const int n_core = hwloc_get_nbobjs_by_type(topo, HWLOC_OBJ_CORE);
    const int n_pu = hwloc_get_nbobjs_by_type(topo, HWLOC_OBJ_PU);
    const int n_numa = hwloc_get_nbobjs_by_type(topo, HWLOC_OBJ_NUMANODE);

    out.packages = (n_pkg > 0) ? n_pkg : 1;
    out.logical_pus = (n_pu > 0) ? n_pu : std::max(1, static_cast<int>(std::thread::hardware_concurrency()));
    out.physical_cores = (n_core > 0) ? n_core : out.logical_pus;
    out.numa_nodes = (n_numa > 0) ? n_numa : 1;
    out.smt_ratio = static_cast<double>(out.logical_pus) /
                    static_cast<double>(std::max(1, out.physical_cores));

    const struct hwloc_topology_support* sup = hwloc_topology_get_support(topo);
    if (sup && sup->cpubind) {
        out.affinity_supported = (sup->cpubind->set_thisthread_cpubind != 0);
    }
    if (sup && sup->membind) {
        out.numa_supported = (out.numa_nodes > 1) && (sup->membind->alloc_membind != 0);
    }

    if (n_core > 0) {
        for (int i = 0; i < n_core; ++i) {
            hwloc_obj_t core = hwloc_get_obj_by_type(topo, HWLOC_OBJ_CORE, static_cast<unsigned>(i));
            if (core && core->cpuset) {
                const int first_pu = hwloc_bitmap_first(core->cpuset);
                if (first_pu >= 0) out.physical_core_os_ids.push_back(first_pu);
            }
        }
    }
    if (out.physical_core_os_ids.empty()) {
        for (int i = 0; i < n_pu; ++i) {
            hwloc_obj_t pu = hwloc_get_obj_by_type(topo, HWLOC_OBJ_PU, static_cast<unsigned>(i));
            if (pu) out.physical_core_os_ids.push_back(static_cast<int>(pu->os_index));
        }
    }

    const hwloc_obj_type_t cache_types[3] = {
        HWLOC_OBJ_L1CACHE, HWLOC_OBJ_L2CACHE, HWLOC_OBJ_L3CACHE
    };
    const int cache_levels[3] = {1, 2, 3};

    for (int ci = 0; ci < 3; ++ci) {
        const int n_c = hwloc_get_nbobjs_by_type(topo, cache_types[ci]);
        if (n_c > 0) {
            hwloc_obj_t cobj = hwloc_get_obj_by_type(topo, cache_types[ci], 0);
            if (cobj && cobj->attr && cobj->attr->cache.size > 0) {
                CacheLevelInfo cli;
                cli.level = cache_levels[ci];
                cli.size_bytes = static_cast<size_t>(cobj->attr->cache.size);
                cli.line_size_bytes = (cobj->attr->cache.linesize > 0)
                                          ? static_cast<size_t>(cobj->attr->cache.linesize)
                                          : 64;
                cli.associativity = cobj->attr->cache.associativity;
                cli.type = (cobj->attr->cache.type == HWLOC_OBJ_CACHE_DATA)
                               ? "Data"
                               : (cobj->attr->cache.type == HWLOC_OBJ_CACHE_INSTRUCTION
                                      ? "Instruction"
                                      : "Unified");
                if (cobj->cpuset) {
                    const int w = hwloc_bitmap_weight(cobj->cpuset);
                    cli.shared_by_pus = (w > 0) ? w : 1;
                }
                out.caches.push_back(cli);
                if (cli.line_size_bytes > 0) out.cache_line_bytes = cli.line_size_bytes;
                if (cache_levels[ci] == 1) out.l1d_bytes = cli.size_bytes;
                if (cache_levels[ci] == 2) out.l2_bytes = cli.size_bytes;
                if (cache_levels[ci] == 3) out.l3_bytes = cli.size_bytes;
            }
        }
    }

    hwloc_topology_destroy(topo);
    return true;
}
#endif

}  // namespace

bool query_hwloc_overrides(int& physical_cores, int& logical_pus,
                           size_t& l1d_bytes, size_t& l2_bytes, size_t& l3_bytes) noexcept {
#if defined(NVXK_ENABLE_HWLOC)
    try {
        TopologyInfo raw;
        if (!discover_with_hwloc_raw(raw)) return false;
        if (raw.physical_cores > 0) physical_cores = raw.physical_cores;
        if (raw.logical_pus > 0) logical_pus = raw.logical_pus;
        if (raw.l1d_bytes >= 4096) l1d_bytes = raw.l1d_bytes;
        if (raw.l2_bytes >= 16384) l2_bytes = raw.l2_bytes;
        if (raw.l3_bytes >= 65536) l3_bytes = raw.l3_bytes;
        return true;
    } catch (...) {
        return false;
    }
#else
    (void)physical_cores; (void)logical_pus;
    (void)l1d_bytes; (void)l2_bytes; (void)l3_bytes;
    return false;
#endif
}

TopologyInfo discover() {
#if defined(NVXK_ENABLE_HWLOC)
    TopologyInfo hwloc_info;
    if (discover_with_hwloc_raw(hwloc_info)) {
        return hwloc_info;
    }
#endif
    return discover_native_fallback();
}

const TopologyInfo& cached_topology() {
    static TopologyInfo info = discover();
    return info;
}

bool pin_current_thread_to_core(int core_index) noexcept {
    if (core_index < 0) return false;
    const TopologyInfo& info = cached_topology();
    if (!info.affinity_supported || info.physical_core_os_ids.empty()) {
        return false;
    }
    const int os_id = info.physical_core_os_ids[
        static_cast<size_t>(core_index) % info.physical_core_os_ids.size()];

#if defined(NVXK_ENABLE_HWLOC)
    if (info.hwloc_active) {
        hwloc_topology_t topo = nullptr;
        if (hwloc_topology_init(&topo) == 0 && hwloc_topology_load(topo) == 0) {
            hwloc_cpuset_t set = hwloc_bitmap_alloc();
            bool ok = false;
            if (set) {
                hwloc_bitmap_only(set, static_cast<unsigned>(os_id));
                ok = (hwloc_set_cpubind(topo, set, HWLOC_CPUBIND_THREAD) == 0);
                hwloc_bitmap_free(set);
            }
            hwloc_topology_destroy(topo);
            if (ok) return true;
        } else if (topo) {
            hwloc_topology_destroy(topo);
        }
    }
#endif

#if defined(__linux__)
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(os_id, &cpuset);
    return pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset) == 0;
#else
    (void)os_id;
    return false;
#endif
}

int pin_openmp_team(int num_threads) noexcept {
#if defined(_OPENMP)
    if (num_threads <= 1) {
        return pin_current_thread_to_core(0) ? 1 : 0;
    }
    int pinned = 0;
#pragma omp parallel num_threads(num_threads) reduction(+:pinned)
    {
        const int tid = omp_get_thread_num();
        if (pin_current_thread_to_core(tid)) {
            pinned += 1;
        }
    }
    return pinned;
#else
    (void)num_threads;
    return pin_current_thread_to_core(0) ? 1 : 0;
#endif
}

#if defined(NVXK_ENABLE_HWLOC)
namespace {
std::mutex& hwloc_alloc_mutex() {
    static std::mutex m;
    return m;
}
std::unordered_set<void*>& hwloc_alloc_ptrs() {
    static std::unordered_set<void*> s;
    return s;
}
}  // namespace
#endif

void* alloc_numa_aligned(size_t bytes, size_t alignment, int numa_node) noexcept {
    if (bytes == 0) return nullptr;
    if (alignment < sizeof(void*)) alignment = sizeof(void*);
    // Round alignment up to a power of 2 as required by posix_memalign / _aligned_malloc.
    if ((alignment & (alignment - 1)) != 0) {
        size_t pow2 = sizeof(void*);
        while (pow2 < alignment && pow2 <= (std::numeric_limits<size_t>::max() >> 1)) {
            pow2 <<= 1;
        }
        alignment = pow2;
    }

#if defined(NVXK_ENABLE_HWLOC)
    const TopologyInfo& info = cached_topology();
    if (info.hwloc_active && info.numa_supported && numa_node >= 0 && numa_node < info.numa_nodes) {
        hwloc_topology_t topo = nullptr;
        if (hwloc_topology_init(&topo) == 0 && hwloc_topology_load(topo) == 0) {
            hwloc_obj_t node_obj = hwloc_get_obj_by_type(topo, HWLOC_OBJ_NUMANODE,
                                                         static_cast<unsigned>(numa_node));
            void* ptr = nullptr;
            if (node_obj && node_obj->nodeset) {
                ptr = hwloc_alloc_membind(topo, bytes, node_obj->nodeset,
                                          HWLOC_MEMBIND_BIND, HWLOC_MEMBIND_BYNODESET);
            }
            hwloc_topology_destroy(topo);
            if (ptr) {
                try {
                    std::lock_guard<std::mutex> lock(hwloc_alloc_mutex());
                    hwloc_alloc_ptrs().insert(ptr);
                } catch (...) {
                    // If tracking fails, free via hwloc and fall back to standard aligned alloc
                    hwloc_topology_t t2 = nullptr;
                    if (hwloc_topology_init(&t2) == 0 && hwloc_topology_load(t2) == 0) {
                        hwloc_free(t2, ptr, bytes);
                        hwloc_topology_destroy(t2);
                    }
                    ptr = nullptr;
                }
                if (ptr) return ptr;
            }
        } else if (topo) {
            hwloc_topology_destroy(topo);
        }
    }
#else
    (void)numa_node;
#endif

#if defined(_WIN32)
    return _aligned_malloc(bytes, alignment);
#else
    void* ptr = nullptr;
    if (posix_memalign(&ptr, alignment, bytes) != 0) return nullptr;
    return ptr;
#endif
}

void free_numa_aligned(void* ptr, size_t bytes) noexcept {
    if (!ptr) return;
#if defined(NVXK_ENABLE_HWLOC)
    bool allocated_by_hwloc = false;
    {
        std::lock_guard<std::mutex> lock(hwloc_alloc_mutex());
        auto it = hwloc_alloc_ptrs().find(ptr);
        if (it != hwloc_alloc_ptrs().end()) {
            hwloc_alloc_ptrs().erase(it);
            allocated_by_hwloc = true;
        }
    }
    if (allocated_by_hwloc) {
        hwloc_topology_t topo = nullptr;
        if (hwloc_topology_init(&topo) == 0 && hwloc_topology_load(topo) == 0) {
            hwloc_free(topo, ptr, bytes);
            hwloc_topology_destroy(topo);
            return;
        } else if (topo) {
            hwloc_topology_destroy(topo);
        }
    }
#endif
    (void)bytes;
#if defined(_WIN32)
    _aligned_free(ptr);
#else
    std::free(ptr);
#endif
}

std::string format_summary(const TopologyInfo& info) {
    std::ostringstream oss;
    oss << "Topology Backend : " << info.backend_name
        << " (hwloc_active=" << (info.hwloc_active ? "yes" : "no") << ")\n";
    oss << "Packages / Cores : " << info.packages << " socket(s), "
        << info.physical_cores << " physical core(s), "
        << info.logical_pus << " logical PU(s) (SMT=" << info.smt_ratio << "x)\n";
    oss << "NUMA Nodes       : " << info.numa_nodes
        << " (numa_bind=" << (info.numa_supported ? "supported" : "single-node/unavailable")
        << ", cpu_affinity=" << (info.affinity_supported ? "supported" : "unavailable") << ")\n";
    oss << "Caches           : L1d=" << (info.l1d_bytes / 1024) << " KiB, "
        << "L2=" << (info.l2_bytes / 1024) << " KiB, "
        << "L3=" << (info.l3_bytes / 1024) << " KiB "
        << "(line=" << info.cache_line_bytes << " B)\n";
    return oss.str();
}

}  // namespace topology
}  // namespace nvxk
