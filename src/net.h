#ifndef NET_H
#define NET_H

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "colour.h"
#include "piece.h"
#include "pos.h"
#include "score.h"
#include "square.h"

typedef enum {
	// This differs from the standard Piece definition in a few ways:
	// * There is no None member
	// * No differentiation between light/dark square bishops
	// * Single (non-coloured) king (would be redundant having both as friendly king position is already encoded in the input layer)
	NetPieceWPawn,
	NetPieceWKnight,
	NetPieceWBishop,
	NetPieceWRook,
	NetPieceWQueen,
	NetPieceBPawn,
	NetPieceBKnight,
	NetPieceBBishop,
	NetPieceBRook,
	NetPieceBQueen,
	NetPieceKing,
	NetPieceNB,
} NetPiece;

typedef struct Net Net; // a set of weights

typedef struct {
	int16_t values[ColourNB][256];
} NetAccumulator;

typedef struct NetTunePositions NetTunePositions; // a collection of positions each with the final game result

// General functions
void netInit(void);
void netQuit(void);

const Net *netGet(void); // gets the currently loaded net

// Net functions
Net *netNew(void);
void netFree(Net *net);

Net *netLoad(const char *path);
bool netSave(const Net *net, const char *path);

Net *netNewMaterial(void); // uses 1:3:3:5:9 material values to make a basic test net
Net *netNewPST(void); // uses evalPST (exposed by eval.h) to make a basic test net

// Evaluation functions
Score netEvaluate(const Pos *pos);
Score netEvaluateNet(const Pos *pos, const Net *net);
Score netEvaluateNetAccum(Colour stm, const Net *net, const NetAccumulator *accum, bool verbose);

// Accumulator functions
void netAccumulatorCalc(const Net *net, const Pos *pos, NetAccumulator *accum); // calculates the accumulator values from scratch rather than using any cached values in the Pos struct

bool netAccumulatorIsEqual(const NetAccumulator *a, const NetAccumulator *b);
void netAccumulatorCopy(NetAccumulator *dest, const NetAccumulator *src);

void netAccumulatorDebug(const NetAccumulator *accum);

// for the following functions piece, fromPiece and toPiece should never be PieceNone - i.e. they must have a NetPiece counterpart
void netAccumulatorAdd(NetAccumulator *accum, const Net *net, const Pos *pos, Sq sq, Piece piece);
void netAccumulatorRemove(NetAccumulator *accum, const Net *net, const Pos *pos, Sq sq, Piece piece);
void netAccumulatorMove(NetAccumulator *accum, const Net *net, const Pos *pos, Sq fromSq, Piece fromPiece, Sq toSq, Piece toPiece);

// Tuning functions
NetTunePositions *netTunePositionsNew(size_t count); // count can be 0 if not known in advance
void netTunePositionsFree(NetTunePositions *positions);

#endif
