#ifndef EVAL_H
#define EVAL_H

#include "pos.h"
#include "score.h"

typedef enum
{
  EvalMatTypeInvalid,
  EvalMatTypeOther,
  EvalMatTypeDraw, // Insufficient material draw (KvK, KNvK, and bishops of single colour).
  EvalMatTypeKNNvK,
  EvalMatTypeKPvK,
  EvalMatTypeKBPvK,
}EvalMatType;

void evalInit(void);
void evalQuit(void);
Score evaluate(const Pos *pos); // Returns score in CP.
void evalClear(void); // Clear all saved data (called when we receive 'ucinewgame', for example).
EvalMatType evalGetMatType(const Pos *pos);

#endif
