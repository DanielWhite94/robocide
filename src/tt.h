#ifndef TT_H
#define TT_H

#include <stdbool.h>

#include "depth.h"
#include "move.h"
#include "pos.h"
#include "score.h"

void ttInit(void);
void ttQuit(void);

void ttClear(void);

bool ttRead(const Pos *pos, Depth ply, Move *move, Depth *depth, Score *score, Bound *bound);
Move ttReadMove(const Pos *pos); // Either returns move or MoveInvalid if no match found.
void ttWrite(const Pos *pos, Depth ply, Depth depth, Move move, Score score, Bound bound);

#endif
