#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "move.h"
#include "util.h"

// Move is 16 bits composed of the following (from MSB to LSB):
// 4 bits - To-piece (the to-piece is the piece found on the to-sq after the move is made - i.e. the moving piece or promotion piece).
// 6 bits - From square.
// 6 bits - To square.
// Castling is represented soley by the king movement.
// 'Special' moves such as MoveInvalid and MoveNone are encoded with
// fromSq==toSq and toPiece undefined (these can never be real moves).

STATICASSERT(SqNB<=(1u<<6));
STATICASSERT(PieceNB<=(1u<<4));
#define MoveShiftToSq 0
#define MoveShiftFromSq 6
#define MoveShiftToPiece 12

const Move MoveInvalid=((((Move)SqA1)<<MoveShiftFromSq)|(((Move)SqA1)<<MoveShiftToSq));
const Move MoveNone=((((Move)SqB1)<<MoveShiftFromSq)|(((Move)SqB1)<<MoveShiftToSq));

const char MovePromoChar[PieceTypeNB]={
	[PieceTypeNone]='\0',
	[PieceTypePawn]='\0',
	[PieceTypeKnight]='n',
	[PieceTypeBishopL]='b',
	[PieceTypeBishopD]='b',
	[PieceTypeRook]='r',
	[PieceTypeQueen]='q',
	[PieceTypeKing]='\0'
};

bool moveIsValid(Move move) {
	return (moveGetFromSq(move)!=moveGetToSqRaw(move));
}

Move moveMake(Sq fromSq, Sq toSq, Piece toPiece) {
	assert(sqIsValid(fromSq));
	assert(sqIsValid(toSq));
	assert(pieceIsValid(toPiece));
	return (((Move)fromSq)<<MoveShiftFromSq)|(((Move)toSq)<<MoveShiftToSq)|(((Move)toPiece)<<MoveShiftToPiece);
}

Sq moveGetFromSq(Move move) {
	return (move>>MoveShiftFromSq)&63;
}

Sq moveGetToSqRaw(Move move) {
	return (move>>MoveShiftToSq)&63;
}

Piece moveGetToPiece(Move move) {
	assert(moveIsValid(move));
	return move>>MoveShiftToPiece;
}

PieceType moveGetToPieceType(Move move) {
	assert(moveIsValid(move));
	return pieceGetType(moveGetToPiece(move));
}

Colour moveGetColour(Move move) {
	assert(moveIsValid(move));
	return pieceGetColour(moveGetToPiece(move));
}

bool moveIsDP(Move move) {
	assert(moveIsValid(move));
	int toRank=sqRank(moveGetToSqRaw(move));
	int fromRank=sqRank(moveGetFromSq(move));
	return (moveGetToPieceType(move)==PieceTypePawn && abs(toRank-fromRank)==2);
}

