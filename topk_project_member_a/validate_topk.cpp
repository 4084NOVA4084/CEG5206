#include "topk_baselines.h"
#include "topk_oracle.h"
#include "data_generator.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

using TopKFunction = void (*)(const float*, size_t, size_t, float*);

bool check_equal(std::vector<float>& expected, std::vector<float>& actual) {
    if (expected.size() != actual.size()) {
        return false;
    }

    for (size_t i = 0; i < expected.size(); ++i) {
        if (std::isnan(expected[i]) && std::isnan(actual[i])) {
            continue;
        }

        if (expected[i] != actual[i]) {
            return false;
        }
    }

    return true;
}

bool run_single(const std::string& test_name, const std::vector<float>& input, size_t K, TopKFunction algo) {
    size_t N = input.size();

    std::vector<float> expected(K);
    std::vector<float> actual(K);

    topk_oracle(input.data(), N, K, expected.data());

    algo(input.data(), N, K, actual.data());

    bool passed = check_equal(expected, actual);

    std::cout << (passed ? "PASS: " : "FAIL: ") << test_name 
        << " (N=" << N << ", K=" << K << ")" << "\n";

    if (!passed) {
        std::cout << "Expected: ";
        for (float value : expected) {
            std::cout << value << " ";
        }

        std::cout << "\nActual:   ";
        for (float value : actual) {
            std::cout << value << " ";
        }

        std::cout << "\n";
    }

    return passed;
}

bool run_all(const std::string& algo_name, TopKFunction algo) {
    int total = 0;
    int passed = 0;
    auto test = [&](const std::string& name, const std::vector<float>& input, size_t K) {
        total++;
        if (run_single(name, input, K, algo)) {
            passed++;
        }
    };

    test("uniform", generate_uniform(1000), 64);

    test("normal", generate_normal(1000), 64);

    test("descending", generate_descending(1024), 16);

    test("duplicates", generate_duplicates(1000), 64);

    test("identical", std::vector<float>(100, 3.14f), 8);

    test("boundary", generate_boundary_values(100),8);

    test("N == K", generate_uniform(64), 64);

    test("N < 2K", generate_uniform(100), 64);

    test("N not divisible by 8 (SIMD width)", generate_uniform(101), 8);

    test("large values", generate_uniform(32768), 256);

    test("negative values", 
        std::vector<float>{-5.0f, -2.0f, -9.0f, -1.0f,-7.0f, -3.0f, -8.0f, -4.0f}, 4);

    test("mixed values (positive and negative)", 
        std::vector<float>{-5.0f, 0.0f, 3.0f, -1.0f, 7.0f, 2.0f, -9.0f, 4.0f, 1.0f, 6.0f}, 4);

    std::cout << algo_name << ": " << passed << "/" << total << " tests passed\n\n";

    return passed == total;
}

int main() {
    bool all_passed = true;

    if (!run_all("min-heap", topk_minheap)) {
        all_passed = false;
    }

    if (!run_all("nth_element", topk_nth_element)) {
        all_passed = false;
    }

    if (all_passed) {
        std::cout << "VALIDATION PASSED\n";
    } else {
        std::cout << "VALIDATION FAILED\n";
    }

    return 0;
}
