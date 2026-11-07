#include "simd_bitonic_kernel.h"

// Entry points for inspect_asm.py. Each one keeps a single stage of the kernel
// as its own symbol, so the audit can count that stage's jumps and spills.

#define PROBE extern "C" __attribute__((noinline))

PROBE void probe_sort_block(float* block) {
    _mm256_storeu_ps(block, topk_detail::sort_block_bitonic(_mm256_loadu_ps(block)));
}

PROBE void probe_normalize_nan(float* block) {
    _mm256_storeu_ps(block, topk_detail::normalize_nan_8(_mm256_loadu_ps(block)));
}

PROBE void probe_merge_pair(float* left, float* right) {
    const topk_detail::sorted_pair_8 merged =
        topk_detail::bitonic_merge_8(_mm256_loadu_ps(left), _mm256_loadu_ps(right));
    _mm256_storeu_ps(left, merged.low);
    _mm256_storeu_ps(right, merged.high);
}

// The K = 8 fast path, spelled out: at this size the compiler keeps the public
// function out of line, where it would drag the scalar path into the count.
PROBE void probe_merge_k8(float* reservoir, const float* block) {
    const __m256 sorted = topk_detail::sort_block_bitonic(
        topk_detail::normalize_nan_8(_mm256_loadu_ps(block)));
    topk_detail::merge_block_into_reservoir(reservoir, 8, sorted);
}

PROBE void probe_merge_k64(float* reservoir, const float* block) {
    simd_bitonic_merge_8(reservoir, 64, block);
}

PROBE void probe_merge_k1024(float* reservoir, const float* block) {
    simd_bitonic_merge_8(reservoir, 1024, block);
}

// Forces an out-of-line copy of the public function, which also holds the K
// dispatch and the scalar path for K % 8 != 0.
PROBE void probe_merge_any_k(float* reservoir, std::size_t K, const float* block) {
    simd_bitonic_merge_8(reservoir, K, block);
}
