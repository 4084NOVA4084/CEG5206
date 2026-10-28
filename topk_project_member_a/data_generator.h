#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

// uniformly distributed in [0, 1)
std::vector<float> generate_uniform(size_t N, uint32_t seed = 5206);

// normally distributed
std::vector<float> generate_normal(size_t N, uint32_t seed = 5206);

// sorted in descending order
std::vector<float> generate_descending(size_t N);

// duplicates
std::vector<float> generate_duplicates(size_t N, uint32_t seed = 5206);

// NaN, infinity
std::vector<float> generate_boundary_values(size_t N, uint32_t seed = 5206);