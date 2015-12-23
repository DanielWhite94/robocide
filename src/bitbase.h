#ifndef BITBASE_H
#define BITBASE_H

#include "pos.h"

typedef enum { BitBaseResultDraw, BitBaseResultWin } BitBaseResult;
#define BitBaseResultBit 1

void bitbaseInit(void);
void bitbaseQuit(void);

BitBaseResult bitbaseProbe(const Pos *pos); // Position must be KPvK.

#endif
