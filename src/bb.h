#ifndef BB_H
#define BB_H

#include <assert.h>
#include "types.h"

extern const bb_t BBA1, BBB1, BBC1, BBD1, BBE1, BBF1, BBG1, BBH1,
                  BBA8, BBB8, BBC8, BBD8, BBE8, BBF8, BBG8, BBH8;
extern const bb_t BBFileA, BBFileB, BBFileC, BBFileD,
                  BBFileE, BBFileF, BBFileG, BBFileH;
extern const bb_t BBRank1, BBRank2, BBRank3, BBRank4,
                  BBRank5, BBRank6, BBRank7, BBRank8;
extern const bb_t BBLight, BBDark;

extern bb_t BBBetween[64][64], BBBeyond[64][64];

extern const int BBScanForwardTable[64];

void BBInit();
static inline sq_t BBScanReset(bb_t *Set);
static inline sq_t BBScanForward(bb_t Set);
static inline bb_t BBNorthOne(bb_t Set);
static inline bb_t BBSouthOne(bb_t Set);
static inline bb_t BBWestOne(bb_t Set);
static inline bb_t BBEastOne(bb_t Set);
static inline bb_t BBNorthFill(bb_t Set);
static inline bb_t BBSouthFill(bb_t Set);
static inline bb_t BBFileFill(bb_t Set);
static inline bb_t BBSqToRank(sq_t Sq);
static inline bb_t BBWingify(bb_t Set);
static inline int BBPopCount(bb_t X);
static inline bb_t BBForwardOne(bb_t Set, col_t Colour);
void BBDraw(bb_t Set);

static inline sq_t BBScanReset(bb_t *Set)
{
  assert(Set!=0);
  sq_t Sq=BBScanForward(*Set);
  *Set&=((*Set)-1); // Reset LS1B
  return Sq;
}

static inline sq_t BBScanForward(bb_t Set)
{
  assert(Set!=0);
  return BBScanForwardTable[((Set^(Set-1))*0x03f79d71b4cb0a89llu)>>58];
}

static inline bb_t BBNorthOne(bb_t Set)
{
  return (Set<<8);
}

static inline bb_t BBSouthOne(bb_t Set)
{
  return (Set>>8);
}

static inline bb_t BBWestOne(bb_t Set)
{
  return ((Set & ~BBFileA)>>1);
}

static inline bb_t BBEastOne(bb_t Set)
{
  return ((Set & ~BBFileH)<<1);
}

static inline bb_t BBNorthFill(bb_t Set)
{
  Set|=(Set<<8);
  Set|=(Set<<16);
  Set|=(Set<<32);
  return Set;
}

static inline bb_t BBSouthFill(bb_t Set)
{
  Set|=(Set>>8);
  Set|=(Set>>16);
  Set|=(Set>>32);
  return Set;
}

static inline bb_t BBFileFill(bb_t Set)
{
  return (BBNorthFill(Set) | BBSouthFill(Set));
}

static inline bb_t BBSqToRank(sq_t Sq)
{
  return (BBRank1<<(SQ_Y(Sq)*8));
}

static inline bb_t BBWingify(bb_t Set)
{
  return (BBWestOne(Set) | BBEastOne(Set));
}

static inline int BBPopCount(bb_t X)
{
  X=X-((X>>1) & 0x5555555555555555llu);
  X=(X&0x3333333333333333llu)+((X>>2) & 0x3333333333333333llu);
  X=(X+(X>>4)) & 0x0f0f0f0f0f0f0f0fllu;
  X=(X*0x0101010101010101llu)>>56;
  return (int)X;
}

static inline bb_t BBForwardOne(bb_t Set, col_t Colour)
{
  return (Colour==white ? BBNorthOne(Set) : BBSouthOne(Set));
}

#endif
