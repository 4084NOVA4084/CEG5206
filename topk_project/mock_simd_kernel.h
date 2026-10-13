#pragma once
#include <algorithm>
#include <cstddef>

// ============================================================
// MOCK implementation of simd_bitonic_merge_8
// Replace with Member B's real AVX2 implementation later
//
// Semantics: Given a sorted Top-K reservoir (ascending order,
// reservoir[0] = current minimum / K-th bound), and 8 new floats,
// update the reservoir to keep the global Top-K largest elements.
// The reservoir remains sorted ascending after each call.
// ============================================================

inline void simd_bitonic_merge_8(
    float* topk_reservoir,   // sorted ascending, size = K
    size_t K,
    const float* new_block_8 // exactly 8 floats
) {
    // For each of the 8 incoming elements:
    // If it is larger than the current K-th smallest (reservoir[0]),
    // insert it into the reservoir and re-sort.
    for (int i = 0; i < 8; ++i) {
        if (new_block_8[i] > topk_reservoir[0]) {
            topk_reservoir[0] = new_block_8[i];
            // Restore sorted ascending order (sift up)
            size_t pos = 0;
            while (pos + 1 < K && topk_reservoir[pos] > topk_reservoir[pos + 1]) {
                std::swap(topk_reservoir[pos], topk_reservoir[pos + 1]);
                ++pos;
            }
        }
    }
    // Post-condition: topk_reservoir is sorted ascending,
    // topk_reservoir[0] = current minimum among Top-K candidates
}