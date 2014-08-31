#include "scoredmove.h"

ScoredMove scoredMoveMake(MoveScore score, Move move)
{
  return (((ScoredMove)score)<<MoveBit) | ((ScoredMove)move);
}

MoveScore scoredMoveGetScore(ScoredMove scoredMove)
{
  return scoredMove>>MoveBit;
}

Move scoredMoveGetMove(ScoredMove scoredMove)
{
  return scoredMove & ((1llu<<MoveBit)-1);
}
