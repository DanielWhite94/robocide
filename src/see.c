#include <assert.h>

#include "attacks.h"
#include "bb.h"
#include "see.h"

const int seePieceValue[PieceTypeNB]={
	[PieceTypeNone]=0,
	[PieceTypePawn]=1,
	[PieceTypeKnight]=3,
	[PieceTypeBishopL]=3,
	[PieceTypeBishopD]=3,
	[PieceTypeRook]=5,
	[PieceTypeQueen]=9,
	[PieceTypeKing]=255
};

////////////////////////////////////////////////////////////////////////////////
// Private prototypes.
////////////////////////////////////////////////////////////////////////////////

BB seeGetLeastValuable(const Pos *pos, BB atkDef, Colour colour, PieceType *pieceType);
BB seeAttacksTo(const Pos *pos, Sq sq, BB occ);

////////////////////////////////////////////////////////////////////////////////
// Public functions.
////////////////////////////////////////////////////////////////////////////////

int see(const Pos *pos, Sq fromSq, Sq toSq) {
	PieceType attackerType=pieceGetType(posGetPieceOnSq(pos, fromSq));
	Piece victimType=pieceGetType(posGetPieceOnSq(pos, toSq));
	Colour stm=posGetSTM(pos);
	assert(pieceTypeIsValid(attackerType));
	assert(pieceTypeIsValid(victimType));

	BB occ=posGetBBAll(pos);
	BB atkDef=seeAttacksTo(pos, toSq, occ);
	BB fromSet=bbSq(fromSq);

	BB mayXRay=occ^posGetBBPiece(pos, PieceWKnight)^posGetBBPiece(pos, PieceWKing)^
								 posGetBBPiece(pos, PieceBKnight)^posGetBBPiece(pos, PieceBKing);
	assert(mayXRay==(posGetBBPiece(pos, PieceWPawn)|posGetBBPiece(pos, PieceBPawn)|
									 posGetBBPiece(pos, PieceWBishopL)|posGetBBPiece(pos, PieceBBishopL)|
									 posGetBBPiece(pos, PieceWBishopD)|posGetBBPiece(pos, PieceBBishopD)|
									 posGetBBPiece(pos, PieceWRook)|posGetBBPiece(pos, PieceBRook)|
									 posGetBBPiece(pos, PieceWQueen)|posGetBBPiece(pos, PieceBQueen)));

	int gain[32], depth=0;
	gain[depth]=seePieceValue[victimType];
	do {
		++depth;
		gain[depth]=seePieceValue[attackerType]-gain[depth-1]; // Speculative store, if defended.

		// Pruning.
		if (utilMax(-gain[depth-1], gain[depth])<0)
			break;

		// 'Capture'.
		occ^=fromSet; // Remove piece from occupancy.
		atkDef^=fromSet; // Remove piece from attacks & defenders list.
		stm=colourSwap(stm); // Swap colour.

		// Add any new attacks needed.
		if (fromSet & mayXRay)
			atkDef|=seeAttacksTo(pos, toSq, occ); // TODO: Something more intelligent?

		// Look for next attacker.
		fromSet=seeGetLeastValuable(pos, atkDef, stm, &attackerType);
	} while(fromSet);

	while (--depth)
		gain[depth-1]=-utilMax(-gain[depth-1], gain[depth]);

	return gain[0];
}

int seeSign(const Pos *pos, Sq fromSq, Sq toSq) {
	// No need for SEE?
	PieceType victimType=pieceGetType(posGetPieceOnSq(pos, toSq));
	PieceType attackerType=pieceGetType(posGetPieceOnSq(pos, fromSq));
	if (victimType==PieceTypeNone || seePieceValue[attackerType]<=seePieceValue[victimType])
		return 0;

	return see(pos, fromSq, toSq);
}

////////////////////////////////////////////////////////////////////////////////
// Private Functions.
////////////////////////////////////////////////////////////////////////////////

BB seeGetLeastValuable(const Pos *pos, BB atkDef, Colour colour, PieceType *pieceType) {
	for(*pieceType=PieceTypePawn;*pieceType<=PieceTypeKing;++*pieceType) {
		BB set=(atkDef & posGetBBPiece(pos, pieceMake(*pieceType, colour)));
		if (set)
			return (set & -set);
	}

	// No attackers
	return BBNone;
}

BB seeAttacksTo(const Pos *pos, Sq sq, BB occ) {
	BB set=BBNone;

	// Pawns.
	BB wing=bbWingify(bbSq(sq));
	set|=((bbForwardOne(wing, ColourWhite) & posGetBBPiece(pos, PieceWPawn)) |
	      (bbForwardOne(wing, ColourBlack) & posGetBBPiece(pos, PieceBPawn)));

	// Knights.
	set|=(attacksKnight(sq) & (posGetBBPiece(pos, PieceWKnight) | posGetBBPiece(pos, PieceBKnight)));

	// Diagonal sliders.
	set|=(attacksBishop(sq, occ) &
	      (posGetBBPiece(pos, PieceWBishopL) | posGetBBPiece(pos, PieceBBishopL) |
	       posGetBBPiece(pos, PieceWBishopD) | posGetBBPiece(pos, PieceBBishopD) |
	       posGetBBPiece(pos, PieceWQueen) | posGetBBPiece(pos, PieceBQueen)));

	// Horizontal/vertical sliders.
	set|=(attacksRook(sq, occ) &
	      (posGetBBPiece(pos, PieceWRook) | posGetBBPiece(pos, PieceBRook) |
	       posGetBBPiece(pos, PieceWQueen) | posGetBBPiece(pos, PieceBQueen)));

	// Kings.
	set|=(attacksKing(sq) & (posGetBBPiece(pos, PieceWKing) | posGetBBPiece(pos, PieceBKing)));

	return (set & occ);
}
