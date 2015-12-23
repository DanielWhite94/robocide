#include <assert.h>
#include <stdlib.h>

#include "attacks.h"
#include "magicmoves.h"

BB attacksArrayKnight[SqNB], attacksArrayKing[SqNB];

void attacksInit(void) {
	// Attack arrays for knight and king.
	Sq from, to;
	for(from=0;from<SqNB;++from) {
		attacksArrayKnight[from]=BBNone;
		attacksArrayKing[from]=BBNone;
		for(to=0;to<SqNB;++to) {
			int dX=abs(sqFile(from)-sqFile(to));
			int dY=abs(sqRank(from)-sqRank(to));
			if ((dX==1 && dY==2) || (dX==2 && dY==1))
				attacksArrayKnight[from]|=bbSq(to);
			if (dX<=1 && dY<=1 && (dX!=0 || dY!=0))
				attacksArrayKing[from]|=bbSq(to);
		}
	}
	
	// Magic move generation for sliders.
	initmagicmoves();
}

BB attacksPawn(Sq sq, Colour colour) {
	assert(colourIsValid(colour));
	return bbForwardOne(bbWingify(bbSq(sq)), colour);
}

BB attacksKnight(Sq sq) {
	assert(sqIsValid(sq));
	return attacksArrayKnight[sq];
}

BB attacksBishop(Sq sq, BB occ) {
	assert(sqIsValid(sq));
	return Bmagic(sq, occ);
}

BB attacksRook(Sq sq, BB occ) {
	assert(sqIsValid(sq));
	return Rmagic(sq, occ);
}

BB attacksQueen(Sq sq, BB occ) {
	assert(sqIsValid(sq));
	return (Bmagic(sq, occ)|Rmagic(sq, occ));
}

BB attacksKing(Sq sq) {
	assert(sqIsValid(sq));
	return attacksArrayKing[sq];
}

BB attacksPiece(Piece piece, Sq sq, BB occ) {
	assert(pieceIsValid(piece));
	assert(sqIsValid(sq));
	
	switch(pieceGetType(piece)) {
		case PieceTypePawn: return attacksPawn(sq, pieceGetColour(piece)); break;
		case PieceTypeKnight: return attacksKnight(sq); break;
		case PieceTypeBishopL: return attacksBishop(sq, occ); break;
		case PieceTypeBishopD: return attacksBishop(sq, occ); break;
		case PieceTypeRook: return attacksRook(sq, occ); break;
		case PieceTypeQueen: return attacksQueen(sq, occ); break;
		case PieceTypeKing: return attacksKing(sq); break;
		default: assert(false); return BBNone; break;
	}
}

BB attacksPieceType(PieceType type, Sq sq, BB occ) {
	assert(type>=PieceTypeKnight && type<=PieceTypeKing);
	assert(sqIsValid(sq));
	
	switch(type) {
		case PieceTypeKnight: return attacksKnight(sq); break;
		case PieceTypeBishopL: return attacksBishop(sq, occ); break;
		case PieceTypeBishopD: return attacksBishop(sq, occ); break;
		case PieceTypeRook: return attacksRook(sq, occ); break;
		case PieceTypeQueen: return attacksQueen(sq, occ); break;
		case PieceTypeKing: return attacksKing(sq); break;
		default: assert(false); return BBNone; break;
	}
}
