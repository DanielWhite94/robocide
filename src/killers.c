#include <assert.h>
#include <string.h>

#include "killers.h"

MoveSet killers[DepthMax];

Move killersGetN(Depth ply, unsigned index) {
	assert(ply<DepthMax);
	assert(index<KillersPerPly);

	return moveSetGetN(killers[ply], index);
}

bool killersMoveIsKiller(Depth ply, Move move) {
	assert(ply<DepthMax);

	return moveSetContains(killers[ply], move);
}

void killersCutoff(Depth ply, Move move) {
	assert(ply<DepthMax);
	assert(moveIsValid(move));

	moveSetAdd(&killers[ply], move);
}

void killersClear(void) {
	STATICASSERT(MoveSetEmpty==0);
	memset(killers, 0, sizeof(killers));
}
