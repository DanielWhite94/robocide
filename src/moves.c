#include <assert.h>

#include "moves.h"
#include "search.h"

////////////////////////////////////////////////////////////////////////////////
// Private prototypes
////////////////////////////////////////////////////////////////////////////////

void movesSort(ScoredMove *start, ScoredMove *end); // descending order (best move first)

////////////////////////////////////////////////////////////////////////////////
// Public functions
////////////////////////////////////////////////////////////////////////////////

void movesInit(Moves *moves, const Pos *pos, bool quiets)
{
  moves->pos=pos;
  moves->genCaptures=true;
  moves->genQuiets=quiets;
}

void movesRewind(Moves *moves, Move ttMove)
{
  moves->stage=MovesStageTT;
  moves->ttMove=ttMove;
}

Move movesNext(Moves *moves)
{
  switch(moves->stage)
  {
    case MovesStageTT:
      // Update stage and Next ptr ready for next call (at most one TT move)
      moves->stage=MovesStageCaptures;
      moves->next=moves->list;
      
      // Do we have a TT move?
      if (moveIsValid(moves->ttMove))
        return moves->ttMove;
      
      // Fall through
    case MovesStageCaptures:
      // Do we need to generate any moves?
      if (moves->genCaptures)
      {
        assert(moves->next==moves->list);
        moves->end=moves->list;
        posGenPseudoCaptures(moves);
        movesSort(moves->next, moves->end);
        moves->genCaptures=false;
      }
      
      // Return moves one at a time
      while (moves->next<moves->end)
      {
        Move move=scoredMoveGetMove(*moves->next++);
        if (move!=moves->ttMove) // exclude TT move as this is searched earlier
          return move;
      }
      
      // No captures left, fall through
      moves->stage=MovesStageQuiets;
      assert(moves->next==moves->end);
    case MovesStageQuiets:
      // Do we need to generate any moves?
      if (moves->genQuiets)
      {
        assert(moves->next==moves->end);
        posGenPseudoQuiets(moves);
        movesSort(moves->next, moves->end);
        moves->genQuiets=false;
      }
      
      // Return moves one at a time
      while (moves->next<moves->end)
      {
        Move move=scoredMoveGetMove(*moves->next++);
        if (move!=moves->ttMove) // exclude TT move as this is searched earlier
          return move;
      }
      
      // No moves left
      return MoveInvalid;
    break;
  }
  
  assert(false);
  return MoveInvalid;
}

const Pos *movesGetPos(Moves *moves)
{
  return moves->pos;
}

void movesPush(Moves *moves, Move move)
{
  assert(moves->end>=moves->list && moves->end<moves->list+MovesMax);
  
  // Combine with score and add to list
  MoveScore score=searchScoreMove(moves->pos, move);
  *moves->end++=scoredMoveMake(score, move);
}

////////////////////////////////////////////////////////////////////////////////
// Private functions
////////////////////////////////////////////////////////////////////////////////

void movesSort(ScoredMove *start, ScoredMove *end)
{
  // Insertion sort - best move first
  ScoredMove *ptr;
  for(ptr=start+1;ptr<end;++ptr)
  {
    ScoredMove temp=*ptr, *tempPtr;
    for(tempPtr=ptr-1;tempPtr>=start && temp>*tempPtr;--tempPtr)
      *(tempPtr+1)=*tempPtr;
    *(tempPtr+1)=temp;
  }
}
