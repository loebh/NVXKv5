#include <nvxk/config.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <mutex>

namespace nvxk {

namespace {

std::mutex& config_mutex() {
    static std::mutex m;
    return m;
}

RuntimeConfig& config_storage() {
    static RuntimeConfig cfg;
    return cfg;
}

std::string to_lower_ascii(std::string s) {
    for (char& ch : s) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return s;
}

}  // namespace

const char* backend_kind_name(BackendKind kind) noexcept {
    switch (kind) {
        case BackendKind::Auto:       return "auto";
        case BackendKind::Scalar:     return "scalar";
        case BackendKind::NvxkNative: return "nvxk-native";
        case BackendKind::Highway:    return "highway";
        case BackendKind::Blis:       return "blis";
        default:                      return "unknown";
    }
}

BackendKind parse_backend_kind(const std::string& name) noexcept {
    const std::string low = to_lower_ascii(name);
    if (low == "auto" || low.empty()) return BackendKind::Auto;
    if (low == "scalar" || low == "ref" || low == "reference") return BackendKind::Scalar;
    if (low == "native" || low == "nvxk" || low == "nvxk-native" || low == "avx2") {
        return BackendKind::NvxkNative;
    }
    if (low == "highway" || low == "hwy" || low == "simd") return BackendKind::Highway;
    if (low == "blis") return BackendKind::Blis;
    return BackendKind::Auto;
}

RuntimeConfig get_runtime_config() {
    std::lock_guard<std::mutex> lock(config_mutex());
    return config_storage();
}

void set_runtime_config(const RuntimeConfig& cfg) {
    std::lock_guard<std::mutex> lock(config_mutex());
    config_storage() = cfg;
}

void apply_env_overrides() {
    std::lock_guard<std::mutex> lock(config_mutex());
    RuntimeConfig& cfg = config_storage();

    if (const char* v = std::getenv("NVXK_BACKEND")) {
        cfg.gemm_backend = parse_backend_kind(v);
    }
    if (const char* v = std::getenv("NVXK_VEC_BACKEND")) {
        cfg.vector_backend = parse_backend_kind(v);
    }
    if (const char* v = std::getenv("NVXK_THREADS")) {
        const int t = std::atoi(v);
        if (t >= 0) cfg.max_threads = t;
    }
    if (const char* v = std::getenv("NVXK_AFFINITY")) {
        const std::string low = to_lower_ascii(v);
        cfg.enable_hwloc_affinity = !(low == "0" || low == "off" || low == "false");
    }
    if (const char* v = std::getenv("NVXK_TRACE")) {
        const std::string low = to_lower_ascii(v);
        cfg.enable_trace = (low == "1" || low == "on" || low == "true");
    }
}

}  // namespace nvxk
