#include "nvxk_internal.h"
#include <algorithm>
#include <cstring>

#if defined(_OPENMP)
#include <omp.h>
#endif

#if !defined(XK_FORCE_SCALAR_FALLBACK) && \
    (defined(__GNUC__) || defined(__clang__)) && \
    (defined(__i386__) || defined(__x86_64__))
#define XK_ENABLE_X86_AVX2_DISPATCH 1
#include <immintrin.h>
#define XK_TARGET_AVX2 __attribute__((target("avx2")))
#define XK_TARGET_AVX2_FMA __attribute__((target("avx2,fma")))
#else
#define XK_ENABLE_X86_AVX2_DISPATCH 0
#define XK_TARGET_AVX2
#define XK_TARGET_AVX2_FMA
#endif

#if XK_ENABLE_X86_AVX2_DISPATCH && defined(__x86_64__) && \
    (defined(__GNUC__) || defined(__clang__))
#define XK_ENABLE_X86_64_ASM_KERNEL 1
#else
#define XK_ENABLE_X86_64_ASM_KERNEL 0
#endif

#if defined(__GNUC__) || defined(__clang__)
#define XK_RESTRICT __restrict__
#define XK_ALWAYS_INLINE __attribute__((always_inline)) inline
#elif defined(_MSC_VER)
#define XK_RESTRICT __restrict
#define XK_ALWAYS_INLINE __forceinline
#else
#define XK_RESTRICT
#define XK_ALWAYS_INLINE inline
#endif

bool XK_has_avx2_fma() {
#if XK_ENABLE_X86_AVX2_DISPATCH
    static const bool supported = []() {
        __builtin_cpu_init();
        return (__builtin_cpu_supports("avx2") != 0) &&
               (__builtin_cpu_supports("fma") != 0);
    }();
    return supported;
#else
    return false;
#endif
}

bool XK_has_avx2_asm_kernel() {
#if XK_ENABLE_X86_64_ASM_KERNEL
    return XK_has_avx2_fma();
#else
    return false;
#endif
}

namespace xk_internal {

bool has_avx2_only() {
#if XK_ENABLE_X86_AVX2_DISPATCH
    static const bool supported = []() {
        __builtin_cpu_init();
        return __builtin_cpu_supports("avx2") != 0;
    }();
    return supported;
#else
    return false;
#endif
}

#if XK_ENABLE_X86_AVX2_DISPATCH

static XK_TARGET_AVX2 void vector_add_avx2_f32_impl(
    const float* XK_RESTRICT a, const float* XK_RESTRICT b,
    float* XK_RESTRICT c, size_t n) {
    size_t i = 0;
    for (; i + 31 < n; i += 32) {
        _mm256_storeu_ps(&c[i + 0],  _mm256_add_ps(_mm256_loadu_ps(&a[i + 0]),  _mm256_loadu_ps(&b[i + 0])));
        _mm256_storeu_ps(&c[i + 8],  _mm256_add_ps(_mm256_loadu_ps(&a[i + 8]),  _mm256_loadu_ps(&b[i + 8])));
        _mm256_storeu_ps(&c[i + 16], _mm256_add_ps(_mm256_loadu_ps(&a[i + 16]), _mm256_loadu_ps(&b[i + 16])));
        _mm256_storeu_ps(&c[i + 24], _mm256_add_ps(_mm256_loadu_ps(&a[i + 24]), _mm256_loadu_ps(&b[i + 24])));
    }
    for (; i + 7 < n; i += 8) {
        _mm256_storeu_ps(&c[i], _mm256_add_ps(_mm256_loadu_ps(&a[i]), _mm256_loadu_ps(&b[i])));
    }
    for (; i < n; ++i) c[i] = a[i] + b[i];
}

static XK_TARGET_AVX2 void vector_add_avx2_f64_impl(
    const double* XK_RESTRICT a, const double* XK_RESTRICT b,
    double* XK_RESTRICT c, size_t n) {
    size_t i = 0;
    for (; i + 15 < n; i += 16) {
        _mm256_storeu_pd(&c[i + 0],  _mm256_add_pd(_mm256_loadu_pd(&a[i + 0]),  _mm256_loadu_pd(&b[i + 0])));
        _mm256_storeu_pd(&c[i + 4],  _mm256_add_pd(_mm256_loadu_pd(&a[i + 4]),  _mm256_loadu_pd(&b[i + 4])));
        _mm256_storeu_pd(&c[i + 8],  _mm256_add_pd(_mm256_loadu_pd(&a[i + 8]),  _mm256_loadu_pd(&b[i + 8])));
        _mm256_storeu_pd(&c[i + 12], _mm256_add_pd(_mm256_loadu_pd(&a[i + 12]), _mm256_loadu_pd(&b[i + 12])));
    }
    for (; i + 3 < n; i += 4) {
        _mm256_storeu_pd(&c[i], _mm256_add_pd(_mm256_loadu_pd(&a[i]), _mm256_loadu_pd(&b[i])));
    }
    for (; i < n; ++i) c[i] = a[i] + b[i];
}

// ============================================================================
// SIMD-Accelerated Panel Packing (f32)
// ============================================================================
static XK_TARGET_AVX2_FMA void pack_a_panel_6_f32(
    size_t mr, size_t kc, const float* XK_RESTRICT A, size_t lda,
    float* XK_RESTRICT A_packed) {
    if (mr == 6) {
        const float* a0 = A;
        const float* a1 = A + lda;
        const float* a2 = A + 2 * lda;
        const float* a3 = A + 3 * lda;
        const float* a4 = A + 4 * lda;
        const float* a5 = A + 5 * lda;
        size_t k = 0;
        for (; k + 3 < kc; k += 4) {
            __m128 r0 = _mm_loadu_ps(a0 + k);
            __m128 r1 = _mm_loadu_ps(a1 + k);
            __m128 r2 = _mm_loadu_ps(a2 + k);
            __m128 r3 = _mm_loadu_ps(a3 + k);
            _MM_TRANSPOSE4_PS(r0, r1, r2, r3);

            __m128 r4 = _mm_loadu_ps(a4 + k);
            __m128 r5 = _mm_loadu_ps(a5 + k);
            __m128 lo45 = _mm_unpacklo_ps(r4, r5);  // [a4[0], a5[0], a4[1], a5[1]]
            __m128 hi45 = _mm_unpackhi_ps(r4, r5);  // [a4[2], a5[2], a4[3], a5[3]]

            _mm_storeu_ps(A_packed + 0, r0);
            _mm_storel_pi(reinterpret_cast<__m64*>(A_packed + 4), lo45);

            _mm_storeu_ps(A_packed + 6, r1);
            _mm_storeh_pi(reinterpret_cast<__m64*>(A_packed + 10), lo45);

            _mm_storeu_ps(A_packed + 12, r2);
            _mm_storel_pi(reinterpret_cast<__m64*>(A_packed + 16), hi45);

            _mm_storeu_ps(A_packed + 18, r3);
            _mm_storeh_pi(reinterpret_cast<__m64*>(A_packed + 22), hi45);

            A_packed += 24;
        }
        for (; k < kc; ++k) {
            A_packed[0] = a0[k];
            A_packed[1] = a1[k];
            A_packed[2] = a2[k];
            A_packed[3] = a3[k];
            A_packed[4] = a4[k];
            A_packed[5] = a5[k];
            A_packed += 6;
        }
    } else {
        for (size_t k = 0; k < kc; ++k) {
            for (size_t r = 0; r < mr; ++r) A_packed[r] = A[r * lda + k];
            for (size_t r = mr; r < 6; ++r) A_packed[r] = 0.0f;
            A_packed += 6;
        }
    }
}

static XK_TARGET_AVX2_FMA void pack_b_slab_16_f32(
    size_t kc, size_t nr, const float* XK_RESTRICT B, size_t ldb,
    float* XK_RESTRICT B_packed) {
    if (nr == 16) {
        size_t k = 0;
        for (; k + 3 < kc; k += 4) {
            const float* b0p = &B[(k + 0) * ldb];
            const float* b1p = &B[(k + 1) * ldb];
            const float* b2p = &B[(k + 2) * ldb];
            const float* b3p = &B[(k + 3) * ldb];
            _mm256_store_ps(B_packed + 0,  _mm256_loadu_ps(b0p));
            _mm256_store_ps(B_packed + 8,  _mm256_loadu_ps(b0p + 8));
            _mm256_store_ps(B_packed + 16, _mm256_loadu_ps(b1p));
            _mm256_store_ps(B_packed + 24, _mm256_loadu_ps(b1p + 8));
            _mm256_store_ps(B_packed + 32, _mm256_loadu_ps(b2p));
            _mm256_store_ps(B_packed + 40, _mm256_loadu_ps(b2p + 8));
            _mm256_store_ps(B_packed + 48, _mm256_loadu_ps(b3p));
            _mm256_store_ps(B_packed + 56, _mm256_loadu_ps(b3p + 8));
            B_packed += 64;
        }
        for (; k < kc; ++k) {
            const float* bp = &B[k * ldb];
            _mm256_store_ps(B_packed + 0, _mm256_loadu_ps(bp));
            _mm256_store_ps(B_packed + 8, _mm256_loadu_ps(bp + 8));
            B_packed += 16;
        }
    } else {
        for (size_t k = 0; k < kc; ++k) {
            for (size_t c = 0; c < nr; ++c) B_packed[c] = B[k * ldb + c];
            for (size_t c = nr; c < 16; ++c) B_packed[c] = 0.0f;
            B_packed += 16;
        }
    }
}

// ============================================================================
// Optimized 6x16 f32 Microkernel (4x K-unrolled, dual-broadcast, prefetch)
// ============================================================================
#define XK_FMA_STEP_6x16_F32(ap, bp)                      \
    do {                                                  \
        const __m256 b0 = _mm256_load_ps((bp) + 0);       \
        const __m256 b1 = _mm256_load_ps((bp) + 8);       \
        __m256 a0 = _mm256_broadcast_ss((ap) + 0);        \
        c00 = _mm256_fmadd_ps(a0, b0, c00);               \
        c01 = _mm256_fmadd_ps(a0, b1, c01);               \
        a0 = _mm256_broadcast_ss((ap) + 1);               \
        c10 = _mm256_fmadd_ps(a0, b0, c10);               \
        c11 = _mm256_fmadd_ps(a0, b1, c11);               \
        a0 = _mm256_broadcast_ss((ap) + 2);               \
        c20 = _mm256_fmadd_ps(a0, b0, c20);               \
        c21 = _mm256_fmadd_ps(a0, b1, c21);               \
        a0 = _mm256_broadcast_ss((ap) + 3);               \
        c30 = _mm256_fmadd_ps(a0, b0, c30);               \
        c31 = _mm256_fmadd_ps(a0, b1, c31);               \
        a0 = _mm256_broadcast_ss((ap) + 4);               \
        c40 = _mm256_fmadd_ps(a0, b0, c40);               \
        c41 = _mm256_fmadd_ps(a0, b1, c41);               \
        a0 = _mm256_broadcast_ss((ap) + 5);               \
        c50 = _mm256_fmadd_ps(a0, b0, c50);               \
        c51 = _mm256_fmadd_ps(a0, b1, c51);               \
    } while (0)

static XK_TARGET_AVX2_FMA void micro_6x16_full_f32(
    size_t kc, bool init_c,
    const float* XK_RESTRICT A_packed,
    const float* XK_RESTRICT B_packed,
    float* XK_RESTRICT C, size_t ldc) {
    __m256 c00, c01, c10, c11, c20, c21, c30, c31, c40, c41, c50, c51;

    if (init_c) {
        c00 = _mm256_setzero_ps(); c01 = _mm256_setzero_ps();
        c10 = _mm256_setzero_ps(); c11 = _mm256_setzero_ps();
        c20 = _mm256_setzero_ps(); c21 = _mm256_setzero_ps();
        c30 = _mm256_setzero_ps(); c31 = _mm256_setzero_ps();
        c40 = _mm256_setzero_ps(); c41 = _mm256_setzero_ps();
        c50 = _mm256_setzero_ps(); c51 = _mm256_setzero_ps();
    } else {
        c00 = _mm256_loadu_ps(&C[0 * ldc]); c01 = _mm256_loadu_ps(&C[0 * ldc + 8]);
        c10 = _mm256_loadu_ps(&C[1 * ldc]); c11 = _mm256_loadu_ps(&C[1 * ldc + 8]);
        c20 = _mm256_loadu_ps(&C[2 * ldc]); c21 = _mm256_loadu_ps(&C[2 * ldc + 8]);
        c30 = _mm256_loadu_ps(&C[3 * ldc]); c31 = _mm256_loadu_ps(&C[3 * ldc + 8]);
        c40 = _mm256_loadu_ps(&C[4 * ldc]); c41 = _mm256_loadu_ps(&C[4 * ldc + 8]);
        c50 = _mm256_loadu_ps(&C[5 * ldc]); c51 = _mm256_loadu_ps(&C[5 * ldc + 8]);
    }

    size_t k = 0;
    for (; k + 3 < kc; k += 4) {

        XK_FMA_STEP_6x16_F32(A_packed + 0,  B_packed + 0);
        XK_FMA_STEP_6x16_F32(A_packed + 6,  B_packed + 16);
        XK_FMA_STEP_6x16_F32(A_packed + 12, B_packed + 32);
        XK_FMA_STEP_6x16_F32(A_packed + 18, B_packed + 48);

        A_packed += 24;
        B_packed += 64;
    }
    for (; k < kc; ++k) {
        XK_FMA_STEP_6x16_F32(A_packed, B_packed);
        A_packed += 6;
        B_packed += 16;
    }

    _mm256_storeu_ps(&C[0 * ldc], c00); _mm256_storeu_ps(&C[0 * ldc + 8], c01);
    _mm256_storeu_ps(&C[1 * ldc], c10); _mm256_storeu_ps(&C[1 * ldc + 8], c11);
    _mm256_storeu_ps(&C[2 * ldc], c20); _mm256_storeu_ps(&C[2 * ldc + 8], c21);
    _mm256_storeu_ps(&C[3 * ldc], c30); _mm256_storeu_ps(&C[3 * ldc + 8], c31);
    _mm256_storeu_ps(&C[4 * ldc], c40); _mm256_storeu_ps(&C[4 * ldc + 8], c41);
    _mm256_storeu_ps(&C[5 * ldc], c50); _mm256_storeu_ps(&C[5 * ldc + 8], c51);
}

static XK_TARGET_AVX2_FMA void micro_6x16_edge_f32(
    size_t kc, bool init_c,
    const float* XK_RESTRICT A_packed,
    const float* XK_RESTRICT B_packed,
    float* XK_RESTRICT C, size_t ldc,
    size_t mr, size_t nr) {
    __m256 c00, c01, c10, c11, c20, c21, c30, c31, c40, c41, c50, c51;

    if (init_c) {
        c00 = _mm256_setzero_ps(); c01 = _mm256_setzero_ps();
        c10 = _mm256_setzero_ps(); c11 = _mm256_setzero_ps();
        c20 = _mm256_setzero_ps(); c21 = _mm256_setzero_ps();
        c30 = _mm256_setzero_ps(); c31 = _mm256_setzero_ps();
        c40 = _mm256_setzero_ps(); c41 = _mm256_setzero_ps();
        c50 = _mm256_setzero_ps(); c51 = _mm256_setzero_ps();
    } else {
        alignas(32) float tmp_c[6 * 16] = {};
        for (size_t r = 0; r < mr; ++r) {
            for (size_t c = 0; c < nr; ++c) tmp_c[r * 16 + c] = C[r * ldc + c];
        }
        c00 = _mm256_load_ps(&tmp_c[0 * 16]); c01 = _mm256_load_ps(&tmp_c[0 * 16 + 8]);
        c10 = _mm256_load_ps(&tmp_c[1 * 16]); c11 = _mm256_load_ps(&tmp_c[1 * 16 + 8]);
        c20 = _mm256_load_ps(&tmp_c[2 * 16]); c21 = _mm256_load_ps(&tmp_c[2 * 16 + 8]);
        c30 = _mm256_load_ps(&tmp_c[3 * 16]); c31 = _mm256_load_ps(&tmp_c[3 * 16 + 8]);
        c40 = _mm256_load_ps(&tmp_c[4 * 16]); c41 = _mm256_load_ps(&tmp_c[4 * 16 + 8]);
        c50 = _mm256_load_ps(&tmp_c[5 * 16]); c51 = _mm256_load_ps(&tmp_c[5 * 16 + 8]);
    }

    size_t k = 0;
    for (; k + 3 < kc; k += 4) {
        XK_FMA_STEP_6x16_F32(A_packed + 0,  B_packed + 0);
        XK_FMA_STEP_6x16_F32(A_packed + 6,  B_packed + 16);
        XK_FMA_STEP_6x16_F32(A_packed + 12, B_packed + 32);
        XK_FMA_STEP_6x16_F32(A_packed + 18, B_packed + 48);
        A_packed += 24;
        B_packed += 64;
    }
    for (; k < kc; ++k) {
        XK_FMA_STEP_6x16_F32(A_packed, B_packed);
        A_packed += 6;
        B_packed += 16;
    }

    alignas(32) float tmp_c[6 * 16];
    _mm256_store_ps(&tmp_c[0 * 16], c00); _mm256_store_ps(&tmp_c[0 * 16 + 8], c01);
    _mm256_store_ps(&tmp_c[1 * 16], c10); _mm256_store_ps(&tmp_c[1 * 16 + 8], c11);
    _mm256_store_ps(&tmp_c[2 * 16], c20); _mm256_store_ps(&tmp_c[2 * 16 + 8], c21);
    _mm256_store_ps(&tmp_c[3 * 16], c30); _mm256_store_ps(&tmp_c[3 * 16 + 8], c31);
    _mm256_store_ps(&tmp_c[4 * 16], c40); _mm256_store_ps(&tmp_c[4 * 16 + 8], c41);
    _mm256_store_ps(&tmp_c[5 * 16], c50); _mm256_store_ps(&tmp_c[5 * 16 + 8], c51);
    for (size_t r = 0; r < mr; ++r) {
        for (size_t c = 0; c < nr; ++c) C[r * ldc + c] = tmp_c[r * 16 + c];
    }
}

#undef XK_FMA_STEP_6x16_F32

// ============================================================================
// Experimental Hand-Tuned x86-64 AVX2+FMA Inline Assembly 6x16 Microkernel
// Explicit register allocation: ymm4..ymm15 accumulators, ymm0..ymm1 B,
// ymm2..ymm3 alternating A broadcasts, 4x K-unrolled with software prefetch.
// ============================================================================
#if XK_ENABLE_X86_64_ASM_KERNEL
static XK_TARGET_AVX2_FMA void micro_6x16_full_asm_f32(
    size_t kc, bool init_c,
    const float* XK_RESTRICT A_packed,
    const float* XK_RESTRICT B_packed,
    float* XK_RESTRICT C, size_t ldc) {
    const size_t ldc_bytes = ldc * sizeof(float);
    const size_t k_quads = kc >> 2;
    const size_t k_rem = kc & 3;
    const uint64_t init_flag = init_c ? 1ULL : 0ULL;

    const float* a_ptr = A_packed;
    const float* b_ptr = B_packed;
    float* c_ptr = C;
    size_t cnt = k_quads;
    size_t rem = k_rem;

    __asm__ __volatile__(
        // Load or zero C accumulators (ymm4..ymm15)
        "testq %[init_f], %[init_f]\n\t"
        "jz 1f\n\t"
        "vxorps %%ymm4,  %%ymm4,  %%ymm4\n\t"
        "vxorps %%ymm5,  %%ymm5,  %%ymm5\n\t"
        "vxorps %%ymm6,  %%ymm6,  %%ymm6\n\t"
        "vxorps %%ymm7,  %%ymm7,  %%ymm7\n\t"
        "vxorps %%ymm8,  %%ymm8,  %%ymm8\n\t"
        "vxorps %%ymm9,  %%ymm9,  %%ymm9\n\t"
        "vxorps %%ymm10, %%ymm10, %%ymm10\n\t"
        "vxorps %%ymm11, %%ymm11, %%ymm11\n\t"
        "vxorps %%ymm12, %%ymm12, %%ymm12\n\t"
        "vxorps %%ymm13, %%ymm13, %%ymm13\n\t"
        "vxorps %%ymm14, %%ymm14, %%ymm14\n\t"
        "vxorps %%ymm15, %%ymm15, %%ymm15\n\t"
        "jmp 2f\n\t"
        "1:\n\t"
        "movq %[c], %%rax\n\t"
        "vmovups   (%%rax), %%ymm4\n\t"
        "vmovups 32(%%rax), %%ymm5\n\t"
        "addq %[ldcb], %%rax\n\t"
        "vmovups   (%%rax), %%ymm6\n\t"
        "vmovups 32(%%rax), %%ymm7\n\t"
        "addq %[ldcb], %%rax\n\t"
        "vmovups   (%%rax), %%ymm8\n\t"
        "vmovups 32(%%rax), %%ymm9\n\t"
        "addq %[ldcb], %%rax\n\t"
        "vmovups   (%%rax), %%ymm10\n\t"
        "vmovups 32(%%rax), %%ymm11\n\t"
        "addq %[ldcb], %%rax\n\t"
        "vmovups   (%%rax), %%ymm12\n\t"
        "vmovups 32(%%rax), %%ymm13\n\t"
        "addq %[ldcb], %%rax\n\t"
        "vmovups   (%%rax), %%ymm14\n\t"
        "vmovups 32(%%rax), %%ymm15\n\t"

        // Main 4x K-unrolled loop
        "2:\n\t"
        "testq %[cnt], %[cnt]\n\t"
        "jz 4f\n\t"
        ".p2align 4\n\t"
        "3:\n\t"

        // k + 0
        "vmovaps    0(%[b]), %%ymm0\n\t"
        "vmovaps   32(%[b]), %%ymm1\n\t"
        "vbroadcastss  0(%[a]), %%ymm2\n\t"
        "vbroadcastss  4(%[a]), %%ymm3\n\t"
        "vfmadd231ps %%ymm0, %%ymm2, %%ymm4\n\t"
        "vfmadd231ps %%ymm1, %%ymm2, %%ymm5\n\t"
        "vbroadcastss  8(%[a]), %%ymm2\n\t"
        "vfmadd231ps %%ymm0, %%ymm3, %%ymm6\n\t"
        "vfmadd231ps %%ymm1, %%ymm3, %%ymm7\n\t"
        "vbroadcastss 12(%[a]), %%ymm3\n\t"
        "vfmadd231ps %%ymm0, %%ymm2, %%ymm8\n\t"
        "vfmadd231ps %%ymm1, %%ymm2, %%ymm9\n\t"
        "vbroadcastss 16(%[a]), %%ymm2\n\t"
        "vfmadd231ps %%ymm0, %%ymm3, %%ymm10\n\t"
        "vfmadd231ps %%ymm1, %%ymm3, %%ymm11\n\t"
        "vbroadcastss 20(%[a]), %%ymm3\n\t"
        "vfmadd231ps %%ymm0, %%ymm2, %%ymm12\n\t"
        "vfmadd231ps %%ymm1, %%ymm2, %%ymm13\n\t"
        "vfmadd231ps %%ymm0, %%ymm3, %%ymm14\n\t"
        "vfmadd231ps %%ymm1, %%ymm3, %%ymm15\n\t"

        // k + 1
        "vmovaps   64(%[b]), %%ymm0\n\t"
        "vmovaps   96(%[b]), %%ymm1\n\t"
        "vbroadcastss 24(%[a]), %%ymm2\n\t"
        "vbroadcastss 28(%[a]), %%ymm3\n\t"
        "vfmadd231ps %%ymm0, %%ymm2, %%ymm4\n\t"
        "vfmadd231ps %%ymm1, %%ymm2, %%ymm5\n\t"
        "vbroadcastss 32(%[a]), %%ymm2\n\t"
        "vfmadd231ps %%ymm0, %%ymm3, %%ymm6\n\t"
        "vfmadd231ps %%ymm1, %%ymm3, %%ymm7\n\t"
        "vbroadcastss 36(%[a]), %%ymm3\n\t"
        "vfmadd231ps %%ymm0, %%ymm2, %%ymm8\n\t"
        "vfmadd231ps %%ymm1, %%ymm2, %%ymm9\n\t"
        "vbroadcastss 40(%[a]), %%ymm2\n\t"
        "vfmadd231ps %%ymm0, %%ymm3, %%ymm10\n\t"
        "vfmadd231ps %%ymm1, %%ymm3, %%ymm11\n\t"
        "vbroadcastss 44(%[a]), %%ymm3\n\t"
        "vfmadd231ps %%ymm0, %%ymm2, %%ymm12\n\t"
        "vfmadd231ps %%ymm1, %%ymm2, %%ymm13\n\t"
        "vfmadd231ps %%ymm0, %%ymm3, %%ymm14\n\t"
        "vfmadd231ps %%ymm1, %%ymm3, %%ymm15\n\t"

        // k + 2
        "vmovaps  128(%[b]), %%ymm0\n\t"
        "vmovaps  160(%[b]), %%ymm1\n\t"
        "vbroadcastss 48(%[a]), %%ymm2\n\t"
        "vbroadcastss 52(%[a]), %%ymm3\n\t"
        "vfmadd231ps %%ymm0, %%ymm2, %%ymm4\n\t"
        "vfmadd231ps %%ymm1, %%ymm2, %%ymm5\n\t"
        "vbroadcastss 56(%[a]), %%ymm2\n\t"
        "vfmadd231ps %%ymm0, %%ymm3, %%ymm6\n\t"
        "vfmadd231ps %%ymm1, %%ymm3, %%ymm7\n\t"
        "vbroadcastss 60(%[a]), %%ymm3\n\t"
        "vfmadd231ps %%ymm0, %%ymm2, %%ymm8\n\t"
        "vfmadd231ps %%ymm1, %%ymm2, %%ymm9\n\t"
        "vbroadcastss 64(%[a]), %%ymm2\n\t"
        "vfmadd231ps %%ymm0, %%ymm3, %%ymm10\n\t"
        "vfmadd231ps %%ymm1, %%ymm3, %%ymm11\n\t"
        "vbroadcastss 68(%[a]), %%ymm3\n\t"
        "vfmadd231ps %%ymm0, %%ymm2, %%ymm12\n\t"
        "vfmadd231ps %%ymm1, %%ymm2, %%ymm13\n\t"
        "vfmadd231ps %%ymm0, %%ymm3, %%ymm14\n\t"
        "vfmadd231ps %%ymm1, %%ymm3, %%ymm15\n\t"

        // k + 3
        "vmovaps  192(%[b]), %%ymm0\n\t"
        "vmovaps  224(%[b]), %%ymm1\n\t"
        "vbroadcastss 72(%[a]), %%ymm2\n\t"
        "vbroadcastss 76(%[a]), %%ymm3\n\t"
        "vfmadd231ps %%ymm0, %%ymm2, %%ymm4\n\t"
        "vfmadd231ps %%ymm1, %%ymm2, %%ymm5\n\t"
        "vbroadcastss 80(%[a]), %%ymm2\n\t"
        "vfmadd231ps %%ymm0, %%ymm3, %%ymm6\n\t"
        "vfmadd231ps %%ymm1, %%ymm3, %%ymm7\n\t"
        "vbroadcastss 84(%[a]), %%ymm3\n\t"
        "vfmadd231ps %%ymm0, %%ymm2, %%ymm8\n\t"
        "vfmadd231ps %%ymm1, %%ymm2, %%ymm9\n\t"
        "vbroadcastss 88(%[a]), %%ymm2\n\t"
        "vfmadd231ps %%ymm0, %%ymm3, %%ymm10\n\t"
        "vfmadd231ps %%ymm1, %%ymm3, %%ymm11\n\t"
        "vbroadcastss 92(%[a]), %%ymm3\n\t"
        "vfmadd231ps %%ymm0, %%ymm2, %%ymm12\n\t"
        "vfmadd231ps %%ymm1, %%ymm2, %%ymm13\n\t"
        "vfmadd231ps %%ymm0, %%ymm3, %%ymm14\n\t"
        "vfmadd231ps %%ymm1, %%ymm3, %%ymm15\n\t"

        "addq $96,  %[a]\n\t"
        "addq $256, %[b]\n\t"
        "decq %[cnt]\n\t"
        "jnz 3b\n\t"

        // Remainder loop (0..3)
        "4:\n\t"
        "testq %[rem], %[rem]\n\t"
        "jz 6f\n\t"
        "5:\n\t"
        "vmovaps   0(%[b]), %%ymm0\n\t"
        "vmovaps  32(%[b]), %%ymm1\n\t"
        "vbroadcastss  0(%[a]), %%ymm2\n\t"
        "vbroadcastss  4(%[a]), %%ymm3\n\t"
        "vfmadd231ps %%ymm0, %%ymm2, %%ymm4\n\t"
        "vfmadd231ps %%ymm1, %%ymm2, %%ymm5\n\t"
        "vbroadcastss  8(%[a]), %%ymm2\n\t"
        "vfmadd231ps %%ymm0, %%ymm3, %%ymm6\n\t"
        "vfmadd231ps %%ymm1, %%ymm3, %%ymm7\n\t"
        "vbroadcastss 12(%[a]), %%ymm3\n\t"
        "vfmadd231ps %%ymm0, %%ymm2, %%ymm8\n\t"
        "vfmadd231ps %%ymm1, %%ymm2, %%ymm9\n\t"
        "vbroadcastss 16(%[a]), %%ymm2\n\t"
        "vfmadd231ps %%ymm0, %%ymm3, %%ymm10\n\t"
        "vfmadd231ps %%ymm1, %%ymm3, %%ymm11\n\t"
        "vbroadcastss 20(%[a]), %%ymm3\n\t"
        "vfmadd231ps %%ymm0, %%ymm2, %%ymm12\n\t"
        "vfmadd231ps %%ymm1, %%ymm2, %%ymm13\n\t"
        "vfmadd231ps %%ymm0, %%ymm3, %%ymm14\n\t"
        "vfmadd231ps %%ymm1, %%ymm3, %%ymm15\n\t"
        "addq $24, %[a]\n\t"
        "addq $64, %[b]\n\t"
        "decq %[rem]\n\t"
        "jnz 5b\n\t"

        // Store C accumulators
        "6:\n\t"
        "movq %[c], %%rax\n\t"
        "vmovups %%ymm4,    (%%rax)\n\t"
        "vmovups %%ymm5,  32(%%rax)\n\t"
        "addq %[ldcb], %%rax\n\t"
        "vmovups %%ymm6,    (%%rax)\n\t"
        "vmovups %%ymm7,  32(%%rax)\n\t"
        "addq %[ldcb], %%rax\n\t"
        "vmovups %%ymm8,    (%%rax)\n\t"
        "vmovups %%ymm9,  32(%%rax)\n\t"
        "addq %[ldcb], %%rax\n\t"
        "vmovups %%ymm10,   (%%rax)\n\t"
        "vmovups %%ymm11, 32(%%rax)\n\t"
        "addq %[ldcb], %%rax\n\t"
        "vmovups %%ymm12,   (%%rax)\n\t"
        "vmovups %%ymm13, 32(%%rax)\n\t"
        "addq %[ldcb], %%rax\n\t"
        "vmovups %%ymm14,   (%%rax)\n\t"
        "vmovups %%ymm15, 32(%%rax)\n\t"
        : [a] "+r"(a_ptr), [b] "+r"(b_ptr), [cnt] "+r"(cnt), [rem] "+r"(rem)
        : [c] "r"(c_ptr), [ldcb] "r"(ldc_bytes), [init_f] "r"(init_flag)
        : "rax", "cc", "memory",
          "ymm0", "ymm1", "ymm2", "ymm3",
          "ymm4", "ymm5", "ymm6", "ymm7",
          "ymm8", "ymm9", "ymm10", "ymm11",
          "ymm12", "ymm13", "ymm14", "ymm15");
}
#endif

// ============================================================================
// Zero-Pack Thin-M Streaming AVX2+FMA Kernel (M in 1..4)
// Eliminates 33%-83% wasted FMAs and 100% of packing overhead for GEMV / low-batch.
// ============================================================================
static XK_TARGET_AVX2_FMA void thin_m_strip_f32(
    const float* XK_RESTRICT A, const float* XK_RESTRICT B, float* XK_RESTRICT C,
    size_t M, size_t K, size_t N, size_t j_begin, size_t j_end) {
    // M is 1..4; process 32 columns per step for M=1..2, 16 columns per step for M=3..4
    if (M == 1) {
        size_t j = j_begin;
        for (; j + 31 < j_end; j += 32) {
            __m256 c0 = _mm256_setzero_ps();
            __m256 c1 = _mm256_setzero_ps();
            __m256 c2 = _mm256_setzero_ps();
            __m256 c3 = _mm256_setzero_ps();
            for (size_t k = 0; k < K; ++k) {
                const __m256 a0 = _mm256_broadcast_ss(&A[k]);
                const float* bp = &B[k * N + j];
                c0 = _mm256_fmadd_ps(a0, _mm256_loadu_ps(bp + 0),  c0);
                c1 = _mm256_fmadd_ps(a0, _mm256_loadu_ps(bp + 8),  c1);
                c2 = _mm256_fmadd_ps(a0, _mm256_loadu_ps(bp + 16), c2);
                c3 = _mm256_fmadd_ps(a0, _mm256_loadu_ps(bp + 24), c3);
            }
            _mm256_storeu_ps(&C[j + 0],  c0);
            _mm256_storeu_ps(&C[j + 8],  c1);
            _mm256_storeu_ps(&C[j + 16], c2);
            _mm256_storeu_ps(&C[j + 24], c3);
        }
        for (; j + 7 < j_end; j += 8) {
            __m256 c0 = _mm256_setzero_ps();
            for (size_t k = 0; k < K; ++k) {
                const __m256 a0 = _mm256_broadcast_ss(&A[k]);
                c0 = _mm256_fmadd_ps(a0, _mm256_loadu_ps(&B[k * N + j]), c0);
            }
            _mm256_storeu_ps(&C[j], c0);
        }
        for (; j < j_end; ++j) {
            float sum = 0.0f;
            for (size_t k = 0; k < K; ++k) sum += A[k] * B[k * N + j];
            C[j] = sum;
        }
        return;
    }

    if (M == 2) {
        const float* A0 = A;
        const float* A1 = A + K;
        float* C0 = C;
        float* C1 = C + N;
        size_t j = j_begin;
        for (; j + 31 < j_end; j += 32) {
            __m256 c00 = _mm256_setzero_ps(), c01 = _mm256_setzero_ps(),
                   c02 = _mm256_setzero_ps(), c03 = _mm256_setzero_ps();
            __m256 c10 = _mm256_setzero_ps(), c11 = _mm256_setzero_ps(),
                   c12 = _mm256_setzero_ps(), c13 = _mm256_setzero_ps();
            for (size_t k = 0; k < K; ++k) {
                const float* bp = &B[k * N + j];
                const __m256 b0 = _mm256_loadu_ps(bp + 0);
                const __m256 b1 = _mm256_loadu_ps(bp + 8);
                const __m256 b2 = _mm256_loadu_ps(bp + 16);
                const __m256 b3 = _mm256_loadu_ps(bp + 24);
                const __m256 a0 = _mm256_broadcast_ss(&A0[k]);
                const __m256 a1 = _mm256_broadcast_ss(&A1[k]);
                c00 = _mm256_fmadd_ps(a0, b0, c00); c01 = _mm256_fmadd_ps(a0, b1, c01);
                c02 = _mm256_fmadd_ps(a0, b2, c02); c03 = _mm256_fmadd_ps(a0, b3, c03);
                c10 = _mm256_fmadd_ps(a1, b0, c10); c11 = _mm256_fmadd_ps(a1, b1, c11);
                c12 = _mm256_fmadd_ps(a1, b2, c12); c13 = _mm256_fmadd_ps(a1, b3, c13);
            }
            _mm256_storeu_ps(&C0[j + 0], c00); _mm256_storeu_ps(&C0[j + 8], c01);
            _mm256_storeu_ps(&C0[j + 16], c02); _mm256_storeu_ps(&C0[j + 24], c03);
            _mm256_storeu_ps(&C1[j + 0], c10); _mm256_storeu_ps(&C1[j + 8], c11);
            _mm256_storeu_ps(&C1[j + 16], c12); _mm256_storeu_ps(&C1[j + 24], c13);
        }
        for (; j + 7 < j_end; j += 8) {
            __m256 c00 = _mm256_setzero_ps(), c10 = _mm256_setzero_ps();
            for (size_t k = 0; k < K; ++k) {
                const __m256 b0 = _mm256_loadu_ps(&B[k * N + j]);
                c00 = _mm256_fmadd_ps(_mm256_broadcast_ss(&A0[k]), b0, c00);
                c10 = _mm256_fmadd_ps(_mm256_broadcast_ss(&A1[k]), b0, c10);
            }
            _mm256_storeu_ps(&C0[j], c00);
            _mm256_storeu_ps(&C1[j], c10);
        }
        for (; j < j_end; ++j) {
            float s0 = 0.0f, s1 = 0.0f;
            for (size_t k = 0; k < K; ++k) {
                const float bv = B[k * N + j];
                s0 += A0[k] * bv;
                s1 += A1[k] * bv;
            }
            C0[j] = s0;
            C1[j] = s1;
        }
        return;
    }

    if (M == 3) {
        const float* A0 = A;
        const float* A1 = A + K;
        const float* A2 = A + 2 * K;
        float* C0 = C;
        float* C1 = C + N;
        float* C2 = C + 2 * N;
        size_t j = j_begin;
        for (; j + 23 < j_end; j += 24) {
            __m256 c00 = _mm256_setzero_ps(), c01 = _mm256_setzero_ps(), c02 = _mm256_setzero_ps();
            __m256 c10 = _mm256_setzero_ps(), c11 = _mm256_setzero_ps(), c12 = _mm256_setzero_ps();
            __m256 c20 = _mm256_setzero_ps(), c21 = _mm256_setzero_ps(), c22 = _mm256_setzero_ps();
            for (size_t k = 0; k < K; ++k) {
                const float* bp = &B[k * N + j];
                const __m256 b0 = _mm256_loadu_ps(bp + 0);
                const __m256 b1 = _mm256_loadu_ps(bp + 8);
                const __m256 b2 = _mm256_loadu_ps(bp + 16);
                const __m256 a0 = _mm256_broadcast_ss(&A0[k]);
                const __m256 a1 = _mm256_broadcast_ss(&A1[k]);
                const __m256 a2 = _mm256_broadcast_ss(&A2[k]);
                c00 = _mm256_fmadd_ps(a0, b0, c00); c01 = _mm256_fmadd_ps(a0, b1, c01); c02 = _mm256_fmadd_ps(a0, b2, c02);
                c10 = _mm256_fmadd_ps(a1, b0, c10); c11 = _mm256_fmadd_ps(a1, b1, c11); c12 = _mm256_fmadd_ps(a1, b2, c12);
                c20 = _mm256_fmadd_ps(a2, b0, c20); c21 = _mm256_fmadd_ps(a2, b1, c21); c22 = _mm256_fmadd_ps(a2, b2, c22);
            }
            _mm256_storeu_ps(&C0[j + 0], c00); _mm256_storeu_ps(&C0[j + 8], c01); _mm256_storeu_ps(&C0[j + 16], c02);
            _mm256_storeu_ps(&C1[j + 0], c10); _mm256_storeu_ps(&C1[j + 8], c11); _mm256_storeu_ps(&C1[j + 16], c12);
            _mm256_storeu_ps(&C2[j + 0], c20); _mm256_storeu_ps(&C2[j + 8], c21); _mm256_storeu_ps(&C2[j + 16], c22);
        }
        for (; j + 7 < j_end; j += 8) {
            __m256 c00 = _mm256_setzero_ps(), c10 = _mm256_setzero_ps(), c20 = _mm256_setzero_ps();
            for (size_t k = 0; k < K; ++k) {
                const __m256 b0 = _mm256_loadu_ps(&B[k * N + j]);
                c00 = _mm256_fmadd_ps(_mm256_broadcast_ss(&A0[k]), b0, c00);
                c10 = _mm256_fmadd_ps(_mm256_broadcast_ss(&A1[k]), b0, c10);
                c20 = _mm256_fmadd_ps(_mm256_broadcast_ss(&A2[k]), b0, c20);
            }
            _mm256_storeu_ps(&C0[j], c00);
            _mm256_storeu_ps(&C1[j], c10);
            _mm256_storeu_ps(&C2[j], c20);
        }
        for (; j < j_end; ++j) {
            float s0 = 0.0f, s1 = 0.0f, s2 = 0.0f;
            for (size_t k = 0; k < K; ++k) {
                const float bv = B[k * N + j];
                s0 += A0[k] * bv; s1 += A1[k] * bv; s2 += A2[k] * bv;
            }
            C0[j] = s0; C1[j] = s1; C2[j] = s2;
        }
        return;
    }

    // M == 4: 4x24 (12 accumulators + 3 B + 1 A = 16 YMM registers!)
    const float* A0 = A;
    const float* A1 = A + K;
    const float* A2 = A + 2 * K;
    const float* A3 = A + 3 * K;
    float* C0 = C;
    float* C1 = C + N;
    float* C2 = C + 2 * N;
    float* C3 = C + 3 * N;
    size_t j = j_begin;
    for (; j + 23 < j_end; j += 24) {
        __m256 c00 = _mm256_setzero_ps(), c01 = _mm256_setzero_ps(), c02 = _mm256_setzero_ps();
        __m256 c10 = _mm256_setzero_ps(), c11 = _mm256_setzero_ps(), c12 = _mm256_setzero_ps();
        __m256 c20 = _mm256_setzero_ps(), c21 = _mm256_setzero_ps(), c22 = _mm256_setzero_ps();
        __m256 c30 = _mm256_setzero_ps(), c31 = _mm256_setzero_ps(), c32 = _mm256_setzero_ps();
        for (size_t k = 0; k < K; ++k) {
            const float* bp = &B[k * N + j];
            const __m256 b0 = _mm256_loadu_ps(bp + 0);
            const __m256 b1 = _mm256_loadu_ps(bp + 8);
            const __m256 b2 = _mm256_loadu_ps(bp + 16);
            __m256 av = _mm256_broadcast_ss(&A0[k]);
            c00 = _mm256_fmadd_ps(av, b0, c00); c01 = _mm256_fmadd_ps(av, b1, c01); c02 = _mm256_fmadd_ps(av, b2, c02);
            av = _mm256_broadcast_ss(&A1[k]);
            c10 = _mm256_fmadd_ps(av, b0, c10); c11 = _mm256_fmadd_ps(av, b1, c11); c12 = _mm256_fmadd_ps(av, b2, c12);
            av = _mm256_broadcast_ss(&A2[k]);
            c20 = _mm256_fmadd_ps(av, b0, c20); c21 = _mm256_fmadd_ps(av, b1, c21); c22 = _mm256_fmadd_ps(av, b2, c22);
            av = _mm256_broadcast_ss(&A3[k]);
            c30 = _mm256_fmadd_ps(av, b0, c30); c31 = _mm256_fmadd_ps(av, b1, c31); c32 = _mm256_fmadd_ps(av, b2, c32);
        }
        _mm256_storeu_ps(&C0[j + 0], c00); _mm256_storeu_ps(&C0[j + 8], c01); _mm256_storeu_ps(&C0[j + 16], c02);
        _mm256_storeu_ps(&C1[j + 0], c10); _mm256_storeu_ps(&C1[j + 8], c11); _mm256_storeu_ps(&C1[j + 16], c12);
        _mm256_storeu_ps(&C2[j + 0], c20); _mm256_storeu_ps(&C2[j + 8], c21); _mm256_storeu_ps(&C2[j + 16], c22);
        _mm256_storeu_ps(&C3[j + 0], c30); _mm256_storeu_ps(&C3[j + 8], c31); _mm256_storeu_ps(&C3[j + 16], c32);
    }
    for (; j + 7 < j_end; j += 8) {
        __m256 c00 = _mm256_setzero_ps(), c10 = _mm256_setzero_ps(),
               c20 = _mm256_setzero_ps(), c30 = _mm256_setzero_ps();
        for (size_t k = 0; k < K; ++k) {
            const __m256 b0 = _mm256_loadu_ps(&B[k * N + j]);
            c00 = _mm256_fmadd_ps(_mm256_broadcast_ss(&A0[k]), b0, c00);
            c10 = _mm256_fmadd_ps(_mm256_broadcast_ss(&A1[k]), b0, c10);
            c20 = _mm256_fmadd_ps(_mm256_broadcast_ss(&A2[k]), b0, c20);
            c30 = _mm256_fmadd_ps(_mm256_broadcast_ss(&A3[k]), b0, c30);
        }
        _mm256_storeu_ps(&C0[j], c00); _mm256_storeu_ps(&C1[j], c10);
        _mm256_storeu_ps(&C2[j], c20); _mm256_storeu_ps(&C3[j], c30);
    }
    for (; j < j_end; ++j) {
        float s0 = 0.0f, s1 = 0.0f, s2 = 0.0f, s3 = 0.0f;
        for (size_t k = 0; k < K; ++k) {
            const float bv = B[k * N + j];
            s0 += A0[k] * bv; s1 += A1[k] * bv; s2 += A2[k] * bv; s3 += A3[k] * bv;
        }
        C0[j] = s0; C1[j] = s1; C2[j] = s2; C3[j] = s3;
    }
}

// ============================================================================
// BLIS f32 Single-Thread & OpenMP Drivers
// ============================================================================
static XK_TARGET_AVX2_FMA void dispatch_micro_6x16_f32(
    size_t kc, bool init_c,
    const float* XK_RESTRICT A_packed,
    const float* XK_RESTRICT B_packed,
    float* XK_RESTRICT C, size_t ldc,
    size_t mr, size_t nr, bool use_asm_kernel) {
    if (mr == 6 && nr == 16) {
#if XK_ENABLE_X86_64_ASM_KERNEL
        if (use_asm_kernel) {
            micro_6x16_full_asm_f32(kc, init_c, A_packed, B_packed, C, ldc);
            return;
        }
#else
        (void)use_asm_kernel;
#endif
        micro_6x16_full_f32(kc, init_c, A_packed, B_packed, C, ldc);
    } else {
        micro_6x16_edge_f32(kc, init_c, A_packed, B_packed, C, ldc, mr, nr);
    }
}

static XK_TARGET_AVX2_FMA void gemm_avx2_blis_f32_impl(
    const float* A, const float* B, float* C,
    size_t M, size_t K, size_t N,
    size_t MC, size_t KC, size_t NC,
    XK_Workspace& ws, bool use_asm_kernel) {
    const size_t mc_cap = xk_detail::checked_round_up(std::min(M, MC), 6);
    const size_t kc_cap = std::min(K, KC);
    const size_t nc_cap = xk_detail::checked_round_up(std::min(N, NC), 16);
    const size_t a_need = xk_detail::checked_element_count(mc_cap, kc_cap);
    const size_t b_need = xk_detail::checked_element_count(kc_cap, nc_cap);

    ws.prepare_f32(b_need, a_need, 1);
    float* a_packed_ptr = ws.a_packed_for_thread(0);
    float* b_packed_ptr = ws.b_packed();

    for (size_t jc = 0; jc < N; jc += NC) {
        const size_t nc = std::min(NC, N - jc);
        for (size_t pc = 0; pc < K; pc += KC) {
            const size_t kc = std::min(KC, K - pc);
            const bool init_c = (pc == 0);

            for (size_t jr = 0; jr < nc; jr += 16) {
                const size_t nr = std::min(static_cast<size_t>(16), nc - jr);
                pack_b_slab_16_f32(kc, nr, &B[pc * N + (jc + jr)], N,
                                   b_packed_ptr + jr * kc);
            }

            for (size_t ic = 0; ic < M; ic += MC) {
                const size_t mc = std::min(MC, M - ic);
                for (size_t ir = 0; ir < mc; ir += 6) {
                    const size_t mr = std::min(static_cast<size_t>(6), mc - ir);
                    pack_a_panel_6_f32(mr, kc, &A[(ic + ir) * K + pc], K,
                                       a_packed_ptr + ir * kc);
                }

                for (size_t jr = 0; jr < nc; jr += 16) {
                    const size_t nr = std::min(static_cast<size_t>(16), nc - jr);
                    const float* b_slab = b_packed_ptr + jr * kc;
                    for (size_t ir = 0; ir < mc; ir += 6) {
                        const size_t mr = std::min(static_cast<size_t>(6), mc - ir);
                        dispatch_micro_6x16_f32(
                            kc, init_c,
                            a_packed_ptr + ir * kc,
                            b_slab,
                            &C[(ic + ir) * N + (jc + jr)], N,
                            mr, nr, use_asm_kernel);
                    }
                }
            }
        }
    }
}

#if defined(_OPENMP)
static XK_TARGET_AVX2_FMA void gemm_avx2_blis_f32_omp_impl(
    const float* A, const float* B, float* C,
    size_t M, size_t K, size_t N,
    size_t MC, size_t KC, size_t NC,
    int thread_count, XK_ParallelAxis axis,
    XK_Workspace& ws, int* actual_thread_count,
    bool use_asm_kernel) {
    if (thread_count < 1) thread_count = 1;

    const size_t num_m_panels = (M / 6) + ((M % 6) != 0 ? 1 : 0);
    const size_t num_n_slabs = (N / 16) + ((N % 16) != 0 ? 1 : 0);

    if (axis == XK_ParallelAxis::Auto || axis == XK_ParallelAxis::None) {
        axis = (num_m_panels < static_cast<size_t>(thread_count) &&
                num_n_slabs > num_m_panels)
                   ? XK_ParallelAxis::AxisN
                   : XK_ParallelAxis::AxisM;
    }

    const size_t max_work_units =
        (axis == XK_ParallelAxis::AxisN) ? num_n_slabs : num_m_panels;
    if (max_work_units <= 1 || thread_count == 1) {
        if (actual_thread_count != 0) *actual_thread_count = 1;
        gemm_avx2_blis_f32_impl(A, B, C, M, K, N, MC, KC, NC, ws, use_asm_kernel);
        return;
    }
    if (static_cast<size_t>(thread_count) > max_work_units) {
        thread_count = static_cast<int>(max_work_units);
    }

    const size_t mc_cap = xk_detail::checked_round_up(std::min(M, MC), 6);
    const size_t kc_cap = std::min(K, KC);
    const size_t nc_cap = xk_detail::checked_round_up(std::min(N, NC), 16);
    const size_t a_need_per_thread = xk_detail::checked_element_count(mc_cap, kc_cap);
    const size_t b_need = xk_detail::checked_element_count(kc_cap, nc_cap);

    ws.prepare_f32(b_need, a_need_per_thread, thread_count);
    float* b_packed_ptr = ws.b_packed();

    if (axis == XK_ParallelAxis::AxisN) {
        #pragma omp parallel num_threads(thread_count)
        {
            const int tid = omp_get_thread_num();
            const int nth = omp_get_num_threads();
            #pragma omp single nowait
            {
                if (actual_thread_count != 0) *actual_thread_count = nth;
            }
            float* a_packed_ptr = ws.a_packed_for_thread(tid);

            for (size_t jc = 0; jc < N; jc += NC) {
                const size_t nc = std::min(NC, N - jc);
                const size_t num_jr = (nc / 16) + ((nc % 16) != 0 ? 1 : 0);
                const size_t j_start =
                    (num_jr * static_cast<size_t>(tid)) / static_cast<size_t>(nth);
                const size_t j_end =
                    (num_jr * static_cast<size_t>(tid + 1)) / static_cast<size_t>(nth);

                if (j_start < j_end) {
                    for (size_t pc = 0; pc < K; pc += KC) {
                        const size_t kc = std::min(KC, K - pc);
                        const bool init_c = (pc == 0);

                        for (size_t jr_idx = j_start; jr_idx < j_end; ++jr_idx) {
                            const size_t jr = jr_idx * 16;
                            const size_t nr = std::min(static_cast<size_t>(16), nc - jr);
                            pack_b_slab_16_f32(kc, nr, &B[pc * N + (jc + jr)], N,
                                               b_packed_ptr + jr * kc_cap);
                        }

                        for (size_t ic = 0; ic < M; ic += MC) {
                            const size_t mc = std::min(MC, M - ic);
                            for (size_t ir = 0; ir < mc; ir += 6) {
                                const size_t mr = std::min(static_cast<size_t>(6), mc - ir);
                                pack_a_panel_6_f32(mr, kc, &A[(ic + ir) * K + pc], K,
                                                   a_packed_ptr + ir * kc);
                            }

                            for (size_t jr_idx = j_start; jr_idx < j_end; ++jr_idx) {
                                const size_t jr = jr_idx * 16;
                                const size_t nr = std::min(static_cast<size_t>(16), nc - jr);
                                const float* b_slab = b_packed_ptr + jr * kc_cap;
                                for (size_t ir = 0; ir < mc; ir += 6) {
                                    const size_t mr = std::min(static_cast<size_t>(6), mc - ir);
                                    dispatch_micro_6x16_f32(
                                        kc, init_c,
                                        a_packed_ptr + ir * kc,
                                        b_slab,
                                        &C[(ic + ir) * N + (jc + jr)], N,
                                        mr, nr, use_asm_kernel);
                                }
                            }
                        }
                    }
                }
                if (jc + NC < N) {
                    #pragma omp barrier
                }
            }
        }
        return;
    }

    // M-axis parallelism: collaborative B packing + disjoint M-panel compute
    #pragma omp parallel num_threads(thread_count)
    {
        const int tid = omp_get_thread_num();
        const int nth = omp_get_num_threads();
        #pragma omp single nowait
        {
            if (actual_thread_count != 0) *actual_thread_count = nth;
        }

        const size_t p_start =
            (num_m_panels * static_cast<size_t>(tid)) / static_cast<size_t>(nth);
        const size_t p_end =
            (num_m_panels * static_cast<size_t>(tid + 1)) / static_cast<size_t>(nth);
        const size_t m_start = p_start * 6;
        const size_t m_end = std::min(M, p_end * 6);
        float* a_packed_ptr = ws.a_packed_for_thread(tid);

        for (size_t jc = 0; jc < N; jc += NC) {
            const size_t nc = std::min(NC, N - jc);
            const size_t num_jr = (nc / 16) + ((nc % 16) != 0 ? 1 : 0);

            for (size_t pc = 0; pc < K; pc += KC) {
                const size_t kc = std::min(KC, K - pc);
                const bool init_c = (pc == 0);

                const std::ptrdiff_t num_jr_s = static_cast<std::ptrdiff_t>(num_jr);
                #pragma omp for schedule(static)
                for (std::ptrdiff_t jr_idx_s = 0; jr_idx_s < num_jr_s; ++jr_idx_s) {
                    const size_t jr_idx = static_cast<size_t>(jr_idx_s);
                    const size_t jr = jr_idx * 16;
                    const size_t nr = std::min(static_cast<size_t>(16), nc - jr);
                    pack_b_slab_16_f32(kc, nr, &B[pc * N + (jc + jr)], N,
                                       b_packed_ptr + jr * kc);
                }

                for (size_t ic = m_start; ic < m_end; ic += MC) {
                    const size_t mc = std::min(MC, m_end - ic);
                    for (size_t ir = 0; ir < mc; ir += 6) {
                        const size_t mr = std::min(static_cast<size_t>(6), mc - ir);
                        pack_a_panel_6_f32(mr, kc, &A[(ic + ir) * K + pc], K,
                                           a_packed_ptr + ir * kc);
                    }

                    for (size_t jr = 0; jr < nc; jr += 16) {
                        const size_t nr = std::min(static_cast<size_t>(16), nc - jr);
                        const float* b_slab = b_packed_ptr + jr * kc;
                        for (size_t ir = 0; ir < mc; ir += 6) {
                            const size_t mr = std::min(static_cast<size_t>(6), mc - ir);
                            dispatch_micro_6x16_f32(
                                kc, init_c,
                                a_packed_ptr + ir * kc,
                                b_slab,
                                &C[(ic + ir) * N + (jc + jr)], N,
                                mr, nr, use_asm_kernel);
                        }
                    }
                }

                // Skip redundant trailing barrier on the final (jc, pc) tile
                if (pc + KC < K || jc + NC < N) {
                    #pragma omp barrier
                }
            }
        }
    }
}
#endif

// ============================================================================
// Upgraded f64 BLIS 6x8 Kernels (4x K-unrolled, dual-broadcast, workspace + OMP)
// ============================================================================
static XK_TARGET_AVX2_FMA void pack_a_panel_6_f64(
    size_t mr, size_t kc, const double* XK_RESTRICT A, size_t lda,
    double* XK_RESTRICT A_packed) {
    if (mr == 6) {
        const double* a0 = A;
        const double* a1 = A + lda;
        const double* a2 = A + 2 * lda;
        const double* a3 = A + 3 * lda;
        const double* a4 = A + 4 * lda;
        const double* a5 = A + 5 * lda;
        for (size_t k = 0; k < kc; ++k) {
            A_packed[0] = a0[k];
            A_packed[1] = a1[k];
            A_packed[2] = a2[k];
            A_packed[3] = a3[k];
            A_packed[4] = a4[k];
            A_packed[5] = a5[k];
            A_packed += 6;
        }
    } else {
        for (size_t k = 0; k < kc; ++k) {
            for (size_t r = 0; r < mr; ++r) A_packed[r] = A[r * lda + k];
            for (size_t r = mr; r < 6; ++r) A_packed[r] = 0.0;
            A_packed += 6;
        }
    }
}

static XK_TARGET_AVX2_FMA void pack_b_slab_8_f64(
    size_t kc, size_t nr, const double* XK_RESTRICT B, size_t ldb,
    double* XK_RESTRICT B_packed) {
    if (nr == 8) {
        for (size_t k = 0; k < kc; ++k) {
            const double* bp = &B[k * ldb];
            _mm256_store_pd(B_packed + 0, _mm256_loadu_pd(bp));
            _mm256_store_pd(B_packed + 4, _mm256_loadu_pd(bp + 4));
            B_packed += 8;
        }
    } else {
        for (size_t k = 0; k < kc; ++k) {
            for (size_t c = 0; c < nr; ++c) B_packed[c] = B[k * ldb + c];
            for (size_t c = nr; c < 8; ++c) B_packed[c] = 0.0;
            B_packed += 8;
        }
    }
}

#define XK_FMA_STEP_6x8_F64(ap, bp)                       \
    do {                                                  \
        const __m256d b0 = _mm256_load_pd((bp) + 0);      \
        const __m256d b1 = _mm256_load_pd((bp) + 4);      \
        __m256d a0 = _mm256_broadcast_sd((ap) + 0);       \
        c00 = _mm256_fmadd_pd(a0, b0, c00);               \
        c01 = _mm256_fmadd_pd(a0, b1, c01);               \
        a0 = _mm256_broadcast_sd((ap) + 1);               \
        c10 = _mm256_fmadd_pd(a0, b0, c10);               \
        c11 = _mm256_fmadd_pd(a0, b1, c11);               \
        a0 = _mm256_broadcast_sd((ap) + 2);               \
        c20 = _mm256_fmadd_pd(a0, b0, c20);               \
        c21 = _mm256_fmadd_pd(a0, b1, c21);               \
        a0 = _mm256_broadcast_sd((ap) + 3);               \
        c30 = _mm256_fmadd_pd(a0, b0, c30);               \
        c31 = _mm256_fmadd_pd(a0, b1, c31);               \
        a0 = _mm256_broadcast_sd((ap) + 4);               \
        c40 = _mm256_fmadd_pd(a0, b0, c40);               \
        c41 = _mm256_fmadd_pd(a0, b1, c41);               \
        a0 = _mm256_broadcast_sd((ap) + 5);               \
        c50 = _mm256_fmadd_pd(a0, b0, c50);               \
        c51 = _mm256_fmadd_pd(a0, b1, c51);               \
    } while (0)

static XK_TARGET_AVX2_FMA void micro_6x8_full_f64(
    size_t kc, bool init_c,
    const double* XK_RESTRICT A_packed,
    const double* XK_RESTRICT B_packed,
    double* XK_RESTRICT C, size_t ldc) {
    __m256d c00, c01, c10, c11, c20, c21, c30, c31, c40, c41, c50, c51;

    if (init_c) {
        c00 = _mm256_setzero_pd(); c01 = _mm256_setzero_pd();
        c10 = _mm256_setzero_pd(); c11 = _mm256_setzero_pd();
        c20 = _mm256_setzero_pd(); c21 = _mm256_setzero_pd();
        c30 = _mm256_setzero_pd(); c31 = _mm256_setzero_pd();
        c40 = _mm256_setzero_pd(); c41 = _mm256_setzero_pd();
        c50 = _mm256_setzero_pd(); c51 = _mm256_setzero_pd();
    } else {
        c00 = _mm256_loadu_pd(&C[0 * ldc]); c01 = _mm256_loadu_pd(&C[0 * ldc + 4]);
        c10 = _mm256_loadu_pd(&C[1 * ldc]); c11 = _mm256_loadu_pd(&C[1 * ldc + 4]);
        c20 = _mm256_loadu_pd(&C[2 * ldc]); c21 = _mm256_loadu_pd(&C[2 * ldc + 4]);
        c30 = _mm256_loadu_pd(&C[3 * ldc]); c31 = _mm256_loadu_pd(&C[3 * ldc + 4]);
        c40 = _mm256_loadu_pd(&C[4 * ldc]); c41 = _mm256_loadu_pd(&C[4 * ldc + 4]);
        c50 = _mm256_loadu_pd(&C[5 * ldc]); c51 = _mm256_loadu_pd(&C[5 * ldc + 4]);
    }

    size_t k = 0;
    for (; k + 3 < kc; k += 4) {
        XK_FMA_STEP_6x8_F64(A_packed + 0,  B_packed + 0);
        XK_FMA_STEP_6x8_F64(A_packed + 6,  B_packed + 8);
        XK_FMA_STEP_6x8_F64(A_packed + 12, B_packed + 16);
        XK_FMA_STEP_6x8_F64(A_packed + 18, B_packed + 24);
        A_packed += 24;
        B_packed += 32;
    }
    for (; k < kc; ++k) {
        XK_FMA_STEP_6x8_F64(A_packed, B_packed);
        A_packed += 6;
        B_packed += 8;
    }

    _mm256_storeu_pd(&C[0 * ldc], c00); _mm256_storeu_pd(&C[0 * ldc + 4], c01);
    _mm256_storeu_pd(&C[1 * ldc], c10); _mm256_storeu_pd(&C[1 * ldc + 4], c11);
    _mm256_storeu_pd(&C[2 * ldc], c20); _mm256_storeu_pd(&C[2 * ldc + 4], c21);
    _mm256_storeu_pd(&C[3 * ldc], c30); _mm256_storeu_pd(&C[3 * ldc + 4], c31);
    _mm256_storeu_pd(&C[4 * ldc], c40); _mm256_storeu_pd(&C[4 * ldc + 4], c41);
    _mm256_storeu_pd(&C[5 * ldc], c50); _mm256_storeu_pd(&C[5 * ldc + 4], c51);
}

static XK_TARGET_AVX2_FMA void micro_6x8_edge_f64(
    size_t kc, bool init_c,
    const double* XK_RESTRICT A_packed,
    const double* XK_RESTRICT B_packed,
    double* XK_RESTRICT C, size_t ldc,
    size_t mr, size_t nr) {
    __m256d c00, c01, c10, c11, c20, c21, c30, c31, c40, c41, c50, c51;

    if (init_c) {
        c00 = _mm256_setzero_pd(); c01 = _mm256_setzero_pd();
        c10 = _mm256_setzero_pd(); c11 = _mm256_setzero_pd();
        c20 = _mm256_setzero_pd(); c21 = _mm256_setzero_pd();
        c30 = _mm256_setzero_pd(); c31 = _mm256_setzero_pd();
        c40 = _mm256_setzero_pd(); c41 = _mm256_setzero_pd();
        c50 = _mm256_setzero_pd(); c51 = _mm256_setzero_pd();
    } else {
        alignas(32) double tmp_c[6 * 8] = {};
        for (size_t r = 0; r < mr; ++r) {
            for (size_t c = 0; c < nr; ++c) tmp_c[r * 8 + c] = C[r * ldc + c];
        }
        c00 = _mm256_load_pd(&tmp_c[0 * 8]); c01 = _mm256_load_pd(&tmp_c[0 * 8 + 4]);
        c10 = _mm256_load_pd(&tmp_c[1 * 8]); c11 = _mm256_load_pd(&tmp_c[1 * 8 + 4]);
        c20 = _mm256_load_pd(&tmp_c[2 * 8]); c21 = _mm256_load_pd(&tmp_c[2 * 8 + 4]);
        c30 = _mm256_load_pd(&tmp_c[3 * 8]); c31 = _mm256_load_pd(&tmp_c[3 * 8 + 4]);
        c40 = _mm256_load_pd(&tmp_c[4 * 8]); c41 = _mm256_load_pd(&tmp_c[4 * 8 + 4]);
        c50 = _mm256_load_pd(&tmp_c[5 * 8]); c51 = _mm256_load_pd(&tmp_c[5 * 8 + 4]);
    }

    size_t k = 0;
    for (; k + 3 < kc; k += 4) {
        XK_FMA_STEP_6x8_F64(A_packed + 0,  B_packed + 0);
        XK_FMA_STEP_6x8_F64(A_packed + 6,  B_packed + 8);
        XK_FMA_STEP_6x8_F64(A_packed + 12, B_packed + 16);
        XK_FMA_STEP_6x8_F64(A_packed + 18, B_packed + 24);
        A_packed += 24;
        B_packed += 32;
    }
    for (; k < kc; ++k) {
        XK_FMA_STEP_6x8_F64(A_packed, B_packed);
        A_packed += 6;
        B_packed += 8;
    }

    alignas(32) double tmp_c[6 * 8];
    _mm256_store_pd(&tmp_c[0 * 8], c00); _mm256_store_pd(&tmp_c[0 * 8 + 4], c01);
    _mm256_store_pd(&tmp_c[1 * 8], c10); _mm256_store_pd(&tmp_c[1 * 8 + 4], c11);
    _mm256_store_pd(&tmp_c[2 * 8], c20); _mm256_store_pd(&tmp_c[2 * 8 + 4], c21);
    _mm256_store_pd(&tmp_c[3 * 8], c30); _mm256_store_pd(&tmp_c[3 * 8 + 4], c31);
    _mm256_store_pd(&tmp_c[4 * 8], c40); _mm256_store_pd(&tmp_c[4 * 8 + 4], c41);
    _mm256_store_pd(&tmp_c[5 * 8], c50); _mm256_store_pd(&tmp_c[5 * 8 + 4], c51);
    for (size_t r = 0; r < mr; ++r) {
        for (size_t c = 0; c < nr; ++c) C[r * ldc + c] = tmp_c[r * 8 + c];
    }
}

#undef XK_FMA_STEP_6x8_F64

static XK_TARGET_AVX2_FMA void gemm_avx2_blis_f64_impl(
    const double* A, const double* B, double* C,
    size_t M, size_t K, size_t N,
    size_t MC, size_t KC, size_t NC,
    XK_Workspace& ws) {
    const size_t mc_cap = xk_detail::checked_round_up(std::min(M, MC), 6);
    const size_t kc_cap = std::min(K, KC);
    const size_t nc_cap = xk_detail::checked_round_up(std::min(N, NC), 8);
    const size_t a_need = xk_detail::checked_element_count(mc_cap, kc_cap);
    const size_t b_need = xk_detail::checked_element_count(kc_cap, nc_cap);

    ws.prepare_f64(b_need, a_need, 1);
    double* a_packed_ptr = ws.a_packed_f64_for_thread(0);
    double* b_packed_ptr = ws.b_packed_f64();

    for (size_t jc = 0; jc < N; jc += NC) {
        const size_t nc = std::min(NC, N - jc);
        for (size_t pc = 0; pc < K; pc += KC) {
            const size_t kc = std::min(KC, K - pc);
            const bool init_c = (pc == 0);

            for (size_t jr = 0; jr < nc; jr += 8) {
                const size_t nr = std::min(static_cast<size_t>(8), nc - jr);
                pack_b_slab_8_f64(kc, nr, &B[pc * N + (jc + jr)], N,
                                  b_packed_ptr + jr * kc);
            }

            for (size_t ic = 0; ic < M; ic += MC) {
                const size_t mc = std::min(MC, M - ic);
                for (size_t ir = 0; ir < mc; ir += 6) {
                    const size_t mr = std::min(static_cast<size_t>(6), mc - ir);
                    pack_a_panel_6_f64(mr, kc, &A[(ic + ir) * K + pc], K,
                                       a_packed_ptr + ir * kc);
                }

                for (size_t jr = 0; jr < nc; jr += 8) {
                    const size_t nr = std::min(static_cast<size_t>(8), nc - jr);
                    const double* b_slab = b_packed_ptr + jr * kc;
                    for (size_t ir = 0; ir < mc; ir += 6) {
                        const size_t mr = std::min(static_cast<size_t>(6), mc - ir);
                        if (mr == 6 && nr == 8) {
                            micro_6x8_full_f64(kc, init_c, a_packed_ptr + ir * kc,
                                               b_slab, &C[(ic + ir) * N + (jc + jr)], N);
                        } else {
                            micro_6x8_edge_f64(kc, init_c, a_packed_ptr + ir * kc,
                                               b_slab, &C[(ic + ir) * N + (jc + jr)], N,
                                               mr, nr);
                        }
                    }
                }
            }
        }
    }
}

#if defined(_OPENMP)
static XK_TARGET_AVX2_FMA void gemm_avx2_blis_f64_omp_impl(
    const double* A, const double* B, double* C,
    size_t M, size_t K, size_t N,
    size_t MC, size_t KC, size_t NC,
    int thread_count, XK_ParallelAxis axis,
    XK_Workspace& ws, int* actual_thread_count) {
    if (thread_count < 1) thread_count = 1;
    const size_t num_m_panels = (M / 6) + ((M % 6) != 0 ? 1 : 0);
    const size_t num_n_slabs = (N / 8) + ((N % 8) != 0 ? 1 : 0);

    if (axis == XK_ParallelAxis::Auto || axis == XK_ParallelAxis::None) {
        axis = (num_m_panels < static_cast<size_t>(thread_count) &&
                num_n_slabs > num_m_panels)
                   ? XK_ParallelAxis::AxisN
                   : XK_ParallelAxis::AxisM;
    }

    const size_t max_work_units =
        (axis == XK_ParallelAxis::AxisN) ? num_n_slabs : num_m_panels;
    if (max_work_units <= 1 || thread_count == 1) {
        if (actual_thread_count != 0) *actual_thread_count = 1;
        gemm_avx2_blis_f64_impl(A, B, C, M, K, N, MC, KC, NC, ws);
        return;
    }
    if (static_cast<size_t>(thread_count) > max_work_units) {
        thread_count = static_cast<int>(max_work_units);
    }

    const size_t mc_cap = xk_detail::checked_round_up(std::min(M, MC), 6);
    const size_t kc_cap = std::min(K, KC);
    const size_t nc_cap = xk_detail::checked_round_up(std::min(N, NC), 8);
    const size_t a_need_per_thread = xk_detail::checked_element_count(mc_cap, kc_cap);
    const size_t b_need = xk_detail::checked_element_count(kc_cap, nc_cap);

    ws.prepare_f64(b_need, a_need_per_thread, thread_count);
    double* b_packed_ptr = ws.b_packed_f64();

    if (axis == XK_ParallelAxis::AxisN) {
        #pragma omp parallel num_threads(thread_count)
        {
            const int tid = omp_get_thread_num();
            const int nth = omp_get_num_threads();
            #pragma omp single nowait
            {
                if (actual_thread_count != 0) *actual_thread_count = nth;
            }
            double* a_packed_ptr = ws.a_packed_f64_for_thread(tid);

            for (size_t jc = 0; jc < N; jc += NC) {
                const size_t nc = std::min(NC, N - jc);
                const size_t num_jr = (nc / 8) + ((nc % 8) != 0 ? 1 : 0);
                const size_t j_start =
                    (num_jr * static_cast<size_t>(tid)) / static_cast<size_t>(nth);
                const size_t j_end =
                    (num_jr * static_cast<size_t>(tid + 1)) / static_cast<size_t>(nth);

                if (j_start < j_end) {
                    for (size_t pc = 0; pc < K; pc += KC) {
                        const size_t kc = std::min(KC, K - pc);
                        const bool init_c = (pc == 0);
                        for (size_t jr_idx = j_start; jr_idx < j_end; ++jr_idx) {
                            const size_t jr = jr_idx * 8;
                            const size_t nr = std::min(static_cast<size_t>(8), nc - jr);
                            pack_b_slab_8_f64(kc, nr, &B[pc * N + (jc + jr)], N,
                                              b_packed_ptr + jr * kc_cap);
                        }
                        for (size_t ic = 0; ic < M; ic += MC) {
                            const size_t mc = std::min(MC, M - ic);
                            for (size_t ir = 0; ir < mc; ir += 6) {
                                const size_t mr = std::min(static_cast<size_t>(6), mc - ir);
                                pack_a_panel_6_f64(mr, kc, &A[(ic + ir) * K + pc], K,
                                                   a_packed_ptr + ir * kc);
                            }
                            for (size_t jr_idx = j_start; jr_idx < j_end; ++jr_idx) {
                                const size_t jr = jr_idx * 8;
                                const size_t nr = std::min(static_cast<size_t>(8), nc - jr);
                                const double* b_slab = b_packed_ptr + jr * kc_cap;
                                for (size_t ir = 0; ir < mc; ir += 6) {
                                    const size_t mr = std::min(static_cast<size_t>(6), mc - ir);
                                    if (mr == 6 && nr == 8) {
                                        micro_6x8_full_f64(kc, init_c, a_packed_ptr + ir * kc,
                                                           b_slab, &C[(ic + ir) * N + (jc + jr)], N);
                                    } else {
                                        micro_6x8_edge_f64(kc, init_c, a_packed_ptr + ir * kc,
                                                           b_slab, &C[(ic + ir) * N + (jc + jr)], N,
                                                           mr, nr);
                                    }
                                }
                            }
                        }
                    }
                }
                if (jc + NC < N) {
                    #pragma omp barrier
                }
            }
        }
        return;
    }

    #pragma omp parallel num_threads(thread_count)
    {
        const int tid = omp_get_thread_num();
        const int nth = omp_get_num_threads();
        #pragma omp single nowait
        {
            if (actual_thread_count != 0) *actual_thread_count = nth;
        }
        const size_t p_start =
            (num_m_panels * static_cast<size_t>(tid)) / static_cast<size_t>(nth);
        const size_t p_end =
            (num_m_panels * static_cast<size_t>(tid + 1)) / static_cast<size_t>(nth);
        const size_t m_start = p_start * 6;
        const size_t m_end = std::min(M, p_end * 6);
        double* a_packed_ptr = ws.a_packed_f64_for_thread(tid);

        for (size_t jc = 0; jc < N; jc += NC) {
            const size_t nc = std::min(NC, N - jc);
            const size_t num_jr = (nc / 8) + ((nc % 8) != 0 ? 1 : 0);

            for (size_t pc = 0; pc < K; pc += KC) {
                const size_t kc = std::min(KC, K - pc);
                const bool init_c = (pc == 0);

                const std::ptrdiff_t num_jr_s = static_cast<std::ptrdiff_t>(num_jr);
                #pragma omp for schedule(static)
                for (std::ptrdiff_t jr_idx_s = 0; jr_idx_s < num_jr_s; ++jr_idx_s) {
                    const size_t jr_idx = static_cast<size_t>(jr_idx_s);
                    const size_t jr = jr_idx * 8;
                    const size_t nr = std::min(static_cast<size_t>(8), nc - jr);
                    pack_b_slab_8_f64(kc, nr, &B[pc * N + (jc + jr)], N,
                                      b_packed_ptr + jr * kc);
                }

                for (size_t ic = m_start; ic < m_end; ic += MC) {
                    const size_t mc = std::min(MC, m_end - ic);
                    for (size_t ir = 0; ir < mc; ir += 6) {
                        const size_t mr = std::min(static_cast<size_t>(6), mc - ir);
                        pack_a_panel_6_f64(mr, kc, &A[(ic + ir) * K + pc], K,
                                           a_packed_ptr + ir * kc);
                    }
                    for (size_t jr = 0; jr < nc; jr += 8) {
                        const size_t nr = std::min(static_cast<size_t>(8), nc - jr);
                        const double* b_slab = b_packed_ptr + jr * kc;
                        for (size_t ir = 0; ir < mc; ir += 6) {
                            const size_t mr = std::min(static_cast<size_t>(6), mc - ir);
                            if (mr == 6 && nr == 8) {
                                micro_6x8_full_f64(kc, init_c, a_packed_ptr + ir * kc,
                                                   b_slab, &C[(ic + ir) * N + (jc + jr)], N);
                            } else {
                                micro_6x8_edge_f64(kc, init_c, a_packed_ptr + ir * kc,
                                                   b_slab, &C[(ic + ir) * N + (jc + jr)], N,
                                                   mr, nr);
                            }
                        }
                    }
                }

                if (pc + KC < K || jc + NC < N) {
                    #pragma omp barrier
                }
            }
        }
    }
}
#endif

#endif  // XK_ENABLE_X86_AVX2_DISPATCH

// ============================================================================
// Internal Dispatch Wrappers
// ============================================================================
void vector_add_avx2_f32_dispatch(const float* a, const float* b, float* c, size_t n) {
#if XK_ENABLE_X86_AVX2_DISPATCH
    if (has_avx2_only()) {
        vector_add_avx2_f32_impl(a, b, c, n);
        return;
    }
#endif
    for (size_t i = 0; i < n; ++i) c[i] = a[i] + b[i];
}

void vector_add_avx2_f64_dispatch(const double* a, const double* b, double* c, size_t n) {
#if XK_ENABLE_X86_AVX2_DISPATCH
    if (has_avx2_only()) {
        vector_add_avx2_f64_impl(a, b, c, n);
        return;
    }
#endif
    for (size_t i = 0; i < n; ++i) c[i] = a[i] + b[i];
}

void gemm_avx2_blis_f32(const float* A, const float* B, float* C,
                        size_t M, size_t K, size_t N,
                        size_t MC, size_t KC, size_t NC,
                        XK_Workspace& ws, bool use_asm_kernel) {
#if XK_ENABLE_X86_AVX2_DISPATCH
    if (XK_has_avx2_fma()) {
        gemm_avx2_blis_f32_impl(A, B, C, M, K, N, MC, KC, NC, ws, use_asm_kernel);
        return;
    }
#else
    (void)MC; (void)KC; (void)NC; (void)ws; (void)use_asm_kernel;
#endif
    scalar_gemm_f32(A, B, C, M, K, N, 1, nullptr);
}

void gemm_avx2_blis_f32_omp(const float* A, const float* B, float* C,
                            size_t M, size_t K, size_t N,
                            size_t MC, size_t KC, size_t NC,
                            int thread_count, XK_ParallelAxis axis,
                            XK_Workspace& ws, int* actual_thread_count,
                            bool use_asm_kernel) {
#if XK_ENABLE_X86_AVX2_DISPATCH && defined(_OPENMP)
    if (XK_has_avx2_fma()) {
        gemm_avx2_blis_f32_omp_impl(A, B, C, M, K, N, MC, KC, NC,
                                    thread_count, axis, ws, actual_thread_count,
                                    use_asm_kernel);
        return;
    }
#elif XK_ENABLE_X86_AVX2_DISPATCH
    (void)axis;
    if (XK_has_avx2_fma()) {
        if (actual_thread_count != nullptr) *actual_thread_count = 1;
        gemm_avx2_blis_f32_impl(A, B, C, M, K, N, MC, KC, NC, ws, use_asm_kernel);
        return;
    }
#else
    (void)MC; (void)KC; (void)NC; (void)axis; (void)ws; (void)use_asm_kernel;
#endif
    scalar_gemm_f32(A, B, C, M, K, N, thread_count, actual_thread_count);
}

void gemm_avx2_thin_m_f32(const float* A, const float* B, float* C,
                          size_t M, size_t K, size_t N,
                          int thread_count, int* actual_thread_count) {
    if (actual_thread_count != nullptr) *actual_thread_count = 1;
#if XK_ENABLE_X86_AVX2_DISPATCH
    if (XK_has_avx2_fma() && M >= 1 && M <= 4) {
#if defined(_OPENMP)
        const size_t strip_unit = (M <= 2) ? 32 : 24;
        const size_t num_strips = (N / strip_unit) + ((N % strip_unit) != 0 ? 1 : 0);
        if (thread_count > 1 && num_strips > 1) {
            const int tc = static_cast<int>(
                std::min(static_cast<size_t>(thread_count), num_strips));
            #pragma omp parallel num_threads(tc)
            {
                const int tid = omp_get_thread_num();
                const int nth = omp_get_num_threads();
                #pragma omp single nowait
                {
                    if (actual_thread_count != nullptr) *actual_thread_count = nth;
                }
                const size_t s_begin =
                    (num_strips * static_cast<size_t>(tid)) / static_cast<size_t>(nth);
                const size_t s_end =
                    (num_strips * static_cast<size_t>(tid + 1)) / static_cast<size_t>(nth);
                const size_t j_begin = s_begin * strip_unit;
                const size_t j_end = std::min(N, s_end * strip_unit);
                if (j_begin < j_end) {
                    thin_m_strip_f32(A, B, C, M, K, N, j_begin, j_end);
                }
            }
            return;
        }
#else
        (void)thread_count;
#endif
        thin_m_strip_f32(A, B, C, M, K, N, 0, N);
        return;
    }
#else
    (void)thread_count;
#endif
    scalar_gemm_f32(A, B, C, M, K, N, thread_count, actual_thread_count);
}

void gemm_avx2_blis_f64(const double* A, const double* B, double* C,
                        size_t M, size_t K, size_t N,
                        size_t MC, size_t KC, size_t NC,
                        XK_Workspace& ws) {
#if XK_ENABLE_X86_AVX2_DISPATCH
    if (XK_has_avx2_fma()) {
        gemm_avx2_blis_f64_impl(A, B, C, M, K, N, MC, KC, NC, ws);
        return;
    }
#else
    (void)MC; (void)KC; (void)NC; (void)ws;
#endif
    scalar_gemm_f64(A, B, C, M, K, N, 1, nullptr);
}

void gemm_avx2_blis_f64_omp(const double* A, const double* B, double* C,
                            size_t M, size_t K, size_t N,
                            size_t MC, size_t KC, size_t NC,
                            int thread_count, XK_ParallelAxis axis,
                            XK_Workspace& ws, int* actual_thread_count) {
#if XK_ENABLE_X86_AVX2_DISPATCH && defined(_OPENMP)
    if (XK_has_avx2_fma()) {
        gemm_avx2_blis_f64_omp_impl(A, B, C, M, K, N, MC, KC, NC,
                                    thread_count, axis, ws, actual_thread_count);
        return;
    }
#elif XK_ENABLE_X86_AVX2_DISPATCH
    (void)axis;
    if (XK_has_avx2_fma()) {
        if (actual_thread_count != nullptr) *actual_thread_count = 1;
        gemm_avx2_blis_f64_impl(A, B, C, M, K, N, MC, KC, NC, ws);
        return;
    }
#else
    (void)MC; (void)KC; (void)NC; (void)axis; (void)ws;
#endif
    scalar_gemm_f64(A, B, C, M, K, N, thread_count, actual_thread_count);
}

}  // namespace xk_internal