#include <nvxk/unified.hpp>

#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {

int g_failures = 0;

void expect_true(bool cond, const char* msg) {
    if (!cond) {
        std::cerr << "[FAIL] " << msg << "\n";
        ++g_failures;
    }
}

void test_topology_and_numa_alloc() {
    const auto& topo = nvxk::topology::cached_topology();
    const XK_HardwareInfo hw = XK_detect_hardware();
    expect_true(topo.physical_cores >= 1, "topology: physical_cores >= 1");
    expect_true(topo.logical_pus >= topo.physical_cores, "topology: logical_pus >= physical_cores");
    expect_true(topo.numa_nodes >= 1, "topology: numa_nodes >= 1");
    expect_true(topo.l1d_bytes >= 4096, "topology: l1d_bytes >= 4 KiB");
    expect_true(topo.l2_bytes >= topo.l1d_bytes, "topology: l2_bytes >= l1d_bytes");
    expect_true(hw.physical_cores == topo.physical_cores, "topology: XK_detect_hardware synced with topology physical_cores");
    expect_true(hw.l1d_bytes == topo.l1d_bytes, "topology: XK_detect_hardware synced with topology l1d_bytes");

#if defined(NVXK_ENABLE_HWLOC)
    expect_true(topo.hwloc_compiled && topo.hwloc_active, "topology: hwloc active when compiled in");
#endif
    expect_true(!nvxk::topology::pin_current_thread_to_core(-1), "topology: negative core_index returns false");
    (void)nvxk::topology::pin_openmp_team(topo.physical_cores);

    // NUMA-aware 64-byte aligned allocation test
    void* buf = nvxk::topology::alloc_numa_aligned(4096, 64, 0);
    expect_true(buf != nullptr, "topology: alloc_numa_aligned returned non-null");
    expect_true((reinterpret_cast<uintptr_t>(buf) % 64) == 0, "topology: alloc_numa_aligned 64-byte aligned");
    static_cast<float*>(buf)[0] = 42.0f;
    static_cast<float*>(buf)[1023] = -7.0f;
    nvxk::topology::free_numa_aligned(buf, 4096);

    std::cout << "[OK] Topology, XK_detect_hardware sync, OpenMP pinning & NUMA alloc ("
              << topo.backend_name << ", cores=" << topo.physical_cores
              << ", L1d=" << topo.l1d_bytes / 1024 << "KiB)\n";
}

void test_highway_simd_v5_equivalence() {
    const auto info = nvxk::simd::query_info();
#if defined(NVXK_ENABLE_HIGHWAY)
    expect_true(info.highway_compiled && info.highway_active, "simd: Highway active when compiled in");
    expect_true(info.lanes_f32 >= 1 && info.lanes_f64 >= 1, "simd: valid lane widths");
#endif

    std::mt19937 rng(42);
    std::uniform_real_distribution<float> dist_f(-3.0f, 3.0f);
    std::uniform_real_distribution<double> dist_d(-3.0, 3.0);

    const std::vector<size_t> sizes = {0, 1, 3, 7, 8, 15, 16, 17, 31, 32, 33, 63, 64, 127, 257, 1025};
    for (size_t n : sizes) {
        const size_t pad = 3;
        std::vector<float> a(n + pad), b(n + pad), c_ref(n + pad, 0.0f), c_hwy(n + pad, 0.0f);
        std::vector<float> gamma(n + pad, 1.25f), beta(n + pad, -0.5f);
        for (size_t i = 0; i < n + pad; ++i) { a[i] = dist_f(rng); b[i] = dist_f(rng); }

        const float* pa = a.data() + 1;
        const float* pb = b.data() + 2;
        float* pr = c_ref.data() + 1;
        float* ph = c_hwy.data() + 1;

        nvxk::simd::vec_add(pa, pb, pr, n, nvxk::BackendKind::Scalar);
        nvxk::simd::vec_add(pa, pb, ph, n, nvxk::BackendKind::Highway);
        for (size_t i = 0; i < n; ++i) expect_true(std::abs(pr[i] - ph[i]) <= 1e-6f, "simd: vec_add f32");

        nvxk::simd::vec_sub(pa, pb, pr, n, nvxk::BackendKind::Scalar);
        nvxk::simd::vec_sub(pa, pb, ph, n, nvxk::BackendKind::Highway);
        for (size_t i = 0; i < n; ++i) expect_true(std::abs(pr[i] - ph[i]) <= 1e-6f, "simd: vec_sub f32");

        nvxk::simd::vec_mul(pa, pb, pr, n, nvxk::BackendKind::Scalar);
        nvxk::simd::vec_mul(pa, pb, ph, n, nvxk::BackendKind::Highway);
        for (size_t i = 0; i < n; ++i) expect_true(std::abs(pr[i] - ph[i]) <= 1e-5f, "simd: vec_mul f32");

        nvxk::simd::vec_exp(pa, pr, n, nvxk::BackendKind::Scalar);
        nvxk::simd::vec_exp(pa, ph, n, nvxk::BackendKind::Highway);
        for (size_t i = 0; i < n; ++i) {
            expect_true(std::abs(pr[i] - ph[i]) <= 1e-4f * std::max(1.0f, std::abs(pr[i])), "simd: vec_exp f32");
        }

        nvxk::simd::vec_sigmoid(pa, pr, n, nvxk::BackendKind::Scalar);
        nvxk::simd::vec_sigmoid(pa, ph, n, nvxk::BackendKind::Highway);
        for (size_t i = 0; i < n; ++i) {
            expect_true(std::abs(pr[i] - ph[i]) <= 1e-5f, "simd: vec_sigmoid f32");
        }

        nvxk::simd::gelu_approx(pa, pr, n, nvxk::BackendKind::Scalar);
        nvxk::simd::gelu_approx(pa, ph, n, nvxk::BackendKind::Highway);
        for (size_t i = 0; i < n; ++i) {
            expect_true(std::abs(pr[i] - ph[i]) <= 1e-4f * std::max(1.0f, std::abs(pr[i])), "simd: gelu_approx f32");
        }

        if (n > 0) {
            nvxk::simd::softmax(pa, pr, n, nvxk::BackendKind::Scalar);
            nvxk::simd::softmax(pa, ph, n, nvxk::BackendKind::Highway);
            double sm_sum = 0.0;
            for (size_t i = 0; i < n; ++i) {
                expect_true(std::abs(pr[i] - ph[i]) <= 1e-5f, "simd: softmax f32");
                sm_sum += ph[i];
            }
            expect_true(std::abs(sm_sum - 1.0) <= 1e-4, "simd: softmax sums to 1");

            nvxk::simd::layernorm(pa, pr, n, 1e-5f, gamma.data() + 1, beta.data() + 1, nvxk::BackendKind::Scalar);
            nvxk::simd::layernorm(pa, ph, n, 1e-5f, gamma.data() + 1, beta.data() + 1, nvxk::BackendKind::Highway);
            for (size_t i = 0; i < n; ++i) {
                expect_true(std::abs(pr[i] - ph[i]) <= 1e-4f * std::max(1.0f, std::abs(pr[i])), "simd: layernorm f32");
            }
        }

        const float d_ref = nvxk::simd::dot(pa, pb, n, nvxk::BackendKind::Scalar);
        const float d_hwy = nvxk::simd::dot(pa, pb, n, nvxk::BackendKind::Highway);
        expect_true(std::abs(d_ref - d_hwy) <= 1e-3f * std::max(1.0f, std::abs(d_ref)), "simd: dot f32");

        std::vector<double> ad(n), bd(n);
        for (size_t i = 0; i < n; ++i) { ad[i] = dist_d(rng); bd[i] = dist_d(rng); }
        const double dd_ref = nvxk::simd::dot(ad.data(), bd.data(), n, nvxk::BackendKind::Scalar);
        const double dd_hwy = nvxk::simd::dot(ad.data(), bd.data(), n, nvxk::BackendKind::Highway);
        expect_true(std::abs(dd_ref - dd_hwy) <= 1e-11 * std::max(1.0, std::abs(dd_ref)), "simd: dot f64");
    }

    // Parallel GEMV test (M >= 64, M*K >= 65536)
    const size_t M = 131, K = 521;
    std::vector<float> A(M * K), x(K), y_ref(M, 0.5f), y_hwy(M, 0.5f);
    for (auto& v : A) v = dist_f(rng);
    for (auto& v : x) v = dist_f(rng);
    nvxk::simd::gemv(M, K, 1.5f, A.data(), K, x.data(), -0.25f, y_ref.data(), nvxk::BackendKind::Scalar);
    nvxk::simd::gemv(M, K, 1.5f, A.data(), K, x.data(), -0.25f, y_hwy.data(), nvxk::BackendKind::Highway);
    for (size_t i = 0; i < M; ++i) {
        expect_true(std::abs(y_ref[i] - y_hwy[i]) <= 1e-3f * std::max(1.0f, std::abs(y_ref[i])), "simd: parallel gemv f32");
    }

    std::cout << "[OK] Highway v5.0 SIMD tests (add/sub/mul/exp/sigmoid/gelu/softmax/layernorm/dot/gemv on "
              << info.active_target << ", f32_lanes=" << info.lanes_f32 << ")\n";
}

void test_blas_and_dispatcher() {
    std::mt19937 rng(123);
    std::uniform_real_distribution<float> dist_f(-2.0f, 2.0f);
    std::uniform_real_distribution<double> dist_d(-2.0, 2.0);

    struct Shape { size_t M, K, N; };
    const std::vector<Shape> shapes = {{1, 1, 1}, {3, 29, 37}, {32, 32, 32}, {65, 71, 59}, {128, 96, 112}};
    for (const auto& s : shapes) {
        std::vector<float> A(s.M * s.K), B(s.K * s.N), C_ref(s.M * s.N), C_auto(s.M * s.N);
        for (auto& v : A) v = dist_f(rng);
        for (auto& v : B) v = dist_f(rng);
        std::fill(C_ref.begin(), C_ref.end(), std::numeric_limits<float>::quiet_NaN());
        std::fill(C_auto.begin(), C_auto.end(), std::numeric_limits<float>::quiet_NaN());

        nvxk::RuntimeConfig sc; sc.gemm_backend = nvxk::BackendKind::Scalar;
        nvxk::dispatch::gemm(A.data(), B.data(), C_ref.data(), s.M, s.K, s.N, &sc);
        nvxk::dispatch::gemm(A.data(), B.data(), C_auto.data(), s.M, s.K, s.N, nullptr);

        for (size_t i = 0; i < C_ref.size(); ++i) {
            expect_true(!std::isnan(C_auto[i]), "dispatcher: beta==0 NaN-safe");
            const double diff = std::abs(double(C_auto[i]) - double(C_ref[i]));
            expect_true(diff <= 1e-4 * std::max(1.0, std::abs(double(C_ref[i]))), "dispatcher: GEMM f32 equivalence");
        }
    }

    // Calibrate BLIS crossover (safe no-op returning threshold when BLIS not linked)
    const double th = nvxk::dispatch::calibrate_blis_crossover();
    expect_true(th > 0.0, "dispatcher: calibrate_blis_crossover returns positive threshold");

    // SYRK & TRSM
    {
        const size_t N = 16, K = 12;
        std::vector<double> A(N * K), C(N * N, 0.0);
        for (auto& v : A) v = dist_d(rng);
        nvxk::blas::syrk_f64(false, XK_Transpose::NoTrans, N, K, 1.0, A.data(), K, 0.0, C.data(), N);
        for (size_t i = 0; i < N; ++i)
            for (size_t j = 0; j <= i; ++j) {
                double ref = 0.0;
                for (size_t k = 0; k < K; ++k) ref += A[i * K + k] * A[j * K + k];
                expect_true(std::abs(C[i * N + j] - ref) <= 1e-11, "blas: syrk_f64");
            }
    }
    {
        const size_t M = 12, N = 8;
        std::vector<double> L(M * M, 0.0), B_orig(M * N), X(M * N);
        for (size_t i = 0; i < M; ++i) {
            for (size_t j = 0; j < i; ++j) L[i * M + j] = dist_d(rng) * 0.2;
            L[i * M + i] = 2.5 + static_cast<double>(i) * 0.1;
        }
        for (size_t i = 0; i < M * N; ++i) B_orig[i] = X[i] = dist_d(rng);
        nvxk::blas::trsm_lower_left_f64(false, M, N, 1.5, L.data(), M, X.data(), N);
        for (size_t i = 0; i < M; ++i)
            for (size_t j = 0; j < N; ++j) {
                double lhs = 0.0;
                for (size_t k = 0; k <= i; ++k) lhs += L[i * M + k] * X[k * N + j];
                expect_true(std::abs(lhs - 1.5 * B_orig[i * N + j]) <= 1e-11, "blas: trsm_f64");
            }
    }
    std::cout << "[OK] Dispatcher, BLIS crossover hook, SYRK & TRSM tests passed\n";
}

void test_concurrency() {
    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t) {
        threads.emplace_back([t]() {
            const size_t N = 48;
            std::vector<float> A(N * N, 1.0f + 0.1f * t), B(N * N, 0.5f), C(N * N, 0.0f);
            nvxk::RuntimeConfig cfg; cfg.max_threads = 1;
            nvxk::dispatch::gemm(A.data(), B.data(), C.data(), N, N, N, &cfg);
            const float expected = static_cast<float>(N) * (1.0f + 0.1f * t) * 0.5f;
            expect_true(std::abs(C[0] - expected) <= 1e-3f, "concurrency: GEMM");
            std::vector<float> y(N * N);
            nvxk::simd::gelu_approx(A.data(), y.data(), N * N);
            expect_true(y[0] > 0.0f, "concurrency: Highway GELU");
        });
    }
    for (auto& th : threads) th.join();
    std::cout << "[OK] Concurrency & thread-safety test passed\n";
}

}  // namespace

int main() {
    test_topology_and_numa_alloc();
    test_highway_simd_v5_equivalence();
    test_blas_and_dispatcher();
    test_concurrency();
    if (g_failures > 0) {
        std::cerr << "[FAIL] Total failures: " << g_failures << "\n";
        return 1;
    }
    std::cout << "[PASS] All NVXK v5.0.0 Unified Computing Engine integration & numerical tests succeeded.\n";
    return 0;
}
