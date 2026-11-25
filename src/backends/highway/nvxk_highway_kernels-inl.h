// Google Highway 1.4.0 SIMD Kernels for NVXK Unified Computing Engine v5.0.0
// Compiled across all supported ISA targets via hwy/foreach_target.h for runtime dispatch.

#if defined(NVXK_HIGHWAY_KERNELS_INL_H_) == defined(HWY_TARGET_TOGGLE)
#ifdef NVXK_HIGHWAY_KERNELS_INL_H_
#undef NVXK_HIGHWAY_KERNELS_INL_H_
#else
#define NVXK_HIGHWAY_KERNELS_INL_H_
#endif

#include <cmath>
#include <cstddef>
#include <limits>
#include "hwy/highway.h"
#include "hwy/contrib/math/math-inl.h"

HWY_BEFORE_NAMESPACE();
namespace nvxk {
namespace simd {
namespace HWY_NAMESPACE {

namespace hn = hwy::HWY_NAMESPACE;

size_t GetLanesF32() {
    const hn::ScalableTag<float> d;
    return hn::Lanes(d);
}

size_t GetLanesF64() {
    const hn::ScalableTag<double> d;
    return hn::Lanes(d);
}

template <typename T>
void VecAddImpl(const T* HWY_RESTRICT a, const T* HWY_RESTRICT b,
                T* HWY_RESTRICT c, size_t n) {
    const hn::ScalableTag<T> d;
    const size_t N = hn::Lanes(d);
    size_t i = 0;
    for (; i + N <= n; i += N) {
        hn::StoreU(hn::Add(hn::LoadU(d, a + i), hn::LoadU(d, b + i)), d, c + i);
    }
    for (; i < n; ++i) c[i] = a[i] + b[i];
}

void VecAddF32(const float* a, const float* b, float* c, size_t n) { VecAddImpl(a, b, c, n); }
void VecAddF64(const double* a, const double* b, double* c, size_t n) { VecAddImpl(a, b, c, n); }

template <typename T>
void VecSubImpl(const T* HWY_RESTRICT a, const T* HWY_RESTRICT b,
                T* HWY_RESTRICT c, size_t n) {
    const hn::ScalableTag<T> d;
    const size_t N = hn::Lanes(d);
    size_t i = 0;
    for (; i + N <= n; i += N) {
        hn::StoreU(hn::Sub(hn::LoadU(d, a + i), hn::LoadU(d, b + i)), d, c + i);
    }
    for (; i < n; ++i) c[i] = a[i] - b[i];
}

void VecSubF32(const float* a, const float* b, float* c, size_t n) { VecSubImpl(a, b, c, n); }
void VecSubF64(const double* a, const double* b, double* c, size_t n) { VecSubImpl(a, b, c, n); }

template <typename T>
void VecMulImpl(const T* HWY_RESTRICT a, const T* HWY_RESTRICT b,
                T* HWY_RESTRICT c, size_t n) {
    const hn::ScalableTag<T> d;
    const size_t N = hn::Lanes(d);
    size_t i = 0;
    for (; i + N <= n; i += N) {
        hn::StoreU(hn::Mul(hn::LoadU(d, a + i), hn::LoadU(d, b + i)), d, c + i);
    }
    for (; i < n; ++i) c[i] = a[i] * b[i];
}

void VecMulF32(const float* a, const float* b, float* c, size_t n) { VecMulImpl(a, b, c, n); }
void VecMulF64(const double* a, const double* b, double* c, size_t n) { VecMulImpl(a, b, c, n); }

template <typename T>
void VecAxpyImpl(T alpha, const T* HWY_RESTRICT x, T* HWY_RESTRICT y, size_t n) {
    const hn::ScalableTag<T> d;
    const size_t N = hn::Lanes(d);
    const auto va = hn::Set(d, alpha);
    size_t i = 0;
    for (; i + N <= n; i += N) {
        hn::StoreU(hn::MulAdd(va, hn::LoadU(d, x + i), hn::LoadU(d, y + i)), d, y + i);
    }
    for (; i < n; ++i) y[i] = alpha * x[i] + y[i];
}

void VecAxpyF32(float alpha, const float* x, float* y, size_t n) { VecAxpyImpl(alpha, x, y, n); }
void VecAxpyF64(double alpha, const double* x, double* y, size_t n) { VecAxpyImpl(alpha, x, y, n); }

template <typename T>
void VecScaleImpl(T alpha, const T* HWY_RESTRICT x, T* HWY_RESTRICT y, size_t n) {
    const hn::ScalableTag<T> d;
    const size_t N = hn::Lanes(d);
    const auto va = hn::Set(d, alpha);
    size_t i = 0;
    for (; i + N <= n; i += N) {
        hn::StoreU(hn::Mul(va, hn::LoadU(d, x + i)), d, y + i);
    }
    for (; i < n; ++i) y[i] = alpha * x[i];
}

void VecScaleF32(float alpha, const float* x, float* y, size_t n) { VecScaleImpl(alpha, x, y, n); }
void VecScaleF64(double alpha, const double* x, double* y, size_t n) { VecScaleImpl(alpha, x, y, n); }

template <typename T>
T DotImpl(const T* HWY_RESTRICT a, const T* HWY_RESTRICT b, size_t n) {
    const hn::ScalableTag<T> d;
    const size_t N = hn::Lanes(d);
    auto acc0 = hn::Zero(d);
    auto acc1 = hn::Zero(d);
    size_t i = 0;
    for (; i + 2 * N <= n; i += 2 * N) {
        acc0 = hn::MulAdd(hn::LoadU(d, a + i), hn::LoadU(d, b + i), acc0);
        acc1 = hn::MulAdd(hn::LoadU(d, a + i + N), hn::LoadU(d, b + i + N), acc1);
    }
    acc0 = hn::Add(acc0, acc1);
    for (; i + N <= n; i += N) {
        acc0 = hn::MulAdd(hn::LoadU(d, a + i), hn::LoadU(d, b + i), acc0);
    }
    T total = hn::ReduceSum(d, acc0);
    for (; i < n; ++i) total += a[i] * b[i];
    return total;
}

float DotF32(const float* a, const float* b, size_t n) { return DotImpl(a, b, n); }
double DotF64(const double* a, const double* b, size_t n) { return DotImpl(a, b, n); }

template <typename T>
T SumImpl(const T* HWY_RESTRICT x, size_t n) {
    const hn::ScalableTag<T> d;
    const size_t N = hn::Lanes(d);
    auto acc0 = hn::Zero(d);
    auto acc1 = hn::Zero(d);
    size_t i = 0;
    for (; i + 2 * N <= n; i += 2 * N) {
        acc0 = hn::Add(acc0, hn::LoadU(d, x + i));
        acc1 = hn::Add(acc1, hn::LoadU(d, x + i + N));
    }
    acc0 = hn::Add(acc0, acc1);
    for (; i + N <= n; i += N) {
        acc0 = hn::Add(acc0, hn::LoadU(d, x + i));
    }
    T total = hn::ReduceSum(d, acc0);
    for (; i < n; ++i) total += x[i];
    return total;
}

float SumF32(const float* x, size_t n) { return SumImpl(x, n); }
double SumF64(const double* x, size_t n) { return SumImpl(x, n); }

template <typename T>
T MaxValImpl(const T* HWY_RESTRICT x, size_t n) {
    if (n == 0) return -std::numeric_limits<T>::infinity();
    const hn::ScalableTag<T> d;
    const size_t N = hn::Lanes(d);
    size_t i = 0;
    T best = x[0];
    if (n >= N) {
        auto vmax = hn::LoadU(d, x);
        i = N;
        for (; i + N <= n; i += N) {
            vmax = hn::Max(vmax, hn::LoadU(d, x + i));
        }
        best = hn::ReduceMax(d, vmax);
    }
    for (; i < n; ++i) {
        if (x[i] > best) best = x[i];
    }
    return best;
}

float MaxValF32(const float* x, size_t n) { return MaxValImpl(x, n); }
double MaxValF64(const double* x, size_t n) { return MaxValImpl(x, n); }

void ReluF32(const float* HWY_RESTRICT x, float* HWY_RESTRICT y, size_t n) {
    const hn::ScalableTag<float> d;
    const size_t N = hn::Lanes(d);
    const auto vzero = hn::Zero(d);
    size_t i = 0;
    for (; i + N <= n; i += N) {
        hn::StoreU(hn::Max(hn::LoadU(d, x + i), vzero), d, y + i);
    }
    for (; i < n; ++i) y[i] = (x[i] > 0.0f) ? x[i] : 0.0f;
}

void VecExpF32(const float* HWY_RESTRICT x, float* HWY_RESTRICT y, size_t n) {
    const hn::ScalableTag<float> d;
    const size_t N = hn::Lanes(d);
    size_t i = 0;
    for (; i + N <= n; i += N) {
        hn::StoreU(hn::Exp(d, hn::LoadU(d, x + i)), d, y + i);
    }
    for (; i < n; ++i) y[i] = std::exp(x[i]);
}

void VecSigmoidF32(const float* HWY_RESTRICT x, float* HWY_RESTRICT y, size_t n) {
    const hn::ScalableTag<float> d;
    const size_t N = hn::Lanes(d);
    const auto vone = hn::Set(d, 1.0f);
    size_t i = 0;
    for (; i + N <= n; i += N) {
        const auto vx = hn::LoadU(d, x + i);
        const auto eneg = hn::Exp(d, hn::Neg(vx));
        hn::StoreU(hn::Div(vone, hn::Add(vone, eneg)), d, y + i);
    }
    for (; i < n; ++i) y[i] = 1.0f / (1.0f + std::exp(-x[i]));
}

void GeluApproxF32(const float* HWY_RESTRICT x, float* HWY_RESTRICT y, size_t n) {
    const hn::ScalableTag<float> d;
    const size_t N = hn::Lanes(d);
    const auto vhalf = hn::Set(d, 0.5f);
    const auto vone = hn::Set(d, 1.0f);
    const auto vsqrt2pi = hn::Set(d, 0.7978845608028654f);
    const auto vcoeff = hn::Set(d, 0.044715f);
    size_t i = 0;
    for (; i + N <= n; i += N) {
        const auto vx = hn::LoadU(d, x + i);
        const auto vx3 = hn::Mul(hn::Mul(vx, vx), vx);
        const auto inner = hn::Mul(vsqrt2pi, hn::MulAdd(vcoeff, vx3, vx));
        const auto th = hn::Tanh(d, inner);
        hn::StoreU(hn::Mul(hn::Mul(vhalf, vx), hn::Add(vone, th)), d, y + i);
    }
    for (; i < n; ++i) {
        const float v = x[i];
        const float inner = 0.7978845608028654f * (v + 0.044715f * v * v * v);
        y[i] = 0.5f * v * (1.0f + std::tanh(inner));
    }
}

void SoftmaxF32(const float* HWY_RESTRICT x, float* HWY_RESTRICT y, size_t n) {
    if (n == 0) return;
    const float m = MaxValImpl(x, n);
    const hn::ScalableTag<float> d;
    const size_t N = hn::Lanes(d);
    const auto vm = hn::Set(d, m);
    auto vsum = hn::Zero(d);
    size_t i = 0;
    for (; i + N <= n; i += N) {
        const auto ve = hn::Exp(d, hn::Sub(hn::LoadU(d, x + i), vm));
        hn::StoreU(ve, d, y + i);
        vsum = hn::Add(vsum, ve);
    }
    float total = hn::ReduceSum(d, vsum);
    for (; i < n; ++i) {
        const float e = std::exp(x[i] - m);
        y[i] = e;
        total += e;
    }
    if (total > 0.0f) {
        const float inv = 1.0f / total;
        const auto vinv = hn::Set(d, inv);
        size_t j = 0;
        for (; j + N <= n; j += N) {
            hn::StoreU(hn::Mul(hn::LoadU(d, y + j), vinv), d, y + j);
        }
        for (; j < n; ++j) y[j] *= inv;
    }
}

void LayerNormF32(const float* HWY_RESTRICT x, float* HWY_RESTRICT y, size_t n,
                  float eps, const float* gamma, const float* beta) {
    if (n == 0) return;
    const float mean = SumImpl(x, n) / static_cast<float>(n);
    const hn::ScalableTag<float> d;
    const size_t N = hn::Lanes(d);
    const auto vmean = hn::Set(d, mean);
    auto vvar = hn::Zero(d);
    size_t i = 0;
    for (; i + N <= n; i += N) {
        const auto diff = hn::Sub(hn::LoadU(d, x + i), vmean);
        vvar = hn::MulAdd(diff, diff, vvar);
    }
    float var_sum = hn::ReduceSum(d, vvar);
    for (; i < n; ++i) {
        const float diff = x[i] - mean;
        var_sum += diff * diff;
    }
    const float inv_std = 1.0f / std::sqrt(var_sum / static_cast<float>(n) + eps);
    const auto vinv = hn::Set(d, inv_std);

    i = 0;
    if (gamma && beta) {
        for (; i + N <= n; i += N) {
            const auto norm = hn::Mul(hn::Sub(hn::LoadU(d, x + i), vmean), vinv);
            hn::StoreU(hn::MulAdd(norm, hn::LoadU(d, gamma + i), hn::LoadU(d, beta + i)), d, y + i);
        }
        for (; i < n; ++i) y[i] = ((x[i] - mean) * inv_std) * gamma[i] + beta[i];
    } else if (gamma) {
        for (; i + N <= n; i += N) {
            const auto norm = hn::Mul(hn::Sub(hn::LoadU(d, x + i), vmean), vinv);
            hn::StoreU(hn::Mul(norm, hn::LoadU(d, gamma + i)), d, y + i);
        }
        for (; i < n; ++i) y[i] = ((x[i] - mean) * inv_std) * gamma[i];
    } else if (beta) {
        for (; i + N <= n; i += N) {
            const auto norm = hn::Mul(hn::Sub(hn::LoadU(d, x + i), vmean), vinv);
            hn::StoreU(hn::Add(norm, hn::LoadU(d, beta + i)), d, y + i);
        }
        for (; i < n; ++i) y[i] = (x[i] - mean) * inv_std + beta[i];
    } else {
        for (; i + N <= n; i += N) {
            hn::StoreU(hn::Mul(hn::Sub(hn::LoadU(d, x + i), vmean), vinv), d, y + i);
        }
        for (; i < n; ++i) y[i] = (x[i] - mean) * inv_std;
    }
}

void GemvF32(size_t M, size_t K, float alpha, const float* A, size_t lda,
             const float* x, float beta, float* y) {
#if defined(_OPENMP)
    const bool use_omp = (M >= 64 && M <= static_cast<size_t>(std::numeric_limits<std::ptrdiff_t>::max()) &&
                          K > 0 && M >= 65536 / K);
    const std::ptrdiff_t m_signed = static_cast<std::ptrdiff_t>(M);
#pragma omp parallel for schedule(static) if(use_omp)
    for (std::ptrdiff_t i_s = 0; i_s < m_signed; ++i_s) {
        const size_t i = static_cast<size_t>(i_s);
        const float dot_val = DotImpl(A + i * lda, x, K);
        y[i] = (beta == 0.0f) ? (alpha * dot_val) : (alpha * dot_val + beta * y[i]);
    }
#else
    for (size_t i = 0; i < M; ++i) {
        const float dot_val = DotImpl(A + i * lda, x, K);
        y[i] = (beta == 0.0f) ? (alpha * dot_val) : (alpha * dot_val + beta * y[i]);
    }
#endif
}

void GemvF64(size_t M, size_t K, double alpha, const double* A, size_t lda,
             const double* x, double beta, double* y) {
#if defined(_OPENMP)
    const bool use_omp = (M >= 64 && M <= static_cast<size_t>(std::numeric_limits<std::ptrdiff_t>::max()) &&
                          K > 0 && M >= 65536 / K);
    const std::ptrdiff_t m_signed = static_cast<std::ptrdiff_t>(M);
#pragma omp parallel for schedule(static) if(use_omp)
    for (std::ptrdiff_t i_s = 0; i_s < m_signed; ++i_s) {
        const size_t i = static_cast<size_t>(i_s);
        const double dot_val = DotImpl(A + i * lda, x, K);
        y[i] = (beta == 0.0) ? (alpha * dot_val) : (alpha * dot_val + beta * y[i]);
    }
#else
    for (size_t i = 0; i < M; ++i) {
        const double dot_val = DotImpl(A + i * lda, x, K);
        y[i] = (beta == 0.0) ? (alpha * dot_val) : (alpha * dot_val + beta * y[i]);
    }
#endif
}

}  // namespace HWY_NAMESPACE
}  // namespace simd
}  // namespace nvxk
HWY_AFTER_NAMESPACE();

#endif  // NVXK_HIGHWAY_KERNELS_INL_H_
