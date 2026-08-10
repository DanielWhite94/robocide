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

// General functions
void netInit(void);
void netQuit(void);

// Net functions
Net *netNew(void);
void netFree(Net *net);

Net *netLoad(const char *path);
bool netSave(const Net *net, const char *path);

// Evaluation functions
Score netEvaluateRaw(const Pos *pos, const Net *net);

#endif
