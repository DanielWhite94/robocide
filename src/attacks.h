#ifndef ATTACKS_H
#define ATTACKS_H

#include "bb.h"
#include "piece.h"
#include "square.h"

void attacksInit(void);
BB attacksKnight(Sq sq);
BB attacksBishop(Sq sq, BB occ);
BB attacksRook(Sq sq, BB occ);
BB attacksQueen(Sq sq, BB occ);
BB attacksKing(Sq sq);
BB attacksPiece(Piece piece, Sq sq, BB occ);
BB attacksPieceType(PieceType type, Sq sq, BB occ); // As this is colour-agnostic pawns are not supported.

#endif
