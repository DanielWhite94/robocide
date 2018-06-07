#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "main.h"
#include "search.h"
#include "thread.h"
#include "tt.h"
#include "uci.h"
#include "util.h"

// Transposition table entry - 128 bits.
STATICASSERT(MoveBit<=16);
STATICASSERT(ScoreBit<=16);
STATICASSERT(DepthBit<=8);
STATICASSERT(BoundBit<=2);
STATICASSERT(DateBit<=6);
typedef struct {
	union {
		uint64_t int64;
		struct {
			uint16_t keyUpper;
			uint16_t move;
			int16_t score;
			uint8_t depth;
			uint8_t bound:2;
			uint8_t date:6; // Search date at the time the entry was last read/written, used to calculate entry age.
		};
	};
} TTEntry;

// Group ttClusterSize number of entries into each 'bin'.
// When reading from the tt we loop over all entries in the relevant bin, when
// writing we choose the 'least useful' entry to replace.

unsigned ttEntryCount=0;
AtomicUInt64 *tt=NULL;

typedef uint32_t TTKey;

const size_t ttDefaultSizeMb=16;
#define ttEntriesPerCluster 4
#define ttEntrySize 8
STATICASSERT(ttEntrySize==sizeof(AtomicUInt64));
STATICASSERT(ttEntrySize==sizeof(TTEntry));
#define ttKeySize 32 // Only lower 32 bits are used, limiting maximum number of entries
#define ttMaxEntries ((1llu)<<(ttKeySize)) // 2^32
const size_t ttMaxSizeMb=(ttMaxEntries*sizeof(AtomicUInt64))/(1024*1024); // 32gb

////////////////////////////////////////////////////////////////////////////////
// Private prototypes.
////////////////////////////////////////////////////////////////////////////////

bool ttEntryMatch(const Pos *pos, TTEntry entry);
bool ttEntryUnused(TTEntry entry);

unsigned int ttEntryFitness(unsigned int age, Depth depth, bool exact);

Score ttScoreOut(Score score, Depth ply);
Score ttScoreIn(Score score, Depth ply);

TTKey ttKeyFromPos(const Pos *pos);

bool ttResize(unsigned sizeMb); // sizeMb>0
void ttResizeInterface(void *userData, long long int value); // Interface for UCI spin option code.

void ttClearInterface(void *userData); // Interface for UCI button option code.

////////////////////////////////////////////////////////////////////////////////
// Public functions.
////////////////////////////////////////////////////////////////////////////////

void ttInit(void) {
	// Attempt to allocate at default size.
	if (!ttResize(ttDefaultSizeMb))
		mainFatalError("Error: Could not allocate transposition table.\n");

	// Add uci options to change size and clear.
	uciOptionNewSpin("Hash", &ttResizeInterface, NULL, 1, ttMaxSizeMb, ttDefaultSizeMb);
	uciOptionNewButton("Clear Hash", &ttClearInterface, NULL);
}

void ttQuit(void) {
	free((void *)tt);
	tt=NULL;
	ttEntryCount=0;
}

void ttClear(void) {
	memset((void *)tt, 0, ttEntryCount*ttEntrySize);
}

bool ttRead(const Pos *pos, Depth ply, Move *move, Depth *depth, Score *score, Bound *bound) {
	unsigned index=((((uint64_t)ttKeyFromPos(pos))*((uint64_t)ttEntryCount))>>32);
	index&=~((unsigned)ttEntriesPerCluster);

	// Loop over entries in cluster looking for a match.
	unsigned int i;
	for(i=0;i<ttEntriesPerCluster;++i,++index) {
		TTEntry entry={.int64=atomicUInt64Get(tt+index)};

		if (!ttEntryMatch(pos, entry))
			continue;

		// Update entry date (to reset age to 0).
		entry.date=searchGetDate();
		atomicUInt64Set(tt+index, entry.int64);

		// Extract information.
		*move=entry.move;
		*depth=entry.depth;
		*score=ttScoreOut(entry.score, ply);
		*bound=entry.bound;

		return true;
	}

	// No match.
	return false;
}

Move ttReadMove(const Pos *pos) {
	Move move=MoveInvalid;
	Depth dummyDepth;
	Score dummyScore;
	Bound dummyBound;
	ttRead(pos, 0, &move, &dummyDepth, &dummyScore, &dummyBound);
	return move;
}

void ttWrite(const Pos *pos, Depth ply, Depth depth, Move move, Score score, Bound bound) {
	// Sanity checks.
	assert(depthIsValid(ply));
	assert(depthIsValid(depth));
	assert(moveIsValid(move) || move==MoveNone);
	assert(scoreIsValid(score));
	assert(bound!=BoundNone);

	unsigned index=((((uint64_t)ttKeyFromPos(pos))*((uint64_t)ttEntryCount))>>32);
	index&=~((unsigned)ttEntriesPerCluster);

	// Find entry to overwrite.
	unsigned replaceIndex=index;
	unsigned int i, replaceScore=0; // Worst possible score.
	for(i=0;i<ttEntriesPerCluster;++i,++index) {
		TTEntry entry={.int64=atomicUInt64Get(tt+index)};

		// If we find an exact match, simply reuse this entry.
		// We can also be certain that if this entry is unused, we will not find an
		// exact match in a later entry (otherwise said later entry would have
		// instead been written to this unused entry).
		if (ttEntryMatch(pos, entry) || ttEntryUnused(entry)) {
			// Set key (in case entry was previously unused).
			entry.keyUpper=(posGetKey(pos)>>48);

			// Update entry date (to reset age to 0).
			entry.date=searchGetDate();

			// Update move if we have one and it is from a deeper search (or no move already stored).
			if (!moveIsValid(entry.move) || (moveIsValid(move) && depth>=entry.depth))
				entry.move=move;

			// Update score, depth and bound if search was at least as deep as the entry depth.
			if (depth>=entry.depth) {
				entry.score=ttScoreIn(score, ply);
				entry.depth=depth;
				entry.bound=bound;
			}

			// Update cluster/table
			atomicUInt64Set(tt+index, entry.int64);

			return;
		}

		// Otherwise check if entry is better to use than replace.
		unsigned int entryScore=ttEntryFitness(searchDateToAge(entry.date), entry.depth, entry.bound==BoundExact);
		if (entryScore>replaceScore) {
			replaceIndex=index;
			replaceScore=entryScore;
		}
	}

	// Replace entry.
	TTEntry replaceEntry;
	replaceEntry.keyUpper=(posGetKey(pos)>>48);
	replaceEntry.move=move;
	replaceEntry.score=ttScoreIn(score, ply);
	replaceEntry.depth=depth;
	replaceEntry.bound=bound;
	replaceEntry.date=searchGetDate();

	atomicUInt64Set(tt+replaceIndex, replaceEntry.int64);
}

unsigned int ttFull(void) {
	unsigned total=0;

	unsigned index, indexDelta=ttEntryCount/1000, checked;
	for(index=0,checked=0;checked<1000;index+=indexDelta,++checked) {
		TTEntry entry={.int64=atomicUInt64Get(tt+index)};
		total+=!ttEntryUnused(entry);
	}

	return total;
}

////////////////////////////////////////////////////////////////////////////////
// Private functions.
////////////////////////////////////////////////////////////////////////////////

bool ttEntryMatch(const Pos *pos, TTEntry entry) {
	// Key match and move psueudo-legal?
	return (entry.keyUpper==(posGetKey(pos)>>48) && posMoveIsPseudoLegal(pos, entry.move));
}

bool ttEntryUnused(TTEntry entry) {
	return (entry.move==MoveInvalid);
}

unsigned int ttEntryFitness(unsigned int age, Depth depth, bool exact) {
	// Evaluate how 'fit' an entry is to be replaced.
	// Based on the following factors, in order:
	// * Match/unused - if entry is a match or unused it is immediately chosen.
	// * Age - prefer replacing older entries over new ones.
	// * Depth - prefer replacing shallower entries over deeper ones.
	// * Bound - prefer exact bounds to upper- or lower-bounds.
	assert(depthIsValid(depth));
	assert(exact==0 || exact==1);
	return 2*DepthMax*age+2*(DepthMax-1-depth)+(1-exact);
}

Score ttScoreOut(Score score, Depth ply) {
	assert(depthIsValid(ply));
	if (scoreIsMate(score))
		return (score>0 ? score-ply : score+ply); // Adjust to distance from root [to mate].
	else
		return score;
}

Score ttScoreIn(Score score, Depth ply) {
	assert(depthIsValid(ply));
	if (scoreIsMate(score))
		return (score>0 ? score+ply : score-ply); // Adjust to distance from this node [to mate].
	else
		return score;
}

TTKey ttKeyFromPos(const Pos *pos) {
	assert(pos!=NULL);

	STATICASSERT(ttKeySize==32);
	return posGetKey(pos)&0xFFFFFFFFu; // Use lower 32 bits
}

bool ttResize(unsigned sizeMb) {
	// Limit size due to key size.
	if (sizeMb>ttMaxSizeMb)
		sizeMb=ttMaxSizeMb;

	// Find number of entries we can fit in the given size (rounded down to a multiple of the cluster size).
	unsigned entryCount=(1024llu*1024llu*sizeMb)/ttEntrySize;
	entryCount=(entryCount/ttEntriesPerCluster)*ttEntriesPerCluster;
	while(entryCount>0) {
		// Attempt to allocate this number of entries.
		AtomicUInt64 *tempPtr=realloc((void *)tt, entryCount*ttEntrySize);
		if (tempPtr!=NULL) {
			tt=tempPtr;
			ttEntryCount=entryCount;

			ttClear();

			return true;
		}

		// Try again with fewer entries.
		entryCount/=2;
	}

	return false;
}

void ttResizeInterface(void *userData, long long int value) {
	assert(userData!=NULL);

	ttResize(value);
}

void ttClearInterface(void *userData) {
	assert(userData!=NULL);

	ttClear();
}
