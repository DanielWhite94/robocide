#ifndef SEARCH_H
#define SEARCH_H

#include <stdbool.h>
#include <stdint.h>

#include "depth.h"
#include "move.h"
#include "pos.h"
#include "scoredmove.h"
#include "time.h"

#define DateBit 6 // Number of bits searchGetDate() will actually use in its return value.
#define DateMax (1u<<DateBit)

// Entries should be considered private - only here to allow easy allocation on the stack.
typedef struct {
	bool infinite; // Either directly by 'infinite' or during pondering (before changing to false after ponderhit/miss).
	TimeMs startTime; // Time we first noticed the 'go' command.
	TimeMs totalTime, incTime, moveTime; // If any are TimeMsInvalid assumes no limit given.
	unsigned int movesToGo; // 0 implies sudden-death (assuming totalTime and/or inc time are in use).
	Depth depth;// Depth limit. If DepthInvalid assumes no limit given. If greater than DepthMax, reduced to this.
	unsigned long long int nodes; // Node limit. If 0 assumes no limit given.

	Move searchMoves[MovesMax], *searchMovesNext;
} SearchLimit;

void searchInit(void);
void searchQuit(void);

void searchThink(const Pos *pos, const SearchLimit *limit, bool output);
void searchStopAndWait(void); // Instruct search to stop as soon as possible and wait for it to finish.
void searchWait(void); // Wait for search to finish (but do not instruct it to stop immediately if still thinking).

unsigned long long int searchBenchmark(const Pos *pos, Depth depth);

void searchClear(void); // Clear any data search has collected (e.g. history tables).

void searchPonderHit(void); // Tell the search our pondering guess was correct.

MoveScore searchScoreMove(const Pos *pos, Move move);

unsigned int searchGetDate(void);
unsigned int searchDateToAge(unsigned int date);

void searchLimitInit(SearchLimit *limit, TimeMs startTime); // startTime should be as close as possible to the time we received the 'go' command.
void searchLimitSetInfinite(SearchLimit *limit, bool infinite); // From 'infinite' or 'ponder'.
void searchLimitSetTotalTime(SearchLimit *limit, TimeMs totalTime); // From '[w/b]time x' argument.
void searchLimitSetIncTime(SearchLimit *limit, TimeMs incTime); // From '[w/b]inc x' argument.
void searchLimitSetMoveTime(SearchLimit *limit, TimeMs moveTime); // From 'movetime x' argument.
void searchLimitSetDepth(SearchLimit *limit, Depth depth); // From 'depth x' argument.
void searchLimitSetNodes(SearchLimit *limit, unsigned long long int nodes); // From 'nodes x' argument.
void searchLimitSetMovesToGo(SearchLimit *limit, unsigned int movesToGo);
void searchLimitAddMove(SearchLimit *limit, const Pos *pos, Move move); // If no moves added, uses full set.

#endif
