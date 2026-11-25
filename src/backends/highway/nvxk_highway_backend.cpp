#include <nvxk/simd.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

#if defined(NVXK_ENABLE_HIGHWAY)
#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "backends/highway/nvxk_highway_kernels-inl.h"
#include "hwy/foreach_target.h"
#include "backends/highway/nvxk_highway_kernels-inl.h"
#endif

namespace nvxk {
namespace simd {

namespace {

#if defined(NVXK_ENABLE_HIGHWAY)
HWY_EXPORT(GetLanesF32);
HWY_EXPORT(GetLanesF64);
HWY_EXPORT(VecAddF32);
HWY_EXPORT(VecAddF64);
HWY_EXPORT(VecSubF32);
HWY_EXPORT(VecSubF64);
HWY_EXPORT(VecMulF32);
HWY_EXPORT(VecMulF64);
HWY_EXPORT(VecAxpyF32);
HWY_EXPORT(VecAxpyF64);
HWY_EXPORT(VecScaleF32);
HWY_EXPORT(VecScaleF64);
HWY_EXPORT(DotF32);
HWY_EXPORT(DotF64);
HWY_EXPORT(SumF32);
HWY_EXPORT(SumF64);
HWY_EXPORT(MaxValF32);
HWY_EXPORT(MaxValF64);
HWY_EXPORT(ReluF32);
HWY_EXPORT(VecExpF32);
HWY_EXPORT(VecSigmoidF32);
HWY_EXPORT(GeluApproxF32);
HWY_EXPORT(SoftmaxF32);
HWY_EXPORT(LayerNormF32);
HWY_EXPORT(GemvF32);
HWY_EXPORT(GemvF64);
#endif

inline bool should_use_highway(size_t n, BackendKind requested) {
#if !defined(NVXK_ENABLE_HIGHWAY)
    (void)n; (void)requested;
    return false;
#else
    if (requested == BackendKind::Scalar) return false;
    if (requested == BackendKind::Highway) return true;
    const RuntimeConfig cfg = get_runtime_config();
    if (cfg.vector_backend == BackendKind::Scalar) return false;
    if (cfg.vector_backend == BackendKind::Highway) return true;
    return n >= cfg.highway_min_elems_threshold;
#endif
}

inline void check_ptr(const void* p, size_t n, const char* caller) {
    if (n > 0 && p == nullptr) {
        throw std::invalid_argument(std::string(caller) + ": null pointer with non-zero length");
    }
}

}  // namespace

SimdInfo query_info() {
    SimdInfo info;
#if defined(NVXK_ENABLE_HIGHWAY)
    info.highway_compiled = true;
    info.highway_active = true;
    info.backend_name = "Google Highway 1.4.0 (HWY_DYNAMIC_DISPATCH + hwy/contrib/math)";
    const std::vector<int64_t> targets = hwy::SupportedAndGeneratedTargets();
    if (!targets.empty()) {
        info.active_target = hwy::TargetName(targets.front());
        for (int64_t t : targets) info.supported_targets.push_back(hwy::TargetName(t));
    } else {
        info.active_target = "Scalar";
        info.supported_targets.push_back("Scalar");
    }
    info.lanes_f32 = HWY_DYNAMIC_DISPATCH(GetLanesF32)();
    info.lanes_f64 = HWY_DYNAMIC_DISPATCH(GetLanesF64)();
#else
    info.highway_compiled = false;
    info.highway_active = false;
    info.backend_name = "scalar-fallback";
    info.active_target = "Scalar";
    info.lanes_f32 = 1;
    info.lanes_f64 = 1;
    info.supported_targets.push_back("Scalar");
#endif
    return info;
}

void vec_add(const float* a, const float* b, float* c, size_t n, BackendKind backend) {
    check_ptr(a, n, "vec_add"); check_ptr(b, n, "vec_add"); check_ptr(c, n, "vec_add");
    if (n == 0) return;
#if !defined(NVXK_ENABLE_HIGHWAY)
    (void)backend;
#else
    if (should_use_highway(n, backend)) { HWY_DYNAMIC_DISPATCH(VecAddF32)(a, b, c, n); return; }
#endif
    for (size_t i = 0; i < n; ++i) c[i] = a[i] + b[i];
}

void vec_add(const double* a, const double* b, double* c, size_t n, BackendKind backend) {
    check_ptr(a, n, "vec_add"); check_ptr(b, n, "vec_add"); check_ptr(c, n, "vec_add");
    if (n == 0) return;
#if !defined(NVXK_ENABLE_HIGHWAY)
    (void)backend;
#else
    if (should_use_highway(n, backend)) { HWY_DYNAMIC_DISPATCH(VecAddF64)(a, b, c, n); return; }
#endif
    for (size_t i = 0; i < n; ++i) c[i] = a[i] + b[i];
}

void vec_sub(const float* a, const float* b, float* c, size_t n, BackendKind backend) {
    check_ptr(a, n, "vec_sub"); check_ptr(b, n, "vec_sub"); check_ptr(c, n, "vec_sub");
    if (n == 0) return;
#if !defined(NVXK_ENABLE_HIGHWAY)
    (void)backend;
#else
    if (should_use_highway(n, backend)) { HWY_DYNAMIC_DISPATCH(VecSubF32)(a, b, c, n); return; }
#endif
    for (size_t i = 0; i < n; ++i) c[i] = a[i] - b[i];
}

void vec_sub(const double* a, const double* b, double* c, size_t n, BackendKind backend) {
    check_ptr(a, n, "vec_sub"); check_ptr(b, n, "vec_sub"); check_ptr(c, n, "vec_sub");
    if (n == 0) return;
#if !defined(NVXK_ENABLE_HIGHWAY)
    (void)backend;
#else
    if (should_use_highway(n, backend)) { HWY_DYNAMIC_DISPATCH(VecSubF64)(a, b, c, n); return; }
#endif
    for (size_t i = 0; i < n; ++i) c[i] = a[i] - b[i];
}

void vec_mul(const float* a, const float* b, float* c, size_t n, BackendKind backend) {
    check_ptr(a, n, "vec_mul"); check_ptr(b, n, "vec_mul"); check_ptr(c, n, "vec_mul");
    if (n == 0) return;
#if !defined(NVXK_ENABLE_HIGHWAY)
    (void)backend;
#else
    if (should_use_highway(n, backend)) { HWY_DYNAMIC_DISPATCH(VecMulF32)(a, b, c, n); return; }
#endif
    for (size_t i = 0; i < n; ++i) c[i] = a[i] * b[i];
}

void vec_mul(const double* a, const double* b, double* c, size_t n, BackendKind backend) {
    check_ptr(a, n, "vec_mul"); check_ptr(b, n, "vec_mul"); check_ptr(c, n, "vec_mul");
    if (n == 0) return;
#if !defined(NVXK_ENABLE_HIGHWAY)
    (void)backend;
#else
    if (should_use_highway(n, backend)) { HWY_DYNAMIC_DISPATCH(VecMulF64)(a, b, c, n); return; }
#endif
    for (size_t i = 0; i < n; ++i) c[i] = a[i] * b[i];
}

void vec_axpy(float alpha, const float* x, float* y, size_t n, BackendKind backend) {
    check_ptr(x, n, "vec_axpy"); check_ptr(y, n, "vec_axpy");
    if (n == 0) return;
#if !defined(NVXK_ENABLE_HIGHWAY)
    (void)backend;
#else
    if (should_use_highway(n, backend)) { HWY_DYNAMIC_DISPATCH(VecAxpyF32)(alpha, x, y, n); return; }
#endif
    for (size_t i = 0; i < n; ++i) y[i] = alpha * x[i] + y[i];
}

void vec_axpy(double alpha, const double* x, double* y, size_t n, BackendKind backend) {
    check_ptr(x, n, "vec_axpy"); check_ptr(y, n, "vec_axpy");
    if (n == 0) return;
#if !defined(NVXK_ENABLE_HIGHWAY)
    (void)backend;
#else
    if (should_use_highway(n, backend)) { HWY_DYNAMIC_DISPATCH(VecAxpyF64)(alpha, x, y, n); return; }
#endif
    for (size_t i = 0; i < n; ++i) y[i] = alpha * x[i] + y[i];
}

void vec_scale(float alpha, const float* x, float* y, size_t n, BackendKind backend) {
    check_ptr(x, n, "vec_scale"); check_ptr(y, n, "vec_scale");
    if (n == 0) return;
#if !defined(NVXK_ENABLE_HIGHWAY)
    (void)backend;
#else
    if (should_use_highway(n, backend)) { HWY_DYNAMIC_DISPATCH(VecScaleF32)(alpha, x, y, n); return; }
#endif
    for (size_t i = 0; i < n; ++i) y[i] = alpha * x[i];
}

void vec_scale(double alpha, const double* x, double* y, size_t n, BackendKind backend) {
    check_ptr(x, n, "vec_scale"); check_ptr(y, n, "vec_scale");
    if (n == 0) return;
#if !defined(NVXK_ENABLE_HIGHWAY)
    (void)backend;
#else
    if (should_use_highway(n, backend)) { HWY_DYNAMIC_DISPATCH(VecScaleF64)(alpha, x, y, n); return; }
#endif
    for (size_t i = 0; i < n; ++i) y[i] = alpha * x[i];
}

float dot(const float* a, const float* b, size_t n, BackendKind backend) {
    check_ptr(a, n, "dot"); check_ptr(b, n, "dot");
    if (n == 0) return 0.0f;
#if !defined(NVXK_ENABLE_HIGHWAY)
    (void)backend;
#else
    if (should_use_highway(n, backend)) return HWY_DYNAMIC_DISPATCH(DotF32)(a, b, n);
#endif
    double acc = 0.0;
    for (size_t i = 0; i < n; ++i) acc += static_cast<double>(a[i]) * static_cast<double>(b[i]);
    return static_cast<float>(acc);
}

double dot(const double* a, const double* b, size_t n, BackendKind backend) {
    check_ptr(a, n, "dot"); check_ptr(b, n, "dot");
    if (n == 0) return 0.0;
#if !defined(NVXK_ENABLE_HIGHWAY)
    (void)backend;
#else
    if (should_use_highway(n, backend)) return HWY_DYNAMIC_DISPATCH(DotF64)(a, b, n);
#endif
    double acc = 0.0;
    for (size_t i = 0; i < n; ++i) acc += a[i] * b[i];
    return acc;
}

float l2_norm(const float* x, size_t n, BackendKind backend) {
    return std::sqrt(dot(x, x, n, backend));
}

double l2_norm(const double* x, size_t n, BackendKind backend) {
    return std::sqrt(dot(x, x, n, backend));
}

float sum(const float* x, size_t n, BackendKind backend) {
    check_ptr(x, n, "sum");
    if (n == 0) return 0.0f;
#if !defined(NVXK_ENABLE_HIGHWAY)
    (void)backend;
#else
    if (should_use_highway(n, backend)) return HWY_DYNAMIC_DISPATCH(SumF32)(x, n);
#endif
    double acc = 0.0;
    for (size_t i = 0; i < n; ++i) acc += static_cast<double>(x[i]);
    return static_cast<float>(acc);
}

double sum(const double* x, size_t n, BackendKind backend) {
    check_ptr(x, n, "sum");
    if (n == 0) return 0.0;
#if !defined(NVXK_ENABLE_HIGHWAY)
    (void)backend;
#else
    if (should_use_highway(n, backend)) return HWY_DYNAMIC_DISPATCH(SumF64)(x, n);
#endif
    double acc = 0.0;
    for (size_t i = 0; i < n; ++i) acc += x[i];
    return acc;
}

float max_val(const float* x, size_t n, BackendKind backend) {
    check_ptr(x, n, "max_val");
    if (n == 0) return -std::numeric_limits<float>::infinity();
#if !defined(NVXK_ENABLE_HIGHWAY)
    (void)backend;
#else
    if (should_use_highway(n, backend)) return HWY_DYNAMIC_DISPATCH(MaxValF32)(x, n);
#endif
    float best = x[0];
    for (size_t i = 1; i < n; ++i) if (x[i] > best) best = x[i];
    return best;
}

double max_val(const double* x, size_t n, BackendKind backend) {
    check_ptr(x, n, "max_val");
    if (n == 0) return -std::numeric_limits<double>::infinity();
#if !defined(NVXK_ENABLE_HIGHWAY)
    (void)backend;
#else
    if (should_use_highway(n, backend)) return HWY_DYNAMIC_DISPATCH(MaxValF64)(x, n);
#endif
    double best = x[0];
    for (size_t i = 1; i < n; ++i) if (x[i] > best) best = x[i];
    return best;
}

void vec_exp(const float* x, float* y, size_t n, BackendKind backend) {
    check_ptr(x, n, "vec_exp"); check_ptr(y, n, "vec_exp");
    if (n == 0) return;
#if !defined(NVXK_ENABLE_HIGHWAY)
    (void)backend;
#else
    if (should_use_highway(n, backend)) { HWY_DYNAMIC_DISPATCH(VecExpF32)(x, y, n); return; }
#endif
    for (size_t i = 0; i < n; ++i) y[i] = std::exp(x[i]);
}

void vec_sigmoid(const float* x, float* y, size_t n, BackendKind backend) {
    check_ptr(x, n, "vec_sigmoid"); check_ptr(y, n, "vec_sigmoid");
    if (n == 0) return;
#if !defined(NVXK_ENABLE_HIGHWAY)
    (void)backend;
#else
    if (should_use_highway(n, backend)) { HWY_DYNAMIC_DISPATCH(VecSigmoidF32)(x, y, n); return; }
#endif
    for (size_t i = 0; i < n; ++i) y[i] = 1.0f / (1.0f + std::exp(-x[i]));
}

void relu(const float* x, float* y, size_t n, BackendKind backend) {
    check_ptr(x, n, "relu"); check_ptr(y, n, "relu");
    if (n == 0) return;
#if !defined(NVXK_ENABLE_HIGHWAY)
    (void)backend;
#else
    if (should_use_highway(n, backend)) { HWY_DYNAMIC_DISPATCH(ReluF32)(x, y, n); return; }
#endif
    for (size_t i = 0; i < n; ++i) y[i] = (x[i] > 0.0f) ? x[i] : 0.0f;
}

void gelu_approx(const float* x, float* y, size_t n, BackendKind backend) {
    check_ptr(x, n, "gelu_approx"); check_ptr(y, n, "gelu_approx");
    if (n == 0) return;
#if !defined(NVXK_ENABLE_HIGHWAY)
    (void)backend;
#else
    if (should_use_highway(n, backend)) { HWY_DYNAMIC_DISPATCH(GeluApproxF32)(x, y, n); return; }
#endif
    constexpr float kSqrt2OverPi = 0.7978845608028654f;
    constexpr float kCoeff = 0.044715f;
    for (size_t i = 0; i < n; ++i) {
        const float v = x[i];
        const float inner = kSqrt2OverPi * (v + kCoeff * v * v * v);
        y[i] = 0.5f * v * (1.0f + std::tanh(inner));
    }
}

void softmax(const float* x, float* y, size_t n, BackendKind backend) {
    check_ptr(x, n, "softmax"); check_ptr(y, n, "softmax");
    if (n == 0) return;
#if !defined(NVXK_ENABLE_HIGHWAY)
    (void)backend;
#else
    if (should_use_highway(n, backend)) { HWY_DYNAMIC_DISPATCH(SoftmaxF32)(x, y, n); return; }
#endif
    const float m = max_val(x, n, BackendKind::Scalar);
    double s = 0.0;
    for (size_t i = 0; i < n; ++i) {
        const float e = std::exp(x[i] - m);
        y[i] = e;
        s += static_cast<double>(e);
    }
    if (s > 0.0) {
        const float inv = static_cast<float>(1.0 / s);
        for (size_t i = 0; i < n; ++i) y[i] *= inv;
    }
}

void layernorm(const float* x, float* y, size_t n, float eps,
               const float* gamma, const float* beta, BackendKind backend) {
    check_ptr(x, n, "layernorm"); check_ptr(y, n, "layernorm");
    if (n == 0) return;
#if !defined(NVXK_ENABLE_HIGHWAY)
    (void)backend;
#else
    if (should_use_highway(n, backend)) {
        HWY_DYNAMIC_DISPATCH(LayerNormF32)(x, y, n, eps, gamma, beta);
        return;
    }
#endif
    double mean = 0.0;
    for (size_t i = 0; i < n; ++i) mean += static_cast<double>(x[i]);
    mean /= static_cast<double>(n);
    double var = 0.0;
    for (size_t i = 0; i < n; ++i) {
        const double d = static_cast<double>(x[i]) - mean;
        var += d * d;
    }
    var /= static_cast<double>(n);
    const double inv_std = 1.0 / std::sqrt(var + static_cast<double>(eps));
    for (size_t i = 0; i < n; ++i) {
        double v = (static_cast<double>(x[i]) - mean) * inv_std;
        if (gamma) v *= static_cast<double>(gamma[i]);
        if (beta) v += static_cast<double>(beta[i]);
        y[i] = static_cast<float>(v);
    }
}

void gemv(size_t M, size_t K, float alpha, const float* A, size_t lda,
          const float* x, float beta, float* y, BackendKind backend) {
    if (M == 0) return;
    if (K > 0) { check_ptr(A, M * K, "gemv"); check_ptr(x, K, "gemv"); }
    check_ptr(y, M, "gemv");
    if (lda < K) throw std::invalid_argument("gemv: lda < K");
#if !defined(NVXK_ENABLE_HIGHWAY)
    (void)backend;
#else
    if (should_use_highway(K, backend)) {
        HWY_DYNAMIC_DISPATCH(GemvF32)(M, K, alpha, A, lda, x, beta, y);
        return;
    }
#endif
    for (size_t i = 0; i < M; ++i) {
        double acc = 0.0;
        const float* row = A + i * lda;
        for (size_t k = 0; k < K; ++k) acc += static_cast<double>(row[k]) * static_cast<double>(x[k]);
        y[i] = (beta == 0.0f) ? static_cast<float>(alpha * acc)
                              : static_cast<float>(alpha * acc + beta * y[i]);
    }
}

void gemv(size_t M, size_t K, double alpha, const double* A, size_t lda,
          const double* x, double beta, double* y, BackendKind backend) {
    if (M == 0) return;
    if (K > 0) { check_ptr(A, M * K, "gemv"); check_ptr(x, K, "gemv"); }
    check_ptr(y, M, "gemv");
    if (lda < K) throw std::invalid_argument("gemv: lda < K");
#if !defined(NVXK_ENABLE_HIGHWAY)
    (void)backend;
#else
    if (should_use_highway(K, backend)) {
        HWY_DYNAMIC_DISPATCH(GemvF64)(M, K, alpha, A, lda, x, beta, y);
        return;
    }
#endif
    for (size_t i = 0; i < M; ++i) {
        double acc = 0.0;
        const double* row = A + i * lda;
        for (size_t k = 0; k < K; ++k) acc += row[k] * x[k];
        y[i] = (beta == 0.0) ? (alpha * acc) : (alpha * acc + beta * y[i]);
    }
}

}  // namespace simd
}  // namespace nvxk
