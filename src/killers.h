#ifndef KILLERS_H
#define KILLERS_H

#include "depth.h"
#include "move.h"

#define KillersPerPly 4

typedef struct {
	Move moves[DepthMax][KillersPerPly];
} Killers;

extern Killers killersDummy;

void killersInit(void);

Move killersGet(const Killers *killers, Depth ply, unsigned killerIndex);
void killersCutoff(Killers *killers, Depth ply, Move move);
void killersClear(Killers *killers);

#endif
