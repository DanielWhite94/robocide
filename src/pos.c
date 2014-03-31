#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fen.h"
#include "pos.h"

////////////////////////////////////////////////////////////////////////////////
// 
////////////////////////////////////////////////////////////////////////////////

typedef struct
{
  move_t LastMove;
  unsigned int HalfMoveClock;
  sq_t EPSq;
  castrights_t CastRights;
  piece_t CapPiece;
  sq_t CapSq;
}posdata_t;

struct pos_t
{
  bb_t BB[16]; // [piecetype]
  uint8_t Array64[64]; // [sq], gives index to PieceList
  sq_t PieceList[16*16]; // [piecetype*16+n], 0<=n<16
  uint8_t PieceListNext[16]; // [piecetype], gives next empty slot
  posdata_t *DataStart, *DataEnd, *Data;
  col_t STM;
  unsigned int FullMoveNumber;
};

bool PosHaveInit=false; // Have we initialized globals etc. yet?
char PosPieceToCharArray[16];
const char *PosStartFEN="rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

////////////////////////////////////////////////////////////////////////////////
// Private prototypes
////////////////////////////////////////////////////////////////////////////////

void PosInit();
void PosClean(pos_t *Pos);
inline void PosPieceAdd(pos_t *Pos, piece_t Piece, sq_t Sq);
inline void PosPieceRemove(pos_t *Pos, sq_t Sq);

////////////////////////////////////////////////////////////////////////////////
// Public functions
////////////////////////////////////////////////////////////////////////////////

pos_t *PosNew(const char *gFEN)
{
  // Have we initialized globals etc. yet?
  if (!PosHaveInit)
    PosInit(); // Note that this is NOT thread safe
  
  // Create clean position
  pos_t *Pos=malloc(sizeof(pos_t));
  posdata_t *PosData=malloc(sizeof(posdata_t));
  if (Pos==NULL || PosData==NULL)
  {
    free(Pos);
    free(PosData);
    return NULL;
  }
  Pos->DataStart=PosData;
  Pos->DataEnd=PosData+1;
  
  // If no FEN given use initial position
  const char *FEN=(gFEN!=NULL ? gFEN : PosStartFEN);
  
  // Set to FEN
  if (!PosSetToFEN(Pos, FEN))
  {
    PosFree(Pos);
    return NULL;
  }
  
  return Pos;
}

void PosFree(pos_t *Pos)
{
  if (Pos==NULL)
    return;
  free(Pos->DataStart);
  free(Pos);
}

bool PosSetToFEN(pos_t *Pos, const char *String)
{
  // Parse FEN
  fen_t FEN;
  if (!FENRead(&FEN, String))
    return false;
  
  // Set position to clean state
  PosClean(Pos);
  
  // Set position to given FEN
  sq_t Sq;
  for(Sq=0;Sq<64;++Sq)
    if (FEN.Array[Sq]!=empty)
      PosPieceAdd(Pos, FEN.Array[Sq], Sq);
  Pos->STM=FEN.STM;
  Pos->FullMoveNumber=FEN.FullMoveNumber;
  Pos->Data->LastMove=MOVE_NULL;
  Pos->Data->HalfMoveClock=FEN.HalfMoveClock;
  Pos->Data->EPSq=FEN.EPSq; // TODO: Only set EPSq if legal ep capture possible
  Pos->Data->CastRights=FEN.CastRights;
  Pos->Data->CapPiece=empty;
  Pos->Data->CapSq=sqinvalid;
  
  return true;
}

void PosDraw(const pos_t *Pos)
{
  int X, Y;
  for(Y=7;Y>=0;--Y)
  {
    for(X=0;X<8;++X)
      printf(" %c", PosPieceToChar(PosGetPieceOnSq(Pos, XYTOSQ(X,Y))));
    puts("");
  }
}

inline col_t PosGetSTM(const pos_t *Pos)
{
  return Pos->STM;
}

inline piece_t PosGetPieceOnSq(const pos_t *Pos, sq_t Sq)
{
  assert(SQISVALID(Sq));
  return ((Pos->Array64[Sq])>>4);
}

inline bb_t PosGetBBPiece(const pos_t *Pos, piece_t Piece)
{
  assert(PIECEISVALID(Piece));
  return Pos->BB[Piece];
}

inline char PosPieceToChar(piece_t Piece)
{
  assert(PIECEISVALID(Piece) || Piece==empty);
  return PosPieceToCharArray[Piece];
}

inline unsigned int PosPieceCount(const pos_t *Pos, piece_t Piece)
{
  assert(PIECEISVALID(Piece));
  return Pos->PieceListNext[Piece];
}

////////////////////////////////////////////////////////////////////////////////
// Private functions
////////////////////////////////////////////////////////////////////////////////

void PosInit()
{
  // Piece to character array
  memset(PosPieceToCharArray, '?', 16);
  PosPieceToCharArray[empty]='.';
  PosPieceToCharArray[wpawn]='P';
  PosPieceToCharArray[wknight]='N';
  PosPieceToCharArray[wbishop]='B';
  PosPieceToCharArray[wrook]='R';
  PosPieceToCharArray[wqueen]='Q';
  PosPieceToCharArray[wking]='K';
  PosPieceToCharArray[bpawn]='p';
  PosPieceToCharArray[bknight]='n';
  PosPieceToCharArray[bbishop]='b';
  PosPieceToCharArray[brook]='r';
  PosPieceToCharArray[bqueen]='q';
  PosPieceToCharArray[bking]='k';
  
  PosHaveInit=true;
}

void PosClean(pos_t *Pos)
{
  memset(Pos->BB, 0, 16*sizeof(bb_t));
  memset(Pos->Array64, 0, 64*sizeof(uint8_t));
  int I;
  for(I=0;I<16;++I)
    Pos->PieceListNext[I]=16*I;
  Pos->STM=white;
  Pos->FullMoveNumber=1;
  Pos->Data=Pos->DataStart;
  Pos->Data->LastMove=MOVE_NULL;
  Pos->Data->HalfMoveClock=0;
  Pos->Data->EPSq=sqinvalid;
  Pos->Data->CastRights=castrights_none;
  Pos->Data->CapPiece=empty;
  Pos->Data->CapSq=sqinvalid;
}

inline void PosPieceAdd(pos_t *Pos, piece_t Piece, sq_t Sq)
{
  assert(PIECEISVALID(Piece));
  assert(SQISVALID(Sq));
  assert(PosGetPieceOnSq(Pos, Sq)==empty);
  
  Pos->BB[Piece]|=BBSqToBB(Sq);
  uint8_t Index=(Pos->PieceListNext[Piece]++);
  Pos->Array64[Sq]=Index;
  Pos->PieceList[Index]=Sq;
}

inline void PosPieceRemove(pos_t *Pos, sq_t Sq)
{
  assert(SQISVALID(Sq));
  assert(PosGetPieceOnSq(Pos, Sq)!=empty);
  
  uint8_t Index=Pos->Array64[Sq];
  piece_t Piece=(Index>>4);
  Pos->BB[Piece]&=~BBSqToBB(Sq);
  Pos->Array64[Sq]=0;
  uint8_t LastIndex=(--Pos->PieceListNext[Piece]);
  Pos->PieceList[Index]=Pos->PieceList[LastIndex];
}
