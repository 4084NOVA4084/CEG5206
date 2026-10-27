#include <algorithm>
#include <vector>
#include <functional>
#include <cmath>
#include <limits>

void topk_oracle(const float* input, size_t N, size_t K, float* output) {
    std::vector<double> data(input, input + N); // Convert to fp64 double

    // Normalize NaNs
    for (double& value : data) {
        if (std::isnan(value)) {
            value = -std::numeric_limits<double>::infinity();
        }
    }

    std::sort(data.begin(), data.end(), std::greater<double>());

    // Retrieve top k elements
    std::copy_n(data.begin(), K, output);
}
