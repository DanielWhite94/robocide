#ifdef NNUE_ARCH_MIN

#ifndef NNUE_MIN_H
#define NNUE_MIN_H

// Piece-Square NNUE network without a hidden layer

#include <stdint.h>
#include <stdbool.h>

#define NnueAccumulatorSize 64

typedef struct NnueAccumulator NnueAccumulator;
typedef struct NnueNet NnueNet; // a set of weights and biases

#include "../move.h"
#include "../pos.h"
#include "../score.h"

struct NnueAccumulator {
	int16_t values[ColourNB][NnueAccumulatorSize];
};

// Network functions
NnueNet *nnueNetNew(const NnueNet *src); // src can be NULL
void nnueNetFree(NnueNet *net);

NnueNet *nnueNetLoad(const char *path);
bool nnueNetSave(const NnueNet *net, const char *path);

// Accumulator functions
bool nnueAccumulatorCalcRequiredMakeMove(const Pos *pos, Move move);
void nnueAccumulatorCalc(const NnueNet *net, const Pos *pos, NnueAccumulator *accum); // calculates the accumulator values from scratch rather than using any cached values in the Pos struct

bool nnueAccumulatorIsEqual(const NnueAccumulator *a, const NnueAccumulator *b);
void nnueAccumulatorCopy(NnueAccumulator *dest, const NnueAccumulator *src);

void nnueAccumulatorDebug(const NnueAccumulator *accum);

// for the following functions piece, fromPiece and toPiece should never be PieceNone
void nnueAccumulatorAdd(NnueAccumulator *accum, const NnueNet *net, const Pos *pos, Sq sq, Piece piece);
void nnueAccumulatorRemove(NnueAccumulator *accum, const NnueNet *net, const Pos *pos, Sq sq, Piece piece);
void nnueAccumulatorMove(NnueAccumulator *accum, const NnueNet *net, const Pos *pos, Sq fromSq, Piece fromPiece, Sq toSq, Piece toPiece);

// Evaluation functions
Score nnueEvaluateNetAccum(Colour stm, const NnueNet *net, const NnueAccumulator *accum, bool verbose);

#endif

#endif
