#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "score.h"

bool scoreIsValid(Score score) {
	return (score>=-ScoreInf && score<=ScoreInf);
}

int scoreValue(Score score) {
	assert(scoreIsValid(score));
	assert(!scoreIsMate(score));

	score%=0x2000;
	if (score>4096)
		score-=8192;
	else if (score<-4096)
		score+=8192;
	return score;
}

void scoreToStr(Score score, Bound bound, char str[static 32]) {
	assert(scoreIsValid(score));
	assert(boundIsValid(bound));
	assert(bound!=BoundNone);

	// Basic score (either in centipawns or distance to mate).
	if (scoreIsMate(score))
		sprintf(str, "mate %i", ((score<0) ? -scoreMateDistance(score) : scoreMateDistance(score)));
	else
		sprintf(str, "cp %i", scoreValue(score));

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

Score scoreMatedIn(unsigned int ply) {
	assert(ply<512);
	return -ScoreMate+ply;
}

int scoreMateDistance(Score score) {
	assert(scoreIsValid(score));
	assert(scoreIsMate(score));
	return ((1-abs(score)-scoreMatedIn(0))/2);
}
