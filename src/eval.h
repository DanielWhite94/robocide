#ifndef EVAL_H
#define EVAL_H

#include "pos.h"
#include "types.h"

void EvalInit();
void EvalQuit();
score_t Evaluate(const pos_t *Pos); // returns score in CP

#endif
