#include <string.h>

#include "util.h"

uint64_t utilRandState=31;

bool utilStrEqual(const char *a, const char *b) {
	return (strcmp(a, b)==0);
}

void utilRandSeed(uint64_t seed) {
	utilRandState=seed;
}

uint64_t utilRand64(void) {
	// Uses xorshift*.
	utilRandState^=utilRandState>>12;
	utilRandState^=utilRandState<<25;
	utilRandState^=utilRandState>>27;
	return utilRandState*2685821657736338717llu;
}

unsigned utilFileCountLines(FILE *file) {
	char buffer[4096];
	unsigned count=0;
	while(1) {
		size_t res=fread(buffer, 1, 4096, file);
		if (ferror(file))
			break;

		for(unsigned i=0; i<res; i++)
			if (buffer[i]=='\n')
				count++;

		if (feof(file))
			break;
	}

	rewind(file);

	return count;
}
