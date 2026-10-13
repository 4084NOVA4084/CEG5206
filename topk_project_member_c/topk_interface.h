#pragma once
#include <cstddef>

// Unified top-level API

void topk_selector(const float* input, size_t N, size_t K, float* output);

// Micro-kernel interface delivered by Member B:
// Merges an aligned 8-float vector chunk into a sorted Top-K buffer

void simd_bitonic_merge_8(float* topk_reservoir, size_t K, const float* new_block_8);