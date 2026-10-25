#pragma once

#include <cstddef>

void topk_minheap(const float* input, size_t N, size_t K, float* output);
void topk_nth_element(const float* input, size_t N, size_t K, float* output);
