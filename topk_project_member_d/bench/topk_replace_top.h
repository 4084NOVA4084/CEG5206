#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <functional>
#include <limits>
#include <vector>

// Hand-written scalar Top-K: the fair baseline for "what do SIMD and tiling buy?".
// Same algorithm as Member A's topk_minheap, without its two overheads:
//   - pop + push costs 2 log K per replacement; one sift-down from the root is enough.
//   - it calls isnan on every element, but `x > bound` is already false for NaN.
// Only the first K elements need an explicit NaN -> -inf, because they enter unfiltered.
inline void topk_replace_top(const float* input, size_t N, size_t K, float* output) {
    if (K == 0 || K > N) return;

    const float neg_inf = -std::numeric_limits<float>::infinity();
    std::vector<float> storage(K);
    float* heap = storage.data();
    for (size_t i = 0; i < K; ++i) heap[i] = std::isnan(input[i]) ? neg_inf : input[i];
    std::make_heap(heap, heap + K, std::greater<float>());  // heap[0] is the K-th largest

    float bound = heap[0];
    for (size_t i = K; i < N; ++i) {
        const float x = input[i];
        if (!(x > bound)) continue;

        size_t hole = 0;
        for (;;) {
            size_t child = 2 * hole + 1;
            if (child >= K) break;
            if (child + 1 < K && heap[child + 1] < heap[child]) ++child;
            if (!(heap[child] < x)) break;
            heap[hole] = heap[child];
            hole = child;
        }
        heap[hole] = x;
        bound = heap[0];
    }

    std::sort(heap, heap + K, std::greater<float>());
    std::copy(heap, heap + K, output);
}
