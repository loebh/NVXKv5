#ifndef NVXK_CONFIG_HPP
#define NVXK_CONFIG_HPP

#include <cstddef>
#include <string>

#define NVXK_UNIFIED_VERSION_MAJOR 5
#define NVXK_UNIFIED_VERSION_MINOR 0
#define NVXK_UNIFIED_VERSION_PATCH 0
#define NVXK_UNIFIED_VERSION_STRING "5.0.0"

#if defined(NVXK_ENABLE_HIGHWAY)
#define NVXK_HAS_HIGHWAY 1
#else
#define NVXK_HAS_HIGHWAY 0
#endif

#if defined(NVXK_ENABLE_BLIS)
#define NVXK_HAS_BLIS 1
#else
#define NVXK_HAS_BLIS 0
#endif

#if defined(NVXK_ENABLE_HWLOC)
#define NVXK_HAS_HWLOC 1
#else
#define NVXK_HAS_HWLOC 0
#endif

namespace nvxk {

enum class BackendKind {
    Auto = 0,
    Scalar = 1,
    NvxkNative = 2,
    Highway = 3,
    Blis = 4
};

const char* backend_kind_name(BackendKind kind) noexcept;
BackendKind parse_backend_kind(const std::string& name) noexcept;

struct RuntimeConfig {
    BackendKind gemm_backend = BackendKind::Auto;
    BackendKind vector_backend = BackendKind::Auto;
    int max_threads = 0;  // 0 = auto (governed by NVXK planner & topology)
    size_t memory_budget_bytes = 64 * 1024 * 1024;
    bool enable_hwloc_affinity = true;
    bool enable_trace = false;
    bool blis_crossover_calibrated = false;
    // Policy thresholds (empirically tunable via calibrate_blis_crossover)
    double small_gemm_flops_threshold = 8192.0;
    double blis_gemm_flops_threshold = 2.0e7;
    size_t highway_min_elems_threshold = 16;
};

RuntimeConfig get_runtime_config();
void set_runtime_config(const RuntimeConfig& cfg);
void apply_env_overrides();

}  // namespace nvxk

#endif  // NVXK_CONFIG_HPP
