#ifndef KILLERS_H
#define KILLERS_H

#include "depth.h"
#include "move.h"
#include "moveset.h"

#define KillersPerPly MoveSetSize

Move killersGetN(Depth ply, unsigned index);
bool killersMoveIsKiller(Depth ply, Move move);

void killersCutoff(Depth ply, Move move);

void killersClear(void);

#endif
