#include <assert.h>
#include <stdio.h>

#include "moveset.h"

Move moveSetGetN(MoveSet set, unsigned index) {
	assert(index<MoveSetSize);

	return ((set>>(index*MoveBit))&MoveMask);
}

bool moveSetContains(MoveSet set, Move move) {
	// TODO: try SWAR to do 4 comparisons in parallel
	for(unsigned i=0; i<MoveSetSize; ++i)
		if (move==moveSetGetN(set, i))
			return true;
	return false;
}

void moveSetAdd(MoveSet *set, Move move) {
	// TODO: can probably optimise this loop-get code

	// Check if move is already a killer (and identify the slot it occupies)
	for(unsigned i=0; i<MoveSetSize; ++i) {
		if (moveSetGetN(*set, i)==move) {
			// Shift moves 'before' existing killer 'down one' (overwriting it), and move said killer to the 'front'.
			MoveSet keepMask =(0xFFFFFFFFFFFFFFFFllu<<(MoveBit+MoveBit*i-1))<<1;
			MoveSet shiftMask=(0xFFFFFFFFFFFFFFFFllu>>(63-MoveBit*i))>>1;
			*set=(*set & keepMask)|((*set & shiftMask)<<MoveBit)|move;
			return;
		}
	}

	// Shift existing moves down and add new one
	*set=((*set<<MoveBit)|move);

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
