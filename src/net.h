#ifndef NET_H
#define NET_H

#include "pos.h"
#include "score.h"

void netInit(void);
void netQuit(void);

Score netEvaluateRaw(const Pos *pos);

#endif
