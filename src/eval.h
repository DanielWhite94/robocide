#ifndef EVAL_H
#define EVAL_H

typedef enum {
	EvalMatTypeInvalid,
	EvalMatTypeOther,
	EvalMatTypeDraw, // Insufficient material draw (KvK, KNvK, and bishops of single colour).
	EvalMatTypeKNNvK,
	EvalMatTypeKPvK,
	EvalMatTypeKBPvK, // Lone king against pawns, and bishops of a single colour, any number of each.
	EvalMatTypeNB
} EvalMatType;

#include "piece.h"
#include "pos.h"
#include "score.h"
#include "square.h"

void evalInit(void);
void evalQuit(void);

Score evaluate(const Pos *pos); // Returns score in CP.

EvalMatType evalComputeMatType(const Pos *pos);

#endif
