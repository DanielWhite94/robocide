#ifndef MOVE_H
#define MOVE_H

#include <stdbool.h>

#include "colour.h"
#include "piece.h"
#include "square.h"

typedef unsigned int Move;

// These first three defines should not need to be used outside of this module
STATICASSERT(SqNB<=(1u<<6));
STATICASSERT(PieceNB<=(1u<<4));
#define MoveShiftToSq 0
#define MoveShiftFromSq 6
#define MoveShiftToPiece 12

// These next two defines are intended to be used externally if needed
#define MoveBit 16 // Number of bits Move actually uses.
#define MoveMask ((((Move)1)<<MoveBit)-1)
#define MoveNB ((1lu)<<MoveBit)
#define MoveInvalid ((((Move)SqA1)<<MoveShiftFromSq)|(((Move)SqA1)<<MoveShiftToSq)|(((Move)PieceNone)<<MoveShiftToPiece)) // i.e. 0
STATICASSERT(MoveInvalid==0);

bool moveIsValid(Move move);

Move moveMake(Sq fromSq, Sq toSq, Piece toPiece); // see moveGetToPiece().

Sq moveGetFromSq(Move move);
Sq moveGetToSqRaw(Move move); // Returns rook's from square for castling
Piece moveGetToPiece(Move move); // Piece that will be on the ToSq after the move (e.g. the piece on FromSq unless a pawn promotion).
PieceType moveGetToPieceType(Move move);
Colour moveGetColour(Move move);

bool moveIsDP(Move move); // Is move a double pawn push.

#endif
