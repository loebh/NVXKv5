#ifndef NVXK_SIMD_HPP
#define NVXK_SIMD_HPP

#include <nvxk/config.hpp>
#include <cstddef>
#include <string>
#include <vector>

namespace nvxk {
namespace simd {

struct SimdInfo {
    std::string backend_name = "scalar";
    bool highway_compiled = false;
    bool highway_active = false;
    std::string active_target = "Scalar";
    size_t lanes_f32 = 1;
    size_t lanes_f64 = 1;
    std::vector<std::string> supported_targets;
};

SimdInfo query_info();

// Portable vectorized operations (NVXK owns dispatch: Google Highway 1.4.0
// HWY_DYNAMIC_DISPATCH when enabled & beneficial, or Scalar reference fallback).
// All functions validate non-null pointers when n > 0 and safely handle
// unaligned pointers and arbitrary sizes.

void vec_add(const float* a, const float* b, float* c, size_t n,
             BackendKind backend = BackendKind::Auto);
void vec_add(const double* a, const double* b, double* c, size_t n,
             BackendKind backend = BackendKind::Auto);

void vec_sub(const float* a, const float* b, float* c, size_t n,
             BackendKind backend = BackendKind::Auto);
void vec_sub(const double* a, const double* b, double* c, size_t n,
             BackendKind backend = BackendKind::Auto);

void vec_mul(const float* a, const float* b, float* c, size_t n,
             BackendKind backend = BackendKind::Auto);
void vec_mul(const double* a, const double* b, double* c, size_t n,
             BackendKind backend = BackendKind::Auto);

// AXPY / FMA: y[i] = alpha * x[i] + y[i]
void vec_axpy(float alpha, const float* x, float* y, size_t n,
              BackendKind backend = BackendKind::Auto);
void vec_axpy(double alpha, const double* x, double* y, size_t n,
              BackendKind backend = BackendKind::Auto);

void vec_scale(float alpha, const float* x, float* y, size_t n,
               BackendKind backend = BackendKind::Auto);
void vec_scale(double alpha, const double* x, double* y, size_t n,
               BackendKind backend = BackendKind::Auto);

float dot(const float* a, const float* b, size_t n,
          BackendKind backend = BackendKind::Auto);
double dot(const double* a, const double* b, size_t n,
           BackendKind backend = BackendKind::Auto);

float l2_norm(const float* x, size_t n,
              BackendKind backend = BackendKind::Auto);
double l2_norm(const double* x, size_t n,
               BackendKind backend = BackendKind::Auto);

float sum(const float* x, size_t n,
          BackendKind backend = BackendKind::Auto);
double sum(const double* x, size_t n,
           BackendKind backend = BackendKind::Auto);

float max_val(const float* x, size_t n,
              BackendKind backend = BackendKind::Auto);
double max_val(const double* x, size_t n,
               BackendKind backend = BackendKind::Auto);

// Fully vectorized transcendental, activation & normalization kernels (v5.0.0)
void vec_exp(const float* x, float* y, size_t n,
             BackendKind backend = BackendKind::Auto);
void vec_sigmoid(const float* x, float* y, size_t n,
                 BackendKind backend = BackendKind::Auto);
void relu(const float* x, float* y, size_t n,
          BackendKind backend = BackendKind::Auto);
void gelu_approx(const float* x, float* y, size_t n,
                 BackendKind backend = BackendKind::Auto);
void softmax(const float* x, float* y, size_t n,
             BackendKind backend = BackendKind::Auto);
void layernorm(const float* x, float* y, size_t n, float eps = 1e-5f,
               const float* gamma = nullptr, const float* beta = nullptr,
               BackendKind backend = BackendKind::Auto);

// Row-major GEMV: y = alpha * A(M x K) * x(K) + beta * y(M)
void gemv(size_t M, size_t K, float alpha, const float* A, size_t lda,
          const float* x, float beta, float* y,
          BackendKind backend = BackendKind::Auto);
void gemv(size_t M, size_t K, double alpha, const double* A, size_t lda,
          const double* x, double beta, double* y,
          BackendKind backend = BackendKind::Auto);

}  // namespace simd
}  // namespace nvxk

#endif  // NVXK_SIMD_HPP
