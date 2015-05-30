#ifndef KILLERS_H
#define KILLERS_H

#include "depth.h"
#include "move.h"

#define KillersPerPly 4

extern Move killers[DepthMax][KillersPerPly];

void killersCutoff(Depth ply, Move move);
void killersClear(void);

#endif