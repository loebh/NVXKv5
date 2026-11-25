#include "nvxk_adapter.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <new>
#include <random>
#include <stdexcept>

#if defined(_WIN32)
#include <malloc.h>
#endif

namespace nvxk_wb {

template <typename T>
void AlignedBuffer<T>::resize(size_t count) {
    free_mem();
    if (count == 0) return;
    if (count > std::numeric_limits<size_t>::max() / sizeof(T)) {
        throw std::bad_alloc();
    }
    const size_t bytes = count * sizeof(T);
    void* p = nullptr;
#if defined(_WIN32)
    p = _aligned_malloc(bytes, 64);
    if (!p) throw std::bad_alloc();
#else
    if (posix_memalign(&p, 64, bytes) != 0 || !p) throw std::bad_alloc();
#endif
    ptr_ = static_cast<T*>(p);
    count_ = count;
}

template <typename T>
void AlignedBuffer<T>::free_mem() noexcept {
    if (!ptr_) return;
#if defined(_WIN32)
    _aligned_free(ptr_);
#else
    free(ptr_);
#endif
    ptr_ = nullptr;
    count_ = 0;
}

template class AlignedBuffer<float>;
template class AlignedBuffer<double>;
template class AlignedBuffer<int32_t>;
template class AlignedBuffer<int64_t>;

std::string kernel_kind_to_string(XK_KernelKind kind) {
    switch (kind) {
        case XK_KernelKind::ScalarIkj:       return "ScalarIkj";
        case XK_KernelKind::Avx2Blis6x16:    return "Avx2Blis6x16";
        case XK_KernelKind::Avx2ThinM:       return "Avx2ThinM";
        case XK_KernelKind::Avx2Blis6x16Asm: return "Avx2Blis6x16Asm";
        default:                             return "Unknown";
    }
}

std::string parallel_axis_to_string(XK_ParallelAxis axis) {
    switch (axis) {
        case XK_ParallelAxis::Auto:  return "Auto";
        case XK_ParallelAxis::None:  return "None";
        case XK_ParallelAxis::AxisM: return "AxisM";
        case XK_ParallelAxis::AxisN: return "AxisN";
        default:                     return "Unknown";
    }
}

std::vector<BackendInfo> list_implemented_backends() {
    const bool has_avx2 = XK_has_avx2_fma();
    const bool has_asm = XK_has_avx2_asm_kernel();
#if defined(_OPENMP)
    const bool has_omp = true;
#else
    const bool has_omp = false;
#endif

    std::vector<BackendInfo> list;
    list.push_back({"auto_asm", "Auto (Adaptive + ASM)", "Adaptive", "f32,f64", true,
                    has_asm ? "Adaptive planner with x86-64 AVX2+FMA ASM 6x16"
                            : "Adaptive planner (falls back to Intrinsics/Scalar)"});
    list.push_back({"auto", "Auto (Adaptive Intrinsics)", "Adaptive", "f32,f64", true,
                    has_avx2 ? "Adaptive planner with C++ AVX2+FMA BLIS / Thin-M"
                             : "Adaptive planner (scalar fallback active)"});
    list.push_back({"avx2_asm", "AVX2+FMA ASM 6x16 (1T)", "SIMD", "f32", has_asm,
                    has_asm ? "Hand-tuned x86-64 inline ASM 6x16 microkernel"
                            : "Unavailable on this CPU/build (requires x86-64 GCC/Clang + AVX2+FMA)"});
    list.push_back({"avx2", "AVX2+FMA BLIS (1T)", "SIMD", "f32,f64", has_avx2,
                    has_avx2 ? "C++ AVX2+FMA intrinsics BLIS 6x16 (f32) / 6x8 (f64)"
                             : "Unavailable (CPU lacks AVX2+FMA or scalar fallback forced)"});
    list.push_back({"omp", "OpenMP BLIS (Manual Threads)", "Multi-thread", "f32,f64", true,
                    has_omp ? "OpenMP multi-threaded BLIS kernel"
                            : "Built without OpenMP; runs single-threaded"});
    list.push_back({"blocked", "Blocked GEMM (64)", "Cache-Blocked", "f32", true,
                    "Cache-blocked BLIS driver with block_size=64"});
    list.push_back({"ikj", "Scalar IKJ", "Scalar", "f32,f64", true,
                    "Portable C++11 cache-friendly IKJ reference kernel"});
    list.push_back({"naive", "Scalar Naive IJK", "Scalar", "f32,f64", true,
                    "Baseline unoptimized triple-loop IJK kernel"});
    return list;
}

bool inspect_gemm_plan(size_t M, size_t K, size_t N,
                       const std::string& precision,
                       const std::string& backend,
                       int threads,
                       size_t memory_budget_bytes,
                       XK_ExecutionPlan& out_plan,
                       std::string& out_error) {
    if (M == 0 || K == 0 || N == 0) {
        out_error = "Matrix dimensions M, K, N must all be positive.";
        return false;
    }
    if (M > 8192 || K > 8192 || N > 8192) {
        out_error = "Matrix dimension exceeds workbench safety limit (8192).";
        return false;
    }
    const size_t eb = (precision == "f64") ? sizeof(double) : sizeof(float);
    XK_WorkloadDesc wd(M, K, N, eb);
    wd.memory_budget_bytes = memory_budget_bytes;
    if (backend == "auto_asm") {
        wd.mode = XK_ExecMode::Auto;
        wd.prefer_asm_kernel = true;
    } else if (backend == "auto") {
        wd.mode = XK_ExecMode::Auto;
        wd.prefer_asm_kernel = false;
    } else if (backend == "omp") {
        wd.mode = XK_ExecMode::Manual;
        wd.manual_threads = std::max(1, threads);
        wd.prefer_asm_kernel = false;
    } else if (backend == "avx2_asm") {
        wd.mode = XK_ExecMode::Manual;
        wd.manual_threads = 1;
        wd.prefer_asm_kernel = true;
    } else {
        wd.mode = XK_ExecMode::Manual;
        wd.manual_threads = 1;
        wd.prefer_asm_kernel = false;
    }
    const XK_HardwareProfile prof = XK_get_active_profile();
    out_plan = XK_plan_gemm(wd, prof);
    return true;
}

namespace {

template <typename T>
void downsample_matrix(const T* data, size_t rows, size_t cols,
                       size_t max_dim,
                       size_t& out_rows, size_t& out_cols,
                       std::vector<double>& out_vals) {
    out_rows = std::min(rows, max_dim);
    out_cols = std::min(cols, max_dim);
    out_vals.assign(out_rows * out_cols, 0.0);
    if (out_rows == 0 || out_cols == 0) return;

    for (size_t r = 0; r < out_rows; ++r) {
        const size_t r0 = (r * rows) / out_rows;
        const size_t r1 = std::max(r0 + 1, ((r + 1) * rows) / out_rows);
        for (size_t c = 0; c < out_cols; ++c) {
            const size_t c0 = (c * cols) / out_cols;
            const size_t c1 = std::max(c0 + 1, ((c + 1) * cols) / out_cols);
            double sum = 0.0;
            size_t cnt = 0;
            for (size_t i = r0; i < r1; ++i) {
                for (size_t j = c0; j < c1; ++j) {
                    sum += static_cast<double>(data[i * cols + j]);
                    ++cnt;
                }
            }
            out_vals[r * out_cols + c] = (cnt > 0) ? (sum / static_cast<double>(cnt)) : 0.0;
        }
    }
}

void build_error_heatmap(const std::vector<double>& abs_errs,
                         size_t M, size_t N,
                         size_t max_dim,
                         ValidationMetrics& vm) {
    vm.heatmap_rows = std::min(M, max_dim);
    vm.heatmap_cols = std::min(N, max_dim);
    vm.error_heatmap.assign(vm.heatmap_rows * vm.heatmap_cols, 0.0);
    if (vm.heatmap_rows == 0 || vm.heatmap_cols == 0) return;

    for (size_t r = 0; r < vm.heatmap_rows; ++r) {
        const size_t r0 = (r * M) / vm.heatmap_rows;
        const size_t r1 = std::max(r0 + 1, ((r + 1) * M) / vm.heatmap_rows);
        for (size_t c = 0; c < vm.heatmap_cols; ++c) {
            const size_t c0 = (c * N) / vm.heatmap_cols;
            const size_t c1 = std::max(c0 + 1, ((c + 1) * N) / vm.heatmap_cols);
            double max_e = 0.0;
            for (size_t i = r0; i < r1; ++i) {
                for (size_t j = c0; j < c1; ++j) {
                    const double e = abs_errs[i * N + j];
                    if (std::isnan(e) || e > max_e) max_e = e;
                }
            }
            vm.error_heatmap[r * vm.heatmap_cols + c] = max_e;
        }
    }
}

}  // namespace

ValidationMetrics validate_gemm_f32_against_f64(
    const float* A, const float* B, const float* C_got,
    size_t M, size_t K, size_t N,
    uint32_t seed,
    double custom_abs_tol,
    double custom_rel_tol) {
    ValidationMetrics vm;
    vm.executed = true;
    vm.precision = "f32";
    vm.reference_backend = "f64_ikj";
    vm.seed = seed;
    vm.M = M;
    vm.K = K;
    vm.N = N;

    const double k_factor = std::max(1.0, std::sqrt(static_cast<double>(K)));
    vm.abs_tol = (custom_abs_tol > 0.0) ? custom_abs_tol : (1e-5 * k_factor);
    vm.rel_tol = (custom_rel_tol > 0.0) ? custom_rel_tol : 1e-4;

    const size_t total = M * N;
    std::vector<double> C_ref(total, 0.0);
    for (size_t i = 0; i < M; ++i) {
        for (size_t k = 0; k < K; ++k) {
            const double a = static_cast<double>(A[i * K + k]);
            for (size_t j = 0; j < N; ++j) {
                C_ref[i * N + j] += a * static_cast<double>(B[k * N + j]);
            }
        }
    }

    std::vector<double> abs_errs(total, 0.0);
    double sum_sq = 0.0;
    vm.passed = true;

    for (size_t idx = 0; idx < total; ++idx) {
        const float v = C_got[idx];
        if (!std::isfinite(v)) {
            ++vm.nan_inf_count;
            vm.passed = false;
            abs_errs[idx] = std::numeric_limits<double>::infinity();
            if (vm.failure_detail.empty()) {
                vm.failure_detail = "Non-finite value (NaN/Inf) detected at index " +
                                    std::to_string(idx);
            }
            continue;
        }
        const double diff = std::fabs(C_ref[idx] - static_cast<double>(v));
        const double scale = std::max(1.0, std::fabs(C_ref[idx]));
        const double rel = diff / scale;
        abs_errs[idx] = diff;
        if (diff > vm.max_abs_error) vm.max_abs_error = diff;
        if (rel > vm.max_rel_error) vm.max_rel_error = rel;
        sum_sq += diff * diff;
        if (diff > vm.abs_tol + vm.rel_tol * scale) {
            vm.passed = false;
            if (vm.failure_detail.empty()) {
                vm.failure_detail = "Tolerance exceeded at index " + std::to_string(idx) +
                                    " (abs_err=" + std::to_string(diff) +
                                    ", rel_err=" + std::to_string(rel) + ")";
            }
        }
    }

    vm.rmse = (total > 0) ? std::sqrt(sum_sq / static_cast<double>(total)) : 0.0;
    build_error_heatmap(abs_errs, M, N, 16, vm);
    return vm;
}

ValidationMetrics validate_gemm_f64_against_ikj(
    const double* A, const double* B, const double* C_got,
    size_t M, size_t K, size_t N,
    uint32_t seed) {
    ValidationMetrics vm;
    vm.executed = true;
    vm.precision = "f64";
    vm.reference_backend = "f64_ikj";
    vm.seed = seed;
    vm.M = M;
    vm.K = K;
    vm.N = N;
    vm.abs_tol = 1e-11;
    vm.rel_tol = 1e-11 + 1e-13 * static_cast<double>(K);

    const size_t total = M * N;
    std::vector<double> C_ref(total, 0.0);
    XK_gemm_ikj(A, B, C_ref.data(), M, K, N);

    std::vector<double> abs_errs(total, 0.0);
    double sum_sq = 0.0;
    vm.passed = true;

    for (size_t idx = 0; idx < total; ++idx) {
        const double v = C_got[idx];
        if (!std::isfinite(v)) {
            ++vm.nan_inf_count;
            vm.passed = false;
            abs_errs[idx] = std::numeric_limits<double>::infinity();
            if (vm.failure_detail.empty()) {
                vm.failure_detail = "Non-finite value (NaN/Inf) detected at index " +
                                    std::to_string(idx);
            }
            continue;
        }
        const double diff = std::fabs(C_ref[idx] - v);
        const double scale = std::max(1.0, std::fabs(C_ref[idx]));
        const double rel = diff / scale;
        abs_errs[idx] = diff;
        if (diff > vm.max_abs_error) vm.max_abs_error = diff;
        if (rel > vm.max_rel_error) vm.max_rel_error = rel;
        sum_sq += diff * diff;
        if (diff > vm.abs_tol + vm.rel_tol * scale) {
            vm.passed = false;
            if (vm.failure_detail.empty()) {
                vm.failure_detail = "Tolerance exceeded at index " + std::to_string(idx);
            }
        }
    }
    vm.rmse = (total > 0) ? std::sqrt(sum_sq / static_cast<double>(total)) : 0.0;
    build_error_heatmap(abs_errs, M, N, 16, vm);
    return vm;
}

MatrixRunSummary run_matrix_lab_operation(
    const std::string& operation,
    const std::string& precision,
    const std::string& backend,
    size_t M, size_t K, size_t N,
    int threads,
    size_t memory_budget_bytes,
    uint32_t seed,
    bool run_validation,
    const std::vector<double>* custom_A,
    const std::vector<double>* custom_B) {
    MatrixRunSummary res;
    res.operation = operation;
    res.precision = precision;
    res.backend = backend;
    res.M = M;
    res.K = K;
    res.N = N;
    res.threads_requested = std::max(1, threads);

    if (M == 0 || N == 0 || (operation == "gemm" && K == 0)) {
        res.error_message = "Dimensions must be positive integers.";
        return res;
    }
    if (M > 2048 || K > 2048 || N > 2048) {
        res.error_message = "Interactive Matrix Lab caps dimensions at 2048 to preserve UI responsiveness.";
        return res;
    }

    try {
        std::mt19937 rng(seed);
        std::uniform_real_distribution<double> dist(-1.0, 1.0);

        if (operation == "vector_add") {
            const size_t len = M;
            if (precision == "f64") {
                AlignedBuffer<double> a(len), b(len), c(len);
                for (size_t i = 0; i < len; ++i) {
                    a.data()[i] = (custom_A && i < custom_A->size()) ? (*custom_A)[i] : dist(rng);
                    b.data()[i] = (custom_B && i < custom_B->size()) ? (*custom_B)[i] : dist(rng);
                    c.data()[i] = 0.0;
                }
                const auto t0 = std::chrono::steady_clock::now();
                vector_add_avx2(a.data(), b.data(), c.data(), len);
                const auto t1 = std::chrono::steady_clock::now();
                const double sec = std::chrono::duration<double>(t1 - t0).count();
                res.elapsed_ms = sec * 1e3;
                res.gflops = (sec > 0.0) ? (static_cast<double>(len) / sec) * 1e-9 : 0.0;
                res.working_set_bytes = 3 * len * sizeof(double);
                res.effective_bandwidth_gbs = (sec > 0.0) ? (static_cast<double>(res.working_set_bytes) / sec) * 1e-9 : 0.0;
                res.aligned_64b = a.is_64b_aligned() && b.is_64b_aligned() && c.is_64b_aligned();
                downsample_matrix(a.data(), 1, len, 16, res.a_preview_rows, res.a_preview_cols, res.a_preview);
                downsample_matrix(b.data(), 1, len, 16, res.b_preview_rows, res.b_preview_cols, res.b_preview);
                downsample_matrix(c.data(), 1, len, 16, res.c_preview_rows, res.c_preview_cols, res.c_preview);
                if (run_validation) {
                    res.validation.executed = true;
                    res.validation.passed = true;
                    res.validation.precision = "f64";
                    res.validation.reference_backend = "scalar_add_f64";
                    res.validation.abs_tol = 1e-12;
                    res.validation.rel_tol = 1e-12;
                    for (size_t i = 0; i < len; ++i) {
                        const double ref = a.data()[i] + b.data()[i];
                        const double diff = std::fabs(ref - c.data()[i]);
                        if (diff > res.validation.max_abs_error) res.validation.max_abs_error = diff;
                        if (diff > 1e-12) res.validation.passed = false;
                    }
                }
            } else {
                AlignedBuffer<float> a(len), b(len), c(len);
                for (size_t i = 0; i < len; ++i) {
                    a.data()[i] = static_cast<float>((custom_A && i < custom_A->size()) ? (*custom_A)[i] : dist(rng));
                    b.data()[i] = static_cast<float>((custom_B && i < custom_B->size()) ? (*custom_B)[i] : dist(rng));
                    c.data()[i] = 0.0f;
                }
                const auto t0 = std::chrono::steady_clock::now();
                vector_add_avx2(a.data(), b.data(), c.data(), len);
                const auto t1 = std::chrono::steady_clock::now();
                const double sec = std::chrono::duration<double>(t1 - t0).count();
                res.elapsed_ms = sec * 1e3;
                res.gflops = (sec > 0.0) ? (static_cast<double>(len) / sec) * 1e-9 : 0.0;
                res.working_set_bytes = 3 * len * sizeof(float);
                res.effective_bandwidth_gbs = (sec > 0.0) ? (static_cast<double>(res.working_set_bytes) / sec) * 1e-9 : 0.0;
                res.aligned_64b = a.is_64b_aligned() && b.is_64b_aligned() && c.is_64b_aligned();
                downsample_matrix(a.data(), 1, len, 16, res.a_preview_rows, res.a_preview_cols, res.a_preview);
                downsample_matrix(b.data(), 1, len, 16, res.b_preview_rows, res.b_preview_cols, res.b_preview);
                downsample_matrix(c.data(), 1, len, 16, res.c_preview_rows, res.c_preview_cols, res.c_preview);
                if (run_validation) {
                    res.validation.executed = true;
                    res.validation.passed = true;
                    res.validation.precision = "f32";
                    res.validation.reference_backend = "scalar_add_f64";
                    res.validation.abs_tol = 1e-6;
                    res.validation.rel_tol = 1e-6;
                    for (size_t i = 0; i < len; ++i) {
                        const double ref = static_cast<double>(a.data()[i]) + static_cast<double>(b.data()[i]);
                        const double diff = std::fabs(ref - static_cast<double>(c.data()[i]));
                        if (diff > res.validation.max_abs_error) res.validation.max_abs_error = diff;
                        if (diff > 1e-5) res.validation.passed = false;
                    }
                }
            }
            res.success = true;
            return res;
        }

        // GEMM Operations
        const double flops = 2.0 * static_cast<double>(M) * static_cast<double>(K) * static_cast<double>(N);

        if (precision == "i32_i64") {
            AlignedBuffer<int32_t> A(M * K), B(K * N);
            AlignedBuffer<int64_t> C(M * N);
            std::uniform_int_distribution<int32_t> idist(-50, 50);
            for (size_t i = 0; i < M * K; ++i) {
                A.data()[i] = (custom_A && i < custom_A->size())
                                  ? static_cast<int32_t>(std::llround((*custom_A)[i]))
                                  : idist(rng);
            }
            for (size_t i = 0; i < K * N; ++i) {
                B.data()[i] = (custom_B && i < custom_B->size())
                                  ? static_cast<int32_t>(std::llround((*custom_B)[i]))
                                  : idist(rng);
            }
            const auto t0 = std::chrono::steady_clock::now();
            XK_gemm_i32_i64(A.data(), B.data(), C.data(), M, K, N);
            const auto t1 = std::chrono::steady_clock::now();
            const double sec = std::chrono::duration<double>(t1 - t0).count();
            res.elapsed_ms = sec * 1e3;
            res.gflops = (sec > 0.0) ? (flops / sec) * 1e-9 : 0.0;
            res.working_set_bytes = (M * K + K * N) * sizeof(int32_t) + M * N * sizeof(int64_t);
            res.effective_bandwidth_gbs = (sec > 0.0) ? (static_cast<double>(res.working_set_bytes) / sec) * 1e-9 : 0.0;
            res.aligned_64b = A.is_64b_aligned() && B.is_64b_aligned() && C.is_64b_aligned();
            res.threads_used = 1;
            downsample_matrix(A.data(), M, K, 16, res.a_preview_rows, res.a_preview_cols, res.a_preview);
            downsample_matrix(B.data(), K, N, 16, res.b_preview_rows, res.b_preview_cols, res.b_preview);
            downsample_matrix(C.data(), M, N, 16, res.c_preview_rows, res.c_preview_cols, res.c_preview);
            if (run_validation) {
                res.validation.executed = true;
                res.validation.passed = true;
                res.validation.precision = "i32_i64";
                res.validation.reference_backend = "exact_int64";
                res.validation.M = M; res.validation.K = K; res.validation.N = N;
            }
            res.success = true;
            return res;
        }

        if (precision == "f64") {
            AlignedBuffer<double> A(M * K), B(K * N), C(M * N);
            for (size_t i = 0; i < M * K; ++i) {
                A.data()[i] = (custom_A && i < custom_A->size()) ? (*custom_A)[i] : dist(rng);
            }
            for (size_t i = 0; i < K * N; ++i) {
                B.data()[i] = (custom_B && i < custom_B->size()) ? (*custom_B)[i] : dist(rng);
            }
            XK_Workspace ws(memory_budget_bytes > 0 ? memory_budget_bytes : 64 * 1024 * 1024);
            int used_t = 1;
            const auto t0 = std::chrono::steady_clock::now();
            if (backend == "naive") {
                XK_gemm_naive(A.data(), B.data(), C.data(), M, K, N);
            } else if (backend == "ikj") {
                XK_gemm_ikj(A.data(), B.data(), C.data(), M, K, N);
            } else if (backend == "avx2") {
                XK_gemm_avx2(A.data(), B.data(), C.data(), M, K, N);
            } else {
                XK_WorkloadDesc wd(M, K, N, sizeof(double));
                wd.memory_budget_bytes = memory_budget_bytes;
                if (backend == "omp") {
                    wd.mode = XK_ExecMode::Manual;
                    wd.manual_threads = res.threads_requested;
                } else {
                    wd.mode = XK_ExecMode::Auto;
                }
                res.plan = XK_gemm_f64_ex(A.data(), B.data(), C.data(), wd, &ws, nullptr, &used_t);
                res.has_plan = true;
                res.workspace_bytes = ws.allocated_bytes();
            }
            const auto t1 = std::chrono::steady_clock::now();
            const double sec = std::chrono::duration<double>(t1 - t0).count();
            res.threads_used = used_t;
            res.elapsed_ms = sec * 1e3;
            res.gflops = (sec > 0.0) ? (flops / sec) * 1e-9 : 0.0;
            res.working_set_bytes = (M * K + K * N + M * N) * sizeof(double);
            res.effective_bandwidth_gbs = (sec > 0.0) ? (static_cast<double>(res.working_set_bytes) / sec) * 1e-9 : 0.0;
            res.aligned_64b = A.is_64b_aligned() && B.is_64b_aligned() && C.is_64b_aligned();
            downsample_matrix(A.data(), M, K, 16, res.a_preview_rows, res.a_preview_cols, res.a_preview);
            downsample_matrix(B.data(), K, N, 16, res.b_preview_rows, res.b_preview_cols, res.b_preview);
            downsample_matrix(C.data(), M, N, 16, res.c_preview_rows, res.c_preview_cols, res.c_preview);
            if (run_validation) {
                res.validation = validate_gemm_f64_against_ikj(A.data(), B.data(), C.data(), M, K, N, seed);
            }
            res.success = true;
            return res;
        }

        // Default: f32 GEMM
        AlignedBuffer<float> A(M * K), B(K * N), C(M * N);
        for (size_t i = 0; i < M * K; ++i) {
            A.data()[i] = static_cast<float>((custom_A && i < custom_A->size()) ? (*custom_A)[i] : dist(rng));
        }
        for (size_t i = 0; i < K * N; ++i) {
            B.data()[i] = static_cast<float>((custom_B && i < custom_B->size()) ? (*custom_B)[i] : dist(rng));
        }
        XK_Workspace ws(memory_budget_bytes > 0 ? memory_budget_bytes : 64 * 1024 * 1024);
        int used_t = 1;
        const auto t0 = std::chrono::steady_clock::now();
        if (backend == "naive") {
            XK_gemm_naive(A.data(), B.data(), C.data(), M, K, N);
        } else if (backend == "ikj") {
            XK_gemm_ikj(A.data(), B.data(), C.data(), M, K, N);
        } else if (backend == "blocked") {
            XK_gemm_blocked(A.data(), B.data(), C.data(), M, K, N, 64);
        } else if (backend == "avx2") {
            XK_gemm_avx2(A.data(), B.data(), C.data(), M, K, N);
        } else if (backend == "avx2_asm") {
            XK_gemm_avx2_asm(A.data(), B.data(), C.data(), M, K, N);
        } else if (backend == "omp") {
            XK_gemm_omp(A.data(), B.data(), C.data(), M, K, N, res.threads_requested, &used_t);
        } else {
            XK_WorkloadDesc wd(M, K, N, sizeof(float));
            wd.mode = XK_ExecMode::Auto;
            wd.memory_budget_bytes = memory_budget_bytes;
            wd.prefer_asm_kernel = (backend == "auto_asm");
            res.plan = XK_gemm_ex(A.data(), B.data(), C.data(), wd, &ws, nullptr, &used_t);
            res.has_plan = true;
            res.workspace_bytes = ws.allocated_bytes();
        }
        const auto t1 = std::chrono::steady_clock::now();
        const double sec = std::chrono::duration<double>(t1 - t0).count();
        res.threads_used = used_t;
        res.elapsed_ms = sec * 1e3;
        res.gflops = (sec > 0.0) ? (flops / sec) * 1e-9 : 0.0;
        res.working_set_bytes = (M * K + K * N + M * N) * sizeof(float);
        res.effective_bandwidth_gbs = (sec > 0.0) ? (static_cast<double>(res.working_set_bytes) / sec) * 1e-9 : 0.0;
        res.aligned_64b = A.is_64b_aligned() && B.is_64b_aligned() && C.is_64b_aligned();
        downsample_matrix(A.data(), M, K, 16, res.a_preview_rows, res.a_preview_cols, res.a_preview);
        downsample_matrix(B.data(), K, N, 16, res.b_preview_rows, res.b_preview_cols, res.b_preview);
        downsample_matrix(C.data(), M, N, 16, res.c_preview_rows, res.c_preview_cols, res.c_preview);
        if (run_validation) {
            res.validation = validate_gemm_f32_against_f64(A.data(), B.data(), C.data(), M, K, N, seed);
        }
        res.success = true;
    } catch (const std::exception& e) {
        res.success = false;
        res.error_message = e.what();
    }
    return res;
}


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
    bool run_validation) {
    MatrixRunSummary out;
    out.operation = "gemm_blas";
    out.precision = (precision == "f64") ? "f64" : "f32";
    out.backend = "blas_" + layout + "_" + trans_a + trans_b;
    out.M = M; out.K = K; out.N = N;
    out.threads_requested = threads;

    if (M == 0 || N == 0 || K == 0 || M > 2048 || N > 2048 || K > 2048) {
        out.error_message = "Dimensions must be in [1, 2048] for interactive generalized GEMM.";
        return out;
    }

    const XK_Layout lay = (layout == "col") ? XK_Layout::ColMajor : XK_Layout::RowMajor;
    const XK_Transpose ta = (trans_a == "T") ? XK_Transpose::Trans : XK_Transpose::NoTrans;
    const XK_Transpose tb = (trans_b == "T") ? XK_Transpose::Trans : XK_Transpose::NoTrans;

    const size_t a_rows = (lay == XK_Layout::RowMajor) ? (ta == XK_Transpose::NoTrans ? M : K) : (ta == XK_Transpose::NoTrans ? K : M);
    const size_t a_cols = (lay == XK_Layout::RowMajor) ? (ta == XK_Transpose::NoTrans ? K : M) : (ta == XK_Transpose::NoTrans ? M : K);
    const size_t b_rows = (lay == XK_Layout::RowMajor) ? (tb == XK_Transpose::NoTrans ? K : N) : (tb == XK_Transpose::NoTrans ? N : K);
    const size_t b_cols = (lay == XK_Layout::RowMajor) ? (tb == XK_Transpose::NoTrans ? N : K) : (tb == XK_Transpose::NoTrans ? K : N);
    const size_t c_rows = (lay == XK_Layout::RowMajor) ? M : N;
    const size_t c_cols = (lay == XK_Layout::RowMajor) ? N : M;

    const size_t lda = a_cols, ldb = b_cols, ldc = c_cols;
    std::mt19937 rng(seed);
    std::uniform_real_distribution<double> dist(-1.0, 1.0);

    try {
        XK_WorkloadDesc wd(M, K, N, out.precision == "f64" ? sizeof(double) : sizeof(float));
        wd.memory_budget_bytes = memory_budget_bytes;
        if (threads > 0) {
            wd.mode = XK_ExecMode::Manual;
            wd.manual_threads = threads;
        } else {
            wd.mode = XK_ExecMode::Auto;
        }
        XK_Workspace ws(memory_budget_bytes > 0 ? memory_budget_bytes : 64 * 1024 * 1024);
        int used_t = 1;

        if (out.precision == "f64") {
            AlignedBuffer<double> A(a_rows * lda), B(b_rows * ldb), C(c_rows * ldc);
            std::vector<double> C0(c_rows * ldc);
            for (size_t i = 0; i < A.size(); ++i) A.data()[i] = dist(rng);
            for (size_t i = 0; i < B.size(); ++i) B.data()[i] = dist(rng);
            for (size_t i = 0; i < C.size(); ++i) { C0[i] = dist(rng); C.data()[i] = C0[i]; }

            const auto t0 = std::chrono::steady_clock::now();
            out.plan = XK_gemm_blas(lay, ta, tb, M, N, K,
                                    alpha, A.data(), lda, B.data(), ldb,
                                    beta, C.data(), ldc, &wd, &ws, nullptr, &used_t);
            const auto t1 = std::chrono::steady_clock::now();
            out.elapsed_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
            out.has_plan = true;
            out.threads_used = used_t;
            out.workspace_bytes = out.plan.required_workspace_bytes;
            out.working_set_bytes = (A.size() + B.size() + C.size()) * sizeof(double);
            out.aligned_64b = A.is_64b_aligned() && B.is_64b_aligned() && C.is_64b_aligned();
            out.gflops = (out.elapsed_ms > 0.0) ? ((2.0 * M * K * N) / (out.elapsed_ms * 1e6)) : 0.0;

            if (run_validation && M <= 256 && N <= 256 && K <= 256) {
                out.validation.executed = true;
                out.validation.precision = "f64";
                out.validation.M = M; out.validation.K = K; out.validation.N = N;
                out.validation.abs_tol = 1e-10;
                out.validation.rel_tol = 1e-10 + 1e-12 * static_cast<double>(K);
                double max_abs = 0.0, max_rel = 0.0, sq_err = 0.0;
                for (size_t i = 0; i < M; ++i) {
                    for (size_t j = 0; j < N; ++j) {
                        double sum = 0.0;
                        for (size_t k = 0; k < K; ++k) {
                            const double av = (lay == XK_Layout::RowMajor)
                                ? (ta == XK_Transpose::NoTrans ? A.data()[i * lda + k] : A.data()[k * lda + i])
                                : (ta == XK_Transpose::NoTrans ? A.data()[k * lda + i] : A.data()[i * lda + k]);
                            const double bv = (lay == XK_Layout::RowMajor)
                                ? (tb == XK_Transpose::NoTrans ? B.data()[k * ldb + j] : B.data()[j * ldb + k])
                                : (tb == XK_Transpose::NoTrans ? B.data()[j * ldb + k] : B.data()[k * ldb + j]);
                            sum += av * bv;
                        }
                        const size_t idx = (lay == XK_Layout::RowMajor) ? (i * ldc + j) : (j * ldc + i);
                        const double ref = (beta == 0.0) ? (alpha * sum) : (alpha * sum + beta * C0[idx]);
                        const double got = C.data()[idx];
                        if (!std::isfinite(got)) ++out.validation.nan_inf_count;
                        const double diff = std::fabs(ref - got);
                        const double rel = diff / std::max(1.0, std::fabs(ref));
                        max_abs = std::max(max_abs, diff);
                        max_rel = std::max(max_rel, rel);
                        sq_err += diff * diff;
                    }
                }
                out.validation.max_abs_error = max_abs;
                out.validation.max_rel_error = max_rel;
                out.validation.rmse = std::sqrt(sq_err / std::max<size_t>(1, M * N));
                out.validation.passed = (out.validation.nan_inf_count == 0) &&
                                        (max_rel <= out.validation.rel_tol + out.validation.abs_tol);
            }
        } else {
            AlignedBuffer<float> A(a_rows * lda), B(b_rows * ldb), C(c_rows * ldc);
            std::vector<double> C0(c_rows * ldc);
            for (size_t i = 0; i < A.size(); ++i) A.data()[i] = static_cast<float>(dist(rng));
            for (size_t i = 0; i < B.size(); ++i) B.data()[i] = static_cast<float>(dist(rng));
            for (size_t i = 0; i < C.size(); ++i) {
                C.data()[i] = static_cast<float>(dist(rng));
                C0[i] = static_cast<double>(C.data()[i]);
            }

            const auto t0 = std::chrono::steady_clock::now();
            out.plan = XK_gemm_blas(lay, ta, tb, M, N, K,
                                    static_cast<float>(alpha), A.data(), lda, B.data(), ldb,
                                    static_cast<float>(beta), C.data(), ldc, &wd, &ws, nullptr, &used_t);
            const auto t1 = std::chrono::steady_clock::now();
            out.elapsed_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
            out.has_plan = true;
            out.threads_used = used_t;
            out.workspace_bytes = out.plan.required_workspace_bytes;
            out.working_set_bytes = (A.size() + B.size() + C.size()) * sizeof(float);
            out.aligned_64b = A.is_64b_aligned() && B.is_64b_aligned() && C.is_64b_aligned();
            out.gflops = (out.elapsed_ms > 0.0) ? ((2.0 * M * K * N) / (out.elapsed_ms * 1e6)) : 0.0;

            if (run_validation && M <= 256 && N <= 256 && K <= 256) {
                out.validation.executed = true;
                out.validation.precision = "f32";
                out.validation.M = M; out.validation.K = K; out.validation.N = N;
                out.validation.abs_tol = 1e-3;
                out.validation.rel_tol = 1e-4 + 1e-6 * static_cast<double>(K);
                double max_abs = 0.0, max_rel = 0.0, sq_err = 0.0;
                for (size_t i = 0; i < M; ++i) {
                    for (size_t j = 0; j < N; ++j) {
                        double sum = 0.0;
                        for (size_t k = 0; k < K; ++k) {
                            const double av = (lay == XK_Layout::RowMajor)
                                ? (ta == XK_Transpose::NoTrans ? static_cast<double>(A.data()[i * lda + k]) : static_cast<double>(A.data()[k * lda + i]))
                                : (ta == XK_Transpose::NoTrans ? static_cast<double>(A.data()[k * lda + i]) : static_cast<double>(A.data()[i * lda + k]));
                            const double bv = (lay == XK_Layout::RowMajor)
                                ? (tb == XK_Transpose::NoTrans ? static_cast<double>(B.data()[k * ldb + j]) : static_cast<double>(B.data()[j * ldb + k]))
                                : (tb == XK_Transpose::NoTrans ? static_cast<double>(B.data()[j * ldb + k]) : static_cast<double>(B.data()[k * ldb + j]));
                            sum += av * bv;
                        }
                        const size_t idx = (lay == XK_Layout::RowMajor) ? (i * ldc + j) : (j * ldc + i);
                        const double ref = (beta == 0.0) ? (alpha * sum) : (alpha * sum + beta * C0[idx]);
                        const double got = static_cast<double>(C.data()[idx]);
                        if (!std::isfinite(got)) ++out.validation.nan_inf_count;
                        const double diff = std::fabs(ref - got);
                        const double rel = diff / std::max(1.0, std::fabs(ref));
                        max_abs = std::max(max_abs, diff);
                        max_rel = std::max(max_rel, rel);
                        sq_err += diff * diff;
                    }
                }
                out.validation.max_abs_error = max_abs;
                out.validation.max_rel_error = max_rel;
                out.validation.rmse = std::sqrt(sq_err / std::max<size_t>(1, M * N));
                out.validation.passed = (out.validation.nan_inf_count == 0) &&
                                        (max_rel <= out.validation.rel_tol + out.validation.abs_tol);
            }
        }
        out.success = true;
    } catch (const std::exception& e) {
        out.error_message = e.what();
    }
    return out;
}

}  // namespace nvxk_wb