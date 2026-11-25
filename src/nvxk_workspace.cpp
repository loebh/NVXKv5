#include <nvxk.h>
#include <cstdlib>
#include <limits>
#include <new>
#include <stdexcept>
#if defined(_WIN32)
#include <malloc.h>
#endif

namespace {

void* xk_aligned_alloc(size_t bytes) {
    if (bytes == 0) return nullptr;
#if defined(_WIN32)
    void* ptr = _aligned_malloc(bytes, 64);
    if (!ptr) throw std::bad_alloc();
    return ptr;
#else
    void* ptr = nullptr;
    if (posix_memalign(&ptr, 64, bytes) != 0 || !ptr) {
        throw std::bad_alloc();
    }
    return ptr;
#endif
}

void xk_aligned_free(void* ptr) noexcept {
    if (!ptr) return;
#if defined(_WIN32)
    _aligned_free(ptr);
#else
    free(ptr);
#endif
}

}  // namespace

XK_Workspace::XK_Workspace(size_t memory_budget_bytes)
    : budget_bytes_(memory_budget_bytes),
      b_ptr_(nullptr),
      b_bytes_(0),
      a_ptr_(nullptr),
      a_bytes_(0),
      a_slot_stride_(0) {}

XK_Workspace::~XK_Workspace() {
    release();
}

void XK_Workspace::set_memory_budget(size_t bytes) {
    budget_bytes_ = bytes;
    if (budget_bytes_ > 0 && allocated_bytes() > budget_bytes_) {
        release();
    }
}

void XK_Workspace::prepare_raw(size_t elem_size, size_t b_elems,
                               size_t a_elems_per_thread, int num_threads) {
    if (num_threads < 1) num_threads = 1;

    // Pad each thread's A_packed slot to a 64-byte cache-line boundary to prevent false sharing.
    const size_t align_elems = (elem_size >= 64) ? 1 : (64 / elem_size);
    const size_t slot_stride = xk_detail::checked_round_up(a_elems_per_thread, align_elems);
    const size_t total_a_elems =
        xk_detail::checked_element_count(slot_stride, static_cast<size_t>(num_threads));

    const size_t need_b_bytes =
        xk_detail::checked_element_count(b_elems, elem_size);
    const size_t need_a_bytes =
        xk_detail::checked_element_count(total_a_elems, elem_size);

    if (need_b_bytes > std::numeric_limits<size_t>::max() - need_a_bytes) {
        throw std::length_error("XK_Workspace: requested size overflows size_t");
    }
    const size_t total_need = need_b_bytes + need_a_bytes;
    if (budget_bytes_ > 0 && total_need > budget_bytes_) {
        throw std::length_error("XK_Workspace: required scratchpad exceeds memory budget");
    }

    const size_t retained_b = (need_b_bytes > b_bytes_) ? need_b_bytes : b_bytes_;
    const size_t retained_a = (need_a_bytes > a_bytes_) ? need_a_bytes : a_bytes_;
    const bool watermark_exceeds_budget =
        (budget_bytes_ > 0) &&
        (retained_b > budget_bytes_ || retained_a > budget_bytes_ - retained_b);

    if (watermark_exceeds_budget) {
        // Reallocate to exact needed sizes so retained watermark never exceeds budget_bytes_.
        void* next_b = xk_aligned_alloc(need_b_bytes);
        void* next_a = nullptr;
        try {
            next_a = xk_aligned_alloc(need_a_bytes);
        } catch (...) {
            xk_aligned_free(next_b);
            throw;
        }
        xk_aligned_free(b_ptr_);
        xk_aligned_free(a_ptr_);
        b_ptr_ = next_b;
        b_bytes_ = need_b_bytes;
        a_ptr_ = next_a;
        a_bytes_ = need_a_bytes;
    } else {
        if (need_b_bytes > b_bytes_) {
            void* next_b = xk_aligned_alloc(need_b_bytes);
            xk_aligned_free(b_ptr_);
            b_ptr_ = next_b;
            b_bytes_ = need_b_bytes;
        }

        if (need_a_bytes > a_bytes_) {
            void* next_a = xk_aligned_alloc(need_a_bytes);
            xk_aligned_free(a_ptr_);
            a_ptr_ = next_a;
            a_bytes_ = need_a_bytes;
        }
    }

    a_slot_stride_ = slot_stride;
}

void XK_Workspace::prepare_f32(size_t b_floats, size_t a_floats_per_thread,
                               int num_threads) {
    prepare_raw(sizeof(float), b_floats, a_floats_per_thread, num_threads);
}

void XK_Workspace::prepare_f64(size_t b_doubles, size_t a_doubles_per_thread,
                               int num_threads) {
    prepare_raw(sizeof(double), b_doubles, a_doubles_per_thread, num_threads);
}

void XK_Workspace::release() noexcept {
    xk_aligned_free(b_ptr_);
    xk_aligned_free(a_ptr_);
    b_ptr_ = nullptr;
    b_bytes_ = 0;
    a_ptr_ = nullptr;
    a_bytes_ = 0;
    a_slot_stride_ = 0;
}