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

File fileMirror(File file)
{
  return file^7;
}

Rank rankFlip(Rank rank)
{
  return rank^7;
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

Sq sqNorth(Sq sq, unsigned int n)
{
  assert(sqIsValid(sq));
  assert(sqRank(sq)<8-n);
  return sq+(8*n);
}

Sq sqSouth(Sq sq, unsigned int n)
{
  assert(sqIsValid(sq));
  assert(sqRank(sq)>=n);
  return sq-(8*n);
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
  return (colour==ColourWhite ? sqNorth(sq,1) : sqSouth(sq,1));
}

Sq sqBackwardOne(Sq sq, Colour colour)
{
  return (colour==ColourWhite ? sqSouth(sq,1) : sqNorth(sq,1));
}

bool sqIsLight(Sq sq)
{
  assert(sqIsValid(sq));
  return ((sqFile(sq)^sqRank(sq))&1);
}
