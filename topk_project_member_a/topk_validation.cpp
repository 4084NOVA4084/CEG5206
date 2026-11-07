#include "topk_validation.h"
#include "topk_oracle.h"
#include "data_generator.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <vector>

namespace {

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

bool run_single(const std::string& test_name, const std::vector<float>& input, size_t K,
                const TopKFunction& algo, bool verbose) {
    size_t N = input.size();

    std::vector<float> expected(K);
    std::vector<float> actual(K);

    topk_oracle(input.data(), N, K, expected.data());

    algo(input.data(), N, K, actual.data());

    bool passed = check_equal(expected, actual);

    if (verbose || !passed) {
        std::cout << (passed ? "PASS: " : "FAIL: ") << test_name
            << " (N=" << N << ", K=" << K << ")" << "\n";
    }

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

}  // namespace

bool run_validation(const TopKFunction& candidate_func, const std::string& name, bool verbose) {
    const float inf = std::numeric_limits<float>::infinity();
    const float nan = std::numeric_limits<float>::quiet_NaN();

    int total = 0;
    int passed = 0;
    auto test = [&](const std::string& test_name, const std::vector<float>& input, size_t K) {
        total++;
        if (run_single(test_name, input, K, candidate_func, verbose)) {
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

    // K = N forces the NaN and -inf entries into the output, where a plain
    // K = 8 run on the same data would never look at them.
    test("boundary, N == K", generate_boundary_values(64), 64);

    test("boundary, N == K not divisible by 8", generate_boundary_values(101), 101);

    test("N == K not divisible by 8", generate_uniform(13), 13);

    test("K not divisible by 8", generate_uniform(1000), 13);

    test("K = 1", generate_uniform(1000), 1);

    test("N = 1", std::vector<float>{2.5f}, 1);

    test("all NaN", std::vector<float>(37, nan), 8);

    test("all +inf", std::vector<float>(37, inf), 8);

    test("all -inf", std::vector<float>(37, -inf), 8);

    // A NaN at the front of a block must not hide the lanes behind it.
    test("NaN leads the tail block",
        std::vector<float>{0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, nan, 5.0f}, 1);

    test("NaN leads a full block",
        std::vector<float>{nan, 9.0f, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f}, 2);

    {
        // Ascending input raises the threshold on every block.
        std::vector<float> ascending = generate_descending(4099);
        std::reverse(ascending.begin(), ascending.end());
        test("ascending (filter worst case)", ascending, 64);
    }

    std::cout << name << ": " << passed << "/" << total << " tests passed\n";
    if (verbose) {
        std::cout << "\n";
    }

    return passed == total;
}
