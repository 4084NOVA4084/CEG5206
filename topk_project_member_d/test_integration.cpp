#include "parallel_topk.h"
#include "bench/topk_replace_top.h"
#include "../topk_project_member_a/topk_baselines.h"
#include "../topk_project_member_a/topk_validation.h"
#include "../topk_project_member_c/cache_tiled_engine.h"

#include <algorithm>
#include <cstddef>
#include <iostream>
#include <string>
#include <vector>

#ifdef _OPENMP
#include <omp.h>
#endif

// Runs Member A's run_validation suite on every implementation in the project:
//   - the scalar baselines,
//   - the single-threaded tiled SIMD engine, through topk_selector and at several tile sizes,
//   - the OpenMP wrapper for every merge strategy and thread count, to catch races.

namespace {

const char* merge_name(MergeStrategy s) {
    switch (s) {
        case MergeStrategy::Sequential: return "sequential";
        case MergeStrategy::TwoLevel: return "two-level";
        case MergeStrategy::Tree: return "tree";
    }
    return "?";
}

}  // namespace

int main() {
    int failures = 0;
    int runs = 0;
    auto run = [&](const TopKFunction& fn, const std::string& name) {
        ++runs;
        if (!run_validation(fn, name, false)) ++failures;
    };

#if defined(__AVX2__)
    std::cout << "AVX2 path of Member B's kernel: enabled\n";
#else
    std::cout << "AVX2 path of Member B's kernel: disabled (portable fallback)\n";
#endif

    std::cout << "\n--- baselines\n";
    run(topk_minheap, "min-heap");
    run(topk_nth_element, "nth_element");
    run(topk_replace_top, "replace-top");

    std::cout << "\n--- tiled engine, single thread\n";
    run(topk_selector, "topk_selector");
    for (size_t B : {512, 1024, 2048, 4096}) {
        run([B](const float* in, size_t n, size_t k, float* out) { topk_tiled(in, n, k, out, B); },
            "tiled B=" + std::to_string(B));
    }

    int max_threads = 1;
#ifdef _OPENMP
    max_threads = omp_get_max_threads();
#endif
    std::vector<int> thread_counts = {1, 2, 4, 8, 16, 32};
    if (std::find(thread_counts.begin(), thread_counts.end(), max_threads) == thread_counts.end()) {
        thread_counts.push_back(max_threads);
    }

    std::cout << "\n--- OpenMP wrapper on the tiled engine\n";
    for (MergeStrategy merge : {MergeStrategy::Sequential, MergeStrategy::TwoLevel,
                                MergeStrategy::Tree}) {
        for (int P : thread_counts) {
            ParallelTopKConfig cfg;
            cfg.num_threads = P;
            cfg.merge = merge;
            cfg.min_elems_per_thread = 1;  // force P partitions on the suite's small inputs
            run([cfg](const float* in, size_t n, size_t k, float* out) {
                    topk_parallel(in, n, k, out, topk_selector, cfg);
                },
                std::string(merge_name(merge)) + " P=" + std::to_string(P));
        }
    }

    std::cout << "\n=== " << (runs - failures) << "/" << runs
              << " implementations passed run_validation ===\n";
    return failures == 0 ? 0 : 1;
}
