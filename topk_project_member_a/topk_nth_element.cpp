#include "topk_baselines.h"

#include <algorithm>
#include <vector>
#include <functional>
#include <cmath>
#include <limits>

void topk_nth_element(const float* input, size_t N, size_t K, float* output) {
    if (K == 0 || K > N) return;

    std::vector<float> data(input, input + N);

    // normalize nan values
    for (float& value : data) {
        if (std::isnan(value)) {
            value = -std::numeric_limits<float>::infinity();
        }
    }

    // partition
    if (K < N) {
        std::nth_element(data.begin(), data.begin() + K, data.end(), std::greater<float>());
    }

    // sort first K elements in descending order
    std::sort(data.begin(), data.begin() + K, std::greater<float>());

    // copy to output
    std::copy_n(data.begin(), K, output);
}