#pragma once
#include <cstddef>

// ============================================================
// Cache-Aware Streaming Top-K Selection Engine
//
// Design:
//   - Partition input array into contiguous tiles of size B
//   - B is chosen so the active working set fits in L1d/L2 cache
//   - Maintain a sorted ascending Top-K reservoir in registers/L1
//   - Early-discard: skip tiles/elements below the current K-th bound
//   - Filter 8-wide blocks with an ordered broadcast compare (AVX2)
//     or scalar comparisons; NaNs never hide valid lanes, including tails
//   - For surviving 8-element blocks, call simd_bitonic_merge_8
//   - Output is descending, with NaNs ranked as -infinity
// ============================================================

// Primary entry point (implements topk_interface.h contract)
// K = 0 writes nothing (output may be null). K > N returns all N values, then fills
// output[N..K) with -infinity.
void topk_selector(const float* input, size_t N, size_t K, float* output);

// Exposed for testing/benchmarking individual tile sizes
// Requires 1 <= K <= N
void topk_tiled(
    const float* input,
    size_t N,
    size_t K,
    float* output,
    size_t tile_size_B   // number of floats per tile
);