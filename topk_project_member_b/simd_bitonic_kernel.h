#pragma once

#include <algorithm>
#include <cstddef>
#include <cmath>
#include <limits>

#if defined(__AVX2__)
#include <immintrin.h>
#endif

// ============================================================
// Member B: SIMD Bitonic Sorting Network Micro-Kernel
//
// Design:
//   - Sort incoming 8-float blocks with a fixed AVX2 network
//   - Use vector min/max and permute/blend operations
//   - Merge sorted blocks into an ascending Top-K reservoir
//   - Treat NaN values as -infinity for deterministic selection
//   - Fall back to std::sort when AVX2 is unavailable
// ============================================================

namespace topk_detail {

#if defined(__AVX2__)
// ============================================================
// Internal helper: branchless 8-lane Bitonic sorting network
//
// The network uses a fixed compare-exchange schedule. It does
// not branch on input data and keeps the active values in SIMD
// registers throughout the sorting stages.
// ============================================================

inline __m256 sort_block_bitonic(__m256 values) {
    const __m256i swap_adjacent = _mm256_setr_epi32(
        1, 0, 3, 2, 5, 4, 7, 6);
    const __m256i swap_pairs = _mm256_setr_epi32(
        2, 3, 0, 1, 6, 7, 4, 5);
    const __m256i swap_halves = _mm256_setr_epi32(
        4, 5, 6, 7, 0, 1, 2, 3);

    const __m256 stage_2 = _mm256_castsi256_ps(
        _mm256_setr_epi32(-1, 0, 0, -1, -1, 0, 0, -1));
    const __m256 stage_4_pairs = _mm256_castsi256_ps(
        _mm256_setr_epi32(-1, -1, 0, 0, 0, 0, -1, -1));
    const __m256 stage_4_adjacent = _mm256_castsi256_ps(
        _mm256_setr_epi32(-1, 0, -1, 0, 0, -1, 0, -1));
    const __m256 stage_8_halves = _mm256_castsi256_ps(
        _mm256_setr_epi32(-1, -1, -1, -1, 0, 0, 0, 0));
    const __m256 stage_8_pairs = _mm256_castsi256_ps(
        _mm256_setr_epi32(-1, -1, 0, 0, -1, -1, 0, 0));
    const __m256 stage_8_adjacent = _mm256_castsi256_ps(
        _mm256_setr_epi32(-1, 0, -1, 0, -1, 0, -1, 0));

    auto compare_exchange = []( __m256 current, __m256 partner,
                                __m256 ascending_mask) {
        const __m256 low = _mm256_min_ps(current, partner);
        const __m256 high = _mm256_max_ps(current, partner);
        return _mm256_blendv_ps(high, low, ascending_mask);
    };

    values = compare_exchange(
        values, _mm256_permutevar8x32_ps(values, swap_adjacent),
        stage_2);
    values = compare_exchange(
        values, _mm256_permutevar8x32_ps(values, swap_pairs),
        stage_4_pairs);
    values = compare_exchange(
        values, _mm256_permutevar8x32_ps(values, swap_adjacent),
        stage_4_adjacent);
    values = compare_exchange(
        values, _mm256_permutevar8x32_ps(values, swap_halves),
        stage_8_halves);
    values = compare_exchange(
        values, _mm256_permutevar8x32_ps(values, swap_pairs),
        stage_8_pairs);
    return compare_exchange(
        values, _mm256_permutevar8x32_ps(values, swap_adjacent),
        stage_8_adjacent);
}

// ============================================================
// Internal helper: sorted pair returned by an 8-wide merge
//
// low  - lower eight values from the 16-value merge
// high - upper eight values from the 16-value merge
// ============================================================

struct sorted_pair_8 {
    __m256 low;
    __m256 high;
};

// Both inputs must be sorted ascending. Reversing the second
// input creates a bitonic sequence. The two compare-exchange
// halves produce the lower and upper portions of the merge.
inline sorted_pair_8 bitonic_merge_8(__m256 left, __m256 right) {
    const __m256i reverse = _mm256_setr_epi32(7, 6, 5, 4, 3, 2, 1, 0);
    const __m256 reversed_right =
        _mm256_permutevar8x32_ps(right, reverse);
    const __m256 low = sort_block_bitonic(_mm256_min_ps(left, reversed_right));
    const __m256 high = sort_block_bitonic(_mm256_max_ps(left, reversed_right));
    return {low, high};
}

// ============================================================
// Internal helper: return the upper half of an 8-wide merge
//
// This is the register-level operation used when only the
// largest eight values from two sorted blocks are required.
// ============================================================

inline __m256 bitonic_merge_top_8(__m256 left, __m256 right) {
    return bitonic_merge_8(left, right).high;
}

// ============================================================
// Internal helper: sorted 16-element block representation
//
// A 16-element sorted block is stored as two ascending AVX2
// registers: low contains elements [0, 7], high contains
// elements [8, 15].
// ============================================================

struct sorted_block_16 {
    __m256 low;
    __m256 high;
};

// Merge two sorted 16-element blocks and retain their top 16
// values. Carry propagation between the two 8-wide partitions
// keeps the operation in SIMD registers without scalar,
// data-dependent comparisons.
inline sorted_block_16 bitonic_merge_top_16(
    sorted_block_16 left,
    sorted_block_16 right
) {
    const sorted_pair_8 high = bitonic_merge_8(left.high, right.high);
    const sorted_pair_8 low = bitonic_merge_8(left.low, high.low);
    return {low.high, high.high};
}
#endif

// ============================================================
// Internal helper: sort one incoming 8-float block
//
// AVX2 builds use the branchless Bitonic network. Other builds
// retain a portable implementation for functional testing.
// ============================================================

inline void sort_block_8(float* block) {
#if defined(__AVX2__)
    const __m256 values = sort_block_bitonic(_mm256_loadu_ps(block));
    _mm256_storeu_ps(block, values);
#else
    std::sort(block, block + 8);
#endif
}

}  // namespace topk_detail

// ============================================================
// Internal helper: normalize NaN values
//
// NaNs are treated as lower than all valid values so selection
// remains deterministic under the project's FP32 contract.
// ============================================================

inline void normalize_nan_block(float* block) {
    for (int i = 0; i < 8; ++i) {
        if (std::isnan(block[i])) {
            block[i] = -std::numeric_limits<float>::infinity();
        }
    }
}

// ============================================================
// Public micro-kernel: merge one 8-float block into Top-K
//
// Parameters:
//   topk_reservoir - sorted ascending reservoir of size K
//                    containing the K largest values seen so far
//   K              - reservoir size; AVX2 path uses K values
//                    when K is a multiple of eight
//   new_block_8    - eight input values to merge
//
// Post-condition:
//   topk_reservoir remains sorted ascending and contains the
//   K largest values seen so far.
// ============================================================

inline void simd_bitonic_merge_8(
    float* topk_reservoir,
    std::size_t K,
    const float* new_block_8
) {
    float sorted_block[8];
    std::copy(new_block_8, new_block_8 + 8, sorted_block);
    normalize_nan_block(sorted_block);
    topk_detail::sort_block_8(sorted_block);

#if defined(__AVX2__)
    if (K >= 8 && K % 8 == 0) {
        __m256 carry = _mm256_loadu_ps(sorted_block);
        for (std::size_t offset = K; offset != 0; offset -= 8) {
            const std::size_t index = offset - 8;
            const __m256 reservoir =
                _mm256_loadu_ps(topk_reservoir + index);
            const topk_detail::sorted_pair_8 merged =
                topk_detail::bitonic_merge_8(reservoir, carry);
            _mm256_storeu_ps(topk_reservoir + index, merged.high);
            carry = merged.low;
        }
        return;
    }
#endif

    for (const float value : sorted_block) {
        if (value <= topk_reservoir[0]) {
            continue;
        }

        topk_reservoir[0] = value;
        std::size_t pos = 0;
        while (pos + 1 < K &&
               topk_reservoir[pos] > topk_reservoir[pos + 1]) {
            std::swap(topk_reservoir[pos], topk_reservoir[pos + 1]);
            ++pos;
        }
    }
}
