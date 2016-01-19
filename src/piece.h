#ifndef PIECE_H
#define PIECE_H

#include <stdbool.h>

#include "colour.h"
#include "util.h"

// Piece types are guaranteed to be consecutive and in this order.
typedef enum {
	PieceTypeNone,
	PieceTypePawn,
	PieceTypeKnight,
	PieceTypeBishopL,
	PieceTypeBishopD,
	PieceTypeRook,
	PieceTypeQueen,
	PieceTypeKing,
	PieceTypeNB
} PieceType;

// For each colour pieces are guaranteed to be consecutive and in the same order
// as in the piece types above.
STATICASSERT((PieceTypeNB<=8)); // To ensure white and black pieces don't overlap.
typedef enum {
	PieceNone=PieceTypeNone,
	PieceWPawn=PieceTypePawn,
	PieceWKnight=PieceTypeKnight,
	PieceWBishopL=PieceTypeBishopL,
	PieceWBishopD=PieceTypeBishopD,
	PieceWRook=PieceTypeRook,
	PieceWQueen=PieceTypeQueen,
	PieceWKing=PieceTypeKing,
	PieceBPawn=(8|PieceTypePawn),
	PieceBKnight=(8|PieceTypeKnight),
	PieceBBishopL=(8|PieceTypeBishopL),
	PieceBBishopD=(8|PieceTypeBishopD),
	PieceBRook=(8|PieceTypeRook),
	PieceBQueen=(8|PieceTypeQueen),
	PieceBKing=(8|PieceTypeKing),
	PieceNB=2*8
} Piece;
#define PieceBit 4

bool pieceTypeIsValid(PieceType type);
bool pieceIsValid(Piece piece);

Colour pieceGetColour(Piece piece);
PieceType pieceGetType(Piece piece);

Piece pieceMake(PieceType type, Colour colour);

char pieceToChar(Piece piece);
char pieceTypeToPromoChar(PieceType type);

#endif
