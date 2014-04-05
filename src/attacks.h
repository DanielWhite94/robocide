#ifndef ATTACKS_H
#define ATTACKS_H

#include <assert.h>
#include "magicmoves.h"
#include "types.h"

extern bb_t AttacksArrayKnight[64], AttacksArrayKing[64];

void AttacksInit();

static inline bb_t AttacksKnight(sq_t Sq)
{
  assert(SQ_ISVALID(Sq));
  return AttacksArrayKnight[Sq];
}

static inline bb_t AttacksBishop(sq_t Sq, bb_t Occ)
{
  assert(SQ_ISVALID(Sq));
  return Bmagic(Sq, Occ);
}

static inline bb_t AttacksRook(sq_t Sq, bb_t Occ)
{
  assert(SQ_ISVALID(Sq));
  return Rmagic(Sq, Occ);
}

static inline bb_t AttacksQueen(sq_t Sq, bb_t Occ)
{
  assert(SQ_ISVALID(Sq));
  return (Bmagic(Sq, Occ)|Rmagic(Sq, Occ));
}

static inline bb_t AttacksKing(sq_t Sq)
{
  assert(SQ_ISVALID(Sq));
  return AttacksArrayKing[Sq];
}

#endif
