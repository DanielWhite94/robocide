#ifndef NNUE_UTIL_H
#define NNUE_UTIL_H

#include <stdint.h>

int32_t nnueMul(int8_t a, int8_t b);
int32_t nnueCReLU(int32_t x);
int32_t nnueClamp(int32_t x, int32_t min, int32_t max);

void nnueLayerDebug8(const int8_t *values, size_t count);
void nnueLayerDebug16(const int16_t *values, size_t count);
void nnueLayerDebug32(const int32_t *values, size_t count);

#endif
