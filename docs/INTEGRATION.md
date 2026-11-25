# Hướng dẫn Tích hợp vào Ứng dụng khác (Integration Guide)

**NVXK v2** cung cấp hai mức tích hợp: liên kết C++ trực tiếp (`include/nvxk.h`) và liên kết qua giao diện C ABI ổn định (`include/nvxk_c.h` + `libnvxk.so` / `nvxk.dll`).

## 1. Tích hợp vào Dự án C++ bằng CMake *(chưa chạy CMake trong sandbox)*

Đặt thư mục `NVXK_v2` vào `third_party/nvxk` trong dự án của bạn và thêm vào `CMakeLists.txt`:

```cmake
add_subdirectory(third_party/nvxk)

add_executable(my_solver main.cpp)
target_link_libraries(my_solver PRIVATE nvxk_core)
```

Target `nvxk_core` tự động xuất đường dẫn header `include/` và phụ thuộc `OpenMP::OpenMP_CXX`.

## 2. Tích hợp vào Dự án C99 (qua C ABI `include/nvxk_c.h`)

Biên dịch thư viện chia sẻ trước bằng `make all` (tạo `libnvxk.so` trên Linux hoặc `nvxk.dll` trên Windows):

```c
#include <stdio.h>
#include "nvxk_c.h"

int main(void) {
    float A[4] = {1.0f, 2.0f, 3.0f, 4.0f};
    float B[4] = {5.0f, 6.0f, 7.0f, 8.0f};
    float C[4] = {0};
    int used_threads = 0;

    int rc = nvxk_sgemm(A, B, C, 2, 2, 2, &used_threads);
    if (rc != NVXK_OK) {
        fprintf(stderr, "NVXK error (%d): %s\n", rc, nvxk_last_error());
        return 1;
    }
    printf("C = [[%.1f, %.1f], [%.1f, %.1f]] (threads=%d)\n",
           C[0], C[1], C[2], C[3], used_threads);
    return 0;
}
```

Biên dịch và chạy trên Linux:

```bash
gcc -std=c99 -O2 -Iinclude demo.c -L. -lnvxk -Wl,-rpath,. -o demo
./demo
```

## 3. Tích hợp vào Python (`ctypes` + `NumPy`) *(đã xác minh trực tiếp trong `scripts/bench_vs_openblas.py`)*

```python
import ctypes
import os
import numpy as np

lib = ctypes.CDLL(os.path.abspath("libnvxk.so"))
lib.nvxk_sgemm.argtypes = [
    ctypes.POINTER(ctypes.c_float),
    ctypes.POINTER(ctypes.c_float),
    ctypes.POINTER(ctypes.c_float),
    ctypes.c_size_t, ctypes.c_size_t, ctypes.c_size_t,
    ctypes.POINTER(ctypes.c_int),
]
lib.nvxk_sgemm.restype = ctypes.c_int

A = np.ascontiguousarray(np.ones((64, 128), dtype=np.float32))
B = np.ascontiguousarray(np.full((128, 64), 0.5, dtype=np.float32))
C = np.empty((64, 64), dtype=np.float32)
used = ctypes.c_int(0)

rc = lib.nvxk_sgemm(
    A.ctypes.data_as(ctypes.POINTER(ctypes.c_float)),
    B.ctypes.data_as(ctypes.POINTER(ctypes.c_float)),
    C.ctypes.data_as(ctypes.POINTER(ctypes.c_float)),
    64, 128, 64, ctypes.byref(used),
)
assert rc == 0
```

*Lưu ý bắt buộc*: Mảng NumPy phải liên tục kiểu C (`np.ascontiguousarray(..., dtype=np.float32)`) và `C` không được trỏ trùng vùng nhớ với `A` hoặc `B`.

## 4. Tích hợp vào Rust (FFI `extern "C"`) *(chưa xác minh vì sandbox không có `rustc`)*

```rust
use std::os::raw::{c_int, c_float};

#[link(name = "nvxk")]
extern "C" {
    fn nvxk_sgemm(
        a: *const c_float, b: *const c_float, c: *mut c_float,
        m: usize, k: usize, n: usize, out_threads: *mut c_int,
    ) -> c_int;
}
```

Khi link, truyền đường dẫn thư mục chứa `libnvxk.so` / `nvxk.dll` qua `cargo:rustc-link-search=native=...` trong `build.rs`.