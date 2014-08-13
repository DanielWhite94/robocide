#ifndef SEARCH_H
#define SEARCH_H

#include <stdbool.h>
#include "pos.h"
#include "time.h"

bool SearchInit();
void SearchQuit();
void SearchThink(const pos_t *Pos, ms_t StartTime, ms_t SearchTime, bool Infinite, bool Ponder);
void SearchStop();
void SearchClear(); // Clear any data search has collected (e.g. history tables)
void SearchPonderHit();

#endif
