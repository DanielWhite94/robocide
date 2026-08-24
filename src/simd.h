#ifndef SIMD_H
#define SIMD_H

#include <immintrin.h>

#if defined(__AVX2__)
typedef __m256i SimdVector;
#else
typedef __m128i SimdVector;
#endif

#define SimdAlignment 32

SimdVector simdAddEpi16(SimdVector x, SimdVector y);
SimdVector simdSubEpi16(SimdVector x, SimdVector y);

#endif
