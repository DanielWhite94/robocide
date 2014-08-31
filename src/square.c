#include <assert.h>

#include "square.h"

bool fileIsValid(File file)
{
  return (file>=FileA && file<=FileH);
}

bool rankIsValid(Rank rank)
{
  return (rank>=Rank1 && rank<=Rank8);
}

bool sqIsValid(Sq sq)
{
  return (sq>=SqA1 && sq<=SqH8);
}

char fileChar(File file)
{
  assert(fileIsValid(file));
  return file+'a';
}

char rankChar(Rank rank)
{
  assert(rankIsValid(rank));
  return rank+'1';
}

File fileFromChar(char c)
{
  return c-'a';
}

Rank rankFromChar(char c)
{
  return c-'1';
}

Sq sqMake(File file, Rank rank)
{
  return (rank<<3)+file;
}

File sqFile(Sq sq)
{
  assert(sqIsValid(sq));
  return (sq&7);
}

Rank sqRank(Sq sq)
{
  assert(sqIsValid(sq));
  return (sq>>3);
}

Sq sqMirror(Sq sq)
{
  assert(sqIsValid(sq));
  return (sq^7);
}

Sq sqFlip(Sq sq)
{
  assert(sqIsValid(sq));
  return (sq^56);
}

Sq sqNorthOne(Sq sq)
{
  assert(sqIsValid(sq));
  assert(sqRank(sq)!=Rank8);
  return sq+8;
}

Sq sqSouthOne(Sq sq)
{
  assert(sqIsValid(sq));
  assert(sqRank(sq)!=Rank1);
  return sq-8;
}

Sq sqWestOne(Sq sq)
{
  assert(sqIsValid(sq));
  assert(sqFile(sq)!=FileA);
  return sq-1;
}

Sq sqEastOne(Sq sq)
{
  assert(sqIsValid(sq));
  assert(sqFile(sq)!=FileH);
  return sq+1;
}

Sq sqForwardOne(Sq sq, Colour colour)
{
  return (colour==ColourWhite ? sqNorthOne(sq) : sqSouthOne(sq));
}

Sq sqBackwardOne(Sq sq, Colour colour)
{
  return (colour==ColourWhite ? sqSouthOne(sq) : sqNorthOne(sq));
}

bool sqIsLight(Sq sq)
{
  assert(sqIsValid(sq));
  return ((sqFile(sq)^sqRank(sq))&1);
}
