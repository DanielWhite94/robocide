#include "simd.h"

SimdVector simdAddEpi16(SimdVector x, SimdVector y) {
#if defined(__AVX2__)
	return _mm256_add_epi16(x, y);
#else
	return _mm_add_epi16(x, y);
#endif
}

SimdVector simdAddEpi32(SimdVector x, SimdVector y) {
#if defined(__AVX2__)
	return _mm256_add_epi32(x, y);
#else
	return _mm_add_epi32(x, y);
#endif
}

SimdVector simdSubEpi16(SimdVector x, SimdVector y) {
#if defined(__AVX2__)
	return _mm256_sub_epi16(x, y);
#else
	return _mm_sub_epi16(x, y);
#endif
}

SimdVector simdMinEpi16(SimdVector x, SimdVector y) {
#if defined(__AVX2__)
	return _mm256_min_epi16(x, y);
#else
	return _mm_min_epi16(x, y);
#endif
}

SimdVector simdMaxEpi16(SimdVector x, SimdVector y) {
#if defined(__AVX2__)
	return _mm256_max_epi16(x, y);
#else
	return _mm_max_epi16(x, y);
#endif
}

SimdVector simdMulloEpi16(SimdVector x, SimdVector y) {
#if defined(__AVX2__)
	return _mm256_mullo_epi16(x, y);
#else
	return _mm_mullo_epi16(x, y);
#endif
}

SimdVector simdMAddEpi16(SimdVector x, SimdVector y) {
#if defined(__AVX2__)
	return _mm256_madd_epi16(x, y);
#else
	return _mm_madd_epi16(x, y);
#endif
}

SimdVector simdSetZero(void) {
#if defined(__AVX2__)
	return _mm256_setzero_si256();
#else
	return _mm_setzero_si128();
#endif
}

SimdVector simdSetEpi16(int16_t x) {
#if defined(__AVX2__)
	return _mm256_set1_epi16(x);
#else
	return _mm_set1_epi16(x);
#endif
}

int32_t simdHAddEpi32(SimdVector x) {
#if defined(__AVX2__)
	// Get the lower and upper half of the register
	__m128i x0=_mm256_castsi256_si128(x);
	__m128i x1=_mm256_extracti128_si256(x, 1);

	// Add the lower and upper half vertically
	x0=_mm_add_epi32(x0, x1);

	// Get the upper half of the result
	x1=_mm_unpackhi_epi64(x0, x0);

	// Add the lower and upper half vertically
	x0=_mm_add_epi32(x0, x1);

	// Shuffle the result so that the lower 32-bits are directly above the second-lower 32-bits
	x1=_mm_shuffle_epi32(x0, _MM_SHUFFLE(2, 3, 0, 1));

	// Add the lower 32-bits to the second-lower 32-bits vertically
	x0=_mm_add_epi32(x0, x1);

	// Cast the result to the 32-bit integer type and return it
	return _mm_cvtsi128_si32(x0);
#else
	const int32_t *values=(const int32_t *)&x;
	return values[0]+values[1]+values[2]+values[3];
#endif
}
