#ifndef MOVES_H
#define MOVES_H

#include <stdbool.h>

typedef struct Moves Moves;

#include "move.h"
#include "pos.h"

#define MovesMax 256
struct Moves
{
	// All entries should be considered private - only here to allow easy allocation on the stack.
	Move list[MovesMax], *next, *end;
	const Pos *pos;
};

void movesInit(Moves *moves, const Pos *pos);

void movesRewind(Moves *moves);

Move movesNext(Moves *moves); // Returns distinct moves until none remain (then returning MoveInvalid).

const Pos *movesGetPos(Moves *moves);

void movesPush(Moves *moves, Move move); // Used by generators to add moves.

#endif
