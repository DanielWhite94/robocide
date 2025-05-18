#ifndef HISTORY_H
#define HISTORY_H

#include <stdint.h>

#include "piece.h"
#include "square.h"

typedef uint64_t History;
#define HistoryBit 41
extern const History HistoryMax;

void historyInc(Piece fromPiece, Sq toSq, unsigned int depth);
History historyGet(Piece fromPiece, Sq toSq);
void historyAge(void);
void historyClear(void);

bool historyImport(const char *path);
bool historyExport(const char *path);

#endif
