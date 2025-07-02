#include "../attacks.h"
#include "../bb.h"
#include "../colour.h"
#include "wrapper.h"

unsigned wrapperBbPopCount(uint64_t x) {
	return bbPopCount(x);
}

unsigned wrapperBbScanForward(uint64_t x) {
	return bbScanForward(x);
}

uint64_t wrapperAttacksKing(unsigned square) {
	return attacksKing(square);
}

uint64_t wrapperAttacksKnight(unsigned square) {
	return attacksKnight(square);
}

uint64_t wrapperAttacksRook(unsigned square, uint64_t occ) {
	return attacksRook(square, occ);
}

uint64_t wrapperAttacksBishop(unsigned square, uint64_t occ) {
	return attacksBishop(square, occ);
}

uint64_t wrapperAttacksQueen(unsigned square, uint64_t occ) {
	return attacksQueen(square, occ);
}

uint64_t wrapperAttacksPawn(unsigned square, unsigned colour) {
	// Note: probing code has white/black definitions flipped compared to ours
	STATICASSERT(ColourWhite==0);
	STATICASSERT(ColourBlack==1);
	return attacksPawn(square, colourSwap(colour));
}
