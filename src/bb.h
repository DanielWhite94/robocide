#ifndef BB_H
#define BB_H

#include <assert.h>
#include "types.h"

extern const bb_t BBB1, BBC1, BBD1, BBE1, BBF1, BBG1,
                  BBB8, BBC8, BBD8, BBE8, BBF8, BBG8;
extern const bb_t BBFileA, BBFileB, BBFileC, BBFileD,
                  BBFileE, BBFileF, BBFileG, BBFileH;
extern const bb_t BBRank1, BBRank2, BBRank3, BBRank4,
                  BBRank5, BBRank6, BBRank7, BBRank8;

extern const int BBScanForwardTable[64];

static inline bb_t BBSqToBB(sq_t Sq);
static inline sq_t BBScanReset(bb_t *Set);
static inline sq_t BBScanForward(bb_t Set);
static inline bb_t BBNorthOne(bb_t Set);
static inline bb_t BBSouthOne(bb_t Set);
static inline bb_t BBWestOne(bb_t Set);
static inline bb_t BBEastOne(bb_t Set);
static inline bb_t BBWingify(bb_t Set);

static inline bb_t BBSqToBB(sq_t Sq)
{
  return (((bb_t)1)<<Sq);
}

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

static inline bb_t BBWingify(bb_t Set)
{
  return (BBWestOne(Set) | BBEastOne(Set));
}

#endif
