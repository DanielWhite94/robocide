#ifndef MOVE_H
#define MOVE_H

#include <stdbool.h>

#include "colour.h"
#include "piece.h"
#include "square.h"

typedef unsigned int Move;

#define MoveBit 16 // Number of bits Move actually uses.
extern const Move MoveInvalid; // i.e. undefined/not set.

bool moveIsValid(Move move);

Move moveMake(Sq fromSq, Sq toSq, Piece toPiece); // see moveGetToPiece().

Sq moveGetFromSq(Move move);
Sq moveGetToSqRaw(Move move); // Returns rook's from square for castling
Piece moveGetToPiece(Move move); // Piece that will be on the ToSq after the move (e.g. the piece on FromSq unless a pawn promotion).
PieceType moveGetToPieceType(Move move);
Colour moveGetColour(Move move);

bool moveIsDP(Move move); // Is move a double pawn push.

#endif
