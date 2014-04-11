#include "eval.h"

score_t Evaluate(const pos_t *Pos)
{
  score_t Score=0;
  
  // Material
  Score+=100*PosPieceCount(Pos, wpawn)+325*PosPieceCount(Pos, wknight)
        +325*PosPieceCount(Pos, wbishop)+550*PosPieceCount(Pos, wrook)
        +1000*PosPieceCount(Pos, wqueen);
  Score-=100*PosPieceCount(Pos, bpawn)+325*PosPieceCount(Pos, bknight)
        +325*PosPieceCount(Pos, bbishop)+550*PosPieceCount(Pos, brook)
        +1000*PosPieceCount(Pos, bqueen);
  
  // Adjust for side to move
  if (PosGetSTM(Pos)==black)
    Score=-Score;
  
  return Score;
}
