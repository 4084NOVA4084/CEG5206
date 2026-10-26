#include "bench_common.h"
#include "perf_control.h"
#include "../parallel_topk.h"
#include "../reference_baselines.h"
#include "../../topk_project_member_c/cache_tiled_engine.h"

#include <cstdio>
#include <functional>
#include <string>
#include <vector>

// One dataset per process (generation is the slow part); list options expand
// to a cartesian product, one CSV row each:
//   bench_topk --n 32M --algo heap,tiled,omp_tiled --k 8,64,1024 --threads 1,2,4,8
// tiled = Member C's engine on B's kernel; omp_* = the wrapper around a kernel.
// omp_heap is there to show thread scaling with no SIMD in the picture.

namespace {

struct Options {
    size_t n = 1 << 24;
    std::string dist = "uniform";
    uint64_t seed = 5206;
    std::vector<std::string> algos = {"omp_tiled"};
    std::vector<size_t> ks = {64};
    std::vector<size_t> threads = {0};  // 0 = all
    std::vector<size_t> tiles = {1024};
    std::vector<std::string> merges = {"seq"};
    size_t min_chunk = 32768;
    int reps = 10;
    int warmup = 2;
    bool verify = false;
    bool header = false;
    size_t flush_mb = 0;  // >0: cold-cache runs
    std::string perf_ctl, perf_ack;
};

void usage() {
    std::fprintf(stderr,
        "usage: bench_topk [--n N] [--dist uniform|gaussian|ascending|descending|duplicates|special]\n"
        "                  [--algo a,b,...] [--k k1,k2,...] [--threads p1,p2,...] [--tile b1,...]\n"
        "                  [--merge seq,two,tree] [--min-chunk M] [--reps R] [--warmup W]\n"
        "                  [--seed S] [--verify] [--csv-header] [--flush-mb MB]\n"
        "                  [--perf-ctl FIFO --perf-ack FIFO]\n"
        "sizes accept K/M/G suffixes; --threads 0 means all OpenMP threads\n");
}

Options parse_args(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto value = [&]() -> std::string {
            if (i + 1 >= argc) throw std::invalid_argument("missing value for " + a);
            return argv[++i];
        };
        if (a == "--n") o.n = bench::parse_size(value());
        else if (a == "--dist") o.dist = value();
        else if (a == "--seed") o.seed = std::stoull(value());
        else if (a == "--algo") o.algos = bench::split_list(value());
        else if (a == "--k") o.ks = bench::parse_size_list(value());
        else if (a == "--threads") o.threads = bench::parse_size_list(value());
        else if (a == "--tile") o.tiles = bench::parse_size_list(value());
        else if (a == "--merge") o.merges = bench::split_list(value());
        else if (a == "--min-chunk") o.min_chunk = bench::parse_size(value());
        else if (a == "--reps") o.reps = std::stoi(value());
        else if (a == "--warmup") o.warmup = std::stoi(value());
        else if (a == "--verify") o.verify = true;
        else if (a == "--csv-header") o.header = true;
        else if (a == "--flush-mb") o.flush_mb = bench::parse_size(value());
        else if (a == "--perf-ctl") o.perf_ctl = value();
        else if (a == "--perf-ack") o.perf_ack = value();
        else if (a == "--help" || a == "-h") { usage(); std::exit(0); }
        else throw std::invalid_argument("unknown option " + a);
    }
    return o;
}

MergeStrategy parse_merge(const std::string& m) {
    if (m == "seq" || m == "sequential") return MergeStrategy::Sequential;
    if (m == "two" || m == "twolevel" || m == "two-level") return MergeStrategy::TwoLevel;
    if (m == "tree") return MergeStrategy::Tree;
    throw std::invalid_argument("unknown merge strategy " + m);
}

bool is_parallel(const std::string& algo) { return algo.rfind("omp_", 0) == 0; }
bool uses_tile(const std::string& algo) { return algo == "tiled" || algo == "omp_tiled"; }

topk_kernel_fn single_thread_kernel(const std::string& algo, size_t tile) {
    if (algo == "heap" || algo == "omp_heap") return topk_minheap_baseline;
    if (algo == "nth" || algo == "omp_nth") return topk_nth_element_baseline;
    if (algo == "tiled" || algo == "omp_tiled") {
        return [tile](const float* in, size_t n, size_t k, float* out) {
            topk_tiled(in, n, k, out, tile);
        };
    }
    throw std::invalid_argument("unknown algorithm " + algo);
}

// Deliberately a different algorithm from every candidate.
std::vector<float> reference_topk(const float* data, size_t N, size_t K) {
    const auto key = [](float x) {
        return std::isnan(x) ? -std::numeric_limits<float>::infinity() : x;
    };
    std::vector<float> out(K);
    std::partial_sort_copy(data, data + N, out.begin(), out.end(),
                           [&](float a, float b) { return key(a) > key(b); });
    for (float& x : out) x = key(x);
    return out;
}

void flush_caches(std::vector<char>& buf) {
    for (size_t i = 0; i < buf.size(); i += 64) buf[i] = static_cast<char>(buf[i] + 1);
    bench::clobber_memory();
}

struct Config {
    std::string algo;
    size_t k, threads, tile;
    std::string merge;
};

}  // namespace

int main(int argc, char** argv) {
    Options opt;
    try {
        opt = parse_args(argc, argv);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        usage();
        return 2;
    }

    // stderr: stdout must stay pure CSV for the harness
    std::fprintf(stderr, "# bench_topk: omp_max_threads=%d avx2=%d n=%zu dist=%s\n",
                 bench::max_threads(),
#if defined(__AVX2__)
                 1,
#else
                 0,
#endif
                 opt.n, opt.dist.c_str());

    bench::AlignedFloats data(opt.n);
    try {
        bench::fill_distribution(data.data(), opt.n, opt.dist, opt.seed);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 2;
    }

    bench::PerfControl perf;
    if (!perf.open_fifos(opt.perf_ctl, opt.perf_ack)) return 2;
    std::vector<char> flush_buf(opt.flush_mb << 20);

    std::vector<Config> configs;
    for (const auto& algo : opt.algos) {
        for (size_t k : opt.ks) {
            const std::vector<size_t> thread_list =
                is_parallel(algo) ? opt.threads : std::vector<size_t>{1};
            const std::vector<size_t> tile_list =
                uses_tile(algo) ? opt.tiles : std::vector<size_t>{0};
            const std::vector<std::string> merge_list =
                is_parallel(algo) ? opt.merges : std::vector<std::string>{"none"};
            for (size_t p : thread_list)
                for (size_t b : tile_list)
                    for (const auto& m : merge_list) configs.push_back({algo, k, p, b, m});
        }
    }

    if (opt.header) {
        std::printf("algo,dist,n,k,threads,partitions,tile,merge,min_chunk,reps,"
                    "median_ms,min_ms,mean_ms,stdev_ms,melem_s,gbs,local_ms,merge_ms,verified\n");
    }

    int exit_code = 0;
    for (const Config& c : configs) {
        if (c.k == 0 || c.k > opt.n) {
            std::fprintf(stderr, "skip: K=%zu invalid for N=%zu\n", c.k, opt.n);
            continue;
        }
        topk_kernel_fn kernel;
        ParallelTopKConfig pcfg;
        try {
            kernel = single_thread_kernel(c.algo, c.tile);
            if (is_parallel(c.algo)) pcfg.merge = parse_merge(c.merge);
        } catch (const std::exception& e) {
            std::fprintf(stderr, "error: %s\n", e.what());
            return 2;
        }
        pcfg.num_threads = static_cast<int>(c.threads);
        pcfg.min_elems_per_thread = opt.min_chunk;

        std::vector<float> out(c.k);
        ParallelTopKStats stats;
        const bool parallel = is_parallel(c.algo);
        auto run_once = [&]() {
            if (parallel) {
                topk_parallel(data.data(), opt.n, c.k, out.data(), kernel, pcfg, &stats);
            } else {
                kernel(data.data(), opt.n, c.k, out.data());
            }
            bench::do_not_optimize(out[0]);
        };

        for (int w = 0; w < opt.warmup; ++w) run_once();

        // With --flush-mb the perf window is per rep so the flush isn't counted.
        std::vector<double> times, local_times, merge_times;
        const bool per_rep_roi = opt.flush_mb > 0;
        if (!per_rep_roi) perf.enable();
        for (int r = 0; r < opt.reps; ++r) {
            if (per_rep_roi) {
                flush_caches(flush_buf);
                perf.enable();
            }
            const double t0 = bench::now_seconds();
            run_once();
            const double t1 = bench::now_seconds();
            if (per_rep_roi) perf.disable();
            times.push_back(t1 - t0);
            local_times.push_back(stats.local_seconds);
            merge_times.push_back(stats.merge_seconds);
        }
        if (!per_rep_roi) perf.disable();

        int verified = -1;
        if (opt.verify) {
            verified = (out == reference_topk(data.data(), opt.n, c.k)) ? 1 : 0;
            if (!verified) {
                std::fprintf(stderr, "VERIFY FAILED: %s k=%zu threads=%zu\n", c.algo.c_str(),
                             c.k, c.threads);
                exit_code = 1;
            }
        }

        const bench::Summary t = bench::summarize(times);
        const bench::Summary lt = bench::summarize(local_times);
        const bench::Summary mt = bench::summarize(merge_times);
        const double melem_s = t.median > 0 ? opt.n / t.median / 1e6 : 0.0;
        const double gbs = t.median > 0 ? opt.n * sizeof(float) / t.median / 1e9 : 0.0;
        const int partitions = parallel ? stats.partitions : 1;
        const size_t threads_col =
            parallel ? (c.threads == 0 ? static_cast<size_t>(bench::max_threads()) : c.threads) : 1;

        std::printf("%s,%s,%zu,%zu,%zu,%d,%zu,%s,%zu,%d,%.6f,%.6f,%.6f,%.6f,%.3f,%.3f,%.6f,%.6f,%d\n",
                    c.algo.c_str(), opt.dist.c_str(), opt.n, c.k, threads_col, partitions, c.tile,
                    c.merge.c_str(), parallel ? opt.min_chunk : 0, opt.reps, t.median * 1e3,
                    t.min * 1e3, t.mean * 1e3, t.stdev * 1e3, melem_s, gbs,
                    parallel ? lt.median * 1e3 : t.median * 1e3, parallel ? mt.median * 1e3 : 0.0,
                    verified);
        std::fflush(stdout);
    }
    return exit_code;
}
