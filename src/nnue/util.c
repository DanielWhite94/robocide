#include <assert.h>
#include <stdio.h>

#include "util.h"

int32_t nnueMul(int8_t a, int8_t b) {
	return ((int32_t)a)*((int32_t)b);
}

int32_t nnueCReLU(int32_t x) {
	x/=64;
	return (x<=0 ? 0 : (x<=127 ? x : 127));
}

void nnueLayerDebug8(const int8_t *values, size_t count) {
	assert(values!=NULL);

	for(unsigned i=0; i<count; ++i) {
		if (i%16==0)
			printf("    ");
		printf("%4i ", values[i]);
		if (i%16==15)
			printf("\n");
	}
}

void nnueLayerDebug16(const int16_t *values, size_t count) {
	assert(values!=NULL);

	for(unsigned i=0; i<count; ++i) {
		if (i%16==0)
			printf("    ");
		printf("%6i ", values[i]);
		if (i%16==15)
			printf("\n");
	}
}

void nnueLayerDebug32(const int32_t *values, size_t count) {
	assert(values!=NULL);

	for(unsigned i=0; i<count; ++i) {
		if (i%16==0)
			printf("    ");
		printf("%8i ", values[i]);
		if (i%16==15)
			printf("\n");
	}
}
