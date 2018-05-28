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
#define PieceTypeBit 3

// For each colour pieces are guaranteed to be consecutive and in the same order
// as in the piece types above.
#define PieceColourShift PieceTypeBit
#define PieceColourMask ((((1u)<<ColourBit)-1)<<PieceColourShift)
#define PieceTypeMask (((1u)<<PieceTypeBit)-1)
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
	PieceBPawn=(PieceColourMask|PieceTypePawn),
	PieceBKnight=(PieceColourMask|PieceTypeKnight),
	PieceBBishopL=(PieceColourMask|PieceTypeBishopL),
	PieceBBishopD=(PieceColourMask|PieceTypeBishopD),
	PieceBRook=(PieceColourMask|PieceTypeRook),
	PieceBQueen=(PieceColourMask|PieceTypeQueen),
	PieceBKing=(PieceColourMask|PieceTypeKing),
	PieceNB=2*PieceColourMask
} Piece;
#define PieceBit (PieceColourShift+ColourBit) // 4

bool pieceTypeIsValid(PieceType type);
bool pieceIsValid(Piece piece);

bool pieceTypeIsBishop(PieceType type);

Colour pieceGetColour(Piece piece);
PieceType pieceGetType(Piece piece);

Piece pieceMake(PieceType type, Colour colour);

char pieceToChar(Piece piece);
char pieceTypeToPromoChar(PieceType type);

#endif
