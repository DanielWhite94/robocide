#ifndef TYPES_H
#define TYPES_H

#include <inttypes.h>
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

typedef uint64_t bb_t;

// Move format is rather complicated but fits a lot into 16 bits
typedef uint16_t move_t;
#define MOVE_SHIFTTOSQ    ( 0)
#define MOVE_SHIFTFROMSQ  ( 6)
#define MOVE_SHIFTEXTRA   (12)
#define MOVE_SHIFTCOLOUR  (15)
#define MOVE_MASKEXTRA    (0x3000u)
#define MOVE_MASKPROMO    (0x4000u)
#define MOVE_MASKCOLOUR   (0x8000u)
#define MOVE_EXTRANONE    (0x0000u)
#define MOVE_EXTRAKNIGHT  (0x0000u)
#define MOVE_EXTRACAST    (0x1000u)
#define MOVE_EXTRABISHOP  (0x1000u)
#define MOVE_EXTRAEP      (0x2000u)
#define MOVE_EXTRAROOK    (0x2000u)
#define MOVE_EXTRADP      (0x3000u)
#define MOVE_EXTRAQUEEN   (0x3000u)
#define MOVE_GETTOSQ(M) (((M)>>MOVE_SHIFTTOSQ) & 63)
#define MOVE_GETFROMSQ(M) (((M)>>MOVE_SHIFTFROMSQ) & 63)
#define MOVE_GETCOLOUR(M) ((M)>>MOVE_SHIFTCOLOUR)
#define MOVE_ISCAST(M) (((M)&(MOVE_MASKPROMO|MOVE_MASKEXTRA))==MOVE_EXTRACAST)
#define MOVE_ISEP(M)   (((M)&(MOVE_MASKPROMO|MOVE_MASKEXTRA))==MOVE_EXTRAEP)
#define MOVE_ISDP(M)   (((M)&(MOVE_MASKPROMO|MOVE_MASKEXTRA))==MOVE_EXTRADP)
#define MOVE_ISPROMO(M) (((M) & MOVE_MASKPROMO)!=0)
#define MOVE_NULL 0 // a1a1 is an invalid move anyway
#define MOVES_MAX 256

typedef enum
{
  white=0,
  black=1
}col_t;
#define COL_SWAP(C) ((C)^1)

typedef enum
{
  A1,B1,C1,D1,E1,F1,G1,H1,
  A2,B2,C2,D2,E2,F2,G2,H2,
  A3,B3,C3,D3,E3,F3,G3,H3,
  A4,B4,C4,D4,E4,F4,G4,H4,
  A5,B5,C5,D5,E5,F5,G5,H5,
  A6,B6,C6,D6,E6,F6,G6,H6,
  A7,B7,C7,D7,E7,F7,G7,H7,
  A8,B8,C8,D8,E8,F8,G8,H8,
  sqinvalid=127 // Only needs to be 'far enough' away from the true squares
}sq_t;
#define SQ_ISVALID(S) ((S)>=A1 && (S)<=H8)
#define SQ_X(S) ((S)&7)
#define SQ_Y(S) ((S)>>3)
#define SQ_FLIP(S) ((S)^56)
#define SQ_ISLIGHT(S) ((((S)>>3)^(S))&1)
#define XYTOSQ(X,Y) (((Y)<<3)+(X))
#define SQTOBB(SQ) (((bb_t)1)<<(SQ))

typedef enum
{
  empty=0,
  pawn=1,
  knight=2,
  bishopl=3,
  bishopd=4,
  rook=5,
  queen=6,
  king=7,
  wpawn=pawn,
  wknight=knight,
  wbishopl=bishopl,
  wbishopd=bishopd,
  wrook=rook,
  wqueen=queen,
  wking=king,
  bpawn=8|pawn,
  bknight=8|knight,
  bbishopl=8|bishopl,
  bbishopd=8|bishopd,
  brook=8|rook,
  bqueen=8|queen,
  bking=8|king,
}piece_t;
#define PIECE_ISVALID(P) (((P)>=wpawn && (P)<=wking) || \
                         ((P)>=bpawn && (P)<=bking))
#define PIECE_TYPE(P) ((P)&7)
#define PIECE_COLOUR(P) ((P)>>3)
#define PIECE_MAKE(T,C) (((C)<<3)|(T))

typedef enum
{
  castrights_none=0,
  castrights_q=1,
  castrights_k=2,
  castrights_Q=4,
  castrights_K=8,
  castrights_KQ=castrights_K | castrights_Q,
  castrights_Kk=castrights_K | castrights_k,
  castrights_Kq=castrights_K | castrights_q,
  castrights_Qk=castrights_Q | castrights_k,
  castrights_Qq=castrights_Q | castrights_q,
  castrights_kq=castrights_k | castrights_q,
  castrights_KQk=castrights_KQ | castrights_k,
  castrights_KQq=castrights_KQ | castrights_q,
  castrights_Kkq=castrights_Kk | castrights_q,
  castrights_Qkq=castrights_Qk | castrights_q,
  castrights_KQkq=castrights_KQ | castrights_kq,
}castrights_t;

typedef int16_t score_t;
#define SCORE_INF 32000
#define SCORE_NONE -32100
#define SCORE_DRAW 0
#define SCORE_MATEDIN(P) ((P)-31000)
#define SCORE_ISMATE(S)   (abs(abs(S)+SCORE_MATEDIN(0))<512)
#define SCORE_MATEDIST(S) ((1-abs(S)-SCORE_MATEDIN(0))/2)
#define SCORE_ISVALID(S) ((S)>=-SCORE_INF && (S)<=SCORE_INF)

typedef uint64_t hkey_t;
#define PRIxkey PRIx64

static inline piece_t MOVE_GETPROMO(move_t Move)
{
  // Complicated because we consider light and dark bishops distinct
  assert(MOVE_ISPROMO(Move));
  col_t Colour=MOVE_GETCOLOUR(Move);
  switch(Move & MOVE_MASKEXTRA)
  {
    case MOVE_EXTRAKNIGHT: return PIECE_MAKE(knight, Colour); break;
    case MOVE_EXTRABISHOP:
      return PIECE_MAKE((SQ_ISLIGHT(MOVE_GETTOSQ(Move)) ? bishopl : bishopd), Colour);
    break;
    case MOVE_EXTRAROOK: return PIECE_MAKE(rook, Colour); break;
    case MOVE_EXTRAQUEEN: return PIECE_MAKE(queen, Colour); break;
    default: assert(false); return empty;
  }
}

#endif
