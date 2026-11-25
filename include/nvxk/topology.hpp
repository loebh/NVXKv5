#ifndef NVXK_TOPOLOGY_HPP
#define NVXK_TOPOLOGY_HPP

#include <cstddef>
#include <string>
#include <vector>

namespace nvxk {
namespace topology {

struct CacheLevelInfo {
    int level = 0;              // 1, 2, 3
    std::string type;           // "Data", "Instruction", "Unified"
    size_t size_bytes = 0;
    size_t line_size_bytes = 64;
    int associativity = 0;
    int shared_by_pus = 1;
};

struct TopologyInfo {
    std::string backend_name = "native-fallback";
    bool hwloc_compiled = false;
    bool hwloc_active = false;
    int packages = 1;
    int physical_cores = 1;
    int logical_pus = 1;
    int numa_nodes = 1;
    size_t l1d_bytes = 32 * 1024;
    size_t l2_bytes = 256 * 1024;
    size_t l3_bytes = 4 * 1024 * 1024;
    size_t cache_line_bytes = 64;
    double smt_ratio = 1.0;
    bool affinity_supported = false;
    bool numa_supported = false;
    std::vector<int> physical_core_os_ids;
    std::vector<CacheLevelInfo> caches;
};

// Discover hardware topology via hwloc (if enabled & functional) or native fallback.
TopologyInfo discover();

// Cached process-wide topology snapshot (thread-safe).
const TopologyInfo& cached_topology();

// Query raw hwloc hardware overrides without calling XK_detect_hardware (avoids recursion).
bool query_hwloc_overrides(int& physical_cores, int& logical_pus,
                           size_t& l1d_bytes, size_t& l2_bytes, size_t& l3_bytes) noexcept;

// Pin the calling thread to the i-th physical core (modulo physical_cores).
// Returns true if pinning succeeded, false if unsupported or declined (never throws).
bool pin_current_thread_to_core(int core_index) noexcept;

// Pin an OpenMP thread team (1 thread per physical core when possible).
// Returns number of threads successfully pinned (0 if OpenMP or affinity unavailable).
int pin_openmp_team(int num_threads) noexcept;

// NUMA-aware aligned memory allocation (uses hwloc_alloc_membind on multi-NUMA
// systems when hwloc is active, and 64-byte aligned OS allocation otherwise).
void* alloc_numa_aligned(size_t bytes, size_t alignment = 64, int numa_node = -1) noexcept;
void free_numa_aligned(void* ptr, size_t bytes) noexcept;

// Human-readable multi-line topology diagnostic summary.
std::string format_summary(const TopologyInfo& info);

}  // namespace topology
}  // namespace nvxk

#endif  // NVXK_TOPOLOGY_HPP
