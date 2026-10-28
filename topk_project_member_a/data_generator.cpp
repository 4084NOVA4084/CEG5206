#include "data_generator.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <random>


std::vector<float> generate_uniform(size_t N, uint32_t seed) {
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    std::vector<float> data(N);

    for (float& value : data) {
        value = dist(rng);
    }

    return data;
}

std::vector<float> generate_normal(size_t N, uint32_t seed) {
    std::mt19937 rng(seed);
    std::normal_distribution<float> dist(0.0f, 1.0f); // mean=0, sd=1
    std::vector<float> data(N);

    for (float& value : data) {
        value = dist(rng);
    }

    return data;
}

std::vector<float> generate_descending(size_t N) {
    std::vector<float> data(N);

    for (size_t i = 0; i < N; i++) {
        data[i] = static_cast<float>(N - i);
    }

    return data;
}

std::vector<float> generate_duplicates(size_t N, uint32_t seed) {
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);

    std::vector<float> data(N, 0.5f); // all identical

    size_t random_count = N / 10; // replace 10% with random values
    for (size_t i = 0; i < random_count; ++i) {
        data[i] = dist(rng);
    }

    std::shuffle(data.begin(), data.end(), rng);

    return data;
}

std::vector<float> generate_boundary_values(size_t N, uint32_t seed) {
    std::vector<float> data = generate_uniform(N, seed);

    if (N > 0) {
        data[0] = std::numeric_limits<float>::infinity();
    }

    if (N > 1) {
        data[1] = -std::numeric_limits<float>::infinity();
    }

    if (N > 2) {
        data[2] = std::numeric_limits<float>::quiet_NaN();
    }

    if (N > 3) {
        data[3] = 0.0f;
    }

    if (N > 4) {
        data[4] = -0.0f;
    }

    if (N > 5) {
        data[5] = std::numeric_limits<float>::denorm_min();
    }

    return data;
}
