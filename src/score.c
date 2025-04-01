#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "score.h"

bool scoreIsValid(Score score) {
	return (score>=-ScoreInf && score<=ScoreInf);
}

void scoreToStr(Score score, Bound bound, char str[static 32]) {
	assert(scoreIsValid(score));
	assert(boundIsValid(bound));
	assert(bound!=BoundNone);

	// Basic score (either in centipawns or distance to mate).
	if (scoreIsMate(score))
		sprintf(str, "mate %i", ((score<0) ? -scoreMateDistanceMoves(score) : scoreMateDistanceMoves(score)));
	else
		sprintf(str, "cp %i", score);

	// Upper/lowerbound?
	if (bound==BoundLower)
		strcat(str, " lowerbound");
	else if (bound==BoundUpper)
		strcat(str, " upperbound");
}

bool scoreIsMate(Score score) {
	assert(scoreIsValid(score));
	return (abs(abs(score)+scoreMatedIn(0))<512);
}

Score scoreMateIn(unsigned int ply) {
	assert(ply<512);
	return ScoreMate-ply; // ScoreMate to indicate giving checkmate, -ply to give shorter mates a higher score (i.e. do not delay giving the mate).
}

Score scoreMatedIn(unsigned int ply) {
	assert(ply<512);
	return -ScoreMate+ply; // -ScoreMate to indicate being checkmated, +ply to give longer mates a higher score (i.e. delay).
}

int scoreMateDistancePly(Score score) {
	assert(scoreIsValid(score));
	assert(scoreIsMate(score));
	return -abs(score)-scoreMatedIn(0);
}

int scoreMateDistanceMoves(Score score) {
	assert(scoreIsValid(score));
	assert(scoreIsMate(score));
	return (scoreMateDistancePly(score)+1)/2;
}
