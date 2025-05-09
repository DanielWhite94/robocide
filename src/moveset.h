#ifndef MOVESET_H
#define MOVESET_H

#include <stdint.h>

#include "move.h"
#include "util.h"

STATICASSERT(MoveBit==16);
#define MoveSetSize 4
typedef uint64_t MoveSet; // four 16-bit moves packed into a single 64-bit integer

STATICASSERT(MoveInvalid==0);
#define MoveSetEmpty ((((MoveSet)MoveInvalid)<<0)|(((MoveSet)MoveInvalid)<<16)|(((MoveSet)MoveInvalid)<<32)|(((MoveSet)MoveInvalid)<<48))
STATICASSERT(MoveSetEmpty==0);

Move moveSetGetN(MoveSet set, unsigned index); // 0<=index<MoveSetSize
bool moveSetContains(MoveSet set, Move move);

void moveSetAdd(MoveSet *set, Move move);

void moveSetDebug(MoveSet set);

#endif
