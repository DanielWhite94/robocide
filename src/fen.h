#ifndef FEN_H
#define FEN_H

#include <stdbool.h>

#include "colour.h"
#include "piece.h"
#include "pos.h"
#include "square.h"

typedef struct {
	Piece array[64];
	Colour stm;
	CastRights castRights;
	Sq epSq;
	unsigned int halfMoveNumber, fullMoveNumber;
} Fen;

bool fenRead(Fen *data, const char *string);

#endif
