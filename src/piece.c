#include <assert.h>

#include "piece.h"

const char PieceChar[PieceNB]={
	[PieceNone]='.',
	[PieceWPawn]='P', [PieceWKnight]='N', [PieceWBishop]='B', [PieceWRook]='R', [PieceWQueen]='Q', [PieceWKing]='K',
	[PieceBPawn]='p', [PieceBKnight]='n', [PieceBBishop]='b', [PieceBRook]='r', [PieceBQueen]='q', [PieceBKing]='k'
};
const char PromoChar[PieceTypeNB]={
	[PieceTypeKnight]='n', [PieceTypeBishop]='b', [PieceTypeRook]='r', [PieceTypeQueen]='q'
};
const char PieceTypeStr[PieceTypeNB][8]={
	[PieceTypePawn]="Pawn",
	[PieceTypeKnight]="Knight",
	[PieceTypeBishop]="Bishop",
	[PieceTypeRook]="Rook",
	[PieceTypeQueen]="Queen",
	[PieceTypeKing]="King"
};

bool pieceTypeIsValid(PieceType type) {
	return (type>=PieceTypePawn && type<=PieceTypeKing);
}

bool pieceIsValid(Piece piece) {
	return ((piece>=PieceWPawn && piece<=PieceWKing) ||
	        (piece>=PieceBPawn && piece<=PieceBKing));
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
	assert(type==PieceTypeKnight || type==PieceTypeBishop || type==PieceTypeRook || type==PieceTypeQueen);
	return PromoChar[type];
}

const char *pieceTypeToStr(PieceType type) {
	assert(pieceTypeIsValid(type));
	return PieceTypeStr[type];
}
