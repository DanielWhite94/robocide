#ifndef SCOREDMOVE_H
#define SCOREDMOVE_H

#include <stdint.h>

#include "move.h"
#include "util.h"

typedef uint64_t MoveScore;
#define MoveScoreBit 48 // Number of bits MoveScore actually uses.

STATICASSERT(MoveBit+MoveScoreBit<=64);
typedef uint64_t ScoredMove; // Score and move combined into 64 bit value.

ScoredMove scoredMoveMake(MoveScore score, Move move);
MoveScore scoredMoveGetScore(ScoredMove scoredMove);
Move scoredMoveGetMove(ScoredMove scoredMove);

#endif
