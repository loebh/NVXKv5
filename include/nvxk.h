#ifndef NVXK_H
#define NVXK_H

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

// ============================================================================
// Phase 5/6: Hardware Discovery, Workload Analysis, Planner & Workspace Types
// ============================================================================
// Library version constants (v3.0.0)
#define NVXK_CPP_VERSION_MAJOR 3
#define NVXK_CPP_VERSION_MINOR 0
#define NVXK_CPP_VERSION_PATCH 0

enum class XK_Layout {
    RowMajor = 0,
    ColMajor = 1
};

enum class XK_Transpose {
    NoTrans = 0,
    Trans = 1
};

enum class XK_ExecMode {
    Auto = 0,
    Manual = 1
};

enum class XK_ParallelAxis {
    Auto = 0,
    None = 1,
    AxisM = 2,
    AxisN = 3
};

enum class XK_KernelKind {
    ScalarIkj = 0,
    Avx2Blis6x16 = 1,
    Avx2ThinM = 2,
    Avx2Blis6x16Asm = 3
};

struct XK_HardwareInfo {
    std::string vendor;
    std::string brand;
    int physical_cores;
    int logical_threads;
    size_t l1d_bytes;
    size_t l2_bytes;
    size_t l3_bytes;
    bool has_avx2;
    bool has_fma;
    bool has_avx512f;
    bool has_neon;

    XK_HardwareInfo()
        : vendor("unknown"),
          brand("unknown"),
          physical_cores(1),
          logical_threads(1),
          l1d_bytes(32 * 1024),
          l2_bytes(256 * 1024),
          l3_bytes(4 * 1024 * 1024),
          has_avx2(false),
          has_fma(false),
          has_avx512f(false),
          has_neon(false) {}
};

struct XK_HardwareProfile {
    XK_HardwareInfo hw;
    bool calibrated;
    double peak_gflops_1t;
    double mem_bandwidth_gbs;
    double omp_fork_overhead_us;
    double omp_barrier_overhead_us;
    double smt_efficiency;

    XK_HardwareProfile()
        : hw(),
          calibrated(false),
          peak_gflops_1t(80.0),
          mem_bandwidth_gbs(25.0),
          omp_fork_overhead_us(35.0),
          omp_barrier_overhead_us(6.0),
          smt_efficiency(0.12) {}
};

struct XK_WorkloadDesc {
    size_t M;
    size_t K;
    size_t N;
    size_t elem_bytes;
    size_t memory_budget_bytes;  // 0 = inherit workspace budget (default 64 MiB)
    XK_ExecMode mode;
    int manual_threads;          // used when mode == Manual (<=0 -> 1)
    size_t manual_mc;            // 0 = auto
    size_t manual_kc;            // 0 = auto
    size_t manual_nc;            // 0 = auto
    XK_ParallelAxis manual_axis;
    bool prefer_asm_kernel;      // experimental toggle for intrinsics vs inline ASM

    XK_WorkloadDesc(size_t m = 0, size_t k = 0, size_t n = 0,
                    size_t eb = sizeof(float))
        : M(m),
          K(k),
          N(n),
          elem_bytes(eb),
          memory_budget_bytes(0),
          mode(XK_ExecMode::Auto),
          manual_threads(1),
          manual_mc(0),
          manual_kc(0),
          manual_nc(0),
          manual_axis(XK_ParallelAxis::Auto),
          prefer_asm_kernel(true) {}
};

struct XK_PlanExplanation {
    double arithmetic_intensity;
    bool is_compute_bound;
    double predicted_time_us;
    double predicted_1t_time_us;
    double predicted_omp_overhead_us;
    bool smt_l2_shared;
    bool budget_clamped;
    std::string reason;

    XK_PlanExplanation()
        : arithmetic_intensity(0.0),
          is_compute_bound(true),
          predicted_time_us(0.0),
          predicted_1t_time_us(0.0),
          predicted_omp_overhead_us(0.0),
          smt_l2_shared(false),
          budget_clamped(false),
          reason() {}
};

struct XK_ExecutionPlan {
    XK_KernelKind kernel;
    XK_ParallelAxis axis;
    int threads;
    size_t mc;
    size_t kc;
    size_t nc;
    size_t required_workspace_bytes;
    XK_PlanExplanation explanation;

    XK_ExecutionPlan()
        : kernel(XK_KernelKind::ScalarIkj),
          axis(XK_ParallelAxis::None),
          threads(1),
          mc(120),
          kc(256),
          nc(1024),
          required_workspace_bytes(0),
          explanation() {}
};

class XK_Workspace {
public:
    explicit XK_Workspace(size_t memory_budget_bytes = 64 * 1024 * 1024);
    ~XK_Workspace();

    XK_Workspace(const XK_Workspace&) = delete;
    XK_Workspace& operator=(const XK_Workspace&) = delete;

    void set_memory_budget(size_t bytes);
    size_t memory_budget_bytes() const { return budget_bytes_; }
    size_t allocated_bytes() const { return b_bytes_ + a_bytes_; }

    // Pre-allocate 64-byte aligned scratchpads BEFORE entering any OpenMP parallel region.
    void prepare_f32(size_t b_floats, size_t a_floats_per_thread, int num_threads);
    void prepare_f64(size_t b_doubles, size_t a_doubles_per_thread, int num_threads);

    float* b_packed() { return static_cast<float*>(b_ptr_); }
    float* a_packed_for_thread(int tid) {
        return a_ptr_
                   ? (static_cast<float*>(a_ptr_) +
                      static_cast<size_t>(tid) * a_slot_stride_)
                   : nullptr;
    }

    double* b_packed_f64() { return static_cast<double*>(b_ptr_); }
    double* a_packed_f64_for_thread(int tid) {
        return a_ptr_
                   ? (static_cast<double*>(a_ptr_) +
                      static_cast<size_t>(tid) * a_slot_stride_)
                   : nullptr;
    }

    void release() noexcept;

private:
    void prepare_raw(size_t elem_size, size_t b_elems,
                     size_t a_elems_per_thread, int num_threads);

    size_t budget_bytes_;
    void* b_ptr_;
    size_t b_bytes_;
    void* a_ptr_;
    size_t a_bytes_;
    size_t a_slot_stride_;
};

// ============================================================================
// Hardware, Profile & Planner APIs
// ============================================================================
XK_HardwareInfo XK_detect_hardware();
XK_HardwareProfile XK_default_hardware_profile();
XK_HardwareProfile XK_calibrate_hardware(double budget_ms = 200.0);
bool XK_save_hardware_profile(const XK_HardwareProfile& profile,
                              const std::string& path);
bool XK_load_hardware_profile(const std::string& path,
                              XK_HardwareProfile& out_profile);
void XK_set_active_profile(const XK_HardwareProfile& profile);
XK_HardwareProfile XK_get_active_profile();

XK_ExecutionPlan XK_plan_gemm(const XK_WorkloadDesc& workload,
                              const XK_HardwareProfile& profile);

XK_ExecutionPlan XK_gemm_ex(const float* A, const float* B, float* C,
                            const XK_WorkloadDesc& workload,
                            XK_Workspace* workspace = nullptr,
                            const XK_HardwareProfile* profile = nullptr,
                            int* actual_thread_count = nullptr);

XK_ExecutionPlan XK_gemm_f64_ex(const double* A, const double* B, double* C,
                                const XK_WorkloadDesc& workload,
                                XK_Workspace* workspace = nullptr,
                                const XK_HardwareProfile* profile = nullptr,
                                int* actual_thread_count = nullptr);

// Generalized BLAS-style GEMM (v3.0+):
//   C = alpha * op(A) * op(B) + beta * C
// Supports RowMajor/ColMajor, NoTrans/Trans, arbitrary lda/ldb/ldc, alpha/beta,
// NaN-safe beta == 0, and zero-overhead fast-path routing to BLIS/ASM/Thin-M kernels.
XK_ExecutionPlan XK_gemm_blas(XK_Layout layout,
                              XK_Transpose trans_a,
                              XK_Transpose trans_b,
                              size_t M, size_t N, size_t K,
                              float alpha, const float* A, size_t lda,
                              const float* B, size_t ldb,
                              float beta, float* C, size_t ldc,
                              const XK_WorkloadDesc* opts = nullptr,
                              XK_Workspace* workspace = nullptr,
                              const XK_HardwareProfile* profile = nullptr,
                              int* actual_thread_count = nullptr);

XK_ExecutionPlan XK_gemm_blas(XK_Layout layout,
                              XK_Transpose trans_a,
                              XK_Transpose trans_b,
                              size_t M, size_t N, size_t K,
                              double alpha, const double* A, size_t lda,
                              const double* B, size_t ldb,
                              double beta, double* C, size_t ldc,
                              const XK_WorkloadDesc* opts = nullptr,
                              XK_Workspace* workspace = nullptr,
                              const XK_HardwareProfile* profile = nullptr,
                              int* actual_thread_count = nullptr);

const char* XK_version_string();

// ============================================================================
// Legacy / Core Public APIs (100% Backward-Compatible)
// ============================================================================
void vector_add_avx2(const float* a, const float* b, float* c, size_t n);
void vector_add_avx2(const double* a, const double* b, double* c, size_t n);
bool XK_has_avx2_fma();
bool XK_has_avx2_asm_kernel();

void XK_naive(const float* A, const float* B, float* C, size_t N);
void XK_ikj(const float* A, const float* B, float* C, size_t N);
void XK_avx2(const float* A, const float* B, float* C, size_t N);
void XK_avx2_omp(const float* A, const float* B, float* C, size_t N,
                 int thread_count);
void XK_avx2_omp(const float* A, const float* B, float* C, size_t N,
                 int thread_count, int* actual_thread_count);
void XK_auto(const float* A, const float* B, float* C, size_t N);
void XK_omp(const float* A, const float* B, float* C, size_t N,
            int thread_count);
void XK_omp(const float* A, const float* B, float* C, size_t N,
            int thread_count, int* actual_thread_count);

void XK_blocked(const float* A, const float* B, float* C, size_t N);
void XK_blocked(const float* A, const float* B, float* C, size_t N,
                size_t block_size);

void XK_gemm_avx2(const float* A, const float* B, float* C,
                  size_t M, size_t K, size_t N);
void XK_gemm_avx2(const double* A, const double* B, double* C,
                  size_t M, size_t K, size_t N);
void XK_gemm_avx2_asm(const float* A, const float* B, float* C,
                      size_t M, size_t K, size_t N);
void XK_gemm_blocked(const float* A, const float* B, float* C,
                     size_t M, size_t K, size_t N, size_t block_size = 64);
void XK_gemm_omp(const float* A, const float* B, float* C,
                 size_t M, size_t K, size_t N, int thread_count);
void XK_gemm_omp(const float* A, const float* B, float* C,
                 size_t M, size_t K, size_t N, int thread_count,
                 int* actual_thread_count);
void XK_gemm_packed_omp(const float* A, const float* B, float* C,
                        size_t M, size_t K, size_t N, int thread_count,
                        int* actual_thread_count = 0);
#ifdef NVXK_USE_OPENBLAS
void XK_cblas_sgemm(const float* A, const float* B, float* C,
                    size_t M, size_t K, size_t N);
#endif
void XK_gemm(const float* A, const float* B, float* C,
             size_t M, size_t K, size_t N);
void XK_gemm(const double* A, const double* B, double* C,
             size_t M, size_t K, size_t N);
void XK_gemm_i32_i64(const int32_t* A, const int32_t* B, int64_t* C,
                     size_t M, size_t K, size_t N);

// ============================================================================
// Internal Helpers & Template Implementations
// ============================================================================
namespace xk_detail {

template <typename T>
struct accumulator_type {
    typedef typename std::conditional<
        std::is_integral<T>::value && (sizeof(T) <= sizeof(int)),
        int64_t, T>::type type;
};

inline size_t checked_element_count(size_t rows, size_t columns) {
    if (columns != 0 && rows > std::numeric_limits<size_t>::max() / columns) {
        throw std::length_error("matrix dimensions are too large");
    }
    return rows * columns;
}

inline size_t checked_round_up(size_t value, size_t multiple) {
    if (multiple <= 1) return value;
    const size_t remainder = value % multiple;
    if (remainder == 0) return value;
    const size_t pad = multiple - remainder;
    if (value > std::numeric_limits<size_t>::max() - pad) {
        throw std::length_error("aligned capacity exceeds size_t range");
    }
    return value + pad;
}

inline void validate_ptrs(const void* A, const void* B, const void* C,
                          size_t M, size_t K, size_t N, const char* caller) {
    if ((M > 0 && K > 0 && A == nullptr) ||
        (K > 0 && N > 0 && B == nullptr) ||
        (M > 0 && N > 0 && C == nullptr)) {
        throw std::invalid_argument(std::string(caller) + ": null matrix pointer");
    }
}

inline bool ranges_overlap(const void* p1, size_t bytes1,
                           const void* p2, size_t bytes2) {
    if (bytes1 == 0 || bytes2 == 0 || p1 == nullptr || p2 == nullptr) {
        return false;
    }
    const uintptr_t s1 = reinterpret_cast<uintptr_t>(p1);
    const uintptr_t s2 = reinterpret_cast<uintptr_t>(p2);
    const uintptr_t e1 = (s1 > std::numeric_limits<uintptr_t>::max() - bytes1)
                             ? std::numeric_limits<uintptr_t>::max()
                             : (s1 + bytes1);
    const uintptr_t e2 = (s2 > std::numeric_limits<uintptr_t>::max() - bytes2)
                             ? std::numeric_limits<uintptr_t>::max()
                             : (s2 + bytes2);
    return (s1 < e2) && (s2 < e1);
}

inline void validate_no_aliasing(const void* A, const void* B, const void* C,
                                 size_t M, size_t K, size_t N,
                                 size_t elem_a, size_t elem_b, size_t elem_c,
                                 const char* caller) {
    const size_t a_bytes = checked_element_count(checked_element_count(M, K), elem_a);
    const size_t b_bytes = checked_element_count(checked_element_count(K, N), elem_b);
    const size_t c_bytes = checked_element_count(checked_element_count(M, N), elem_c);
    if (ranges_overlap(C, c_bytes, A, a_bytes) ||
        ranges_overlap(C, c_bytes, B, b_bytes)) {
        throw std::invalid_argument(
            std::string(caller) + ": output matrix C must not alias A or B");
    }
}

template <typename T>
void store_result(typename accumulator_type<T>::type value, T& output,
                  typename std::enable_if<std::is_integral<T>::value>::type* = 0) {
    if (value < static_cast<int64_t>(std::numeric_limits<T>::min()) ||
        value > static_cast<int64_t>(std::numeric_limits<T>::max())) {
        throw std::overflow_error("integer matrix result is out of range");
    }
    output = static_cast<T>(value);
}

template <typename T>
void store_result(typename accumulator_type<T>::type value, T& output,
                  typename std::enable_if<!std::is_integral<T>::value>::type* = 0) {
    output = static_cast<T>(value);
}

inline int64_t add_checked(int64_t sum, int64_t product) {
    if ((product > 0 && sum > std::numeric_limits<int64_t>::max() - product) ||
        (product < 0 && sum < std::numeric_limits<int64_t>::min() - product)) {
        throw std::overflow_error("integer matrix accumulation is out of range");
    }
    return sum + product;
}

template <typename T>
typename accumulator_type<T>::type multiply_add(
    typename accumulator_type<T>::type sum, T left, T right,
    typename std::enable_if<std::is_integral<T>::value>::type* = 0) {
    const int64_t product = static_cast<int64_t>(left) * static_cast<int64_t>(right);
    return add_checked(sum, product);
}

template <typename T>
typename accumulator_type<T>::type multiply_add(
    typename accumulator_type<T>::type sum, T left, T right,
    typename std::enable_if<!std::is_integral<T>::value>::type* = 0) {
    return sum + left * right;
}

template <typename T>
void gemm_ikj_impl(const T* A, const T* B, T* C, size_t M, size_t K, size_t N,
                   typename std::enable_if<std::is_integral<T>::value>::type* = 0) {
    checked_element_count(M, N);
    std::vector<typename accumulator_type<T>::type> row_sums(N, 0);
    for (size_t i = 0; i < M; ++i) {
        std::fill(row_sums.begin(), row_sums.end(), 0);
        for (size_t k = 0; k < K; ++k) {
            const T a_ik = A[i * K + k];
            for (size_t j = 0; j < N; ++j) {
                row_sums[j] = multiply_add(row_sums[j], a_ik, B[k * N + j]);
            }
        }
        for (size_t j = 0; j < N; ++j) {
            store_result(row_sums[j], C[i * N + j]);
        }
    }
}

template <typename T>
void gemm_ikj_impl(const T* A, const T* B, T* C, size_t M, size_t K, size_t N,
                   typename std::enable_if<!std::is_integral<T>::value>::type* = 0) {
    const size_t output_count = checked_element_count(M, N);
    for (size_t i = 0; i < output_count; ++i) C[i] = static_cast<T>(0);
    for (size_t i = 0; i < M; ++i) {
        for (size_t k = 0; k < K; ++k) {
            const T a_ik = A[i * K + k];
            for (size_t j = 0; j < N; ++j) {
                C[i * N + j] += a_ik * B[k * N + j];
            }
        }
    }
}

}  // namespace xk_detail

template <typename T>
void XK_gemm_naive(const T* A, const T* B, T* C, size_t M, size_t K, size_t N) {
    static_assert(std::is_floating_point<T>::value ||
                      (std::is_integral<T>::value && std::is_signed<T>::value &&
                       sizeof(T) <= sizeof(int)),
                  "XK_gemm supports floating-point and signed integer types up to int");
    xk_detail::checked_element_count(M, K);
    xk_detail::checked_element_count(K, N);
    xk_detail::checked_element_count(M, N);
    if (M == 0 || N == 0) return;
    xk_detail::validate_ptrs(A, B, C, M, K, N, "XK_gemm_naive");
    xk_detail::validate_no_aliasing(A, B, C, M, K, N,
                                    sizeof(T), sizeof(T), sizeof(T),
                                    "XK_gemm_naive");
    for (size_t i = 0; i < M; ++i) {
        for (size_t j = 0; j < N; ++j) {
            typename xk_detail::accumulator_type<T>::type sum = 0;
            for (size_t k = 0; k < K; ++k) {
                sum = xk_detail::multiply_add(sum, A[i * K + k], B[k * N + j]);
            }
            xk_detail::store_result(sum, C[i * N + j]);
        }
    }
}

template <typename T>
void XK_gemm_ikj(const T* A, const T* B, T* C, size_t M, size_t K, size_t N) {
    static_assert(std::is_floating_point<T>::value ||
                      (std::is_integral<T>::value && std::is_signed<T>::value &&
                       sizeof(T) <= sizeof(int)),
                  "XK_gemm supports floating-point and signed integer types up to int");
    xk_detail::checked_element_count(M, K);
    xk_detail::checked_element_count(K, N);
    xk_detail::checked_element_count(M, N);
    if (M == 0 || N == 0) return;
    xk_detail::validate_ptrs(A, B, C, M, K, N, "XK_gemm_ikj");
    xk_detail::validate_no_aliasing(A, B, C, M, K, N,
                                    sizeof(T), sizeof(T), sizeof(T),
                                    "XK_gemm_ikj");
    xk_detail::gemm_ikj_impl(A, B, C, M, K, N);
}

template <typename T>
void XK_gemm(const T* A, const T* B, T* C, size_t M, size_t K, size_t N) {
    XK_gemm_ikj(A, B, C, M, K, N);
}

#endif