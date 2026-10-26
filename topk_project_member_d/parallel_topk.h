#pragma once
#include <cstddef>
#include <functional>

// Output: descending, NaN ranked as -inf (Member B's policy), so no NaN
// ever comes out. K > N pads output[N, K) with -inf.

// Any single-threaded kernel with the topk_selector signature. Must accept 1 <= K <= N.
using topk_kernel_fn =
    std::function<void(const float* input, size_t N, size_t K, float* output)>;

enum class MergeStrategy {
    Sequential,  // master: one K-way heap merge, O(K log P)
    TwoLevel,    // ~sqrt(P) leaders merge in parallel, then master
    Tree         // log2(P) pairwise rounds, barrier per round
};

struct ParallelTopKConfig {
    int num_threads = 0;                    // 0 -> omp_get_max_threads()
    MergeStrategy merge = MergeStrategy::Sequential;
    size_t min_elems_per_thread = 32768;    // below this fork/join costs more than it saves
};

struct ParallelTopKStats {
    int partitions = 0;
    double local_seconds = 0.0;
    double merge_seconds = 0.0;  // includes barriers / join
};

void topk_parallel(
    const float* input,
    size_t N,
    size_t K,
    float* output,
    const topk_kernel_fn& local_kernel,
    const ParallelTopKConfig& config = ParallelTopKConfig(),
    ParallelTopKStats* stats = nullptr
);

// Member C's engine (default tile) on all threads.
void topk_selector_parallel(const float* input, size_t N, size_t K, float* output);

// Partition t = [partition_begin(N, P, t), partition_begin(N, P, t + 1)).
size_t partition_begin(size_t N, size_t P, size_t t);

// Inputs: descending, NaN-free. Return min(K, total length).
size_t merge_topk_two(
    const float* a, size_t na,
    const float* b, size_t nb,
    size_t K, float* out
);

size_t merge_topk_many(
    const float* const* lists,
    const size_t* lengths,
    size_t num_lists,
    size_t K,
    float* out
);
