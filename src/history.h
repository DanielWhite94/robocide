#ifndef HISTORY_H
#define HISTORY_H

#include <stdint.h>

#include "piece.h"
#include "square.h"

typedef uint64_t HistoryCounter;
#define HistoryCounterBit 41
extern const HistoryCounter HistoryCounterMax;

typedef struct {
	HistoryCounter counters[PieceNB][SqNB];
} History;

extern History historyDummy;

void historyInit(void);

void historyInc(History *history, Piece fromPiece, Sq toSq, unsigned int depth);
HistoryCounter historyGet(const History *history, Piece fromPiece, Sq toSq);
void historyAge(History *history);
void historyClear(History *history);

#endif
