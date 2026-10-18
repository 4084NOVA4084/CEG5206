#include "simd_bitonic_kernel.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
#include <random>
#include <vector>

static std::vector<float> reference_topk(
    const std::vector<float>& input,
    std::size_t K
) {
    std::vector<float> values = input;
    const float neg_inf = -std::numeric_limits<float>::infinity();
    for (float& value : values) {
        if (std::isnan(value)) value = neg_inf;
    }
    std::sort(values.begin(), values.end(), std::greater<float>());
    values.resize(K);
    return values;
}

static void test_merge(std::size_t K, std::mt19937& rng) {
    std::uniform_real_distribution<float> distribution(-1000.0f, 1000.0f);
    std::vector<float> input(4096);
    for (float& value : input) value = distribution(rng);
    input[17] = std::numeric_limits<float>::infinity();
    input[31] = -std::numeric_limits<float>::infinity();
    input[47] = std::numeric_limits<float>::denorm_min();
    input[63] = std::numeric_limits<float>::quiet_NaN();

    std::vector<float> reservoir(K, -std::numeric_limits<float>::infinity());
    for (std::size_t offset = 0; offset < input.size(); offset += 8) {
        simd_bitonic_merge_8(reservoir.data(), K, input.data() + offset);
    }

    std::vector<float> expected = reference_topk(input, K);
    std::vector<float> actual(reservoir.rbegin(), reservoir.rend());
    assert(actual == expected);
}

int main() {
    std::mt19937 rng(5206);
    for (std::size_t K : {8UL, 16UL, 32UL, 64UL, 128UL, 256UL, 512UL, 1024UL}) {
        test_merge(K, rng);
    }
    std::cout << "Member B SIMD kernel tests passed\n";
}
