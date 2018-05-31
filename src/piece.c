#include <assert.h>

#include "piece.h"

const char PieceChar[PieceNB]={
	[PieceNone]='.',
	[PieceWPawn]='P', [PieceWKnight]='N', [PieceWBishopL]='B', [PieceWBishopD]='B', [PieceWRook]='R', [PieceWQueen]='Q', [PieceWKing]='K',
	[PieceBPawn]='p', [PieceBKnight]='n', [PieceBBishopL]='b', [PieceBBishopD]='b', [PieceBRook]='r', [PieceBQueen]='q', [PieceBKing]='k'
};
const char PromoChar[PieceTypeNB]={
	[PieceTypeKnight]='n', [PieceTypeBishopL]='b', [PieceTypeBishopD]='b', [PieceTypeRook]='r', [PieceTypeQueen]='q'
};

bool pieceTypeIsValid(PieceType type) {
	return (type>=PieceTypePawn && type<=PieceTypeKing);
}

bool pieceIsValid(Piece piece) {
	return ((piece>=PieceWPawn && piece<=PieceWKing) ||
	        (piece>=PieceBPawn && piece<=PieceBKing));
}

bool pieceTypeIsBishop(PieceType type) {
	return (type==PieceTypeBishopL || type==PieceTypeBishopD);
}

Colour pieceGetColour(Piece piece) {
	assert(pieceIsValid(piece));
	return (piece>>PieceColourShift);
}

PieceType pieceGetType(Piece piece) {
	assert(pieceIsValid(piece) || piece==PieceNone);
	return (piece&PieceTypeMask);
}

Piece pieceMake(PieceType type, Colour colour) {
	assert(pieceTypeIsValid(type));
	assert(colourIsValid(colour));
	return ((((Piece)colour)<<PieceColourShift)|((Piece)type));
}

char pieceToChar(Piece piece) {
	assert(pieceIsValid(piece) || piece==PieceNone);
	return PieceChar[piece];
}

char pieceTypeToPromoChar(PieceType type) {
	assert(type==PieceTypeKnight || type==PieceTypeBishopL || type==PieceTypeBishopD || type==PieceTypeRook || type==PieceTypeQueen);
	return PromoChar[type];
}
