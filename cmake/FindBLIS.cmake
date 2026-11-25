# FindBLIS.cmake — Locates BLIS (https://github.com/flame/blis)
# Hints: NVXK_BLIS_ROOT or BLIS_ROOT environment/CMake variable.

find_path(BLIS_INCLUDE_DIR
    NAMES blis/blis.h blis.h
    HINTS ${NVXK_BLIS_ROOT} $ENV{NVXK_BLIS_ROOT} ${BLIS_ROOT} $ENV{BLIS_ROOT}
    PATH_SUFFIXES include
)

find_library(BLIS_LIBRARY
    NAMES blis blis-mt libblis
    HINTS ${NVXK_BLIS_ROOT} $ENV{NVXK_BLIS_ROOT} ${BLIS_ROOT} $ENV{BLIS_ROOT}
    PATH_SUFFIXES lib lib64
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(BLIS DEFAULT_MSG BLIS_LIBRARY BLIS_INCLUDE_DIR)

if(BLIS_FOUND AND NOT TARGET BLIS::BLIS)
    add_library(BLIS::BLIS UNKNOWN IMPORTED)
    set_target_properties(BLIS::BLIS PROPERTIES
        IMPORTED_LOCATION "${BLIS_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${BLIS_INCLUDE_DIR}"
    )
endif()
