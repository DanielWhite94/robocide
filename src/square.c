#include <assert.h>
#include <stdlib.h>

#include "square.h"
#include "util.h"

bool sqIsValid(Sq sq) {
	return (sq>=SqA1 && sq<=SqH8);
}

Sq sqMake(File file, Rank rank) {
	assert(fileIsValid(file));
	assert(rankIsValid(rank));
	return (rank<<FileBit)+file;
}

File sqFile(Sq sq) {
	assert(sqIsValid(sq));
	return (sq&SqFileMask);
}

Rank sqRank(Sq sq) {
	assert(sqIsValid(sq));
	return (sq>>FileBit);
}

Sq sqMirror(Sq sq) {
	assert(sqIsValid(sq));
	return (sq^SqFileMask);
}

Sq sqFlip(Sq sq) {
	assert(sqIsValid(sq));
	return (sq^SqRankMask);
}

Sq sqNormalise(Sq sq, Colour colour) {
	return (colour==ColourWhite ? sq : sqFlip(sq));
}

Sq sqNorth(Sq sq, unsigned int n) {
	assert(sqIsValid(sq));
	assert(sqRank(sq)<8-n);
	return sq+(8*n);
}

Sq sqSouth(Sq sq, unsigned int n) {
	assert(sqIsValid(sq));
	assert(sqRank(sq)>=n);
	return sq-(8*n);
}

Sq sqWestOne(Sq sq) {
	assert(sqIsValid(sq));
	assert(sqFile(sq)!=FileA);
	return sq-1;
}

Sq sqEastOne(Sq sq) {
	assert(sqIsValid(sq));
	assert(sqFile(sq)!=FileH);
	return sq+1;
}

Sq sqForwardOne(Sq sq, Colour colour) {
	assert(sqIsValid(sq));
	assert(colourIsValid(colour));
	return (colour==ColourWhite ? sqNorth(sq,1) : sqSouth(sq,1));
}

Sq sqBackwardOne(Sq sq, Colour colour) {
	assert(sqIsValid(sq));
	assert(colourIsValid(colour));
	return (colour==ColourWhite ? sqSouth(sq,1) : sqNorth(sq,1));
}

bool sqIsLight(Sq sq) {
	assert(sqIsValid(sq));
	return ((sqFile(sq)^sqRank(sq))&1);
}

unsigned sqDist(Sq a, Sq b) {
	int dx=abs(((int)sqFile(a))-((int)sqFile(b)));
	int dy=abs(((int)sqRank(a))-((int)sqRank(b)));
	return utilMax(dx, dy);
}
