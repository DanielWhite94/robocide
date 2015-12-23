#include <assert.h>

#include "rank.h"

bool rankIsValid(Rank rank) {
	return (rank>=Rank1 && rank<=Rank8);
}

char rankToChar(Rank rank) {
	assert(rankIsValid(rank));
	return rank+'1';
}

Rank rankFromChar(char c) {
	assert(c>=rankToChar(Rank1) && c<=rankToChar(Rank8));
	return c-'1';
}

Rank rankFlip(Rank rank) {
	assert(rankIsValid(rank));
	return rank^7;
}
