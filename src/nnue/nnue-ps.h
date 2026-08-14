#ifndef NNUE_PS_H
#define NNUE_PS_H

// Piece-Square NNUE network (very basic and very small)

#include <stdint.h>
#include <stdbool.h>

#define NnueAccumulatorSize 64

typedef struct NnueAccumulator NnueAccumulator;
typedef struct NnueNet NnueNet; // a set of weights and biases

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

NnueNet *nnueNetNewMaterial(void); // uses 1:3:3:5:9 material values to make a basic test net
NnueNet *nnueNetNewPST(void); // uses evalPST (exposed by eval.h) to make a basic test net

bool nnueNetFeatureSetIsSimple(void); // if false then have to recalc when e.g. kings move

// Accumulator functions
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
