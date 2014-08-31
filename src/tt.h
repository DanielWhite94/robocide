#ifndef TT_H
#define TT_H

#include <stdbool.h>

#include "move.h"
#include "pos.h"
#include "score.h"

void ttInit(void);
void ttQuit(void);
void ttClear(void);
bool ttRead(const Pos *pos, unsigned int ply, Move *move, unsigned int *depth, Score *score, Bound *bound);
Move ttReadMove(const Pos *pos); // Either returns move or MoveInvalid if no match found.
void ttWrite(const Pos *pos, unsigned int ply, unsigned int depth, Move move, Score score, Bound bound);

#endif
