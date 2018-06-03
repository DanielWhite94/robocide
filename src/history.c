#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "history.h"

const HistoryCounter HistoryCounterMax=(((HistoryCounter)1)<<HistoryCounterBit);

History historyDummy;

void historyInit(void) {
	historyClear(&historyDummy);
}

void historyInc(History *history, Piece fromPiece, Sq toSq, unsigned int depth) {
	assert(pieceIsValid(fromPiece));
	assert(sqIsValid(toSq));

	// Increment count in table.
	HistoryCounter *counter=&history->counters[fromPiece][toSq];
	*counter+=(((HistoryCounter)1)<<utilMin(depth, HistoryCounterBit-1));

	// Overflow? (not a literal overflow, but beyond desired range).
	if (*counter>=HistoryCounterMax)
		historyAge(history);
	assert(*counter<HistoryCounterMax);
}

HistoryCounter historyGet(const History *history, Piece fromPiece, Sq toSq) {
	assert(pieceIsValid(fromPiece));
	assert(sqIsValid(toSq));
	assert(history->counters[fromPiece][toSq]<HistoryCounterMax);
	return history->counters[fromPiece][toSq];
}

void historyAge(History *history) {
	unsigned int i, j;
	for(i=0;i<PieceNB;++i)
		for(j=0;j<SqNB;++j)
			history->counters[i][j]/=2;
}

void historyClear(History *history) {
	memset(history, 0, sizeof(History));
}
