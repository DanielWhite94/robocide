#include <assert.h>
#include <string.h>

#include "bb.h"
#include "uci.h"
#include "util.h"

const BB BBNone=0x0000000000000000llu, BBAll=0xFFFFFFFFFFFFFFFFllu;
const BB BBLight=0x55AA55AA55AA55AAllu, BBDark=0xAA55AA55AA55AA55llu;

const BB BBFileA=0x0101010101010101llu, BBRank1=0x00000000000000FFllu;

const unsigned int BBScanForwardTable[64]={
   0, 47,  1, 56, 48, 27,  2, 60,
  57, 49, 41, 37, 28, 16,  3, 61,
  54, 58, 35, 52, 50, 42, 21, 44,
  38, 32, 29, 23, 17, 11,  4, 62,
  46, 55, 26, 59, 40, 36, 15, 53,
  34, 51, 20, 43, 31, 22, 10, 45,
  25, 39, 14, 33, 19, 30,  9, 24,
  13, 18,  8, 12,  7,  6,  5, 63
};

BB BBBetween[SqNB][SqNB], BBBeyond[SqNB][SqNB];

void bbInit(void)
{
  // Note: The code below uses 0x88 coordinates
# define TOSQ(s) (sqMake(((s)&0xF0)>>4 , ((s)&0x07)))
  memset(BBBetween, 0, sizeof(BB)*SqNB*SqNB);
  memset(BBBeyond, 0, sizeof(BB)*SqNB*SqNB);
  unsigned int sqA, sqB, sqC;
  const int dirs[8]={-17,-16,-15,-1,+1,+15,+16,+17};
  int dirI, dir;
  // Loop over every square, then every direction, then every square in that
  // direction
  for(sqA=0;sqA<128;sqA=((sqA+9)&~8))
    for(dirI=0;dirI<8;++dirI)
    {
      Sq sqA64=TOSQ(sqA);
      dir=dirs[dirI];
      BB set=BBNone;
      for(sqB=sqA+dir;;sqB+=dir)
      {
        // Bad square?
        if (sqB & 0x88)
          break;
        
        // Set BBBetween array
        Sq sqB64=TOSQ(sqB);
        BBBetween[sqA64][sqB64]=set;
        
        // Set BBBeyond array
        for(sqC=sqB+dir;!(sqC & 0x88);sqC+=dir)
          BBBeyond[sqA64][sqB64]|=bbSq(TOSQ(sqC));
        
        // Add current square to set
        set|=bbSq(sqB64);
      }
    }
# undef TOSQ
}

void bbDraw(BB bb)
{
  unsigned int x, y;
  for(y=7;y>=0;--y)
  {
    for(x=0;x<8;++x)
      uciWrite(" %i", (bbSq(sqMake(x,y)) & bb)!=BBNone);
    uciWrite("\n");
  }
}

BB bbSq(Sq sq)
{
  STATICASSERT(SqNB<=64);
  return ((BB)1)<<sq;
}

BB bbFile(File file)
{
  assert(fileIsValid(file));
  return (BBFileA<<file);
}

BB bbRank(Rank rank)
{
  assert(rankIsValid(rank));
  return (BBRank1<<(8*rank));
}

unsigned int bbPopCount(BB bb)
{
  bb=bb-((bb>>1) & 0x5555555555555555llu);
  bb=(bb&0x3333333333333333llu)+((bb>>2) & 0x3333333333333333llu);
  bb=(bb+(bb>>4)) & 0x0f0f0f0f0f0f0f0fllu;
  bb=(bb*0x0101010101010101llu)>>56;
  return (unsigned int)bb;
}

Sq bbScanReset(BB *bb)
{
  assert(*bb!=BBNone);
  Sq sq=bbScanForward(*bb);
  *bb&=(*bb-1); // Reset least-significant bit.
  return sq;
}

Sq bbScanForward(BB bb)
{
  assert(bb!=BBNone);
  return BBScanForwardTable[((bb^(bb-1))*0x03f79d71b4cb0a89llu)>>58];
}

BB bbNorth(BB bb, unsigned int n)
{
  return (bb<<(8*n));
}

BB bbSouth(BB bb, unsigned int n)
{
  return (bb>>(8*n));
}

BB bbNorthOne(BB bb)
{
  return bbNorth(bb, 1);
}

BB bbSouthOne(BB bb)
{
  return bbSouth(bb, 1);
}

BB bbWestOne(BB bb)
{
  return ((bb & ~bbFile(FileA))>>1);
}

BB bbEastOne(BB bb)
{
  return ((bb & ~bbFile(FileH))<<1);
}

BB bbForwardOne(BB bb, Colour colour)
{
  return (colour==ColourWhite ? bbNorthOne(bb) : bbSouthOne(bb));
}

BB bbNorthFill(BB bb)
{
  bb|=bbNorth(bb, 1);
  bb|=bbNorth(bb, 2);
  bb|=bbNorth(bb, 4);
  return bb;
}

BB bbSouthFill(BB bb)
{
  bb|=bbSouth(bb, 1);
  bb|=bbSouth(bb, 2);
  bb|=bbSouth(bb, 4);
  return bb;
}

BB bbFileFill(BB bb)
{
  return bbNorthFill(bb) | bbSouthFill(bb);
}

BB bbWingify(BB bb)
{
  return bbWestOne(bb) | bbEastOne(bb);
}

BB bbBetween(BB sq1, BB sq2)
{
  assert(sqIsValid(sq1));
  assert(sqIsValid(sq2));
  return BBBetween[sq1][sq2];
}

BB bbBeyond(BB sq1, BB sq2)
{
  assert(sqIsValid(sq1));
  assert(sqIsValid(sq2));
  return BBBeyond[sq1][sq2];
}
