#ifndef PERFT_H
#define PERFT_H

#include "pos.h"

void Perft(pos_t *Pos, unsigned int MaxDepth);
void Divide(pos_t *Pos, unsigned int Depth);
unsigned long long int PerftRaw(pos_t *Pos, unsigned int Depth);

#endif
