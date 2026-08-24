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
SimdVector simdAddEpi32(SimdVector x, SimdVector y);

SimdVector simdSubEpi16(SimdVector x, SimdVector y);

SimdVector simdMinEpi16(SimdVector x, SimdVector y);
SimdVector simdMaxEpi16(SimdVector x, SimdVector y);

SimdVector simdMulloEpi16(SimdVector x, SimdVector y);
SimdVector simdMAddEpi16(SimdVector x, SimdVector y);

SimdVector simdSetZero(void);
SimdVector simdSetEpi16(int16_t x);

int32_t simdHAddEpi32(SimdVector x);

#endif
