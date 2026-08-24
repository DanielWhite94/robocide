#include "simd.h"

SimdVector simdAddEpi16(SimdVector x, SimdVector y) {
#if defined(__AVX2__)
	return _mm256_add_epi16(x, y);
#else
	return _mm_add_epi16(x, y);
#endif
}

SimdVector simdSubEpi16(SimdVector x, SimdVector y) {
#if defined(__AVX2__)
	return _mm256_sub_epi16(x, y);
#else
	return _mm_sub_epi16(x, y);
#endif
}
