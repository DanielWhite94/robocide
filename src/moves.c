#include <assert.h>

#include "moves.h"

////////////////////////////////////////////////////////////////////////////////
// Public functions.
////////////////////////////////////////////////////////////////////////////////

void movesInit(Moves *moves, const Pos *pos) {
	moves->end=moves->next=moves->list;
	moves->pos=pos;
	posGenPseudoMoves(moves, MoveTypeAny);
}

void movesRewind(Moves *moves) {
	moves->next=moves->list;
}

Move movesNext(Moves *moves) {
	// Return moves one at a time.
	if (moves->next<moves->end)
		return *moves->next++;

	return MoveInvalid;
}

const Pos *movesGetPos(Moves *moves) {
	return moves->pos;
}

void movesPush(Moves *moves, Move move) {
	assert(moves->end>=moves->list && moves->end<moves->list+MovesMax);
	*moves->end++=move;
}
