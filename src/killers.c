#include "killers.h"

Move killers[DepthMax][KillersPerPly];

void killersCutoff(Depth ply, Move move) {
	int i;

	// Find which slot to overwrite.
	for(i=0;i<KillersPerPly-1;++i) {
		if (move==killers[ply][i] || move==MoveInvalid)
			break;
	}

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