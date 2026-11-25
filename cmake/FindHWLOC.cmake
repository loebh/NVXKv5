# FindHWLOC.cmake — Locates hwloc (https://www.open-mpi.org/projects/hwloc/)
# Hints: NVXK_HWLOC_ROOT or HWLOC_ROOT environment/CMake variable.

find_path(HWLOC_INCLUDE_DIR
    NAMES hwloc.h
    HINTS ${NVXK_HWLOC_ROOT} $ENV{NVXK_HWLOC_ROOT} ${HWLOC_ROOT} $ENV{HWLOC_ROOT}
    PATH_SUFFIXES include
)

find_library(HWLOC_LIBRARY
    NAMES hwloc libhwloc
    HINTS ${NVXK_HWLOC_ROOT} $ENV{NVXK_HWLOC_ROOT} ${HWLOC_ROOT} $ENV{HWLOC_ROOT}
    PATH_SUFFIXES lib lib64
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(HWLOC DEFAULT_MSG HWLOC_LIBRARY HWLOC_INCLUDE_DIR)

if(HWLOC_FOUND AND NOT TARGET HWLOC::HWLOC)
    add_library(HWLOC::HWLOC UNKNOWN IMPORTED)
    set_target_properties(HWLOC::HWLOC PROPERTIES
        IMPORTED_LOCATION "${HWLOC_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${HWLOC_INCLUDE_DIR}"
    )
endif()
