#ifndef SEARCH_H
#define SEARCH_H

#include <stdbool.h>
#include <stdint.h>
#include "pos.h"
#include "time.h"

typedef uint64_t movescore_t;

bool SearchInit();
void SearchQuit();
void SearchThink(const pos_t *Pos, ms_t StartTime, ms_t SearchTime, bool Infinite, bool Ponder);
void SearchStop();
void SearchClear(); // Clear any data search has collected (e.g. history tables)
void SearchPonderHit();
movescore_t SearchScoreMove(const pos_t *Pos, move_t Move);

#endif
