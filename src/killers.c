#include <assert.h>

#include "killers.h"

Move killers[DepthMax][KillersPerPly];

Move killersGetN(Depth ply, unsigned index) {
	assert(index<KillersPerPly);
	return killers[ply][index];
}

bool killersMoveIsKiller(Depth ply, Move move) {
	for(unsigned i=0; i<KillersPerPly; ++i)
		if (move==killers[ply][i])
			return true;
	return false;
}

void killersCutoff(Depth ply, Move move) {
	assert(moveIsValid(move));

	int i;

	// Find which slot to overwrite.
	// (we may have an empty slot, or the move may already be in the list)
	for(i=0;i<KillersPerPly-1;++i)
		if (move==killers[ply][i] || killers[ply][i]==MoveInvalid)
			break;

	// Move entries down, and insert 'new' move at front.
	for(;i>0;--i)
		killers[ply][i]=killers[ply][i-1];
	killers[ply][0]=move;
}

void killersClear(void) {
	int i, j;
	for(i=0;i<DepthMax;++i)
		for(j=0;j<KillersPerPly;++j)
			killers[i][j]=MoveInvalid;
}
