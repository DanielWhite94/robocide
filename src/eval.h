#ifndef EVAL_H
#define EVAL_H

#include "pos.h"
#include "types.h"

typedef enum
{
  evalmattype_invalid,
  evalmattype_other,
  evalmattype_KNNvK,
}evalmattype_t;

void EvalInit();
void EvalQuit();
score_t Evaluate(const pos_t *Pos); // returns score in CP
void EvalClear(); // clear all saved data (called when we receive 'ucinewgame', for example)
evalmattype_t EvalGetMatType(const pos_t *Pos);

#endif
