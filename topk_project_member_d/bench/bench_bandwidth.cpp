#include "bench_common.h"

#include <cstdio>
#include <string>
#include <vector>

// Streaming max = Top-1, the cheapest possible Top-K pass. Its GB/s is the
// ceiling for any Top-K at that working-set size (the roofline's memory roofs).
// Threads rescan their own slice inside one parallel region; otherwise
// fork/join would dominate the cache-sized runs.

namespace {

constexpr size_t kLanes = 32;  // 4 AVX2 / 8 NEON accumulators; 64 spilled on NEON

// Independent lanes need no reassociation, so this vectorises without -ffast-math.
float slice_max(const float* p, size_t n) {
    float acc[kLanes];
    for (size_t j = 0; j < kLanes; ++j) acc[j] = -std::numeric_limits<float>::infinity();
    size_t i = 0;
    for (; i + kLanes <= n; i += kLanes) {
        for (size_t j = 0; j < kLanes; ++j) acc[j] = p[i + j] > acc[j] ? p[i + j] : acc[j];
    }
    float m = -std::numeric_limits<float>::infinity();
    for (; i < n; ++i) m = p[i] > m ? p[i] : m;
    for (size_t j = 0; j < kLanes; ++j) m = acc[j] > m ? acc[j] : m;
    return m;
}

struct Options {
    std::vector<size_t> threads = {1, 0};
    size_t min_size = 16 << 10;
    size_t max_size = 512u << 20;
    double target_bytes = 2e9;  // bytes streamed per measurement
    int trials = 5;
    bool header = false;
};

}  // namespace

int main(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto value = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        try {
            if (a == "--threads") o.threads = bench::parse_size_list(value());
            else if (a == "--min-size") o.min_size = bench::parse_size(value());
            else if (a == "--max-size") o.max_size = bench::parse_size(value());
            else if (a == "--target-bytes") o.target_bytes = std::stod(value());
            else if (a == "--trials") o.trials = std::stoi(value());
            else if (a == "--csv-header") o.header = true;
            else throw std::invalid_argument("unknown option " + a);
        } catch (const std::exception& e) {
            std::fprintf(stderr,
                         "error: %s\nusage: bench_bandwidth [--threads 1,0] [--min-size 16K] "
                         "[--max-size 512M] [--target-bytes 2e9] [--trials 5] [--csv-header]\n",
                         e.what());
            return 2;
        }
    }

    const size_t max_elems = o.max_size / sizeof(float);
    bench::AlignedFloats data(max_elems);
    bench::fill_distribution(data.data(), max_elems, "uniform", 1);

    if (o.header) std::printf("kernel,threads,bytes,reps,best_ms,gbs,melem_s\n");

    for (size_t requested : o.threads) {
        const int P = requested == 0 ? bench::max_threads() : static_cast<int>(requested);
        for (size_t bytes = o.min_size; bytes <= o.max_size; bytes *= 2) {
            const size_t n = bytes / sizeof(float);
            const long long reps = std::max<long long>(
                1, static_cast<long long>(o.target_bytes / static_cast<double>(bytes)));
            double best = 1e30;
            for (int trial = 0; trial < o.trials; ++trial) {
                float result = -std::numeric_limits<float>::infinity();
                const double t0 = bench::now_seconds();
                #pragma omp parallel num_threads(P) reduction(max : result)
                {
                    int t = 0, nt = 1;
#ifdef _OPENMP
                    t = omp_get_thread_num();
                    nt = omp_get_num_threads();
#endif
                    const size_t begin = n * t / nt;
                    const size_t end = n * (t + 1) / nt;
                    const float* p = data.data() + begin;
                    float m = -std::numeric_limits<float>::infinity();
                    for (long long r = 0; r < reps; ++r) {
                        bench::do_not_optimize(p);  // else the invariant scan gets hoisted
                        const float s = slice_max(p, end - begin);
                        m = s > m ? s : m;
                    }
                    result = m > result ? m : result;
                }
                const double elapsed = bench::now_seconds() - t0;
                bench::do_not_optimize(result);
                best = std::min(best, elapsed);
            }
            const double total = static_cast<double>(bytes) * reps;
            std::printf("stream_max,%d,%zu,%lld,%.6f,%.3f,%.3f\n", P, bytes, reps, best * 1e3,
                        total / best / 1e9, total / sizeof(float) / best / 1e6);
            std::fflush(stdout);
        }
    }
    return 0;
}
