#ifndef SIMD_H
#define SIMD_H

#include <immintrin.h>

#if defined(__AVX2__)

typedef __m256i SimdVector;
#define SIMD_ALIGNMENT 32

static inline SimdVector simdAddEpi16(SimdVector x, SimdVector y) {
	return _mm256_add_epi16(x, y);
}

static inline SimdVector simdSubEpi16(SimdVector x, SimdVector y) {
	return _mm256_sub_epi16(x, y);
}

#else

typedef __m128i SimdVector;
#define SIMD_ALIGNMENT 32

static inline SimdVector simdAddEpi16(SimdVector x, SimdVector y) {
	return _mm_add_epi16(x, y);
}

static inline SimdVector simdSubEpi16(SimdVector x, SimdVector y) {
	return _mm_sub_epi16(x, y);
}

#endif

#endif
