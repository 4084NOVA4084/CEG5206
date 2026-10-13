#include "cache_tiled_engine.h"

#include <iostream>
#include <vector>
#include <algorithm>
#include <numeric>
#include <random>
#include <cassert>
#include <cmath>
#include <limits>
#include <chrono>
#include <iomanip>

// ============================================================
// Oracle: reference Top-K using full sort (FP64)
// Returns top-K values sorted descending
// ============================================================
std::vector<float> oracle_topk(const std::vector<float>& input, size_t K) {
    std::vector<double> tmp(input.begin(), input.end());
    std::sort(tmp.begin(), tmp.end(), std::greater<double>());
    std::vector<float> result(K);
    for (size_t i = 0; i < K; ++i) result[i] = (float)tmp[i];
    return result;
}

// ============================================================
// Comparison helper: check two Top-K result sets are equal
// ============================================================
bool results_match(std::vector<float>& a, std::vector<float>& b) {
    if (a.size() != b.size()) return false;
    std::sort(a.begin(), a.end());
    std::sort(b.begin(), b.end());
    for (size_t i = 0; i < a.size(); ++i) {
        // Handle NaN
        if (std::isnan(a[i]) && std::isnan(b[i])) continue;
        if (a[i] != b[i]) return false;
    }
    return true;
}

// ============================================================
// Test runner macro
// ============================================================
static int test_count = 0;
static int pass_count = 0;

void run_test(const std::string& name,
              const std::vector<float>& input,
              size_t K,
              size_t tile_size = 1024)
{
    ++test_count;
    size_t N = input.size();

    std::vector<float> output(K);
    topk_tiled(input.data(), N, K, output.data(), tile_size);

    std::vector<float> expected = oracle_topk(input, K);

    std::vector<float> out_copy = output;
    bool ok = results_match(out_copy, expected);

    std::cout << "[" << (ok ? "PASS" : "FAIL") << "] " << name
              << " (N=" << N << ", K=" << K << ", tile=" << tile_size << ")\n";
    if (!ok) {
        std::cout << "  Expected: ";
        for (float v : expected) std::cout << v << " ";
        std::cout << "\n  Got:      ";
        for (float v : output)   std::cout << v << " ";
        std::cout << "\n";
    } else {
        ++pass_count;
    }
}

// ============================================================
// Main: all test cases
// ============================================================
int main() {
    std::mt19937 rng(42);

    std::cout << "=== Member C: Cache-Tiled Engine Tests ===\n\n";

    // ----------------------------------------------------------
    // 1. Basic correctness: small uniform random array
    // ----------------------------------------------------------
    {
        std::uniform_real_distribution<float> dist(0.0f, 1.0f);
        std::vector<float> arr(100);
        for (auto& x : arr) x = dist(rng);
        run_test("Small uniform random (N=100, K=10)", arr, 10);
    }

    // ----------------------------------------------------------
    // 2. Boundary: N == K (all elements are Top-K)
    // ----------------------------------------------------------
    {
        std::vector<float> arr = {3.0f, 1.0f, 4.0f, 1.0f, 5.0f, 9.0f, 2.0f, 6.0f};
        run_test("Boundary N=K=8", arr, 8);
    }

    // ----------------------------------------------------------
    // 3. Boundary: K = 1 (single maximum)
    // ----------------------------------------------------------
    {
        std::vector<float> arr(256);
        std::iota(arr.begin(), arr.end(), 0.0f);
        std::shuffle(arr.begin(), arr.end(), rng);
        run_test("K=1 single max (N=256)", arr, 1);
    }

    // ----------------------------------------------------------
    // 4. All identical elements
    // ----------------------------------------------------------
    {
        std::vector<float> arr(200, 3.14f);
        run_test("All identical (N=200, K=10)", arr, 10);
    }

    // ----------------------------------------------------------
    // 5. Reverse sorted
    // ----------------------------------------------------------
    {
        std::vector<float> arr(256);
        std::iota(arr.begin(), arr.end(), 0.0f);
        std::sort(arr.begin(), arr.end(), std::greater<float>());
        run_test("Reverse sorted (N=256, K=16)", arr, 16);
    }

    // ----------------------------------------------------------
    // 6. Array length NOT divisible by 8
    // ----------------------------------------------------------
    {
        std::uniform_real_distribution<float> dist(-100.0f, 100.0f);
        std::vector<float> arr(101);   // 101 = 12*8 + 5
        for (auto& x : arr) x = dist(rng);
        run_test("N not divisible by 8 (N=101, K=8)", arr, 8);
    }

    // ----------------------------------------------------------
    // 7. Array length NOT divisible by tile size
    // ----------------------------------------------------------
    {
        std::uniform_real_distribution<float> dist(0.0f, 1.0f);
        std::vector<float> arr(1337);  // not divisible by 1024
        for (auto& x : arr) x = dist(rng);
        run_test("N not divisible by tile (N=1337, K=20, tile=1024)", arr, 20, 1024);
    }

    // ----------------------------------------------------------
    // 8. Large array (L2-resident scale)
    // ----------------------------------------------------------
    {
        std::uniform_real_distribution<float> dist(0.0f, 1.0f);
        std::vector<float> arr(32768);  // 32K = 128KB → LLC boundary
        for (auto& x : arr) x = dist(rng);
        run_test("Large L2-scale (N=32K, K=64, tile=512)", arr, 64, 512);
        run_test("Large L2-scale (N=32K, K=64, tile=1024)", arr, 64, 1024);
        run_test("Large L2-scale (N=32K, K=64, tile=4096)", arr, 64, 4096);
    }

    // ----------------------------------------------------------
    // 9. Tile parameter sweep (verify correctness across B values)
    // ----------------------------------------------------------
    {
        std::uniform_real_distribution<float> dist(0.0f, 100.0f);
        std::vector<float> arr(5000);
        for (auto& x : arr) x = dist(rng);
        for (size_t tile : {8UL, 16UL, 64UL, 128UL, 512UL, 1024UL, 2048UL, 4096UL}) {
            run_test("Tile sweep (N=5000, K=32, tile=" + std::to_string(tile) + ")",
                     arr, 32, tile);
        }
    }

    // ----------------------------------------------------------
    // 10. Negative values
    // ----------------------------------------------------------
    {
        std::uniform_real_distribution<float> dist(-1000.0f, -1.0f);
        std::vector<float> arr(300);
        for (auto& x : arr) x = dist(rng);
        run_test("All negative (N=300, K=15)", arr, 15);
    }

    // ----------------------------------------------------------
    // 11. Mixed positive/negative/zero
    // ----------------------------------------------------------
    {
        std::vector<float> arr = {-5.0f, 0.0f, 3.0f, -1.0f, 7.0f,
                                   2.0f, -9.0f, 4.0f, 1.0f, 6.0f};
        run_test("Mixed +/-/0 (N=10, K=4)", arr, 4);
    }

    // ----------------------------------------------------------
    // 12. Throughput benchmark
    // ----------------------------------------------------------
    {
        std::cout << "\n=== Throughput Benchmark (tile sweep) ===\n";
        std::cout << std::setw(10) << "TileSize"
                  << std::setw(15) << "Throughput(MElem/s)"
                  << std::setw(15) << "TimeMs" << "\n";

        std::uniform_real_distribution<float> dist(0.0f, 1.0f);
        const size_t N = 1 << 20;  // 1M elements
        const size_t K = 64;
        std::vector<float> arr(N);
        for (auto& x : arr) x = dist(rng);
        std::vector<float> out(K);

        for (size_t tile : {256UL, 512UL, 1024UL, 2048UL, 4096UL}) {
            // Warmup
            topk_tiled(arr.data(), N, K, out.data(), tile);

            auto t0 = std::chrono::high_resolution_clock::now();
            constexpr int REPS = 20;
            for (int r = 0; r < REPS; ++r) {
                topk_tiled(arr.data(), N, K, out.data(), tile);
            }
            auto t1 = std::chrono::high_resolution_clock::now();

            double ms = std::chrono::duration<double, std::milli>(t1 - t0).count() / REPS;
            double meps = (double)N / (ms * 1e3);  // MElem/s

            std::cout << std::setw(10) << tile
                      << std::setw(15) << std::fixed << std::setprecision(1) << meps
                      << std::setw(15) << std::fixed << std::setprecision(3) << ms
                      << "\n";
        }
    }

    // ----------------------------------------------------------
    // Summary
    // ----------------------------------------------------------
    std::cout << "\n=== Summary: " << pass_count << "/" << test_count
              << " tests passed ===\n";

    return (pass_count == test_count) ? 0 : 1;
}