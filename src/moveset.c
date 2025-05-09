#include <assert.h>
#include <stdio.h>

#include "moveset.h"

////////////////////////////////////////////////////////////////////////////////
// Private prototypes.
////////////////////////////////////////////////////////////////////////////////

MoveSet moveSetDetectMove(MoveSet input, Move move); // If move exists in the input set, then the returned set will have a single bit set at the MSB of the word representing the move. Otherwise returns 0.

////////////////////////////////////////////////////////////////////////////////
// Public functions.
////////////////////////////////////////////////////////////////////////////////

Move moveSetGetN(MoveSet set, unsigned index) {
	assert(index<MoveSetSize);

	return ((set>>(index*MoveBit))&MoveMask);
}

bool moveSetContains(MoveSet set, Move move) {
	return (moveSetDetectMove(set, move)!=0);
}

void moveSetAdd(MoveSet *set, Move move) {
	MoveSet moveSet=moveSetDetectMove(*set, move);

	// Use moveSet to shift moves 'before' killer 'down one' (overwriting it), and move said killer to the 'front'.
	// Also, due to an extra step in shiftMask calculation, this code will also handle the case where the move is not already in the set (i.e. moveSet==0) correctly.
	MoveSet keepMask=0xFFFFFFFFFFFFFFFEllu^((moveSet-1)<<1);
	MoveSet shiftMask=((moveSet-1) & 0x7FFFFFFFFFFFFFFFllu)>>(MoveBit-1);
	*set=(*set & keepMask)|((*set & shiftMask)<<MoveBit)|move;

	return;
}

void moveSetDebug(MoveSet set) {
	printf("set=%016lX", set);
	for(unsigned i=0; i<MoveSetSize; ++i) {
		Move move=moveSetGetN(set, i);
		printf(" %c%c%c%c", fileToChar(sqFile(moveGetFromSq(move))), rankToChar(sqRank(moveGetFromSq(move))), fileToChar(sqFile(moveGetToSqRaw(move))), rankToChar(sqRank(moveGetToSqRaw(move))));
	}
	printf("\n");
}

////////////////////////////////////////////////////////////////////////////////
// Private functions.
////////////////////////////////////////////////////////////////////////////////

MoveSet moveSetDetectMove(MoveSet input, Move move) {
	// Create a MoveSet with 4 copies of the given move
	MoveSet m=move;
	m=(m<<MoveBit)|m;
	m=(m<<2*MoveBit)|m;

	// XOR this with the given set so that a match will contain 16 zero bits in that slot
	m^=input;

	// Use classic SWAR method to find (first) zero word
	STATICASSERT(MoveBit==16);
	MoveSet m7=(m & 0x7fff7fff7fff7fffllu)+0x7fff7fff7fff7fffllu;
	return ~(m7 | m | 0x7fff7fff7fff7fffllu);
}
