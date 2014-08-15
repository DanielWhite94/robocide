#include "moves.h"
#include "pos.h"
#include "search.h"
#include "see.h"

////////////////////////////////////////////////////////////////////////////////
// Private prototypes
////////////////////////////////////////////////////////////////////////////////

void MovesSort(scoredmove_t *Start, scoredmove_t *End);

////////////////////////////////////////////////////////////////////////////////
// Public functions
////////////////////////////////////////////////////////////////////////////////

void MovesInit(moves_t *Moves, const pos_t *Pos, bool Quiets)
{
  Moves->Pos=Pos;
  Moves->GenCaptures=true;
  Moves->GenQuiets=Quiets;
}

void MovesRewind(moves_t *Moves, move_t TTMove)
{
  Moves->Stage=movesstage_tt;
  Moves->TTMove=TTMove;
}

move_t MovesNext(moves_t *Moves)
{
  switch(Moves->Stage)
  {
    case movesstage_tt:
      // Update stage and Next ptr ready for next call (at most one TT move)
      Moves->Stage=movesstage_captures;
      Moves->Next=Moves->List;
      
      // Do we have a TT move?
      if (MOVE_ISVALID(Moves->TTMove))
        return Moves->TTMove;
      
      // Fall through
    case movesstage_captures:
      // Do we need to generate any moves?
      if (Moves->GenCaptures)
      {
        assert(Moves->Next==Moves->List);
        
        Moves->End=Moves->List;
        PosGenPseudoCaptures(Moves);
        MovesSort(Moves->Next, Moves->End);
        Moves->GenCaptures=false;
      }
      
      // Return moves one at a time
      while (Moves->Next<Moves->End)
      {
        move_t Move=SCOREDMOVE_MOVE(*Moves->Next++);
        if (Move!=Moves->TTMove) // exclude TT move as this is searched earlier
          return Move;
      }
      
      // No captures left, fall through
      Moves->Stage=movesstage_quiets;
      assert(Moves->Next==Moves->End);
    case movesstage_quiets:
      // Do we need to generate any moves?
      if (Moves->GenQuiets)
      {
        assert(Moves->Next==Moves->End);
        PosGenPseudoQuiets(Moves);
        MovesSort(Moves->Next, Moves->End);
        Moves->GenQuiets=false;
      }
      
      // Return moves one at a time
      while (Moves->Next<Moves->End)
      {
        move_t Move=SCOREDMOVE_MOVE(*Moves->Next++);
        if (Move!=Moves->TTMove) // exclude TT move as this is searched earlier
          return Move;
      }
      
      // No moves left
      return MOVE_INVALID;
    break;
  }
  
  assert(false);
  return MOVE_INVALID;
}

const pos_t *MovesPos(moves_t *Moves)
{
  return Moves->Pos;
}

void MovesPush(moves_t *Moves, move_t Move)
{
  assert(Moves->End>=Moves->List && Moves->End<Moves->List+MOVES_MAX);
  
  // Combine with score and add to list
  movescore_t Score=SearchScoreMove(Moves->Pos, Move);
  *Moves->End++=SCOREDMOVE_MAKE(Score, Move);
}

////////////////////////////////////////////////////////////////////////////////
// Private functions
////////////////////////////////////////////////////////////////////////////////

void MovesSort(scoredmove_t *Start, scoredmove_t *End)
{
  // Insertion sort - best move first
  scoredmove_t *Ptr;
  for(Ptr=Start+1;Ptr<End;++Ptr)
  {
    scoredmove_t Temp=*Ptr, *TempPtr;
    for(TempPtr=Ptr-1;TempPtr>=Start && Temp>*TempPtr;--TempPtr)
      *(TempPtr+1)=*TempPtr;
    *(TempPtr+1)=Temp;
  }
}
