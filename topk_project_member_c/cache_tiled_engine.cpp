#include "cache_tiled_engine.h"
//#include "mock_simd_kernel.h" 
#include "../topk_project_member_b/simd_bitonic_kernel.h"

#include <algorithm>
#include <vector>
#include <numeric>
#include <limits>
#include <cassert>
#include <cstring>

// ============================================================
// Internal helper: initialize the Top-K reservoir
// We fill it with -infinity so any real value will displace it.
// The reservoir is always maintained sorted ASCENDING:
//   reservoir[0]   = current minimum (= K-th largest bound)
//   reservoir[K-1] = current maximum
// ============================================================

static void init_reservoir(float* reservoir, size_t K) {
    std::fill(reservoir, reservoir + K, -std::numeric_limits<float>::infinity());
}

// ============================================================
// Internal helper: get the current threshold (K-th largest bound)
// Since reservoir is sorted ascending, the threshold is reservoir[0].
// ============================================================

static inline float get_threshold(const float* reservoir) {
    return reservoir[0];
}

// ============================================================
// Internal helper: early-discard test against a broadcast bound
//
// True if any of the 8 floats at block is strictly above the
// current K-th bound. The caller broadcasts the bound once per
// merge, which keeps the broadcast out of the scan loop.
//
// TOPK_ABLATE_FILTER is for the optimisation ladder only and is
// off in every normal build (scripts/run_ablation.py):
//   1 = no filter, every block goes to the merge kernel
//   2 = scalar filter, lane by lane with an early exit
// ============================================================

#ifndef TOPK_ABLATE_FILTER
#define TOPK_ABLATE_FILTER 0
#endif

#if TOPK_ABLATE_FILTER == 1
using ThresholdVector = float;

static inline ThresholdVector broadcast_threshold(float threshold) {
    return threshold;
}

static inline bool block_exceeds_threshold(const float*, ThresholdVector) {
    return true;
}
#elif TOPK_ABLATE_FILTER == 2
using ThresholdVector = float;

static inline ThresholdVector broadcast_threshold(float threshold) {
    return threshold;
}

// The early exit keeps this scalar: compilers won't vectorise it.
static inline bool block_exceeds_threshold(const float* block, ThresholdVector bound) {
    for (int i = 0; i < 8; ++i) {
        if (block[i] > bound) {
            return true;
        }
    }
    return false;
}
#elif defined(__AVX2__)
using ThresholdVector = __m256;

static inline ThresholdVector broadcast_threshold(float threshold) {
    return _mm256_set1_ps(threshold);
}

static inline bool block_exceeds_threshold(const float* block, ThresholdVector bound) {
    const __m256 values = _mm256_loadu_ps(block);
    // Ordered comparisons ignore NaNs without hiding valid lanes.
    const __m256 exceeds = _mm256_cmp_ps(values, bound, _CMP_GT_OQ);
    return _mm256_movemask_ps(exceeds) != 0;
}
#else
using ThresholdVector = float;

static inline ThresholdVector broadcast_threshold(float threshold) {
    return threshold;
}

// Bitwise OR with no early exit: the compiler can vectorise this for
// whatever ISA it targets, without -ffast-math.
static inline bool block_exceeds_threshold(const float* block, ThresholdVector bound) {
    int survivor = 0;
    for (int i = 0; i < 8; ++i) {
        survivor |= block[i] > bound;
    }
    return survivor != 0;
}
#endif

// ============================================================
// Core engine: tiled streaming Top-K selection
//
// Key ideas:
//   1. Process input in cache-resident tiles of size B
//   2. Within each tile, scan elements in 8-wide blocks
//   3. Early-discard blocks with no value above threshold
//      (avoids calling the merge kernel on cold data)
//   4. For surviving blocks, call simd_bitonic_merge_8
//
// Parameters:
//   input        - input float array of length N
//   N            - total number of elements
//   K            - number of top elements to select
//   output       - caller-allocated buffer of size K
//   tile_size_B  - number of floats per cache tile
//                  Recommended: 512 (L1) to 4096 (L2)
// ============================================================

void topk_tiled(
    const float* input,
    size_t N,
    size_t K,
    float* output,
    size_t tile_size_B
) {
    assert(K > 0 && K <= N);
    assert(tile_size_B >= 8);  // must fit at least one SIMD block

    // Allocate sorted ascending reservoir on heap (size K)
    std::vector<float> reservoir(K);
    init_reservoir(reservoir.data(), K);

    // Temporary 8-float aligned block for padding the last chunk
    alignas(32) float block[8];

    // --------------------------------------------------------
    // Tile loop: process B floats at a time
    // Each tile ideally fits in L1d (B=512 floats = 2KB)
    //                    or L2  (B=4096 floats = 16KB)
    // --------------------------------------------------------
    for (size_t tile_start = 0; tile_start < N; tile_start += tile_size_B) {

        size_t tile_end = std::min(tile_start + tile_size_B, N);
        size_t tile_len = tile_end - tile_start;
        const float* tile_ptr = input + tile_start;

        // Prefetch next tile into L2 (helps overlap memory latency)
        if (tile_start + tile_size_B < N) {
            __builtin_prefetch(input + tile_start + tile_size_B, 0, 1);
        }

        // Broadcast the current threshold once per tile; it is only
        // refreshed after a merge can have raised it
        ThresholdVector bound = broadcast_threshold(get_threshold(reservoir.data()));

        // ----------------------------------------------------
        // Within-tile 8-wide block loop
        // ----------------------------------------------------
        size_t block_count = tile_len / 8;   // full 8-element blocks
        size_t remainder   = tile_len % 8;   // leftover elements

        for (size_t b = 0; b < block_count; ++b) {
            const float* blk = tile_ptr + b * 8;

            // ------------------------------------------------
            // Early-discard filter:
            // Compare all eight lanes to the broadcast threshold.
            // Skip merging if no valid value exceeds the bound.
            // ------------------------------------------------
            if (!block_exceeds_threshold(blk, bound)) {
                continue;  // discard entire block
            }

            // ------------------------------------------------
            // Surviving block: merge into Top-K reservoir
            // simd_bitonic_merge_8 updates reservoir in-place
            // and maintains sorted ascending order.
            // ------------------------------------------------
            simd_bitonic_merge_8(reservoir.data(), K, blk);

            // Update threshold after merge (reservoir[0] may have changed)
            bound = broadcast_threshold(get_threshold(reservoir.data()));
        }

        // ----------------------------------------------------
        // Handle remainder (< 8 elements at end of tile)
        // Pad with -infinity so the mock/SIMD kernel ignores them
        // ----------------------------------------------------
        if (remainder > 0) {
            std::fill(block, block + 8, -std::numeric_limits<float>::infinity());
            std::memcpy(block, tile_ptr + block_count * 8, remainder * sizeof(float));

            if (block_exceeds_threshold(block, bound)) {
                simd_bitonic_merge_8(reservoir.data(), K, block);
            }
        }
    }

    // --------------------------------------------------------
    // Write output: reservoir is sorted ascending.
    // Output convention: output[0] = largest, output[K-1] = K-th largest
    // (descending order, consistent with the project's Top-K definition)
    // --------------------------------------------------------
    for (size_t i = 0; i < K; ++i) {
        output[i] = reservoir[K - 1 - i];
    }
}

// ============================================================
// Default entry point (uses a sensible default tile size)
// Tile = 1024 floats = 4KB → comfortably fits in L1d (typically 32–48KB)
//
// Edge cases match topk_selector_parallel: K = 0 writes nothing, and
// K > N returns all N values, then fills output[N..K) with -infinity.
// ============================================================

void topk_selector(const float* input, size_t N, size_t K, float* output) {
    if (K == 0) return;

    constexpr size_t DEFAULT_TILE_SIZE = 1024;  // sweep this in Task 5
    const size_t K_valid = std::min(K, N);
    if (K_valid > 0) {
        topk_tiled(input, N, K_valid, output, DEFAULT_TILE_SIZE);
    }
    std::fill(output + K_valid, output + K, -std::numeric_limits<float>::infinity());
}