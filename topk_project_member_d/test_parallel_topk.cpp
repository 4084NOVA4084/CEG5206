#include "parallel_topk.h"
#include "../topk_project_member_a/topk_baselines.h"
#include "../topk_project_member_c/cache_tiled_engine.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>
#include <limits>
#include <numeric>
#include <random>
#include <string>
#include <vector>

#ifdef _OPENMP
#include <omp.h>
#endif

// Week-11 milestone: every kernel x merge strategy x P against an FP64
// oracle, run twice each so P- and rerun-determinism are checked too.

namespace {

const float kInf = std::numeric_limits<float>::infinity();
const float kNaN = std::numeric_limits<float>::quiet_NaN();

int g_checks = 0;
int g_failures = 0;

void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        if (g_failures <= 20) std::cout << "  [FAIL] " << what << "\n";
    }
}

// NaN -> -inf to match the kernels' policy.
std::vector<float> oracle_topk(const std::vector<float>& input, size_t K) {
    std::vector<double> tmp(input.size());
    for (size_t i = 0; i < input.size(); ++i) {
        tmp[i] = std::isnan(input[i]) ? -static_cast<double>(INFINITY)
                                      : static_cast<double>(input[i]);
    }
    std::sort(tmp.begin(), tmp.end(), std::greater<double>());
    std::vector<float> result(K, -kInf);
    for (size_t i = 0; i < std::min(K, tmp.size()); ++i) result[i] = static_cast<float>(tmp[i]);
    return result;
}

// Compare by value: which signed zero wins a tie is allowed to vary with P.
bool same_values(const std::vector<float>& a, const std::vector<float>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (!(a[i] == b[i])) return false;  // NaN != NaN, so a leaked NaN fails
    }
    return true;
}

std::string describe(const std::vector<float>& v, size_t max_items = 8) {
    std::string s;
    for (size_t i = 0; i < std::min(max_items, v.size()); ++i) s += std::to_string(v[i]) + " ";
    if (v.size() > max_items) s += "...";
    return s;
}

struct Dataset {
    std::string name;
    std::vector<float> data;
    std::vector<size_t> ks;
};

std::vector<Dataset> make_datasets() {
    std::mt19937 rng(5206);
    std::vector<Dataset> sets;

    auto uniform = [&](size_t n, float lo, float hi) {
        std::uniform_real_distribution<float> dist(lo, hi);
        std::vector<float> v(n);
        for (float& x : v) x = dist(rng);
        return v;
    };

    sets.push_back({"uniform N=100000", uniform(100000, 0.0f, 1.0f), {1, 8, 13, 64, 1024}});

    {
        std::normal_distribution<float> dist(0.0f, 1.0f);
        std::vector<float> v(65536 + 7);
        for (float& x : v) x = dist(rng);
        sets.push_back({"gaussian N=65543", v, {1, 16, 100, 512}});
    }
    {
        std::uniform_int_distribution<int> dist(0, 9);
        std::vector<float> v(50000);
        for (float& x : v) x = static_cast<float>(dist(rng));
        sets.push_back({"heavy duplicates (10 keys)", v, {1, 8, 64, 5000, 7000}});
    }
    sets.push_back({"all identical", std::vector<float>(4099, 3.14f), {1, 8, 64, 4099}});
    {
        std::vector<float> v = uniform(20000, -10.0f, 10.0f);
        for (size_t i = 0; i < v.size(); i += 97) v[i] = kInf;
        for (size_t i = 13; i < v.size(); i += 89) v[i] = -kInf;
        sets.push_back({"+/-inf mixed", v, {1, 8, 100, 206, 300}});
    }
    {
        std::vector<float> v = uniform(30000, -1.0f, 1.0f);
        for (size_t i = 0; i < v.size(); i += 10) v[i] = kNaN;
        sets.push_back({"10% NaN", v, {1, 8, 64, 27000}});
    }
    {
        std::vector<float> v(1000, kNaN);
        v[500] = 1.0f;
        v[3] = -kInf;
        sets.push_back({"almost all NaN (N=K)", v, {1, 2, 3, 1000}});
    }
    sets.push_back({"N=K=64", uniform(64, -5.0f, 5.0f), {64}});
    sets.push_back({"N=K=1000", uniform(1000, -5.0f, 5.0f), {1000}});
    sets.push_back({"N=1001 (not /8)", uniform(1001, 0.0f, 1.0f), {1, 8, 9, 1001}});
    sets.push_back({"N=99991 (prime)", uniform(99991, 0.0f, 1.0f), {7, 64, 999}});
    sets.push_back({"tiny N=5", {2.0f, -1.0f, 7.0f, 7.0f, 0.5f}, {1, 3, 5}});
    sets.push_back({"N < P*K (N=100)", uniform(100, 0.0f, 1.0f), {8, 64, 100}});
    {
        std::vector<float> v(40000);
        std::iota(v.begin(), v.end(), -20000.0f);
        sets.push_back({"ascending (adversarial)", v, {1, 8, 64, 1024}});
        std::reverse(v.begin(), v.end());
        sets.push_back({"descending", v, {1, 8, 64, 1024}});
    }
    {
        std::vector<float> v(4096);
        for (size_t i = 0; i < v.size(); ++i) v[i] = (i % 2) ? 0.0f : -0.0f;
        v[100] = -1.0f;
        v[200] = 1.0f;
        sets.push_back({"signed zeros", v, {1, 8, 64, 4095}});
    }
    {
        std::vector<float> v(8192);
        for (size_t i = 0; i < v.size(); ++i) {
            v[i] = std::numeric_limits<float>::denorm_min() * static_cast<float>(i % 300);
        }
        v[77] = FLT_MAX;
        v[78] = -FLT_MAX;
        v[79] = FLT_MIN;
        sets.push_back({"denormals + FLT_MAX", v, {1, 8, 64, 600}});
    }
    return sets;
}

struct NamedKernel {
    std::string name;
    topk_kernel_fn fn;
};

std::vector<NamedKernel> make_kernels() {
    return {
        {"tiled(B=1024)", topk_selector},
        {"tiled(B=512)", [](const float* in, size_t n, size_t k, float* out) {
             topk_tiled(in, n, k, out, 512);
         }},
        {"tiled(B=24)", [](const float* in, size_t n, size_t k, float* out) {
             topk_tiled(in, n, k, out, 24);
         }},
        {"minheap", topk_minheap},
        {"nth_element", topk_nth_element},
    };
}

const char* strategy_name(MergeStrategy s) {
    switch (s) {
        case MergeStrategy::Sequential: return "sequential";
        case MergeStrategy::TwoLevel: return "two-level";
        case MergeStrategy::Tree: return "tree";
    }
    return "?";
}

void test_merge_primitives() {
    std::cout << "--- merge primitives\n";
    std::mt19937 rng(7);
    std::uniform_int_distribution<int> small(0, 6);
    const int before = g_failures;

    auto random_list = [&](size_t n) {
        std::vector<float> v(n);
        for (float& x : v) {
            const int r = small(rng);
            x = (r == 0) ? kInf : (r == 1) ? -kInf : static_cast<float>(small(rng));
        }
        std::sort(v.begin(), v.end(), std::greater<float>());
        return v;
    };

    for (int trial = 0; trial < 2000; ++trial) {
        const size_t num_lists = trial % 12;
        std::vector<std::vector<float>> lists;
        std::vector<float> all;
        for (size_t l = 0; l < num_lists; ++l) {
            lists.push_back(random_list(small(rng) * 3));
            all.insert(all.end(), lists.back().begin(), lists.back().end());
        }
        std::sort(all.begin(), all.end(), std::greater<float>());
        const size_t K = 1 + trial % 25;
        const size_t expect_n = std::min(K, all.size());

        std::vector<const float*> ptrs;
        std::vector<size_t> lens;
        for (const auto& l : lists) {
            ptrs.push_back(l.data());
            lens.push_back(l.size());
        }
        std::vector<float> out(K, 123.0f);
        const size_t n = merge_topk_many(ptrs.data(), lens.data(), num_lists, K, out.data());
        out.resize(n);
        check(n == expect_n && same_values(out, std::vector<float>(all.begin(), all.begin() + expect_n)),
              "merge_topk_many trial " + std::to_string(trial));

        if (num_lists >= 2) {
            std::vector<float> two;
            two.insert(two.end(), lists[0].begin(), lists[0].end());
            two.insert(two.end(), lists[1].begin(), lists[1].end());
            std::sort(two.begin(), two.end(), std::greater<float>());
            const size_t m = std::min(K, two.size());
            std::vector<float> out2(K);
            const size_t n2 = merge_topk_two(lists[0].data(), lists[0].size(),
                                             lists[1].data(), lists[1].size(), K, out2.data());
            out2.resize(n2);
            check(n2 == m && same_values(out2, std::vector<float>(two.begin(), two.begin() + m)),
                  "merge_topk_two trial " + std::to_string(trial));
        }
    }
    std::cout << "    " << (g_failures == before ? "ok" : "FAILED") << "\n";
}

void test_partitions() {
    std::cout << "--- partition boundaries\n";
    const int before = g_failures;
    for (size_t N : {0UL, 1UL, 15UL, 16UL, 17UL, 100UL, 1001UL, 65536UL, 999983UL, 33554432UL}) {
        for (size_t P : {1UL, 2UL, 3UL, 4UL, 7UL, 8UL, 16UL, 64UL, 128UL}) {
            bool ok = partition_begin(N, P, 0) == 0 && partition_begin(N, P, P) == N;
            for (size_t t = 0; t < P; ++t) {
                const size_t b = partition_begin(N, P, t);
                const size_t e = partition_begin(N, P, t + 1);
                ok = ok && b <= e && (t == 0 || b % 16 == 0);
                // alignment may shift a boundary by at most one cache line
                ok = ok && (e - b) <= (N + P - 1) / P + 16;
            }
            check(ok, "partition N=" + std::to_string(N) + " P=" + std::to_string(P));
        }
    }
    std::cout << "    " << (g_failures == before ? "ok" : "FAILED") << "\n";
}

void test_end_to_end(const std::vector<int>& thread_counts) {
    const auto datasets = make_datasets();
    const auto kernels = make_kernels();
    const MergeStrategy strategies[] = {MergeStrategy::Sequential, MergeStrategy::TwoLevel,
                                        MergeStrategy::Tree};

    for (const Dataset& ds : datasets) {
        const int before = g_failures;
        const size_t N = ds.data.size();
        for (size_t K : ds.ks) {
            const std::vector<float> expected = oracle_topk(ds.data, K);
            for (const NamedKernel& kernel : kernels) {
                // Raw kernel first: the wrapper's canonicalize() would hide unsorted or NaN output.
                if (K <= N) {
                    std::vector<float> raw(K, 42.0f);
                    kernel.fn(ds.data.data(), N, K, raw.data());
                    check(same_values(raw, expected),
                          ds.name + " | K=" + std::to_string(K) + " | " + kernel.name + " | raw");
                }
                for (MergeStrategy strategy : strategies) {
                    for (int P : thread_counts) {
                        ParallelTopKConfig cfg;
                        cfg.num_threads = P;
                        cfg.merge = strategy;
                        cfg.min_elems_per_thread = 1;  // force P partitions even on tiny N

                        for (int rep = 0; rep < 2; ++rep) {
                            std::vector<float> out(K, 42.0f);
                            ParallelTopKStats stats;
                            topk_parallel(ds.data.data(), N, K, out.data(), kernel.fn, cfg, &stats);
                            const bool ok = same_values(out, expected);
                            const std::string tag = ds.name + " | K=" + std::to_string(K) +
                                                    " | " + kernel.name + " | " +
                                                    strategy_name(strategy) +
                                                    " | P=" + std::to_string(P) +
                                                    " (used " + std::to_string(stats.partitions) +
                                                    ") rep " + std::to_string(rep);
                            check(ok, tag);
                            if (!ok && g_failures <= 5) {
                                std::cout << "      expected: " << describe(expected) << "\n"
                                          << "      got:      " << describe(out) << "\n";
                            }
                        }
                    }
                }
            }
        }
        std::cout << "--- " << ds.name << ": " << (g_failures == before ? "ok" : "FAILED") << "\n";
    }

    // Only the wrapper handles K > N; the kernels assert K <= N.
    {
        const int before = g_failures;
        std::vector<float> small = {1.0f, kNaN, 3.0f, -2.0f, 5.0f};
        const std::vector<float> expected = oracle_topk(small, 9);
        for (int P : thread_counts) {
            ParallelTopKConfig cfg;
            cfg.num_threads = P;
            cfg.min_elems_per_thread = 1;
            std::vector<float> out(9, 42.0f);
            topk_parallel(small.data(), small.size(), 9, out.data(), topk_selector, cfg);
            check(same_values(out, expected), "K > N padding, P=" + std::to_string(P));
        }
        std::cout << "--- K > N padding: " << (g_failures == before ? "ok" : "FAILED") << "\n";
    }

    {
        const int before = g_failures;
        std::mt19937 rng(11);
        std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
        std::vector<float> big(3000017);
        for (float& x : big) x = dist(rng);
        for (size_t K : {1UL, 8UL, 64UL, 1024UL}) {
            std::vector<float> out(K);
            topk_selector_parallel(big.data(), big.size(), K, out.data());
            check(same_values(out, oracle_topk(big, K)),
                  "topk_selector_parallel N=3000017 K=" + std::to_string(K));
        }
        std::cout << "--- topk_selector_parallel (N=3M): "
                  << (g_failures == before ? "ok" : "FAILED") << "\n";
    }
}

}  // namespace

int main(int argc, char** argv) {
    int max_threads = 1;
#ifdef _OPENMP
    max_threads = omp_get_max_threads();
#endif
    std::vector<int> thread_counts = {1, 2, 3, 4, 5, 7, 8, 16, 32};
    if (std::find(thread_counts.begin(), thread_counts.end(), max_threads) == thread_counts.end()) {
        thread_counts.push_back(max_threads);
    }
    if (argc > 1 && std::strcmp(argv[1], "--quick") == 0) thread_counts = {1, 2, 4, 8};

    std::cout << "=== Member D: OpenMP Top-K validation ===\n";
#ifdef _OPENMP
    std::cout << "OpenMP enabled, max threads = " << max_threads << "\n";
#else
    std::cout << "OpenMP DISABLED: partitions run sequentially (logic still checked)\n";
#endif
#if defined(__AVX2__)
    std::cout << "AVX2 path of Member B's kernel: enabled\n";
#else
    std::cout << "AVX2 path of Member B's kernel: disabled (portable fallback)\n";
#endif
    std::cout << "Thread counts:";
    for (int p : thread_counts) std::cout << " " << p;
    std::cout << "\n\n";

    test_merge_primitives();
    test_partitions();
    test_end_to_end(thread_counts);

    std::cout << "\n=== Summary: " << (g_checks - g_failures) << "/" << g_checks
              << " checks passed ===\n";
    return g_failures == 0 ? 0 : 1;
}
