#ifndef MOVES_H
#define MOVES_H

#include <stdbool.h>

typedef struct Moves Moves;

#include "move.h"
#include "pos.h"
#include "scoredmove.h"

typedef enum { MovesStageTT, MovesStageCaptures, MovesStageQuiets } MovesStage;

#define MovesMax 256
struct Moves
{
  // All entries should be considered private - only here to allow easy allocation on the stack.
  ScoredMove list[MovesMax], *next, *end;
  MovesStage stage;
  Move ttMove;
  const Pos *pos;
  bool genCaptures, genQuiets; // true => still need to generate.
};

void movesInit(Moves *moves, const Pos *pos, bool quiets); // quiets - should quiet moves be generated.
void movesRewind(Moves *moves, Move ttMove); // Should be called before movesNext() to rewind to first move (and potentially set/update TT move).
Move movesNext(Moves *moves); // Returns distinct moves until none remain (then returning MoveInvalid).
const Pos *movesGetPos(Moves *moves);
void movesPush(Moves *moves, Move move); // Used by generators to add moves.

#endif
