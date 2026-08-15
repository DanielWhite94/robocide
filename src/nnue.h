#ifndef NNUE_H
#define NNUE_H

#include <stdbool.h>

#include "nnue/nnue-kp.h"
#include "nnue/nnue-ps.h"
#include "pos.h"

extern bool nnueUseNnue;

void nnueInit(void);
void nnueQuit(void);

const NnueNet *nnueNetGet(void); // gets the currently loaded net

Score nnueEvaluate(const Pos *pos);
Score nnueEvaluateNet(const Pos *pos, const NnueNet *net);

#endif
