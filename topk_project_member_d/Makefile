# Knobs:
#   ARCH_FLAGS="-mavx2 -mfma"  default -march=native; AVX2 is what enables Member B's SIMD path
#   OPENMP=0                   serial build, same partitions and merges
#   OMP_PREFIX=/path           macOS libomp (default: conda sw_dev or Homebrew)

CXX        ?= g++
UNAME_S    := $(shell uname -s)
BUILD      := build
B_DIR      := ../topk_project_member_b
C_DIR      := ../topk_project_member_c

ARCH_FLAGS ?= -march=native
OPENMP     ?= 1
CXXFLAGS   ?= -O3
CXXFLAGS   += -std=c++17 -g -fno-omit-frame-pointer -Wall -Wextra $(ARCH_FLAGS)
LDFLAGS    ?=

# Apple clang ships no OpenMP runtime: libomp comes from conda (llvm-openmp) or Homebrew.
ifeq ($(OPENMP),1)
ifeq ($(UNAME_S),Darwin)
OMP_CANDIDATES := $(CONDA_PREFIX) /opt/miniconda3/envs/sw_dev /opt/homebrew/opt/libomp /usr/local/opt/libomp
OMP_PREFIX ?= $(firstword $(foreach d,$(OMP_CANDIDATES),$(if $(wildcard $(d)/include/omp.h),$(d),)))
ifeq ($(OMP_PREFIX),)
$(warning libomp not found; building WITHOUT OpenMP. Fix: conda install llvm-openmp (active env) or brew install libomp)
else
OMP_CFLAGS  := -Xpreprocessor -fopenmp -I$(OMP_PREFIX)/include
OMP_LDFLAGS := -L$(OMP_PREFIX)/lib -lomp -Wl,-rpath,$(OMP_PREFIX)/lib
endif
else
OMP_CFLAGS  := -fopenmp
OMP_LDFLAGS := -fopenmp
endif
endif

CXXFLAGS += $(OMP_CFLAGS)
LDFLAGS  += $(OMP_LDFLAGS)

ENGINE_SRC := parallel_topk.cpp $(C_DIR)/cache_tiled_engine.cpp
ENGINE_HDR := parallel_topk.h reference_baselines.h $(C_DIR)/cache_tiled_engine.h \
              $(B_DIR)/simd_bitonic_kernel.h
BENCH_HDR  := bench/bench_common.h bench/perf_control.h

TARGETS := $(BUILD)/test_parallel_topk $(BUILD)/bench_topk $(BUILD)/bench_bandwidth

.PHONY: all test test-quick asan asm clean
all: $(TARGETS)

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/test_parallel_topk: test_parallel_topk.cpp $(ENGINE_SRC) $(ENGINE_HDR) | $(BUILD)
	$(CXX) $(CXXFLAGS) test_parallel_topk.cpp $(ENGINE_SRC) -o $@ $(LDFLAGS)

$(BUILD)/bench_topk: bench/bench_topk.cpp $(ENGINE_SRC) $(ENGINE_HDR) $(BENCH_HDR) | $(BUILD)
	$(CXX) $(CXXFLAGS) bench/bench_topk.cpp $(ENGINE_SRC) -o $@ $(LDFLAGS)

$(BUILD)/bench_bandwidth: bench/bench_bandwidth.cpp $(BENCH_HDR) | $(BUILD)
	$(CXX) $(CXXFLAGS) bench/bench_bandwidth.cpp -o $@ $(LDFLAGS)

test: $(BUILD)/test_parallel_topk
	./$(BUILD)/test_parallel_topk

test-quick: $(BUILD)/test_parallel_topk
	./$(BUILD)/test_parallel_topk --quick

# No TSan: it false-positives inside uninstrumented OpenMP runtimes.
asan: | $(BUILD)
	$(CXX) $(CXXFLAGS) -O1 -fsanitize=address,undefined test_parallel_topk.cpp $(ENGINE_SRC) \
	    -o $(BUILD)/test_parallel_topk_asan $(LDFLAGS) -fsanitize=address,undefined
	./$(BUILD)/test_parallel_topk_asan --quick

asm: | $(BUILD)
	$(CXX) $(CXXFLAGS) -S -fverbose-asm parallel_topk.cpp -o $(BUILD)/parallel_topk.s
	@echo "wrote $(BUILD)/parallel_topk.s"

clean:
	rm -rf $(BUILD)
