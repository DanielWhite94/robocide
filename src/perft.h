#ifndef PERFT_H
#define PERFT_H

#include "pos.h"

void perft(Pos *pos, unsigned int maxDepth);
void divide(Pos *pos, unsigned int depth);
unsigned long long int perftRaw(Pos *pos, unsigned int depth);

#endif
