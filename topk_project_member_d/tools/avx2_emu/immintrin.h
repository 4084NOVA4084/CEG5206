#pragma once
// Scalar stand-ins for the AVX2 intrinsics that Members B and C use, so their
// `#if defined(__AVX2__)` paths can be tested on a CPU without AVX2 (make
// test-avx2-emu). It proves the logic only: it says nothing about speed, and
// the real instructions still have to run on an x86 machine.
#include <cstdint>
#include <cstdlib>
#include <cstring>

struct __m256 { float lane[8]; };
struct __m256i { std::int32_t lane[8]; };

#define _CMP_UNORD_Q 3
#define _CMP_GT_OQ 30

inline __m256 _mm256_loadu_ps(const float* p) {
    __m256 r;
    std::memcpy(r.lane, p, sizeof r.lane);
    return r;
}

inline void _mm256_storeu_ps(float* p, __m256 a) { std::memcpy(p, a.lane, sizeof a.lane); }

inline __m256 _mm256_set1_ps(float x) {
    __m256 r;
    for (float& lane : r.lane) lane = x;
    return r;
}

inline __m256i _mm256_setr_epi32(std::int32_t a, std::int32_t b, std::int32_t c, std::int32_t d,
                                 std::int32_t e, std::int32_t f, std::int32_t g, std::int32_t h) {
    return {{a, b, c, d, e, f, g, h}};
}

inline __m256 _mm256_castsi256_ps(__m256i a) {
    __m256 r;
    std::memcpy(r.lane, a.lane, sizeof r.lane);
    return r;
}

// VMINPS / VMAXPS return the second operand when either input is NaN or both are zero.
inline __m256 _mm256_min_ps(__m256 a, __m256 b) {
    __m256 r;
    for (int i = 0; i < 8; ++i) r.lane[i] = a.lane[i] < b.lane[i] ? a.lane[i] : b.lane[i];
    return r;
}

inline __m256 _mm256_max_ps(__m256 a, __m256 b) {
    __m256 r;
    for (int i = 0; i < 8; ++i) r.lane[i] = a.lane[i] > b.lane[i] ? a.lane[i] : b.lane[i];
    return r;
}

inline std::uint32_t emu_bits(float x) {
    std::uint32_t u;
    std::memcpy(&u, &x, sizeof u);
    return u;
}

// VBLENDVPS picks from b where the sign bit of the mask lane is set.
inline __m256 _mm256_blendv_ps(__m256 a, __m256 b, __m256 mask) {
    __m256 r;
    for (int i = 0; i < 8; ++i) r.lane[i] = (emu_bits(mask.lane[i]) >> 31) ? b.lane[i] : a.lane[i];
    return r;
}

inline __m256 _mm256_permutevar8x32_ps(__m256 a, __m256i index) {
    __m256 r;
    for (int i = 0; i < 8; ++i) r.lane[i] = a.lane[index.lane[i] & 7];
    return r;
}

// Only the two predicates B and C use. Any other aborts rather than return a wrong mask.
inline __m256 _mm256_cmp_ps(__m256 a, __m256 b, int predicate) {
    if (predicate != _CMP_UNORD_Q && predicate != _CMP_GT_OQ) std::abort();
    __m256 r;
    for (int i = 0; i < 8; ++i) {
        const float x = a.lane[i];
        const float y = b.lane[i];
        const bool hit = predicate == _CMP_UNORD_Q ? (x != x || y != y) : x > y;
        const std::uint32_t bits = hit ? 0xFFFFFFFFu : 0u;
        std::memcpy(&r.lane[i], &bits, sizeof bits);
    }
    return r;
}

inline int _mm256_movemask_ps(__m256 a) {
    int mask = 0;
    for (int i = 0; i < 8; ++i) mask |= static_cast<int>(emu_bits(a.lane[i]) >> 31) << i;
    return mask;
}
