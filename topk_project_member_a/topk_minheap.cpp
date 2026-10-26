#include "topk_baselines.h"

#include <queue>
#include <vector>
#include <functional>
#include <cmath>
#include <limits>

void topk_minheap(const float* input, size_t N, size_t K, float* output) {
    if (K == 0 || K > N) return;

    std::priority_queue<float, std::vector<float>, std::greater<float>> min_heap;

    // insert first K elements
    for (size_t i = 0; i < K; ++i) {
        float value = std::isnan(input[i]) ? -std::numeric_limits<float>::infinity() : input[i];
        min_heap.push(value);
    }

    for (size_t i = K; i < N; ++i) {
        float value = std::isnan(input[i]) ? -std::numeric_limits<float>::infinity() : input[i];
        if (value > min_heap.top()) {
            min_heap.pop();
            min_heap.push(value);
        }
    }

    // extract K largest values in descending order
    for (size_t i = K; i > 0; --i) {
        output[i - 1] = min_heap.top();
        min_heap.pop();
    }
}