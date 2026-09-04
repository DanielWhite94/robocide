#ifndef MOVES_H
#define MOVES_H

#include <stdbool.h>

typedef struct Moves Moves;

#include "depth.h"
#include "move.h"
#include "pos.h"
#include "scoredmove.h"

typedef enum { MovesStagePV, MovesStageTT, MovesStageGenCaptures, MovesStageCaptures, MovesStageKillers, MovesStageCounterMove, MovesStageGenQuiets, MovesStageQuiets } MovesStage;

#define MovesMax 256
struct Moves
{
	// All entries should be considered private - only here to allow easy allocation on the stack.
	ScoredMove list[MovesMax], *next, *end;
	MovesStage stage;
	Move pvMove;
	Move ttMove;
	unsigned int killersIndex;
	const Pos *pos;
	Depth ply;
	MoveType allowed, needed;
	Move savedCounterMove;
};

void movesInit(Moves *moves, const Pos *pos, Depth ply, MoveType type, Move pvMove, Move ttMove);

Move movesNext(Moves *moves); // Returns distinct moves until none remain (then returning MoveInvalid).

const Pos *movesGetPos(Moves *moves);

void movesPush(Moves *moves, Move move); // Used by generators to add moves.

#endif
