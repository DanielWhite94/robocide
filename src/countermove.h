#ifndef COUNTERMOVE_H
#define COUNTERMOVE_H

#include "depth.h"
#include "move.h"

Move counterMoveGetResponseMove(Move prevMove);

void counterMoveCutoff(Move prevMove, Move responseMove);

void counterMoveClear(void);

#endif
