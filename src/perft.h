#ifndef PERFT_H
#define PERFT_H

#include "depth.h"
#include "pos.h"

void perft(Pos *pos, Depth maxDepth);
void divide(Pos *pos, Depth depth);
unsigned long long int perftRaw(Pos *pos, Depth depth);

#endif
