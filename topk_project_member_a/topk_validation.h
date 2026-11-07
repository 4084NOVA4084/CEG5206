#pragma once

#include <cstddef>
#include <functional>
#include <string>

// Any implementation with the frozen topk_selector signature: a plain function,
// or a lambda that captures a tile size or thread count.
using TopKFunction = std::function<void(const float*, size_t, size_t, float*)>;

// Runs the edge-case suite against the FP64 oracle. Returns true if every test
// passes. Failures are always printed; PASS lines only when verbose is true.
bool run_validation(
    const TopKFunction& candidate_func, const std::string& name = "candidate", bool verbose = true);
