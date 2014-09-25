#ifndef BITBASE_H
#define BITBASE_H

#include "pos.h"

typedef enum { BitBaseResultDraw, BitBaseResultWin } BitBaseResult;

void bitbaseInit(void);
void bitbaseQuit(void);
BitBaseResult bitbaseProbe(const Pos *pos); // Position must be KPvK.

#endif
