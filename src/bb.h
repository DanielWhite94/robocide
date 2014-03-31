#ifndef BB_H
#define BB_H

#include "types.h"

static inline bb_t BBSqToBB(sq_t Sq)
{
  return (((bb_t)1)<<Sq);
}

#endif
