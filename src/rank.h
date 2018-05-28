#ifndef RANK_H
#define RANK_H

#include <stdbool.h>

// Ranks are guaranteed to be consecutive starting from 0.
typedef enum { Rank1, Rank2, Rank3, Rank4, Rank5, Rank6, Rank7, Rank8, RankNB } Rank;
#define RankBit 3

bool rankIsValid(Rank rank);

char rankToChar(Rank rank);
Rank rankFromChar(char c);

Rank rankFlip(Rank rank);

#endif
