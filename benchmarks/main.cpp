#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <new>
#include <numeric>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <nvxk.h>

#if defined(_WIN32)
#include <malloc.h>
#endif
#if defined(_OPENMP)
#include <omp.h>
#endif

using namespace std;
using namespace std::chrono;

template <typename T, size_t Alignment = 64>
struct AlignedAllocator {
    using value_type = T;
    AlignedAllocator() noexcept = default;
    template <typename U>
    AlignedAllocator(const AlignedAllocator<U, Alignment>&) noexcept {}
    template <typename U>
    struct rebind {
        using other = AlignedAllocator<U, Alignment>;
    };
    T* allocate(size_t n) {
        if (n == 0) return nullptr;
        if (n > numeric_limits<size_t>::max() / sizeof(T)) throw bad_alloc();
        const size_t bytes = n * sizeof(T);
        void* ptr = nullptr;
#if defined(_WIN32)
        ptr = _aligned_malloc(bytes, Alignment);
        if (!ptr) throw bad_alloc();
#else
        if (posix_memalign(&ptr, Alignment, bytes) != 0 || !ptr) throw bad_alloc();
#endif
        return static_cast<T*>(ptr);
    }
    void deallocate(T* p, size_t) noexcept {
        if (!p) return;
#if defined(_WIN32)
        _aligned_free(p);
#else
        free(p);
#endif
    }
    template <typename U>
    bool operator==(const AlignedAllocator<U, Alignment>&) const noexcept { return true; }
    template <typename U>
    bool operator!=(const AlignedAllocator<U, Alignment>&) const noexcept { return false; }
};

template <typename T>
using AlignedVector = vector<T, AlignedAllocator<T, 64>>;

inline void compiler_memory_barrier() {
#if defined(__GNUC__) || defined(__clang__)
    asm volatile("" : : : "memory");
#endif
}

struct TimingStats {
    double min_seconds = 0.0;
    double median_seconds = 0.0;
    double mean_seconds = 0.0;
    double stddev_seconds = 0.0;
    double p95_seconds = 0.0;
    double p99_seconds = 0.0;
};

static double percentile_sorted(const vector<double>& sorted, double p) {
    if (sorted.empty()) return 0.0;
    if (sorted.size() == 1) return sorted[0];
    const double idx = p * static_cast<double>(sorted.size() - 1);
    const size_t lo = static_cast<size_t>(floor(idx));
    const size_t hi = min(sorted.size() - 1, lo + 1);
    const double frac = idx - static_cast<double>(lo);
    return sorted[lo] * (1.0 - frac) + sorted[hi] * frac;
}

// FIXED: Pre-run hook (e.g., cold-cache flush) executes STRICTLY OUTSIDE the timed window.
template <typename Func, typename PreHook>
TimingStats measure_time(Func&& f, PreHook&& pre_hook, int num_runs = 7, bool cold_cache = false) {
    // Warm-up run
    pre_hook();
    f();
    compiler_memory_barrier();

    size_t iterations = 1;
    if (!cold_cache) {
        const double min_sample_seconds = 0.04;
        double elapsed = 0.0;
        do {
            auto start = steady_clock::now();
            for (size_t j = 0; j < iterations; ++j) {
                f();
                compiler_memory_barrier();
            }
            auto end = steady_clock::now();
            elapsed = duration<double>(end - start).count();
            if (elapsed < min_sample_seconds) {
                if (iterations > numeric_limits<size_t>::max() / 2) break;
                iterations *= 2;
            }
        } while (elapsed < min_sample_seconds);
    }

    TimingStats stats;
    vector<double> times;
    times.reserve(num_runs);
    for (int i = 0; i < num_runs; ++i) {
        if (cold_cache) {
            pre_hook();
            compiler_memory_barrier();
        }
        auto start = steady_clock::now();
        for (size_t j = 0; j < iterations; ++j) {
            f();
            compiler_memory_barrier();
        }
        auto end = steady_clock::now();
        const double sample = duration<double>(end - start).count();
        if (sample <= 0.0 || !isfinite(sample)) return stats;
        times.push_back(sample / static_cast<double>(iterations));
    }

    sort(times.begin(), times.end());
    stats.min_seconds = times.front();
    stats.median_seconds = percentile_sorted(times, 0.50);
    stats.p95_seconds = percentile_sorted(times, 0.95);
    stats.p99_seconds = percentile_sorted(times, 0.99);

    const double sum = accumulate(times.begin(), times.end(), 0.0);
    stats.mean_seconds = sum / static_cast<double>(times.size());
    double var = 0.0;
    for (double t : times) {
        const double d = t - stats.mean_seconds;
        var += d * d;
    }
    stats.stddev_seconds = (times.size() > 1)
                               ? sqrt(var / static_cast<double>(times.size() - 1))
                               : 0.0;
    return stats;
}

void compute_ref_f64(const AlignedVector<float>& A,
                     const AlignedVector<float>& B,
                     vector<double>& C_ref,
                     size_t M, size_t K, size_t N) {
    C_ref.assign(M * N, 0.0);
    for (size_t i = 0; i < M; ++i) {
        for (size_t k = 0; k < K; ++k) {
            const double a = static_cast<double>(A[i * K + k]);
            for (size_t j = 0; j < N; ++j) {
                C_ref[i * N + j] += a * static_cast<double>(B[k * N + j]);
            }
        }
    }
}

bool verify_result(const vector<double>& C_ref,
                   const AlignedVector<float>& C_opt,
                   size_t K, const string& name,
                   double& out_max_rel_err) {
    const double k_factor = max(1.0, sqrt(static_cast<double>(K)));
    const double abs_tol = 1e-5 * k_factor;
    const double rel_tol = 1e-4;
    out_max_rel_err = 0.0;

    for (size_t i = 0; i < C_opt.size(); ++i) {
        const double diff = fabs(C_ref[i] - static_cast<double>(C_opt[i]));
        const double scale = max(1.0, fabs(C_ref[i]));
        out_max_rel_err = max(out_max_rel_err, diff / scale);
        if (!isfinite(C_opt[i]) || diff > abs_tol + rel_tol * scale) {
            cerr << "\n[FAILED] " << name << " at index " << i
                 << ": ref=" << C_ref[i] << ", got=" << C_opt[i]
                 << ", diff=" << diff << "\n";
            return false;
        }
    }
    return true;
}

struct Options {
    vector<size_t> sizes = {128, 256, 512, 1024};
    vector<int> thread_counts = {1, 2, 4, 8};
    int runs = 7;
    string algorithm = "all";
    string csv_path;
    string profile_path;
    size_t m = 0, k = 0, n = 0;
    bool has_shape = false;
    bool calibrate = false;
    bool explain = false;
    bool cold_cache = false;
};

void print_help(const char* prog) {
    cout << "Usage: " << prog << " [options]\n"
         << "  --size N              Run one square matrix size\n"
         << "  --sizes N,N,...       Run several square matrix sizes\n"
         << "  --shape M,K,N         Run rectangular A(MxK) * B(KxN)\n"
         << "  --runs N              Timed samples per config (default: 7)\n"
         << "  --threads N,N,...     OpenMP thread counts (default: 1,2,4,8)\n"
         << "  --algorithm NAME      all, naive, ikj, blocked, avx2, avx2_asm, omp, auto, auto_asm\n"
         << "  --calibrate           Run bounded hardware calibration before benchmarking\n"
         << "  --profile FILE        Load hardware profile (or save after --calibrate)\n"
         << "  --explain             Print Adaptive Planner rationale for each shape\n"
         << "  --cold-cache          Flush L3 cache OUTSIDE the timed window before each sample\n"
         << "  --csv FILE            Write full statistical measurements to CSV\n"
         << "  --help                Show this help\n";
}

template <typename T>
T parse_pos(const string& tok, const string& name) {
    if (tok.empty() || tok.find_first_not_of("0123456789") != string::npos) {
        throw invalid_argument("invalid " + name + ": " + tok);
    }
    unsigned long long v = stoull(tok);
    if (v == 0 || v > static_cast<unsigned long long>(numeric_limits<T>::max())) {
        throw invalid_argument("invalid " + name + ": " + tok);
    }
    return static_cast<T>(v);
}

template <typename T>
vector<T> parse_list(const string& text, const string& name) {
    vector<T> out;
    stringstream ss(text);
    string tok;
    while (getline(ss, tok, ',')) out.push_back(parse_pos<T>(tok, name));
    if (out.empty()) throw invalid_argument("empty " + name);
    return out;
}

Options parse_options(int argc, char** argv) {
    Options opt;
    for (int i = 1; i < argc; ++i) {
        string arg = argv[i];
        if (arg == "--help") {
            print_help(argv[0]);
            exit(EXIT_SUCCESS);
        } else if (arg == "--calibrate") {
            opt.calibrate = true;
        } else if (arg == "--explain") {
            opt.explain = true;
        } else if (arg == "--cold-cache") {
            opt.cold_cache = true;
        } else if (i + 1 < argc) {
            string val = argv[++i];
            if (arg == "--size") opt.sizes = {parse_pos<size_t>(val, "size")};
            else if (arg == "--sizes") opt.sizes = parse_list<size_t>(val, "sizes");
            else if (arg == "--shape") {
                vector<size_t> d = parse_list<size_t>(val, "shape");
                if (d.size() != 3) throw invalid_argument("--shape expects M,K,N");
                opt.m = d[0]; opt.k = d[1]; opt.n = d[2]; opt.has_shape = true;
            } else if (arg == "--runs") opt.runs = parse_pos<int>(val, "runs");
            else if (arg == "--threads") opt.thread_counts = parse_list<int>(val, "threads");
            else if (arg == "--algorithm") {
                if (val != "all" && val != "naive" && val != "ikj" &&
                    val != "blocked" && val != "avx2" && val != "avx2_asm" &&
                    val != "omp" && val != "auto" && val != "auto_asm") {
                    throw invalid_argument("unknown algorithm: " + val);
                }
                opt.algorithm = val;
            }
            else if (arg == "--csv") opt.csv_path = val;
            else if (arg == "--profile") opt.profile_path = val;
            else throw invalid_argument("unknown option: " + arg);
        } else {
            throw invalid_argument("unknown or incomplete option: " + arg);
        }
    }
    return opt;
}

int main(int argc, char** argv) {
    Options opt;
    try {
        opt = parse_options(argc, argv);
    } catch (const exception& e) {
        cerr << "Argument error: " << e.what() << "\n";
        print_help(argv[0]);
        return EXIT_FAILURE;
    }

#if defined(_OPENMP)
    omp_set_dynamic(0);
#endif

    XK_HardwareProfile profile = XK_default_hardware_profile();
    if (!opt.profile_path.empty() && !opt.calibrate) {
        if (XK_load_hardware_profile(opt.profile_path, profile)) {
            cout << "[INFO] Loaded hardware profile from " << opt.profile_path << "\n";
        }
    }
    if (opt.calibrate) {
        profile = XK_calibrate_hardware(200.0);
        if (!opt.profile_path.empty()) {
            XK_save_hardware_profile(profile, opt.profile_path);
        }
    }
    XK_set_active_profile(profile);

    cout << "=====================================================================\n";
    cout << "NVXK Research Benchmark Suite — Hardware & Calibration Summary:\n";
    cout << "CPU        : " << profile.hw.brand << " (" << profile.hw.vendor << ")\n";
    cout << "Topology   : " << profile.hw.physical_cores << " physical cores / "
         << profile.hw.logical_threads << " logical threads\n";
    cout << "Caches     : L1d=" << (profile.hw.l1d_bytes / 1024) << " KiB, "
         << "L2=" << (profile.hw.l2_bytes / 1024) << " KiB, "
         << "L3=" << (profile.hw.l3_bytes / 1024) << " KiB\n";
    cout << "ISA        : AVX2+FMA=" << (XK_has_avx2_fma() ? "Yes" : "No")
         << ", x86-64 ASM=" << (XK_has_avx2_asm_kernel() ? "Yes" : "No") << "\n";
    cout << "Profile    : " << (profile.calibrated ? "Calibrated" : "Default")
         << " (1T=" << fixed << setprecision(1) << profile.peak_gflops_1t
         << " GFLOPS, BW=" << profile.mem_bandwidth_gbs << " GB/s, OMP fork="
         << profile.omp_fork_overhead_us << " us, SMT eff="
         << setprecision(2) << profile.smt_efficiency << ")\n";
    cout << "Cache Mode : " << (opt.cold_cache ? "cold-cache (flush outside timer)" : "warm-cache")
         << " | Runs=" << opt.runs << "\n";
    cout << "=====================================================================\n\n";

    ofstream csv;
    if (!opt.csv_path.empty()) {
        csv.open(opt.csv_path.c_str());
        if (!csv) {
            cerr << "Cannot open CSV: " << opt.csv_path << "\n";
            return EXIT_FAILURE;
        }
        csv << "# CPU: " << profile.hw.brand << "\n";
        csv << "# PhysicalCores: " << profile.hw.physical_cores
            << " LogicalThreads: " << profile.hw.logical_threads << "\n";
        csv << "# CacheMode: " << (opt.cold_cache ? "cold" : "warm") << "\n";
        csv << "M,K,N,algorithm,threads_req,threads_used,min_sec,median_sec,mean_sec,stddev_sec,p95_sec,p99_sec,gflops,workspace_kb,max_rel_err\n";
    }

    vector<uint8_t> flush_buf;
    if (opt.cold_cache) {
        flush_buf.assign(max(profile.hw.l3_bytes * 2, static_cast<size_t>(16 * 1024 * 1024)), 1);
    }
    auto maybe_flush = [&]() {
        if (!flush_buf.empty()) {
            volatile uint8_t acc = 0;
            for (size_t i = 0; i < flush_buf.size(); i += 64) {
                flush_buf[i] ^= 1;
                acc ^= flush_buf[i];
            }
            (void)acc;
        }
    };

    struct Shape { size_t M, K, N; };
    vector<Shape> shapes;
    if (opt.has_shape) shapes.push_back({opt.m, opt.k, opt.n});
    else for (size_t s : opt.sizes) shapes.push_back({s, s, s});

    cout << left << setw(15) << "M x K x N" << setw(11) << "Algorithm"
         << setw(9) << "Threads" << setw(12) << "Median(s)"
         << setw(11) << "Mean(s)" << setw(10) << "Std(s)"
         << setw(10) << "GFLOPS" << setw(9) << "WS(KiB)" << setw(11) << "MaxRelErr" << "\n";
    cout << string(98, '-') << "\n";

    mt19937 rng(42);
    uniform_real_distribution<float> dist(-1.0f, 1.0f);
    XK_Workspace ws(64 * 1024 * 1024);

    const vector<string> algs = {
        "naive", "ikj", "blocked", "avx2", "avx2_asm", "omp", "auto", "auto_asm"
    };
    for (const Shape& sh : shapes) {
        const size_t M = sh.M, K = sh.K, N = sh.N;
        const string shape_str = to_string(M) + "x" + to_string(K) + "x" + to_string(N);
        AlignedVector<float> A(M * K), B(K * N), C(M * N);
        for (size_t i = 0; i < A.size(); ++i) A[i] = dist(rng);
        for (size_t i = 0; i < B.size(); ++i) B[i] = dist(rng);

        vector<double> C_ref;
        compute_ref_f64(A, B, C_ref, M, K, N);
        const double ops = 2.0 * static_cast<double>(M) * K * N;

        if (opt.explain) {
            const XK_ExecutionPlan p = XK_plan_gemm(XK_WorkloadDesc(M, K, N), profile);
            cout << "[PLAN " << shape_str << "] " << p.explanation.reason << "\n";
        }

        for (const string& alg : algs) {
            if (opt.algorithm != "all" && opt.algorithm != alg) continue;
            const vector<int> t_list = (alg == "omp") ? opt.thread_counts : vector<int>(1, 1);

            for (int req_t : t_list) {
                fill(C.begin(), C.end(), numeric_limits<float>::quiet_NaN());
                int used_t = 1;
                size_t ws_bytes = 0;
                TimingStats st;

                if (alg == "naive") {
                    st = measure_time([&]() {
                        XK_gemm_naive(A.data(), B.data(), C.data(), M, K, N);
                    }, maybe_flush, opt.runs, opt.cold_cache);
                } else if (alg == "ikj") {
                    st = measure_time([&]() {
                        XK_gemm_ikj(A.data(), B.data(), C.data(), M, K, N);
                    }, maybe_flush, opt.runs, opt.cold_cache);
                } else if (alg == "blocked") {
                    st = measure_time([&]() {
                        XK_gemm_blocked(A.data(), B.data(), C.data(), M, K, N, 64);
                    }, maybe_flush, opt.runs, opt.cold_cache);
                } else if (alg == "avx2") {
                    st = measure_time([&]() {
                        XK_gemm_avx2(A.data(), B.data(), C.data(), M, K, N);
                    }, maybe_flush, opt.runs, opt.cold_cache);
                } else if (alg == "avx2_asm") {
                    st = measure_time([&]() {
                        XK_gemm_avx2_asm(A.data(), B.data(), C.data(), M, K, N);
                    }, maybe_flush, opt.runs, opt.cold_cache);
                } else if (alg == "omp") {
                    st = measure_time([&]() {
                        XK_gemm_omp(A.data(), B.data(), C.data(), M, K, N, req_t, &used_t);
                    }, maybe_flush, opt.runs, opt.cold_cache);
                } else if (alg == "auto" || alg == "auto_asm") {
                    XK_WorkloadDesc wd(M, K, N);
                    wd.mode = XK_ExecMode::Auto;
                    wd.prefer_asm_kernel = (alg == "auto_asm");
                    XK_ExecutionPlan p = XK_plan_gemm(wd, profile);
                    ws_bytes = p.required_workspace_bytes;
                    st = measure_time([&]() {
                        XK_gemm_ex(A.data(), B.data(), C.data(), wd, &ws, &profile, &used_t);
                    }, maybe_flush, opt.runs, opt.cold_cache);
                }

                double max_rel = 0.0;
                if (!verify_result(C_ref, C, K, alg, max_rel)) return EXIT_FAILURE;

                const double gflops = (st.median_seconds > 0.0)
                                          ? ((ops / st.median_seconds) * 1e-9)
                                          : 0.0;
                const double ws_kb = static_cast<double>(ws_bytes) / 1024.0;
                const string t_col = ((alg == "auto" || alg == "auto_asm") ? "a" : to_string(req_t)) +
                                     "/" + to_string(used_t);

                cout << left << setw(15) << shape_str << setw(11) << alg
                     << setw(9) << t_col
                     << setw(12) << fixed << setprecision(6) << st.median_seconds
                     << setw(11) << setprecision(6) << st.mean_seconds
                     << setw(10) << setprecision(6) << st.stddev_seconds
                     << setw(10) << setprecision(2) << gflops
                     << setw(9) << setprecision(1) << ws_kb
                     << setw(11) << scientific << setprecision(2) << max_rel
                     << defaultfloat << "\n";

                if (csv) {
                    csv << M << "," << K << "," << N << "," << alg << ","
                        << req_t << "," << used_t << ","
                        << setprecision(10) << st.min_seconds << ","
                        << setprecision(10) << st.median_seconds << ","
                        << setprecision(10) << st.mean_seconds << ","
                        << setprecision(10) << st.stddev_seconds << ","
                        << setprecision(10) << st.p95_seconds << ","
                        << setprecision(10) << st.p99_seconds << ","
                        << setprecision(6) << gflops << ","
                        << setprecision(2) << ws_kb << ","
                        << scientific << setprecision(4) << max_rel << defaultfloat << "\n";
                }
            }
        }
    }

    cout << "\n[SUCCESS] All benchmarks passed numerical verification.\n";
    return EXIT_SUCCESS;
}