#include <nvxk.h>
#include <nvxk_c.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>
#if defined(_OPENMP)
#include <omp.h>
#endif

#define XK_CHECK(cond)                                                     \
    do {                                                                   \
        if (!(cond)) {                                                     \
            std::cerr << "[FAIL] " << __FILE__ << ":" << __LINE__          \
                      << " — Check failed: " << #cond << "\n";             \
            std::exit(EXIT_FAILURE);                                       \
        }                                                                  \
    } while (0)

#define XK_EXPECT_THROW(expr, ExType)                                      \
    do {                                                                   \
        bool caught_expected = false;                                      \
        try {                                                              \
            (void)(expr);                                                  \
        } catch (const ExType&) {                                          \
            caught_expected = true;                                        \
        } catch (const std::exception& e) {                                \
            std::cerr << "[FAIL] " << __FILE__ << ":" << __LINE__          \
                      << " — Unexpected exception for " << #expr << ": "   \
                      << e.what() << "\n";                                 \
            std::exit(EXIT_FAILURE);                                       \
        } catch (...) {                                                    \
            std::cerr << "[FAIL] " << __FILE__ << ":" << __LINE__          \
                      << " — Unknown exception for " << #expr << "\n";     \
            std::exit(EXIT_FAILURE);                                       \
        }                                                                  \
        if (!caught_expected) {                                            \
            std::cerr << "[FAIL] " << __FILE__ << ":" << __LINE__          \
                      << " — Expected " << #ExType << " for: " << #expr    \
                      << "\n";                                             \
            std::exit(EXIT_FAILURE);                                       \
        }                                                                  \
    } while (0)

template <typename T>
void fill_nan(std::vector<T>& v) {
    std::fill(v.begin(), v.end(), std::numeric_limits<T>::quiet_NaN());
}

void compute_ref_f64(const std::vector<float>& A, const std::vector<float>& B,
                     std::vector<double>& C_ref, size_t M, size_t K, size_t N) {
    C_ref.assign(M * N, 0.0);
    for (size_t i = 0; i < M; ++i) {
        for (size_t k = 0; k < K; ++k) {
            const double a_ik = static_cast<double>(A[i * K + k]);
            for (size_t j = 0; j < N; ++j) {
                C_ref[i * N + j] += a_ik * static_cast<double>(B[k * N + j]);
            }
        }
    }
}

void check_gemm_f32_close(const std::vector<double>& ref,
                          const std::vector<float>& got,
                          size_t K, double tol_scale = 1.0) {
    XK_CHECK(ref.size() == got.size());
    const double abs_tol = 1e-4 * tol_scale;
    const double rel_tol = (1e-4 + 1e-6 * static_cast<double>(K)) * tol_scale;
    for (size_t i = 0; i < got.size(); ++i) {
        XK_CHECK(std::isfinite(got[i]));
        const double diff = std::fabs(ref[i] - static_cast<double>(got[i]));
        const double scale = std::max(1.0, std::fabs(ref[i]));
        XK_CHECK(diff <= abs_tol + rel_tol * scale);
    }
}

void check_gemm_f64_close(const std::vector<double>& ref,
                          const std::vector<double>& got,
                          size_t K) {
    XK_CHECK(ref.size() == got.size());
    const double abs_tol = 1e-11;
    const double rel_tol = 1e-11 + 1e-13 * static_cast<double>(K);
    for (size_t i = 0; i < got.size(); ++i) {
        XK_CHECK(std::isfinite(got[i]));
        const double diff = std::fabs(ref[i] - got[i]);
        const double scale = std::max(1.0, std::fabs(ref[i]));
        XK_CHECK(diff <= abs_tol + rel_tol * scale);
    }
}

template <typename T>
void test_vector_add(const size_t* sizes, size_t size_count) {
    for (size_t s = 0; s < size_count; ++s) {
        const size_t count = sizes[s];
        std::vector<T> a(count), b(count), result(count);
        fill_nan(result);
        for (size_t i = 0; i < count; ++i) {
            a[i] = static_cast<T>(static_cast<int>(i % 23) - 11) / static_cast<T>(3);
            b[i] = static_cast<T>(static_cast<int>(i % 19) - 9) / static_cast<T>(5);
        }
        vector_add_avx2(a.data(), b.data(), result.data(), count);
        for (size_t i = 0; i < count; ++i) {
            const T expected = a[i] + b[i];
            const T scale = std::max(static_cast<T>(1), std::fabs(expected));
            const T tolerance = static_cast<T>(4) *
                                std::numeric_limits<T>::epsilon() * scale;
            XK_CHECK(std::isfinite(result[i]));
            XK_CHECK(std::fabs(result[i] - expected) <= tolerance);
        }
    }
    vector_add_avx2(static_cast<const T*>(nullptr),
                    static_cast<const T*>(nullptr),
                    static_cast<T*>(nullptr), 0);
    T buf[4] = {1, 2, 3, 4};
    XK_EXPECT_THROW(vector_add_avx2(static_cast<const T*>(nullptr), buf, buf, 4),
                    std::invalid_argument);
    XK_EXPECT_THROW(vector_add_avx2(buf, static_cast<const T*>(nullptr), buf, 4),
                    std::invalid_argument);
    XK_EXPECT_THROW(vector_add_avx2(buf, buf, static_cast<T*>(nullptr), 4),
                    std::invalid_argument);
}

void test_odd_and_large_k_gemm_f32() {
    struct Shape { size_t M, K, N; };
    const Shape shapes[] = {
        {1, 1, 1},
        {3, 5, 11},
        {6, 16, 16},
        {7, 13, 19},
        {13, 17, 23},
        {131, 263, 149}
    };

    for (size_t s = 0; s < sizeof(shapes) / sizeof(shapes[0]); ++s) {
        const size_t M = shapes[s].M;
        const size_t K = shapes[s].K;
        const size_t N = shapes[s].N;

        std::vector<float> A(M * K), B(K * N), C(M * N);
        for (size_t i = 0; i < M * K; ++i) {
            A[i] = static_cast<float>(static_cast<int>(i % 13) - 6) * 0.25f;
        }
        for (size_t i = 0; i < K * N; ++i) {
            B[i] = static_cast<float>(static_cast<int>(i % 11) - 5) * 0.25f;
        }

        std::vector<double> ref;
        compute_ref_f64(A, B, ref, M, K, N);

        fill_nan(C);
        XK_gemm_naive(A.data(), B.data(), C.data(), M, K, N);
        check_gemm_f32_close(ref, C, K);

        fill_nan(C);
        XK_gemm_ikj(A.data(), B.data(), C.data(), M, K, N);
        check_gemm_f32_close(ref, C, K);

        fill_nan(C);
        XK_gemm_avx2(A.data(), B.data(), C.data(), M, K, N);
        check_gemm_f32_close(ref, C, K);

        fill_nan(C);
        XK_gemm_avx2_asm(A.data(), B.data(), C.data(), M, K, N);
        check_gemm_f32_close(ref, C, K);

        fill_nan(C);
        XK_gemm(A.data(), B.data(), C.data(), M, K, N);
        check_gemm_f32_close(ref, C, K);

        fill_nan(C);
        XK_gemm_blocked(A.data(), B.data(), C.data(), M, K, N, 7);
        check_gemm_f32_close(ref, C, K);

        fill_nan(C);
        XK_gemm_blocked(A.data(), B.data(), C.data(), M, K, N, 64);
        check_gemm_f32_close(ref, C, K);

        int actual_threads = 0;
        fill_nan(C);
        XK_gemm_omp(A.data(), B.data(), C.data(), M, K, N, 2, &actual_threads);
        check_gemm_f32_close(ref, C, K);
        XK_CHECK(actual_threads >= 1 && actual_threads <= 2);
    }
}

void test_odd_and_large_k_gemm_f64() {
    struct Shape { size_t M, K, N; };
    const Shape shapes[] = {
        {1, 1, 1},
        {3, 5, 7},
        {6, 8, 8},
        {7, 13, 19},
        {67, 137, 73},
        {132, 260, 140}
    };

    for (size_t s = 0; s < sizeof(shapes) / sizeof(shapes[0]); ++s) {
        const size_t M = shapes[s].M;
        const size_t K = shapes[s].K;
        const size_t N = shapes[s].N;

        std::vector<double> A(M * K), B(K * N), C_ref(M * N), C_opt(M * N);
        for (size_t i = 0; i < M * K; ++i) {
            A[i] = static_cast<double>(static_cast<int>(i % 13) - 6) * 0.125;
        }
        for (size_t i = 0; i < K * N; ++i) {
            B[i] = static_cast<double>(static_cast<int>(i % 11) - 5) * 0.25;
        }

        XK_gemm_ikj(A.data(), B.data(), C_ref.data(), M, K, N);

        fill_nan(C_opt);
        XK_gemm_avx2(A.data(), B.data(), C_opt.data(), M, K, N);
        check_gemm_f64_close(C_ref, C_opt, K);

        fill_nan(C_opt);
        XK_gemm(A.data(), B.data(), C_opt.data(), M, K, N);
        check_gemm_f64_close(C_ref, C_opt, K);

        // Explicit OpenMP f64 test via XK_gemm_f64_ex
        XK_WorkloadDesc wd(M, K, N, sizeof(double));
        wd.mode = XK_ExecMode::Manual;
        wd.manual_threads = 2;
        int used = 0;
        fill_nan(C_opt);
        XK_gemm_f64_ex(A.data(), B.data(), C_opt.data(), wd, nullptr, nullptr, &used);
        check_gemm_f64_close(C_ref, C_opt, K);
    }
}

void test_thin_m_streaming_and_n_axis() {
    struct Shape { size_t M, K, N; };
    const Shape shapes[] = {
        {1, 137, 149},
        {2, 260, 257},
        {3, 73, 193},
        {4, 512, 512},
        {4, 33, 63},
        {5, 73, 193}
    };
    const int threads[] = {1, 2, 4};

    XK_Workspace ws(32 * 1024 * 1024);
    for (size_t s = 0; s < sizeof(shapes) / sizeof(shapes[0]); ++s) {
        const size_t M = shapes[s].M, K = shapes[s].K, N = shapes[s].N;
        std::vector<float> A(M * K), B(K * N), C(M * N);
        for (size_t i = 0; i < M * K; ++i) {
            A[i] = static_cast<float>(static_cast<int>(i % 13) - 6) * 0.25f;
        }
        for (size_t i = 0; i < K * N; ++i) {
            B[i] = static_cast<float>(static_cast<int>(i % 11) - 5) * 0.25f;
        }
        std::vector<double> ref;
        compute_ref_f64(A, B, ref, M, K, N);

        // Auto mode (selects Avx2ThinM for M<=4 on AVX2 CPUs)
        {
            XK_WorkloadDesc desc(M, K, N);
            desc.mode = XK_ExecMode::Auto;
            int used = 0;
            fill_nan(C);
            XK_ExecutionPlan p = XK_gemm_ex(A.data(), B.data(), C.data(), desc, &ws, nullptr, &used);
            check_gemm_f32_close(ref, C, K);
            if (XK_has_avx2_fma() && M <= 4 && N >= 16 && K >= 8) {
                XK_CHECK(p.kernel == XK_KernelKind::Avx2ThinM);
                XK_CHECK(p.required_workspace_bytes == 0);
            }
        }

        // Manual AxisN with explicit KC (exercises BLIS AxisN path)
        for (size_t ti = 0; ti < sizeof(threads) / sizeof(threads[0]); ++ti) {
            XK_WorkloadDesc desc(M, K, N);
            desc.mode = XK_ExecMode::Manual;
            desc.manual_threads = threads[ti];
            desc.manual_axis = XK_ParallelAxis::AxisN;
            desc.manual_kc = 384;

            int used = 0;
            fill_nan(C);
            XK_ExecutionPlan plan =
                XK_gemm_ex(A.data(), B.data(), C.data(), desc, &ws, nullptr, &used);
            check_gemm_f32_close(ref, C, K);
            XK_CHECK(used >= 1 && used <= threads[ti]);
#if defined(_OPENMP)
            if (threads[ti] > 1) {
                XK_CHECK(plan.axis == XK_ParallelAxis::AxisN);
            }
#endif
        }
    }
}

void test_differential_randomized_shapes() {
    std::mt19937 rng(20261124);
    std::uniform_real_distribution<float> dist(-2.0f, 2.0f);
    std::uniform_int_distribution<size_t> dim_dist(1, 140);

    XK_Workspace ws(2 * 1024 * 1024);  // tight 2 MiB budget tests graceful clamping too
    for (int trial = 0; trial < 25; ++trial) {
        const size_t M = dim_dist(rng);
        const size_t K = dim_dist(rng);
        const size_t N = dim_dist(rng);

        std::vector<float> A(M * K), B(K * N), C_auto(M * N), C_asm(M * N);
        for (float& x : A) x = dist(rng);
        for (float& x : B) x = dist(rng);

        std::vector<double> ref;
        compute_ref_f64(A, B, ref, M, K, N);

        XK_WorkloadDesc wd(M, K, N);
        wd.mode = XK_ExecMode::Auto;
        fill_nan(C_auto);
        XK_gemm_ex(A.data(), B.data(), C_auto.data(), wd, &ws);
        check_gemm_f32_close(ref, C_auto, K);

        wd.prefer_asm_kernel = true;
        fill_nan(C_asm);
        XK_gemm_ex(A.data(), B.data(), C_asm.data(), wd, &ws);
        check_gemm_f32_close(ref, C_asm, K);
    }
}

void test_graceful_memory_budget_clamping() {
    // Custom workspace with tight 128 KiB budget must NOT throw on 256^3 when using XK_gemm_ex
    const size_t N = 256;
    std::vector<float> A(N * N, 0.25f), B(N * N, 0.5f), C(N * N, 0.0f);
    std::vector<double> ref;
    compute_ref_f64(A, B, ref, N, N, N);

    XK_Workspace tight_ws(128 * 1024);
    XK_WorkloadDesc wd(N, N, N);
    wd.mode = XK_ExecMode::Manual;
    wd.manual_threads = 1;
    XK_ExecutionPlan p = XK_gemm_ex(A.data(), B.data(), C.data(), wd, &tight_ws);
    check_gemm_f32_close(ref, C, N);
    XK_CHECK(tight_ws.allocated_bytes() <= 128 * 1024);
    if (XK_has_avx2_fma()) {
        XK_CHECK(p.explanation.budget_clamped);
    }
}

void test_c_abi() {
    const size_t M = 19, K = 23, N = 29;
    std::vector<float> A(M * K, 1.5f), B(K * N, -0.5f), C(M * N, 0.0f);
    int used = 0;
    XK_CHECK(nvxk_sgemm(A.data(), B.data(), C.data(), M, K, N, &used) == NVXK_OK);
    XK_CHECK(used >= 1);
    for (float v : C) {
        XK_CHECK(std::fabs(v - (-17.25f)) < 1e-4f);
    }

    // Error handling through C ABI (aliasing must return NVXK_ERR_INVALID_ARG)
    XK_CHECK(nvxk_sgemm(A.data(), B.data(), A.data(), 4, 4, 4, &used) == NVXK_ERR_INVALID_ARG);
    XK_CHECK(nvxk_last_error()[0] != '\0');

    // Double precision C ABI
    std::vector<double> Ad(M * K, 2.0), Bd(K * N, 0.25), Cd(M * N, 0.0);
    XK_CHECK(nvxk_dgemm(Ad.data(), Bd.data(), Cd.data(), M, K, N, &used) == NVXK_OK);
    for (double v : Cd) {
        XK_CHECK(std::fabs(v - 11.5) < 1e-12);
    }

    // Integer overflow through C ABI
    const int32_t big32 = std::numeric_limits<int32_t>::max();
    const int32_t ba[3] = {big32, big32, big32};
    const int32_t bb[3] = {big32, big32, big32};
    int64_t bc[1] = {0};
    XK_CHECK(nvxk_gemm_i32_i64(ba, bb, bc, 1, 3, 1) == NVXK_ERR_OVERFLOW);
}

void test_aliasing_rejection() {
    float buf[64] = {};
    float other[64] = {};
    XK_EXPECT_THROW(XK_gemm_naive(buf, other, buf, 4, 4, 4), std::invalid_argument);
    XK_EXPECT_THROW(XK_gemm_ikj(other, buf, buf, 4, 4, 4), std::invalid_argument);
    XK_EXPECT_THROW(XK_gemm_avx2(buf, other, buf, 4, 4, 4), std::invalid_argument);
    XK_EXPECT_THROW(XK_gemm_blocked(other, buf, buf, 4, 4, 4, 8), std::invalid_argument);
    XK_EXPECT_THROW(XK_gemm_omp(buf, other, buf, 8, 4, 4, 2), std::invalid_argument);
    XK_EXPECT_THROW(XK_gemm(buf, buf, buf, 4, 4, 4), std::invalid_argument);
    XK_EXPECT_THROW(XK_gemm_avx2(buf, other, buf + 4, 4, 4, 4), std::invalid_argument);

    int32_t ibuf[16] = {};
    XK_EXPECT_THROW(
        XK_gemm_i32_i64(ibuf, ibuf, reinterpret_cast<int64_t*>(ibuf), 2, 2, 2),
        std::invalid_argument);
}

void test_integer_gemm_and_overflow() {
    const int32_t ia[] = {100000, 100000};
    const int32_t ib[] = {100000, -100000, 100000, -100000};
    int64_t ic[2] = {};
    XK_gemm_i32_i64(ia, ib, ic, 1, 2, 2);
    XK_CHECK(ic[0] == INT64_C(20000000000));
    XK_CHECK(ic[1] == -INT64_C(20000000000));

    const int32_t big32 = std::numeric_limits<int32_t>::max();
    const int32_t big_a[3] = {big32, big32, big32};
    const int32_t big_b[3] = {big32, big32, big32};
    int64_t big_out[1] = {};
    XK_EXPECT_THROW(XK_gemm_i32_i64(big_a, big_b, big_out, 1, 3, 1),
                    std::overflow_error);

    const int16_t s16_a[] = {200, 200};
    const int16_t s16_b[] = {200, 200};
    int16_t s16_c[1] = {};
    XK_EXPECT_THROW(XK_gemm_naive(s16_a, s16_b, s16_c, 1, 2, 1), std::overflow_error);
    XK_EXPECT_THROW(XK_gemm_ikj(s16_a, s16_b, s16_c, 1, 2, 1), std::overflow_error);
}

void test_phase5_hardware_planner_workspace_profile() {
    const XK_HardwareInfo hw = XK_detect_hardware();
    XK_CHECK(hw.physical_cores >= 1);
    XK_CHECK(hw.logical_threads >= hw.physical_cores);
    XK_CHECK(hw.l1d_bytes >= 16 * 1024);
    XK_CHECK(hw.l2_bytes >= 64 * 1024);

    XK_HardwareProfile i7;
    i7.hw.vendor = "GenuineIntel";
    i7.hw.brand = "Intel(R) Core(TM) i7-7700 CPU @ 3.60GHz";
    i7.hw.physical_cores = 4;
    i7.hw.logical_threads = 8;
    i7.hw.l1d_bytes = 32 * 1024;
    i7.hw.l2_bytes = 256 * 1024;
    i7.hw.l3_bytes = 8 * 1024 * 1024;
    i7.hw.has_avx2 = true;
    i7.hw.has_fma = true;
    i7.calibrated = true;
    i7.peak_gflops_1t = 105.0;
    i7.mem_bandwidth_gbs = 28.0;
    i7.omp_fork_overhead_us = 60.0;
    i7.omp_barrier_overhead_us = 8.0;
    i7.smt_efficiency = 0.10;

    const XK_ExecutionPlan p128 = XK_plan_gemm(XK_WorkloadDesc(128, 128, 128), i7);
    XK_CHECK(p128.threads == 1);
    XK_CHECK(!p128.explanation.reason.empty());

    XK_WorkloadDesc thin(4, 1024, 1024);
    thin.mode = XK_ExecMode::Manual;
    thin.manual_threads = 4;
    const XK_ExecutionPlan p_thin = XK_plan_gemm(thin, i7);
    if (p_thin.threads > 1) {
        XK_CHECK(p_thin.axis == XK_ParallelAxis::AxisN);
    }

    XK_WorkloadDesc man(256, 256, 256);
    man.mode = XK_ExecMode::Manual;
    man.manual_threads = 1;
    man.manual_mc = 48;
    man.manual_kc = 128;
    man.manual_nc = 256;
    const XK_ExecutionPlan p_man = XK_plan_gemm(man, i7);
    XK_CHECK(p_man.threads == 1);
    XK_CHECK(p_man.mc == 48 && p_man.kc == 128 && p_man.nc == 256);

    XK_Workspace tight_ws(1024);
    XK_EXPECT_THROW(tight_ws.prepare_f32(1024, 1024, 2), std::length_error);
    tight_ws.release();
    XK_CHECK(tight_ws.allocated_bytes() == 0);

    const std::string tmp_path = "nvxk_test_profile.cfg";
    XK_CHECK(XK_save_hardware_profile(i7, tmp_path));
    XK_HardwareProfile loaded;
    XK_CHECK(XK_load_hardware_profile(tmp_path, loaded));
    XK_CHECK(loaded.hw.brand == i7.hw.brand);
    XK_CHECK(loaded.hw.physical_cores == 4);
    XK_CHECK(loaded.hw.l2_bytes == 256 * 1024);
    XK_CHECK(std::fabs(loaded.peak_gflops_1t - 105.0) < 1e-6);
    std::remove(tmp_path.c_str());

    const XK_HardwareProfile cal = XK_calibrate_hardware(100.0);
    XK_CHECK(cal.calibrated);
    XK_CHECK(cal.peak_gflops_1t > 0.0);
    XK_CHECK(cal.mem_bandwidth_gbs > 0.0);
}

void test_empty_and_null_checks() {
    float buf[16] = {};
    float other[16] = {};
    XK_gemm_avx2(static_cast<const float*>(nullptr), static_cast<const float*>(nullptr),
                 static_cast<float*>(nullptr), 0, 5, 7);
    XK_EXPECT_THROW(XK_gemm_avx2(static_cast<const float*>(nullptr), buf, other, 2, 2, 2),
                    std::invalid_argument);
    XK_EXPECT_THROW(XK_gemm_blocked(buf, buf, other, 2, 2, 2, 0), std::invalid_argument);
    const size_t huge = std::numeric_limits<size_t>::max() / 2 + 1;
    XK_EXPECT_THROW(XK_gemm_avx2(buf, buf, other, huge, 2, 2), std::length_error);
}


template <typename T>
void compute_blas_ref_f64(XK_Layout layout, XK_Transpose ta, XK_Transpose tb,
                          size_t M, size_t N, size_t K,
                          double alpha, const T* A, size_t lda,
                          const T* B, size_t ldb,
                          double beta, const std::vector<double>& C_init, size_t ldc,
                          std::vector<double>& C_ref) {
    C_ref = C_init;
    auto get_a = [&](size_t i, size_t k) -> double {
        if (layout == XK_Layout::RowMajor) {
            return (ta == XK_Transpose::NoTrans)
                       ? static_cast<double>(A[i * lda + k])
                       : static_cast<double>(A[k * lda + i]);
        } else {
            return (ta == XK_Transpose::NoTrans)
                       ? static_cast<double>(A[k * lda + i])
                       : static_cast<double>(A[i * lda + k]);
        }
    };
    auto get_b = [&](size_t k, size_t j) -> double {
        if (layout == XK_Layout::RowMajor) {
            return (tb == XK_Transpose::NoTrans)
                       ? static_cast<double>(B[k * ldb + j])
                       : static_cast<double>(B[j * ldb + k]);
        } else {
            return (tb == XK_Transpose::NoTrans)
                       ? static_cast<double>(B[j * ldb + k])
                       : static_cast<double>(B[k * ldb + j]);
        }
    };
    for (size_t i = 0; i < M; ++i) {
        for (size_t j = 0; j < N; ++j) {
            const size_t idx = (layout == XK_Layout::RowMajor) ? (i * ldc + j) : (j * ldc + i);
            double sum = 0.0;
            for (size_t k = 0; k < K; ++k) {
                sum += get_a(i, k) * get_b(k, j);
            }
            if (beta == 0.0) {
                C_ref[idx] = alpha * sum;
            } else {
                C_ref[idx] = alpha * sum + beta * C_init[idx];
            }
        }
    }
}

void test_generalized_blas_gemm() {
    std::mt19937 rng(20260301u);
    std::uniform_real_distribution<double> dist(-1.5, 1.5);

    struct Shape { size_t M, N, K; };
    const Shape shapes[] = {
        {1, 1, 1}, {3, 5, 7}, {17, 23, 19}, {65, 49, 73}
    };
    const XK_Layout layouts[] = {XK_Layout::RowMajor, XK_Layout::ColMajor};
    const XK_Transpose trans_opts[] = {XK_Transpose::NoTrans, XK_Transpose::Trans};

    for (const auto& s : shapes) {
        for (auto layout : layouts) {
            for (auto ta : trans_opts) {
                for (auto tb : trans_opts) {
                    const size_t a_rows = (layout == XK_Layout::RowMajor)
                                              ? (ta == XK_Transpose::NoTrans ? s.M : s.K)
                                              : (ta == XK_Transpose::NoTrans ? s.K : s.M);
                    const size_t a_cols = (layout == XK_Layout::RowMajor)
                                              ? (ta == XK_Transpose::NoTrans ? s.K : s.M)
                                              : (ta == XK_Transpose::NoTrans ? s.M : s.K);
                    const size_t b_rows = (layout == XK_Layout::RowMajor)
                                              ? (tb == XK_Transpose::NoTrans ? s.K : s.N)
                                              : (tb == XK_Transpose::NoTrans ? s.N : s.K);
                    const size_t b_cols = (layout == XK_Layout::RowMajor)
                                              ? (tb == XK_Transpose::NoTrans ? s.N : s.K)
                                              : (tb == XK_Transpose::NoTrans ? s.K : s.N);
                    const size_t c_rows = (layout == XK_Layout::RowMajor) ? s.M : s.N;
                    const size_t c_cols = (layout == XK_Layout::RowMajor) ? s.N : s.M;

                    const size_t lda = a_cols + 3;
                    const size_t ldb = b_cols + 5;
                    const size_t ldc = c_cols + 4;

                    std::vector<float> Af(a_rows * lda, 0.0f), Bf(b_rows * ldb, 0.0f);
                    std::vector<double> Ad(a_rows * lda, 0.0), Bd(b_rows * ldb, 0.0);
                    for (size_t r = 0; r < a_rows; ++r) {
                        for (size_t c = 0; c < a_cols; ++c) {
                            const double v = dist(rng);
                            Af[r * lda + c] = static_cast<float>(v);
                            Ad[r * lda + c] = v;
                        }
                    }
                    for (size_t r = 0; r < b_rows; ++r) {
                        for (size_t c = 0; c < b_cols; ++c) {
                            const double v = dist(rng);
                            Bf[r * ldb + c] = static_cast<float>(v);
                            Bd[r * ldb + c] = v;
                        }
                    }

                    // Case 1: alpha=2.25, beta=-0.75 (general accumulation)
                    std::vector<float> Cf(c_rows * ldc, 777.0f);
                    std::vector<double> Cd(c_rows * ldc, 777.0);
                    std::vector<double> C_init_f(c_rows * ldc, 777.0), C_init_d(c_rows * ldc, 777.0);
                    for (size_t r = 0; r < c_rows; ++r) {
                        for (size_t c = 0; c < c_cols; ++c) {
                            const double v = dist(rng);
                            Cf[r * ldc + c] = static_cast<float>(v);
                            C_init_f[r * ldc + c] = static_cast<double>(Cf[r * ldc + c]);
                            Cd[r * ldc + c] = v;
                            C_init_d[r * ldc + c] = v;
                        }
                    }
                    std::vector<double> ref_f, ref_d;
                    compute_blas_ref_f64(layout, ta, tb, s.M, s.N, s.K,
                                         2.25, Af.data(), lda, Bf.data(), ldb,
                                         -0.75, C_init_f, ldc, ref_f);
                    compute_blas_ref_f64(layout, ta, tb, s.M, s.N, s.K,
                                         2.25, Ad.data(), lda, Bd.data(), ldb,
                                         -0.75, C_init_d, ldc, ref_d);

                    XK_gemm_blas(layout, ta, tb, s.M, s.N, s.K,
                                 2.25f, Af.data(), lda, Bf.data(), ldb,
                                 -0.75f, Cf.data(), ldc);
                    XK_gemm_blas(layout, ta, tb, s.M, s.N, s.K,
                                 2.25, Ad.data(), lda, Bd.data(), ldb,
                                 -0.75, Cd.data(), ldc);

                    // Check values & padding canary untouched
                    for (size_t r = 0; r < c_rows; ++r) {
                        for (size_t c = 0; c < c_cols; ++c) {
                            const size_t idx = r * ldc + c;
                            const double tol_f = 1e-3 + (1e-4 + 1e-6 * static_cast<double>(s.K)) * std::max(1.0, std::fabs(ref_f[idx]));
                            XK_CHECK(std::isfinite(Cf[idx]));
                            XK_CHECK(std::fabs(static_cast<double>(Cf[idx]) - ref_f[idx]) <= tol_f);
                            const double tol_d = 1e-10 + 1e-12 * static_cast<double>(s.K) * std::max(1.0, std::fabs(ref_d[idx]));
                            XK_CHECK(std::isfinite(Cd[idx]));
                            XK_CHECK(std::fabs(Cd[idx] - ref_d[idx]) <= tol_d);
                        }
                        for (size_t c = c_cols; c < ldc; ++c) {
                            XK_CHECK(Cf[r * ldc + c] == 777.0f);
                            XK_CHECK(Cd[r * ldc + c] == 777.0);
                        }
                    }

                    // Case 2: NaN-safe beta == 0.0 (C pre-filled with NaN)
                    fill_nan(Cf);
                    compute_blas_ref_f64(layout, ta, tb, s.M, s.N, s.K,
                                         1.5, Af.data(), lda, Bf.data(), ldb,
                                         0.0, C_init_f, ldc, ref_f);
                    XK_gemm_blas(layout, ta, tb, s.M, s.N, s.K,
                                 1.5f, Af.data(), lda, Bf.data(), ldb,
                                 0.0f, Cf.data(), ldc);
                    for (size_t r = 0; r < c_rows; ++r) {
                        for (size_t c = 0; c < c_cols; ++c) {
                            const size_t idx = r * ldc + c;
                            XK_CHECK(std::isfinite(Cf[idx]));
                            const double tol_f = 1e-3 + (1e-4 + 1e-6 * static_cast<double>(s.K)) * std::max(1.0, std::fabs(ref_f[idx]));
                            XK_CHECK(std::fabs(static_cast<double>(Cf[idx]) - ref_f[idx]) <= tol_f);
                        }
                    }
                }
            }
        }
    }

    // Case 3: alpha == 0 with beta == 0 (NaN-safe zero) and alpha == 0 with beta == 3.0
    std::vector<float> C_nan(12, std::numeric_limits<float>::quiet_NaN());
    XK_gemm_blas(XK_Layout::RowMajor, XK_Transpose::NoTrans, XK_Transpose::NoTrans,
                 3, 4, 5, 0.0f, nullptr, 5, nullptr, 4, 0.0f, C_nan.data(), 4);
    for (float v : C_nan) XK_CHECK(v == 0.0f);

    std::vector<float> C_scale(12, 2.5f);
    XK_gemm_blas(XK_Layout::RowMajor, XK_Transpose::NoTrans, XK_Transpose::NoTrans,
                 3, 4, 5, 0.0f, nullptr, 5, nullptr, 4, 3.0f, C_scale.data(), 4);
    for (float v : C_scale) XK_CHECK(std::fabs(v - 7.5f) < 1e-6f);

    // Case 4: Invalid leading dimensions throw std::invalid_argument
    std::vector<float> dummy(16, 1.0f);
    XK_EXPECT_THROW(
        XK_gemm_blas(XK_Layout::RowMajor, XK_Transpose::NoTrans, XK_Transpose::NoTrans,
                     4, 4, 4, 1.0f, dummy.data(), 3, dummy.data(), 4, 0.0f, dummy.data(), 4),
        std::invalid_argument);
}

void test_c_abi_v3() {
    XK_CHECK(nvxk_version_major() == NVXK_VERSION_MAJOR);
    XK_CHECK(nvxk_version_minor() == NVXK_VERSION_MINOR);
    XK_CHECK(nvxk_version_patch() == NVXK_VERSION_PATCH);
    XK_CHECK(std::string(nvxk_version_string()) == "3.0.0");

    nvxk_hw_info_t hw;
    XK_CHECK(nvxk_get_hw_info(&hw) == NVXK_OK);
    XK_CHECK(hw.physical_cores >= 1);
    XK_CHECK(hw.logical_threads >= hw.physical_cores);
    XK_CHECK(nvxk_get_hw_info(nullptr) == NVXK_ERR_INVALID_ARG);

    // Generalized C ABI sgemm_ex & dgemm_ex with TransB and alpha/beta
    const size_t M = 11, N = 13, K = 9;
    std::vector<float> Af(M * K, 1.0f), Bf(N * K, 2.0f), Cf(M * N, 5.0f);
    int t_used = 0;
    XK_CHECK(nvxk_sgemm_ex(NVXK_ROW_MAJOR, NVXK_NO_TRANS, NVXK_TRANS,
                           M, N, K, 0.5f, Af.data(), K, Bf.data(), K,
                           2.0f, Cf.data(), N, 0, &t_used) == NVXK_OK);
    XK_CHECK(t_used >= 1);
    // Expected: 0.5 * (9 * 1 * 2) + 2.0 * 5.0 = 9.0 + 10.0 = 19.0
    for (float v : Cf) XK_CHECK(std::fabs(v - 19.0f) < 1e-4f);

    std::vector<double> Ad(M * K, 1.0), Bd(N * K, 2.0), Cd(M * N, 5.0);
    XK_CHECK(nvxk_dgemm_ex(NVXK_ROW_MAJOR, NVXK_NO_TRANS, NVXK_TRANS,
                           M, N, K, 0.5, Ad.data(), K, Bd.data(), K,
                           2.0, Cd.data(), N, 1, &t_used) == NVXK_OK);
    for (double v : Cd) XK_CHECK(std::fabs(v - 19.0) < 1e-11);

    // Invalid enum returns NVXK_ERR_INVALID_ARG
    XK_CHECK(nvxk_sgemm_ex(99, NVXK_NO_TRANS, NVXK_NO_TRANS,
                           M, N, K, 1.0f, Af.data(), K, Bf.data(), N,
                           0.0f, Cf.data(), N, 0, nullptr) == NVXK_ERR_INVALID_ARG);
    XK_CHECK(std::string(nvxk_last_error()).size() > 0);
}

void test_randomized_stress() {
    std::mt19937 rng(987654u);
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
    for (int iter = 0; iter < 15; ++iter) {
        const size_t M = 1 + (rng() % 47);
        const size_t K = 1 + (rng() % 53);
        const size_t N = 1 + (rng() % 49);
        std::vector<float> A(M * K), B(K * N), C(M * N);
        for (float& x : A) x = dist(rng);
        for (float& x : B) x = dist(rng);
        std::vector<double> ref;
        compute_ref_f64(A, B, ref, M, K, N);
        fill_nan(C);
        XK_gemm(A.data(), B.data(), C.data(), M, K, N);
        check_gemm_f32_close(ref, C, K, 1.5);
    }
}

int main() {
#if defined(_OPENMP)
    omp_set_dynamic(0);
#endif
    const size_t vector_sizes[] = {0, 1, 3, 4, 7, 8, 9, 17, 257};
    test_vector_add<float>(vector_sizes, sizeof(vector_sizes) / sizeof(vector_sizes[0]));
    test_vector_add<double>(vector_sizes, sizeof(vector_sizes) / sizeof(vector_sizes[0]));

    test_odd_and_large_k_gemm_f32();
    test_odd_and_large_k_gemm_f64();
    test_thin_m_streaming_and_n_axis();
    test_differential_randomized_shapes();
    test_graceful_memory_budget_clamping();
    test_c_abi();
    test_aliasing_rejection();
    test_integer_gemm_and_overflow();
    test_phase5_hardware_planner_workspace_profile();
    test_empty_and_null_checks();

    // Regression C2: XK_Workspace watermark must never exceed budget across alternating shapes
    {
        XK_Workspace ws(128 * 1024);
        ws.prepare_f32(25 * 1024, 5 * 1024, 1);  // B=100 KiB, A=20 KiB -> 120 KiB
        XK_CHECK(ws.allocated_bytes() <= 128 * 1024);
        ws.prepare_f32(5 * 1024, 25 * 1024, 1);  // B=20 KiB, A=100 KiB -> must reallocate, not 200 KiB
        XK_CHECK(ws.allocated_bytes() <= 128 * 1024);
    }

    // Regression C3 & L2: Ultra-tight budgets (including Manual overrides) degrade gracefully without throwing
    {
        const size_t M = 64, K = 64, N = 64;
        std::vector<float> A(M * K, 0.5f), B(K * N, 0.25f), C(M * N, 0.0f);
        std::vector<double> ref;
        compute_ref_f64(A, B, ref, M, K, N);

        // 16 KiB budget with Manual 2T & oversized manual blocks -> must clamp & succeed
        XK_Workspace ws16(16 * 1024);
        XK_WorkloadDesc wd(M, K, N);
        wd.mode = XK_ExecMode::Manual;
        wd.manual_threads = 2;
        wd.manual_mc = 120;
        wd.manual_kc = 256;
        wd.manual_nc = 1024;
        fill_nan(C);
        XK_ExecutionPlan p16 = XK_gemm_ex(A.data(), B.data(), C.data(), wd, &ws16);
        check_gemm_f32_close(ref, C, K);
        XK_CHECK(ws16.allocated_bytes() <= 16 * 1024);
        if (XK_has_avx2_fma()) XK_CHECK(p16.explanation.budget_clamped);

        // Extreme 64-byte budget -> must degrade to ScalarIkj (0 workspace) without throwing
        XK_Workspace ws_tiny(64);
        wd.mode = XK_ExecMode::Auto;
        fill_nan(C);
        XK_ExecutionPlan p_tiny = XK_gemm_ex(A.data(), B.data(), C.data(), wd, &ws_tiny);
        check_gemm_f32_close(ref, C, K);
        XK_CHECK(p_tiny.kernel == XK_KernelKind::ScalarIkj);
        XK_CHECK(ws_tiny.allocated_bytes() == 0);
    }

    // Regression C6: XK_gemm_blocked with large block_size must not throw length_error
    {
        const size_t M = 32, K = 32, N = 32;
        std::vector<float> A(M * K, 0.5f), B(K * N, 0.5f), C(M * N, 0.0f);
        std::vector<double> ref;
        compute_ref_f64(A, B, ref, M, K, N);
        XK_gemm_blocked(A.data(), B.data(), C.data(), M, K, N, 8192);
        check_gemm_f32_close(ref, C, K);
    }

    // Regression L1: XK_load_hardware_profile must reject corrupted/invalid profile values
    {
        const std::string bad_path = "nvxk_bad_profile.cfg";
        {
            std::ofstream out(bad_path.c_str());
            out << "nvxk_profile_version=1\n"
                << "physical_cores=-2\n"
                << "logical_threads=0\n"
                << "l1d_bytes=0\n"
                << "peak_gflops_1t=nan\n";
        }
        XK_HardwareProfile loaded;
        XK_CHECK(!XK_load_hardware_profile(bad_path, loaded));
        std::remove(bad_path.c_str());
    }

    test_generalized_blas_gemm();
    test_c_abi_v3();
    test_randomized_stress();
    std::cout << "[PASS] All NVXK unit, differential, C ABI, and hardening tests succeeded.\n";
    return EXIT_SUCCESS;
}