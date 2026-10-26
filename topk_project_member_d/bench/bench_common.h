#pragma once
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <memory>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _OPENMP
#include <omp.h>
#endif

namespace bench {

// Deliberately not touched here: the parallel fill does the first touch.
class AlignedFloats {
public:
    explicit AlignedFloats(size_t n) : n_(n), raw_(new float[n + 16]) {
        const auto addr = reinterpret_cast<std::uintptr_t>(raw_.get());
        ptr_ = reinterpret_cast<float*>((addr + 63) & ~std::uintptr_t(63));
    }
    float* data() { return ptr_; }
    const float* data() const { return ptr_; }
    size_t size() const { return n_; }

private:
    size_t n_;
    std::unique_ptr<float[]> raw_;
    float* ptr_;
};

inline int max_threads() {
#ifdef _OPENMP
    return omp_get_max_threads();
#else
    return 1;
#endif
}

// SplitMix64 finaliser. Value i depends only on (seed, i): same data for any P.
inline uint64_t mix64(uint64_t x) {
    x += 0x9E3779B97F4A7C15ULL;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    return x ^ (x >> 31);
}

inline float unit_float(uint64_t h) {  // 24 bits = float mantissa, stays < 1
    return static_cast<float>(h >> 40) * (1.0f / 16777216.0f);
}

// ascending: worst case for threshold filtering, every block survives.
// duplicates: 16 keys (heavy ties). special: 1% each of +inf, -inf, NaN.
enum class Dist { Uniform, Gaussian, Ascending, Descending, Duplicates, Special };

inline Dist parse_distribution(const std::string& d) {
    if (d == "uniform") return Dist::Uniform;
    if (d == "gaussian") return Dist::Gaussian;
    if (d == "ascending") return Dist::Ascending;
    if (d == "descending") return Dist::Descending;
    if (d == "duplicates") return Dist::Duplicates;
    if (d == "special") return Dist::Special;
    throw std::invalid_argument("unknown distribution: " + d);
}

inline float generate_value(Dist dist, uint64_t seed, size_t i, size_t N) {
    const uint64_t h = mix64(seed * 0x100000001B3ULL ^ static_cast<uint64_t>(i));
    switch (dist) {
        case Dist::Uniform:
            return unit_float(h);
        case Dist::Gaussian: {
            const float u1 = 1.0f - unit_float(h);  // (0, 1], keeps log() finite
            const float u2 = unit_float(mix64(h));
            return std::sqrt(-2.0f * std::log(u1)) * std::cos(6.28318530718f * u2);
        }
        case Dist::Ascending:
            return static_cast<float>(static_cast<double>(i) / N);
        case Dist::Descending:
            return static_cast<float>(static_cast<double>(N - i) / N);
        case Dist::Duplicates:
            return std::floor(unit_float(h) * 16.0f);
        case Dist::Special: {
            const uint64_t r = mix64(h) % 100;
            if (r == 0) return std::numeric_limits<float>::infinity();
            if (r == 1) return -std::numeric_limits<float>::infinity();
            if (r == 2) return std::numeric_limits<float>::quiet_NaN();
            return unit_float(h);
        }
    }
    return 0.0f;
}

// schedule(static) roughly matches the wrapper's partitions, so first touch
// puts each page on the node of the thread that later reads it.
inline void fill_distribution(float* data, size_t N, const std::string& dist_name, uint64_t seed) {
    const Dist dist = parse_distribution(dist_name);
    const long long n = static_cast<long long>(N);
    #pragma omp parallel for schedule(static)
    for (long long i = 0; i < n; ++i) {
        data[i] = generate_value(dist, seed, static_cast<size_t>(i), N);
    }
}

inline double now_seconds() {
    using clock = std::chrono::steady_clock;
    return std::chrono::duration<double>(clock::now().time_since_epoch()).count();
}

struct Summary {
    double median = 0, min = 0, mean = 0, stdev = 0;
};

inline Summary summarize(std::vector<double> v) {
    Summary s;
    if (v.empty()) return s;
    std::sort(v.begin(), v.end());
    const size_t n = v.size();
    s.median = (n % 2) ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
    s.min = v.front();
    s.mean = std::accumulate(v.begin(), v.end(), 0.0) / n;
    double sq = 0;
    for (double x : v) sq += (x - s.mean) * (x - s.mean);
    s.stdev = n > 1 ? std::sqrt(sq / (n - 1)) : 0.0;
    return s;
}

// Stop the compiler from dropping or hoisting the timed work.
template <class T>
inline void do_not_optimize(T const& value) {
#if defined(__GNUC__) || defined(__clang__)
    asm volatile("" : : "r,m"(value) : "memory");
#else
    volatile T sink = value;
    (void)sink;
#endif
}

inline void clobber_memory() {
#if defined(__GNUC__) || defined(__clang__)
    asm volatile("" : : : "memory");
#endif
}

inline std::vector<std::string> split_list(const std::string& s) {
    std::vector<std::string> out;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, ',')) {
        if (!item.empty()) out.push_back(item);
    }
    return out;
}

// 32K, 32M, 1G: binary multiples
inline size_t parse_size(const std::string& s) {
    if (s.empty()) throw std::invalid_argument("empty size");
    size_t mult = 1;
    std::string digits = s;
    const char last = static_cast<char>(std::toupper(static_cast<unsigned char>(s.back())));
    if (last == 'K') mult = 1ULL << 10;
    if (last == 'M') mult = 1ULL << 20;
    if (last == 'G') mult = 1ULL << 30;
    if (mult != 1) digits = s.substr(0, s.size() - 1);
    return static_cast<size_t>(std::stoull(digits)) * mult;
}

inline std::vector<size_t> parse_size_list(const std::string& s) {
    std::vector<size_t> out;
    for (const auto& item : split_list(s)) out.push_back(parse_size(item));
    return out;
}

}  // namespace bench
