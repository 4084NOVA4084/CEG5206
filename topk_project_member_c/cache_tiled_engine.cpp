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
// Core engine: tiled streaming Top-K selection
//
// Key ideas:
//   1. Process input in cache-resident tiles of size B
//   2. Within each tile, scan elements in 8-wide blocks
//   3. Early-discard entire blocks if max(block) <= threshold
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

        // Get current threshold once per tile (avoid repeated loads)
        float threshold = get_threshold(reservoir.data());

        // ----------------------------------------------------
        // Within-tile 8-wide block loop
        // ----------------------------------------------------
        size_t block_count = tile_len / 8;   // full 8-element blocks
        size_t remainder   = tile_len % 8;   // leftover elements

        for (size_t b = 0; b < block_count; ++b) {
            const float* blk = tile_ptr + b * 8;

            // ------------------------------------------------
            // Early-discard filter:
            // Compute max of the 8 elements manually.
            // If max <= threshold, the entire block is below
            // the current K-th bound — skip without merging.
            // (Member B's SIMD kernel will do this via _mm256_max_ps)
            // ------------------------------------------------
            float blk_max = blk[0];
            for (int i = 1; i < 8; ++i) {
                if (blk[i] > blk_max) blk_max = blk[i];
            }

            if (blk_max <= threshold) {
                continue;  // discard entire block
            }

            // ------------------------------------------------
            // Surviving block: merge into Top-K reservoir
            // simd_bitonic_merge_8 updates reservoir in-place
            // and maintains sorted ascending order.
            // ------------------------------------------------
            simd_bitonic_merge_8(reservoir.data(), K, blk);

            // Update threshold after merge (reservoir[0] may have changed)
            threshold = get_threshold(reservoir.data());
        }

        // ----------------------------------------------------
        // Handle remainder (< 8 elements at end of tile)
        // Pad with -infinity so the mock/SIMD kernel ignores them
        // ----------------------------------------------------
        if (remainder > 0) {
            std::fill(block, block + 8, -std::numeric_limits<float>::infinity());
            std::memcpy(block, tile_ptr + block_count * 8, remainder * sizeof(float));

            float blk_max = *std::max_element(block, block + 8);
            if (blk_max > threshold) {
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
// ============================================================

void topk_selector(const float* input, size_t N, size_t K, float* output) {
    constexpr size_t DEFAULT_TILE_SIZE = 1024;  // sweep this in Task 5
    topk_tiled(input, N, K, output, DEFAULT_TILE_SIZE);
}