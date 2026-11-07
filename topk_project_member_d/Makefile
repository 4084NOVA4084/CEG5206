# Knobs:
#   ARCH_FLAGS="-mavx2 -mfma"  default -march=native; AVX2 is what enables Member B's SIMD path
#   OPENMP=0                   serial build, same partitions and merges
#   OMP_PREFIX=/path           macOS libomp (default: conda sw_dev or Homebrew)

CXX        ?= g++
UNAME_S    := $(shell uname -s)
BUILD      := build
A_DIR      := ../topk_project_member_a
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

ENGINE_SRC := parallel_topk.cpp $(C_DIR)/cache_tiled_engine.cpp \
              $(A_DIR)/topk_minheap.cpp $(A_DIR)/topk_nth_element.cpp
ENGINE_HDR := parallel_topk.h $(A_DIR)/topk_baselines.h $(C_DIR)/cache_tiled_engine.h \
              $(B_DIR)/simd_bitonic_kernel.h bench/topk_replace_top.h
VALIDATION_SRC := $(A_DIR)/topk_validation.cpp $(A_DIR)/topk_oracle.cpp $(A_DIR)/data_generator.cpp
VALIDATION_HDR := $(A_DIR)/topk_validation.h $(A_DIR)/topk_oracle.h $(A_DIR)/data_generator.h
BENCH_HDR  := bench/bench_common.h bench/perf_control.h

TARGETS := $(BUILD)/test_parallel_topk $(BUILD)/test_integration $(BUILD)/bench_topk \
           $(BUILD)/bench_bandwidth

ABLATION := $(BUILD)/bench_topk_nofilter $(BUILD)/bench_topk_scalarfilter

.PHONY: all test test-quick test-avx2-emu test-ablation bench-ablation asan asm clean
all: $(TARGETS)

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/test_parallel_topk: test_parallel_topk.cpp $(ENGINE_SRC) $(ENGINE_HDR) | $(BUILD)
	$(CXX) $(CXXFLAGS) test_parallel_topk.cpp $(ENGINE_SRC) -o $@ $(LDFLAGS)

$(BUILD)/test_integration: test_integration.cpp $(ENGINE_SRC) $(ENGINE_HDR) $(VALIDATION_SRC) $(VALIDATION_HDR) | $(BUILD)
	$(CXX) $(CXXFLAGS) test_integration.cpp $(ENGINE_SRC) $(VALIDATION_SRC) -o $@ $(LDFLAGS)

$(BUILD)/bench_topk: bench/bench_topk.cpp $(ENGINE_SRC) $(ENGINE_HDR) $(BENCH_HDR) | $(BUILD)
	$(CXX) $(CXXFLAGS) bench/bench_topk.cpp $(ENGINE_SRC) -o $@ $(LDFLAGS)

$(BUILD)/bench_bandwidth: bench/bench_bandwidth.cpp $(BENCH_HDR) | $(BUILD)
	$(CXX) $(CXXFLAGS) bench/bench_bandwidth.cpp -o $@ $(LDFLAGS)

# bench_topk with Member C's filter removed (=1) or made scalar (=2), for scripts/run_ablation.py.
bench-ablation: $(ABLATION)

$(BUILD)/bench_topk_nofilter: bench/bench_topk.cpp $(ENGINE_SRC) $(ENGINE_HDR) $(BENCH_HDR) | $(BUILD)
	$(CXX) $(CXXFLAGS) -DTOPK_ABLATE_FILTER=1 bench/bench_topk.cpp $(ENGINE_SRC) -o $@ $(LDFLAGS)

$(BUILD)/bench_topk_scalarfilter: bench/bench_topk.cpp $(ENGINE_SRC) $(ENGINE_HDR) $(BENCH_HDR) | $(BUILD)
	$(CXX) $(CXXFLAGS) -DTOPK_ABLATE_FILTER=2 bench/bench_topk.cpp $(ENGINE_SRC) -o $@ $(LDFLAGS)

# The ablation builds must still be correct, or their timings mean nothing.
test-ablation: | $(BUILD)
	$(CXX) $(CXXFLAGS) -DTOPK_ABLATE_FILTER=1 test_integration.cpp $(ENGINE_SRC) $(VALIDATION_SRC) \
	    -o $(BUILD)/test_integration_nofilter $(LDFLAGS)
	$(CXX) $(CXXFLAGS) -DTOPK_ABLATE_FILTER=2 test_integration.cpp $(ENGINE_SRC) $(VALIDATION_SRC) \
	    -o $(BUILD)/test_integration_scalarfilter $(LDFLAGS)
	./$(BUILD)/test_integration_nofilter
	./$(BUILD)/test_integration_scalarfilter

test: $(BUILD)/test_parallel_topk $(BUILD)/test_integration
	./$(BUILD)/test_integration
	./$(BUILD)/test_parallel_topk

test-quick: $(BUILD)/test_parallel_topk $(BUILD)/test_integration
	./$(BUILD)/test_integration
	./$(BUILD)/test_parallel_topk --quick

# Member B's and C's AVX2 paths on any CPU, through scalar stand-ins for the
# intrinsics. Checks logic only; a real AVX2 machine is still needed for speed.
test-avx2-emu: | $(BUILD)
	$(CXX) $(CXXFLAGS) -D__AVX2__ -Itools/avx2_emu test_integration.cpp $(ENGINE_SRC) \
	    $(VALIDATION_SRC) -o $(BUILD)/test_integration_avx2emu $(LDFLAGS)
	$(CXX) $(CXXFLAGS) -D__AVX2__ -Itools/avx2_emu test_parallel_topk.cpp $(ENGINE_SRC) \
	    -o $(BUILD)/test_parallel_topk_avx2emu $(LDFLAGS)
	$(CXX) $(CXXFLAGS) -D__AVX2__ -Itools/avx2_emu $(B_DIR)/test_simd_kernel.cpp \
	    -o $(BUILD)/test_simd_kernel_avx2emu $(LDFLAGS)
	./$(BUILD)/test_simd_kernel_avx2emu
	./$(BUILD)/test_integration_avx2emu
	./$(BUILD)/test_parallel_topk_avx2emu --quick

# No TSan: it false-positives inside uninstrumented OpenMP runtimes.
asan: | $(BUILD)
	$(CXX) $(CXXFLAGS) -O1 -fsanitize=address,undefined test_parallel_topk.cpp $(ENGINE_SRC) \
	    -o $(BUILD)/test_parallel_topk_asan $(LDFLAGS) -fsanitize=address,undefined
	$(CXX) $(CXXFLAGS) -O1 -fsanitize=address,undefined test_integration.cpp $(ENGINE_SRC) \
	    $(VALIDATION_SRC) -o $(BUILD)/test_integration_asan $(LDFLAGS) -fsanitize=address,undefined
	./$(BUILD)/test_integration_asan
	./$(BUILD)/test_parallel_topk_asan --quick

asm: | $(BUILD)
	$(CXX) $(CXXFLAGS) -S -fverbose-asm parallel_topk.cpp -o $(BUILD)/parallel_topk.s
	@echo "wrote $(BUILD)/parallel_topk.s"

clean:
	rm -rf $(BUILD)
