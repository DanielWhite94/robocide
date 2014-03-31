#ifndef TYPES_H
#define TYPES_H

#include <stdint.h>

typedef uint64_t bb_t;

typedef uint16_t move_t;
#define MOVE_NULL (0) // a1a1 is an invalid move anyway

typedef enum
{
  white,
  black
}col_t;

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
  sqinvalid
}sq_t;
#define SQISVALID(S) ((S)>=A1 && (S)<=H8)
#define XYTOSQ(X,Y) (((Y)<<3)+(X))

typedef enum
{
  empty=0,
  pawn=1,
  knight=2,
  bishop=3,
  rook=4,
  queen=5,
  king=6,
  wpawn=pawn,
  wknight=knight,
  wbishop=bishop,
  wrook=rook,
  wqueen=queen,
  wking=king,
  bpawn=8|pawn,
  bknight=8|knight,
  bbishop=8|bishop,
  brook=8|rook,
  bqueen=8|queen,
  bking=8|king
}piece_t;
#define PIECEISVALID(P) (((P)>=wpawn && (P)<=wking) || \
                         ((P)>=bpawn && (P)<=bking))

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

#endif
