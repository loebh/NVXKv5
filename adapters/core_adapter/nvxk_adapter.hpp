#ifndef NVXK_ADAPTER_HPP
#define NVXK_ADAPTER_HPP

#include <nvxk.h>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace nvxk_wb {

struct BackendInfo {
    std::string id;
    std::string display_name;
    std::string category;       // "Adaptive", "SIMD", "Multi-thread", "Scalar"
    std::string supported_precisions; // "f32", "f32,f64", "i32->i64"
    bool available;
    std::string availability_note;
};

struct ValidationMetrics {
    bool executed = false;
    bool passed = false;
    double max_abs_error = 0.0;
    double max_rel_error = 0.0;
    double rmse = 0.0;
    size_t nan_inf_count = 0;
    double abs_tol = 0.0;
    double rel_tol = 0.0;
    std::string reference_backend = "f64_ikj";
    std::string precision = "f32";
    uint32_t seed = 42;
    size_t M = 0, K = 0, N = 0;
    // Downsampled error heatmap (rows x cols, <= 16x16)
    size_t heatmap_rows = 0;
    size_t heatmap_cols = 0;
    std::vector<double> error_heatmap;
    std::string failure_detail;
};

struct MatrixRunSummary {
    bool success = false;
    std::string error_message;
    std::string operation;   // "gemm" or "vector_add"
    std::string precision;   // "f32", "f64", "i32_i64"
    std::string backend;
    size_t M = 0, K = 0, N = 0;
    int threads_requested = 1;
    int threads_used = 1;
    double elapsed_ms = 0.0;
    double gflops = 0.0;
    double effective_bandwidth_gbs = 0.0;
    size_t workspace_bytes = 0;
    size_t working_set_bytes = 0;
    bool aligned_64b = true;
    XK_ExecutionPlan plan;
    bool has_plan = false;
    ValidationMetrics validation;

    // Preview / downsampled matrices for visualization (<= 16x16)
    size_t a_preview_rows = 0, a_preview_cols = 0;
    std::vector<double> a_preview;
    size_t b_preview_rows = 0, b_preview_cols = 0;
    std::vector<double> b_preview;
    size_t c_preview_rows = 0, c_preview_cols = 0;
    std::vector<double> c_preview;
};

// Returns strictly the backends actually implemented in NVXK core.
std::vector<BackendInfo> list_implemented_backends();

// Human-readable name for XK_KernelKind.
std::string kernel_kind_to_string(XK_KernelKind kind);

// Human-readable name for XK_ParallelAxis.
std::string parallel_axis_to_string(XK_ParallelAxis axis);

// Validate inputs and compute plan without running kernel.
bool inspect_gemm_plan(size_t M, size_t K, size_t N,
                       const std::string& precision,
                       const std::string& backend,
                       int threads,
                       size_t memory_budget_bytes,
                       XK_ExecutionPlan& out_plan,
                       std::string& out_error);

// Execute a single matrix/vector operation with optional custom small inputs and f64 validation.
MatrixRunSummary run_matrix_lab_operation(
    const std::string& operation,
    const std::string& precision,
    const std::string& backend,
    size_t M, size_t K, size_t N,
    int threads,
    size_t memory_budget_bytes,
    uint32_t seed,
    bool run_validation,
    const std::vector<double>* custom_A = nullptr,
    const std::vector<double>* custom_B = nullptr);

// Reference f64 GEMM + validation helper reused by both Matrix Lab and Benchmark Runner.
// Execute generalized BLAS GEMM: C = alpha * op(A) * op(B) + beta * C with f64 validation (v3.0+)
MatrixRunSummary run_generalized_gemm_lab(
    const std::string& precision,
    const std::string& layout,
    const std::string& trans_a,
    const std::string& trans_b,
    size_t M, size_t N, size_t K,
    double alpha, double beta,
    int threads,
    size_t memory_budget_bytes,
    uint32_t seed,
    bool run_validation);

ValidationMetrics validate_gemm_f32_against_f64(
    const float* A, const float* B, const float* C_got,
    size_t M, size_t K, size_t N,
    uint32_t seed,
    double custom_abs_tol = -1.0,
    double custom_rel_tol = -1.0);

ValidationMetrics validate_gemm_f64_against_ikj(
    const double* A, const double* B, const double* C_got,
    size_t M, size_t K, size_t N,
    uint32_t seed);

// Simple 64-byte aligned buffer helper (C++17 portable MinGW + Linux).
template <typename T>
class AlignedBuffer {
public:
    AlignedBuffer() = default;
    explicit AlignedBuffer(size_t count) { resize(count); }
    ~AlignedBuffer() { free_mem(); }
    AlignedBuffer(const AlignedBuffer&) = delete;
    AlignedBuffer& operator=(const AlignedBuffer&) = delete;
    AlignedBuffer(AlignedBuffer&& other) noexcept
        : ptr_(other.ptr_), count_(other.count_) {
        other.ptr_ = nullptr;
        other.count_ = 0;
    }
    AlignedBuffer& operator=(AlignedBuffer&& other) noexcept {
        if (this != &other) {
            free_mem();
            ptr_ = other.ptr_;
            count_ = other.count_;
            other.ptr_ = nullptr;
            other.count_ = 0;
        }
        return *this;
    }

    void resize(size_t count);
    T* data() { return ptr_; }
    const T* data() const { return ptr_; }
    size_t size() const { return count_; }
    bool is_64b_aligned() const {
        return ptr_ == nullptr || (reinterpret_cast<uintptr_t>(ptr_) % 64 == 0);
    }

private:
    void free_mem() noexcept;
    T* ptr_ = nullptr;
    size_t count_ = 0;
};

}  // namespace nvxk_wb

#endif