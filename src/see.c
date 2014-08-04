#include <assert.h>
#include "attacks.h"
#include "see.h"

const int SEEPieceValue[8]={0, 1, 3, 3, 3, 5, 9, 255};

////////////////////////////////////////////////////////////////////////////////
// Private prototypes
////////////////////////////////////////////////////////////////////////////////

bb_t SEEGetLeastValuable(const pos_t *Pos, bb_t AtkDef, col_t Colour, piece_t *Piece);
bb_t SEEAttacksTo(const pos_t *Pos, sq_t Sq, bb_t Occ);

////////////////////////////////////////////////////////////////////////////////
// Public functions
////////////////////////////////////////////////////////////////////////////////

int SEE(const pos_t *Pos, sq_t FromSq, sq_t ToSq)
{
  piece_t Attacker=PosGetPieceOnSq(Pos, FromSq);
  piece_t Victim=PosGetPieceOnSq(Pos, ToSq);
  col_t STM=PosGetSTM(Pos);  
  assert(Attacker!=empty);
  assert(Victim!=empty);
  
  bb_t Occ=PosGetBBAll(Pos);
  bb_t AtkDef=SEEAttacksTo(Pos, ToSq, Occ);
  bb_t FromSet=SQTOBB(FromSq);
  
  bb_t MayXRay=Occ^PosGetBBPiece(Pos, wknight)^PosGetBBPiece(Pos, wking)^PosGetBBPiece(Pos, bknight)^PosGetBBPiece(Pos, bking);
  assert(MayXRay==(PosGetBBPiece(Pos, wpawn) | PosGetBBPiece(Pos, wbishopl) | PosGetBBPiece(Pos, wbishopd) | PosGetBBPiece(Pos, wrook) | PosGetBBPiece(Pos, wqueen) | 
                   PosGetBBPiece(Pos, bpawn) | PosGetBBPiece(Pos, bbishopl) | PosGetBBPiece(Pos, bbishopd) | PosGetBBPiece(Pos, brook) | PosGetBBPiece(Pos, bqueen)));
  
  int Gain[32], Depth=0;
  Gain[Depth]=SEEPieceValue[PIECE_TYPE(Victim)];
  do
  {
    ++Depth;
    Gain[Depth]=SEEPieceValue[PIECE_TYPE(Attacker)]-Gain[Depth-1]; // speculative store, if defended
    
    // Pruning
    if (MAX(-Gain[Depth-1], Gain[Depth])<0)
      break;
    
    // 'Capture'
    Occ^=FromSet; // remove piece from occupancy
    AtkDef^=FromSet; // remove piece from attacks & defenders list
    STM=COL_SWAP(STM); // swap colour
    
    // Add any new attacks needed
    if (FromSet & MayXRay)
      AtkDef|=SEEAttacksTo(Pos, ToSq, Occ); // TODO: Something more intelligent?
    
    // Look for next attacker
    FromSet=SEEGetLeastValuable(Pos, AtkDef, STM, &Attacker);
  }while(FromSet);
  
  while (--Depth)
    Gain[Depth-1]=-MAX(-Gain[Depth-1], Gain[Depth]);
  
  return Gain[0];
}

int SEESign(const pos_t *Pos, sq_t FromSq, sq_t ToSq)
{
  // No need for SEE?
  piece_t Victim=PosGetPieceOnSq(Pos, ToSq);
  piece_t Attacker=PosGetPieceOnSq(Pos, FromSq);
  if (Victim==empty || SEEPieceValue[PIECE_TYPE(Attacker)]<=SEEPieceValue[PIECE_TYPE(Victim)])
    return 0;
  
  return SEE(Pos, FromSq, ToSq);
}

////////////////////////////////////////////////////////////////////////////////
// Private Functions
////////////////////////////////////////////////////////////////////////////////

bb_t SEEGetLeastValuable(const pos_t *Pos, bb_t AtkDef, col_t Colour, piece_t *Piece)
{
  piece_t EndPiece=PIECE_MAKE(king,Colour);
  for(*Piece=PIECE_MAKE(pawn,Colour);*Piece<=EndPiece;++*Piece)
  {
    bb_t Set=(AtkDef & PosGetBBPiece(Pos, *Piece));
    if (Set)
      return Set & -Set;
  }
  
  // No attackers
  return 0;
}

bb_t SEEAttacksTo(const pos_t *Pos, sq_t Sq, bb_t Occ)
{
  bb_t Set=0;
  
  // Pawns
  bb_t Wing=BBWingify(SQTOBB(Sq));
  Set|=((BBForwardOne(Wing, white) & PosGetBBPiece(Pos, wpawn)) |
        (BBForwardOne(Wing, black) & PosGetBBPiece(Pos, bpawn)));
  
  // Knights
  Set|=(AttacksKnight(Sq) & (PosGetBBPiece(Pos, wknight) | PosGetBBPiece(Pos, bknight)));
  
  // Diagonal sliders
  Set|=(AttacksBishop(Sq, Occ) & (PosGetBBPiece(Pos, wbishopl) | PosGetBBPiece(Pos, wbishopd) | PosGetBBPiece(Pos, bbishopl) | PosGetBBPiece(Pos, bbishopd) |
                                  PosGetBBPiece(Pos, wqueen) | PosGetBBPiece(Pos, bqueen)));
  
  // Horizontal/vertical sliders
  Set|=(AttacksRook(Sq, Occ) & (PosGetBBPiece(Pos, wrook) | PosGetBBPiece(Pos, brook) | PosGetBBPiece(Pos, wqueen) | PosGetBBPiece(Pos, bqueen)));
  
  // Kings
  Set|=(AttacksKing(Sq) & (PosGetBBPiece(Pos, wking) | PosGetBBPiece(Pos, bking)));
  
  return (Set & Occ);
}
