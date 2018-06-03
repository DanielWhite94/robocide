#include <assert.h>

#include "killers.h"

Killers killersDummy;

void killersInit(void) {
	killersClear(&killersDummy);
}

Move killersGet(const Killers *killers, Depth ply, unsigned killerIndex) {
	assert(ply<DepthMax);
	assert(killerIndex<KillersPerPly);

	return killers->moves[ply][killerIndex];
}

void killersCutoff(Killers *killers, Depth ply, Move move) {
	int i;

	// Find which slot to overwrite.
	for(i=0;i<KillersPerPly-1;++i) {
		if (move==killers->moves[ply][i] || move==MoveInvalid)
			break;
	}

	// Move entries down, and insert 'new' move at front.
	for(;i>0;--i)
		killers->moves[ply][i]=killers->moves[ply][i-1];
	killers->moves[ply][0]=move;
}

void killersClear(Killers *killers) {
	int i, j;
	for(i=0;i<DepthMax;++i)
		for(j=0;j<KillersPerPly;++j)
			killers->moves[i][j]=MoveInvalid;
}
