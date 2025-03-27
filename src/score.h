#ifndef SCORE_H
#define SCORE_H

#include <stdbool.h>

#include "bound.h"

#define SCORETOSTRMAXLEN 32
#define SCORETOSTR(score, bound) ({char *sstr=alloca(SCORETOSTRMAXLEN); scoreToStr((score), (bound), sstr); (const char *)sstr;})

typedef enum {
	ScoreInvalid=-0x8000,
	ScoreDraw=0,
	ScoreHardWin=0x2000, // e.g. KBNvK
	ScoreEasyWin=0x4000, // e.g. KRvK
	ScoreMate=0x6000, // forced mate
	ScoreInf=0x7FFF,
} Score;
#define ScoreBit 16 // Number of bits Score actually uses.

bool scoreIsValid(Score score);

int scoreValue(Score score); // Returns value in centi-pawns. Score should not be a mate score.

void scoreToStr(Score score, Bound bound, char str[static SCORETOSTRMAXLEN]); // "cp/mate VALUE/DISTANCE [lowerbound/upperbound]"

bool scoreIsMate(Score score);
Score scoreMateIn(unsigned int ply);
Score scoreMatedIn(unsigned int ply);
int scoreMateDistance(Score score);

#endif
