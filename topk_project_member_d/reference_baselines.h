#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <functional>
#include <limits>
#include <vector>

// TEMPORARY stand-ins for Member A's Task 1 baselines. Swap the two
// bodies for A's code when it lands; nothing else references them.
// Same contract as topk_selector: descending, K <= N, NaN as -inf.

namespace baseline_detail {

inline float nan_to_neg_inf(float x) {
    return std::isnan(x) ? -std::numeric_limits<float>::infinity() : x;
}

// Hand-rolled replace-top: pop_heap + push_heap would be 2 log K.
inline void sift_down_min(float* heap, size_t K) {
    size_t pos = 0;
    const float value = heap[0];
    for (;;) {
        size_t child = 2 * pos + 1;
        if (child >= K) break;
        if (child + 1 < K && heap[child + 1] < heap[child]) ++child;
        if (heap[child] >= value) break;
        heap[pos] = heap[child];
        pos = child;
    }
    heap[pos] = value;
}

}  // namespace baseline_detail

inline void topk_minheap_baseline(const float* input, size_t N, size_t K, float* output) {
    std::vector<float> heap(K);
    for (size_t i = 0; i < K; ++i) heap[i] = baseline_detail::nan_to_neg_inf(input[i]);
    std::make_heap(heap.begin(), heap.end(), std::greater<float>());

    for (size_t i = K; i < N; ++i) {
        const float x = input[i];
        if (x > heap[0]) {  // NaN fails the compare -> dropped
            heap[0] = x;
            baseline_detail::sift_down_min(heap.data(), K);
        }
    }

    std::sort(heap.begin(), heap.end(), std::greater<float>());
    std::copy(heap.begin(), heap.end(), output);
}

inline void topk_nth_element_baseline(const float* input, size_t N, size_t K, float* output) {
    // Input is const, so partition a copy. Reusing the buffer keeps page
    // faults out of the timing.
    static thread_local std::vector<float> scratch;
    scratch.resize(N);
    std::transform(input, input + N, scratch.begin(), baseline_detail::nan_to_neg_inf);

    std::nth_element(scratch.begin(), scratch.begin() + (K - 1), scratch.end(),
                     std::greater<float>());
    std::sort(scratch.begin(), scratch.begin() + K, std::greater<float>());
    std::copy(scratch.begin(), scratch.begin() + K, output);
}
