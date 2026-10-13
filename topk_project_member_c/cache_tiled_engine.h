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
//   - For surviving 8-element blocks, call simd_bitonic_merge_8
// ============================================================

// Primary entry point (implements topk_interface.h contract)
void topk_selector(const float* input, size_t N, size_t K, float* output);

// Exposed for testing/benchmarking individual tile sizes
void topk_tiled(
    const float* input,
    size_t N,
    size_t K,
    float* output,
    size_t tile_size_B   // number of floats per tile
);