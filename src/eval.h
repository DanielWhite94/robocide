#ifndef EVAL_H
#define EVAL_H

#include <stdint.h>

typedef int32_t Value;
typedef struct { Value mg, eg; } VPair;
extern const VPair VPairZero;

#include "piece.h"
#include "pos.h"
#include "score.h"
#include "square.h"

typedef enum {
	EvalMatTypeInvalid,
	EvalMatTypeOther,
	EvalMatTypeDraw, // Insufficient material draw (KvK, KNvK, and bishops of single colour).
	EvalMatTypeKNNvK,
	EvalMatTypeKPvK,
	EvalMatTypeKBPvK, // Lone king against pawns, and bishops of a single colour, any number of each.
	EvalMatTypeNB
} EvalMatType;
#define EvalMatTypeBit 3

extern VPair evalPST[PieceNB][SqNB];

void evalInit(void);
void evalQuit(void);

Score evaluate(const Pos *pos); // Returns score in CP.

void evalClear(void); // Clear all saved data (called when we receive 'ucinewgame', for example).

EvalMatType evalGetMatType(const Pos *pos);

const char *evalMatTypeToStr(EvalMatType matType);

VPair evalComputePstScore(const Pos *pos);

void evalVPairAddTo(VPair *a, const VPair *b);
void evalVPairSubFrom(VPair *a, const VPair *b);
void evalVPairAddMulTo(VPair *a, const VPair *b, int c);
void evalVPairSubMulFrom(VPair *a, const VPair *b, int c);
void evalVPairNegate(VPair *a);

VPair evalVPairAdd(const VPair *a, const VPair *b);
VPair evalVPairSub(const VPair *a, const VPair *b);
VPair evalVPairNegation(const VPair *a);

#endif
