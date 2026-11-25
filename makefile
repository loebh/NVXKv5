# NVXK Unified Computing Engine v4.0.0 — Portable Makefile
CXX ?= g++
CC  ?= gcc
NO_OMP ?= 0
ifeq ($(NO_OMP),1)
OMP_FLAG =
else
OMP_FLAG = -fopenmp
endif

ENABLE_HIGHWAY ?= 1
ENABLE_HWLOC   ?= 0
ENABLE_BLIS    ?= 0

CXXFLAGS = -std=c++17 -O3 $(OMP_FLAG) -Wall -Wextra -Iinclude -Isrc
TESTFLAGS = -std=c++17 -O2 $(OMP_FLAG) -Wall -Wextra -Iinclude -Isrc
SANFLAGS = -std=c++17 -O1 -g -fsanitize=address,undefined $(OMP_FLAG) -Wall -Wextra -Iinclude -Isrc

SUBSYS_CPPFLAGS =
SUBSYS_LDLIBS   =

ifeq ($(ENABLE_HIGHWAY),1)
HWY_INC ?= third_party/highway
SUBSYS_CPPFLAGS += -DNVXK_ENABLE_HIGHWAY=1 -DHWY_STATIC_DEFINE -I$(HWY_INC)
ifneq ($(HWY_LIB),)
SUBSYS_LDLIBS += $(HWY_LIB)
else
HWY_SRCS = third_party/highway/hwy/targets.cc \
           third_party/highway/hwy/aligned_allocator.cc \
           third_party/highway/hwy/per_target.cc \
           third_party/highway/hwy/print.cc \
           third_party/highway/hwy/abort.cc
endif
endif

ifeq ($(ENABLE_HWLOC),1)
HWLOC_INC ?= third_party/hwloc/include
SUBSYS_CPPFLAGS += -DNVXK_ENABLE_HWLOC=1 -I$(HWLOC_INC)
ifneq ($(HWLOC_GEN_INC),)
SUBSYS_CPPFLAGS += -I$(HWLOC_GEN_INC)
endif
ifneq ($(HWLOC_LIB),)
SUBSYS_LDLIBS += $(HWLOC_LIB)
else
SUBSYS_LDLIBS += -lhwloc
endif
endif

ifeq ($(ENABLE_BLIS),1)
SUBSYS_CPPFLAGS += -DNVXK_ENABLE_BLIS=1 $(BLIS_CFLAGS)
SUBSYS_LDLIBS += $(if $(BLIS_LIBS),$(BLIS_LIBS),-lblis)
endif

CORE_SRCS = src/nvxk_api.cpp src/nvxk_hw.cpp src/nvxk_kernels_avx2.cpp \
            src/nvxk_kernels_scalar.cpp src/nvxk_planner.cpp src/nvxk_workspace.cpp \
            src/core/nvxk_config.cpp src/core/nvxk_dispatcher.cpp \
            src/topology/nvxk_topology.cpp \
            src/backends/highway/nvxk_highway_backend.cpp \
            src/backends/blis/nvxk_blis_backend.cpp $(HWY_SRCS)

WB_SRCS = adapters/core_adapter/nvxk_adapter.cpp \
          diagnostics/cpu/cpu_diagnostics.cpp \
          benchmark/runner/workbench_engine.cpp \
          benchmark/insights/insights_engine.cpp \
          benchmark/reports/report_exporter.cpp \
          ui/server/http_server.cpp

UNAME_S := $(shell uname -s 2>/dev/null || echo Windows)
ifeq ($(UNAME_S),Linux)
SHARED_LIB = libnvxk.so
SHARED_FLAGS = -fPIC -shared -DNVXK_BUILDING_DLL
SYS_LDLIBS = -pthread -lm
else ifeq ($(UNAME_S),Darwin)
SHARED_LIB = libnvxk.dylib
SHARED_FLAGS = -fPIC -dynamiclib -DNVXK_BUILDING_DLL
SYS_LDLIBS = -pthread -lm
else
SHARED_LIB = nvxk.dll
SHARED_FLAGS = -shared -DNVXK_BUILDING_DLL
SYS_LDLIBS = -lws2_32 -lpsapi
endif

all: nvxk_cli nvxk my_app $(SHARED_LIB) nvxk_workbench

nvxk_cli: src/cli/nvxk_cli.cpp $(CORE_SRCS)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(SUBSYS_CPPFLAGS) -o $@ src/cli/nvxk_cli.cpp $(CORE_SRCS) $(SUBSYS_LDLIBS) $(SYS_LDLIBS) $(LDLIBS)

nvxk: benchmarks/main.cpp $(CORE_SRCS)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(SUBSYS_CPPFLAGS) -o $@ benchmarks/main.cpp $(CORE_SRCS) $(SUBSYS_LDLIBS) $(SYS_LDLIBS) $(LDLIBS)

my_app: apps/my_app.cpp $(CORE_SRCS)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(SUBSYS_CPPFLAGS) -o $@ apps/my_app.cpp $(CORE_SRCS) $(SUBSYS_LDLIBS) $(SYS_LDLIBS) $(LDLIBS)

$(SHARED_LIB): $(CORE_SRCS)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(SUBSYS_CPPFLAGS) $(SHARED_FLAGS) -o $@ $(CORE_SRCS) $(SUBSYS_LDLIBS) $(SYS_LDLIBS) $(LDLIBS)

nvxk_workbench: ui/server/main.cpp $(WB_SRCS) $(CORE_SRCS)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(SUBSYS_CPPFLAGS) -o $@ ui/server/main.cpp $(WB_SRCS) $(CORE_SRCS) $(SUBSYS_LDLIBS) $(SYS_LDLIBS) $(LDLIBS)

nvxk-test: tests/nvxk_test.cpp $(CORE_SRCS)
	$(CXX) $(CPPFLAGS) $(TESTFLAGS) $(SUBSYS_CPPFLAGS) -o $@ tests/nvxk_test.cpp $(CORE_SRCS) $(SUBSYS_LDLIBS) $(SYS_LDLIBS) $(LDLIBS)

nvxk-unified-test: tests/unified_integration_test.cpp $(CORE_SRCS)
	$(CXX) $(CPPFLAGS) $(TESTFLAGS) $(SUBSYS_CPPFLAGS) -o $@ tests/unified_integration_test.cpp $(CORE_SRCS) $(SUBSYS_LDLIBS) $(SYS_LDLIBS) $(LDLIBS)

workbench-test: tests/workbench_test.cpp $(WB_SRCS) $(CORE_SRCS)
	$(CXX) $(CPPFLAGS) $(TESTFLAGS) $(SUBSYS_CPPFLAGS) -o $@ tests/workbench_test.cpp $(WB_SRCS) $(CORE_SRCS) $(SUBSYS_LDLIBS) $(SYS_LDLIBS) $(LDLIBS)

test: nvxk-test nvxk-unified-test workbench-test
	./nvxk-test
	./nvxk-unified-test
	./workbench-test

test-san: tests/unified_integration_test.cpp $(CORE_SRCS)
	$(CXX) $(CPPFLAGS) $(SANFLAGS) $(SUBSYS_CPPFLAGS) -o nvxk-unified-test-san tests/unified_integration_test.cpp $(CORE_SRCS) $(SUBSYS_LDLIBS) $(SYS_LDLIBS) $(LDLIBS)
	./nvxk-unified-test-san

.PHONY: all test test-san clean
clean:
	rm -f nvxk_cli nvxk my_app nvxk_workbench nvxk-test nvxk-unified-test nvxk-unified-test-san workbench-test libnvxk.so libnvxk.dylib nvxk.dll