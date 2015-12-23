#ifndef BOUND_H
#define BOUND_H

#include <stdbool.h>

typedef enum {
	BoundNone=0,
	BoundLower=1,
	BoundUpper=2,
	BoundExact=(BoundLower|BoundUpper)
} Bound;
#define BoundBit 2 // Number of bits Bound actually uses.

bool boundIsValid(Bound bound);

#endif
