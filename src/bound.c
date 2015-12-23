#include "bound.h"

bool boundIsValid(Bound bound) {
	return (bound>=BoundNone && bound<=BoundExact);
}
