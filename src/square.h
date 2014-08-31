#ifndef SQUARE_H
#define SQUARE_H

#include <stdbool.h>

#include "colour.h"

// Files are guaranteed to be consecutive starting from 0.
typedef enum { FileA, FileB, FileC, FileD, FileE, FileF, FileG, FileH, FileNB } File;

// Ranks are guaranteed to be consecutive starting from 0.
typedef enum { Rank1, Rank2, Rank3, Rank4, Rank5, Rank6, Rank7, Rank8, RankNB } Rank;

// Squares are guaranteed to be consecutive in the order A1,B1,...,H1,A2,...,H8, starting from 0.
typedef enum
{
  SqA1,SqB1,SqC1,SqD1,SqE1,SqF1,SqG1,SqH1,
  SqA2,SqB2,SqC2,SqD2,SqE2,SqF2,SqG2,SqH2,
  SqA3,SqB3,SqC3,SqD3,SqE3,SqF3,SqG3,SqH3,
  SqA4,SqB4,SqC4,SqD4,SqE4,SqF4,SqG4,SqH4,
  SqA5,SqB5,SqC5,SqD5,SqE5,SqF5,SqG5,SqH5,
  SqA6,SqB6,SqC6,SqD6,SqE6,SqF6,SqG6,SqH6,
  SqA7,SqB7,SqC7,SqD7,SqE7,SqF7,SqG7,SqH7,
  SqA8,SqB8,SqC8,SqD8,SqE8,SqF8,SqG8,SqH8,
  SqNB, SqInvalid=127
}Sq;

bool fileIsValid(File file);
bool rankIsValid(Rank rank);
bool sqIsValid(Sq sq);
char fileChar(File file);
char rankChar(Rank rank);
File fileFromChar(char c);
Rank rankFromChar(char c);
Sq sqMake(File file, Rank rank);
File sqFile(Sq sq);
Rank sqRank(Sq sq);
Sq sqMirror(Sq sq);
Sq sqFlip(Sq sq);
Sq sqNorthOne(Sq sq); // The following routines all assume sq will not fall off
Sq sqSouthOne(Sq sq); // the end of the board as a result of the operation.
Sq sqWestOne(Sq sq);
Sq sqEastOne(Sq sq);
Sq sqForwardOne(Sq sq, Colour colour);
Sq sqBackwardOne(Sq sq, Colour colour);
bool sqIsLight(Sq sq);

#endif