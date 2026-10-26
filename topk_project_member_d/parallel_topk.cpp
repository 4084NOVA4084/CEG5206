#include "parallel_topk.h"
#include "../topk_project_member_c/cache_tiled_engine.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

#ifdef _OPENMP
#include <omp.h>
#endif

namespace {

constexpr size_t kCacheLineBytes = 64;
constexpr size_t kCacheLineFloats = kCacheLineBytes / sizeof(float);  // 16

double now_seconds() {
    using clock = std::chrono::steady_clock;
    return std::chrono::duration<double>(clock::now().time_since_epoch()).count();
}

int default_thread_count() {
#ifdef _OPENMP
    return omp_get_max_threads();
#else
    return 1;
#endif
}

size_t round_up(size_t x, size_t multiple) {
    return (x + multiple - 1) / multiple * multiple;
}

float* align_to_cache_line(float* p) {
    const auto addr = reinterpret_cast<std::uintptr_t>(p);
    const auto aligned = (addr + kCacheLineBytes - 1) & ~std::uintptr_t(kCacheLineBytes - 1);
    return reinterpret_cast<float*>(aligned);
}

// One cache line per thread: neighbours writing len/cur would false-share.
struct alignas(kCacheLineBytes) Slot {
    float* cur = nullptr;
    float* alt = nullptr;  // merge target, swapped with cur
    size_t len = 0;
};

// Merge precondition. Kernels (e.g. Member A's) aren't guaranteed to
// return sorted or NaN-free lists; costs O(K) when they already do.
void canonicalize(float* values, size_t n) {
    const float neg_inf = -std::numeric_limits<float>::infinity();
    for (size_t i = 0; i < n; ++i) {
        if (std::isnan(values[i])) values[i] = neg_inf;
    }
    if (!std::is_sorted(values, values + n, std::greater<float>())) {
        std::sort(values, values + n, std::greater<float>());
    }
}

size_t gather_lists(const std::vector<Slot>& slots, size_t first, size_t last,
                    size_t stride, std::vector<const float*>& lists,
                    std::vector<size_t>& lengths) {
    lists.clear();
    lengths.clear();
    for (size_t t = first; t < last; t += stride) {
        lists.push_back(slots[t].cur);
        lengths.push_back(slots[t].len);
    }
    return lists.size();
}

}  // namespace

// floor(N * t / P) without overflowing N * t, rounded down to a cache
// line so no two partitions share one.
size_t partition_begin(size_t N, size_t P, size_t t) {
    if (t >= P) return N;
    const size_t exact = (N / P) * t + (N % P) * t / P;
    return exact & ~(kCacheLineFloats - 1);
}

size_t merge_topk_two(
    const float* a, size_t na,
    const float* b, size_t nb,
    size_t K, float* out
) {
    const size_t m = std::min(K, na + nb);
    size_t i = 0;
    size_t j = 0;
    for (size_t o = 0; o < m; ++o) {
        const bool take_a = (j >= nb) || (i < na && a[i] >= b[j]);
        out[o] = take_a ? a[i] : b[j];
        i += take_a;
        j += !take_a;
    }
    return m;
}

size_t merge_topk_many(
    const float* const* lists,
    const size_t* lengths,
    size_t num_lists,
    size_t K,
    float* out
) {
    if (num_lists == 0) return 0;
    if (num_lists == 1) {
        const size_t m = std::min(K, lengths[0]);
        std::copy(lists[0], lists[0] + m, out);
        return m;
    }
    if (num_lists == 2) {
        return merge_topk_two(lists[0], lengths[0], lists[1], lengths[1], K, out);
    }

    struct Head {
        float value;
        size_t list;
        size_t pos;
    };
    // Tie-break on list index keeps the result reproducible.
    const auto lower_priority = [](const Head& x, const Head& y) {
        return x.value < y.value || (x.value == y.value && x.list > y.list);
    };

    std::vector<Head> heap;
    heap.reserve(num_lists);
    for (size_t l = 0; l < num_lists; ++l) {
        if (lengths[l] > 0) heap.push_back({lists[l][0], l, 0});
    }
    std::make_heap(heap.begin(), heap.end(), lower_priority);

    size_t written = 0;
    while (written < K && !heap.empty()) {
        std::pop_heap(heap.begin(), heap.end(), lower_priority);
        Head& head = heap.back();
        out[written++] = head.value;
        if (++head.pos < lengths[head.list]) {
            head.value = lists[head.list][head.pos];
            std::push_heap(heap.begin(), heap.end(), lower_priority);
        } else {
            heap.pop_back();
        }
    }
    return written;
}

// Without OpenMP the pragmas vanish and the same partitions/merges run
// serially, so results don't depend on whether OpenMP is on.
void topk_parallel(
    const float* input,
    size_t N,
    size_t K,
    float* output,
    const topk_kernel_fn& local_kernel,
    const ParallelTopKConfig& config,
    ParallelTopKStats* stats
) {
    const double t_start = now_seconds();
    const float neg_inf = -std::numeric_limits<float>::infinity();
    ParallelTopKStats run_stats;

    if (K == 0) {
        if (stats) *stats = run_stats;
        return;
    }
    const size_t K_valid = std::min(K, N);
    if (K_valid == 0) {
        std::fill(output, output + K, neg_inf);
        if (stats) *stats = run_stats;
        return;
    }

    size_t P = config.num_threads > 0 ? static_cast<size_t>(config.num_threads)
                                      : static_cast<size_t>(default_thread_count());
    const size_t min_elems = std::max<size_t>(1, config.min_elems_per_thread);
    P = std::max<size_t>(1, std::min(P, N / min_elems));
    run_stats.partitions = static_cast<int>(P);

    if (P == 1) {
        local_kernel(input, N, K_valid, output);
        canonicalize(output, K_valid);
        std::fill(output + K_valid, output + K, neg_inf);
        run_stats.local_seconds = now_seconds() - t_start;
        if (stats) *stats = run_stats;
        return;
    }

    // Left uninitialised on purpose: each thread first-touches its own
    // slot, which keeps the pages on its NUMA node.
    const size_t capacity = round_up(K_valid, kCacheLineFloats);
    std::unique_ptr<float[]> raw(new float[2 * P * capacity + kCacheLineFloats]);
    float* workspace = align_to_cache_line(raw.get());

    std::vector<Slot> slots(P);
    for (size_t t = 0; t < P; ++t) {
        slots[t].cur = workspace + (2 * t) * capacity;
        slots[t].alt = workspace + (2 * t + 1) * capacity;
    }

    const MergeStrategy strategy = config.merge;
    const size_t group = static_cast<size_t>(std::ceil(std::sqrt(static_cast<double>(P))));
    const long long num_parts = static_cast<long long>(P);
    double t_local_done = t_start;

    #pragma omp parallel num_threads(static_cast<int>(P))
    {
        // static,1 pins partition t to thread t; the tree merge relies on
        // that to merge lists already sitting in the merging thread's cache.
        #pragma omp for schedule(static, 1)
        for (long long t = 0; t < num_parts; ++t) {
            const size_t begin = partition_begin(N, P, static_cast<size_t>(t));
            const size_t end = partition_begin(N, P, static_cast<size_t>(t) + 1);
            Slot& slot = slots[t];
            slot.len = std::min(K_valid, end - begin);
            if (slot.len > 0) {
                local_kernel(input + begin, end - begin, slot.len, slot.cur);
                canonicalize(slot.cur, slot.len);
            }
        }

        #pragma omp master
        t_local_done = now_seconds();

        if (strategy == MergeStrategy::Tree) {
            for (size_t stride = 1; stride < P; stride *= 2) {
                #pragma omp for schedule(static, 1)
                for (long long t = 0; t < num_parts; ++t) {
                    const size_t ut = static_cast<size_t>(t);
                    if (ut % (2 * stride) != 0 || ut + stride >= P) continue;
                    Slot& dst = slots[ut];
                    const Slot& src = slots[ut + stride];
                    dst.len = merge_topk_two(dst.cur, dst.len, src.cur, src.len,
                                             K_valid, dst.alt);
                    std::swap(dst.cur, dst.alt);
                }
            }
        } else if (strategy == MergeStrategy::TwoLevel) {
            std::vector<const float*> lists;
            std::vector<size_t> lengths;
            #pragma omp for schedule(static, 1)
            for (long long t = 0; t < num_parts; ++t) {
                const size_t ut = static_cast<size_t>(t);
                if (ut % group != 0) continue;
                const size_t count = gather_lists(slots, ut, std::min(P, ut + group), 1,
                                                  lists, lengths);
                Slot& leader = slots[ut];
                leader.len = merge_topk_many(lists.data(), lengths.data(), count,
                                             K_valid, leader.alt);
                std::swap(leader.cur, leader.alt);
            }
        }
    }

    size_t written = 0;
    if (strategy == MergeStrategy::Tree) {
        written = slots[0].len;
        std::copy(slots[0].cur, slots[0].cur + written, output);
    } else {
        const size_t stride = (strategy == MergeStrategy::TwoLevel) ? group : 1;
        std::vector<const float*> lists;
        std::vector<size_t> lengths;
        const size_t count = gather_lists(slots, 0, P, stride, lists, lengths);
        written = merge_topk_many(lists.data(), lengths.data(), count, K_valid, output);
    }
    std::fill(output + written, output + K, neg_inf);

    run_stats.local_seconds = t_local_done - t_start;
    run_stats.merge_seconds = now_seconds() - t_local_done;
    if (stats) *stats = run_stats;
}

void topk_selector_parallel(const float* input, size_t N, size_t K, float* output) {
    topk_parallel(input, N, K, output, topk_selector);
}
