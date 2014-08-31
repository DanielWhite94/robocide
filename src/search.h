#ifndef SEARCH_H
#define SEARCH_H

#include <stdbool.h>
#include <stdint.h>

#include "move.h"
#include "pos.h"
#include "scoredmove.h"
#include "time.h"

#define DateBit 6 // Number of bits searchGetDate() will actually use in its return value.
#define DateMax (1u<<DateBit)

void searchInit(void);
void searchQuit(void);
void searchThink(const Pos *pos, TimeMs startTime, TimeMs searchTime, bool infinite, bool ponder);
void searchStop(void);
void searchClear(void); // Clear any data search has collected (e.g. history tables).
void searchPonderHit(void); // Tell the search our pondering guess was correct.
MoveScore searchScoreMove(const Pos *pos, Move move);
unsigned int searchGetDate(void);
unsigned int searchDateToAge(unsigned int date);

#endif
