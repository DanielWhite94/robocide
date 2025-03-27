#include <assert.h>
#include <stdint.h>
#include <stdlib.h>

#include "htable.h"
#include "main.h"
#include "search.h"
#include "tt.h"
#include "uci.h"
#include "util.h"

// Transposition table entry - 64 bits.
STATICASSERT(MoveBit<=16);
STATICASSERT(ScoreBit<=16);
STATICASSERT(DepthBit<=8);
STATICASSERT(BoundBit<=2);
STATICASSERT(DateBit<=6);
typedef struct {
	uint16_t keyUpper;
	uint16_t move;
	int16_t score;
	uint8_t depth;
	uint8_t bound:2;
	uint8_t date:6; // Search date at the time the entry was last read/written, used to calculate entry age.
} TTEntry;

// Group ttClusterSize number of entries into each 'bin'.
// When reading from the tt we loop over all entries in the relevant bin, when
// writing we choose the 'least useful' entry to replace.
#define ttClusterSize (4u)
typedef struct {
	TTEntry entries[ttClusterSize];
} TTCluster;

HTable *tt=NULL;

const size_t ttDefaultSizeMb=16;
#define ttMaxClusters HTableMaxEntryCount // 2^32
#define ttMaxEntries (ttMaxClusters*ttClusterSize) // 2^34
const size_t ttMaxSizeMb=(ttMaxClusters*sizeof(TTCluster))/(1024*1024); // 128gb

////////////////////////////////////////////////////////////////////////////////
// Private prototypes.
////////////////////////////////////////////////////////////////////////////////

bool ttEntryMatch(const Pos *pos, const TTEntry *entry);
bool ttEntryUnused(const TTEntry *entry);

unsigned int ttEntryFitness(unsigned int age, Depth depth, bool exact);

Score ttScoreOut(Score score, Depth ply);
Score ttScoreIn(Score score, Depth ply);

HTableKey ttHTableKeyFromPos(const Pos *pos);

////////////////////////////////////////////////////////////////////////////////
// Public functions.
////////////////////////////////////////////////////////////////////////////////

void ttInit(void) {
	// Setup tt as a HTable.
	tt=htableNew(sizeof(TTCluster), ttDefaultSizeMb);
	if (tt==NULL)
		mainFatalError("Error: Could not allocate transposition table.\n");

	// Add uci options to change size and clear.
	uciOptionNewSpin("Hash", &htableResizeInterface, tt, 1, ttMaxSizeMb, ttDefaultSizeMb);
	uciOptionNewButton("Clear Hash", &htableClearInterface, tt);
}

void ttQuit(void) {
	htableFree(tt);
	tt=NULL;
}

void ttClear(void) {
	htableClear(tt);
}

bool ttRead(const Pos *pos, Depth ply, Move *move, Depth *depth, Score *score, Bound *bound) {
	// Grab cluster.
	HTableKey hTableKey=ttHTableKeyFromPos(pos);
	TTCluster *cluster=htableGrab(tt, hTableKey);

	// Loop over entries in cluster looking for a match.
	unsigned int i;
	TTEntry *entry;
	for(i=0,entry=cluster->entries;i<ttClusterSize;++i,++entry)
		if (ttEntryMatch(pos, entry)) {
			// Update entry date (to reset age to 0).
			entry->date=searchGetDate();

			// Extract information.
			*move=entry->move;
			*depth=entry->depth;
			*score=ttScoreOut(entry->score, ply);
			*bound=entry->bound;

			htableRelease(tt, hTableKey);

			return true;
		}

	// No match.
	htableRelease(tt, hTableKey);
	return false;
}

Move ttReadMove(const Pos *pos, Depth ply) {
	// Sanity checks.
	assert(depthIsValid(ply));

	Move move=MoveInvalid;
	Depth dummyDepth;
	Score dummyScore;
	Bound dummyBound;
	ttRead(pos, ply, &move, &dummyDepth, &dummyScore, &dummyBound);
	return move;
}

void ttWrite(const Pos *pos, Depth ply, Depth depth, Move move, Score score, Bound bound) {
	// Sanity checks.
	assert(depthIsValid(ply));
	assert(depthIsValid(depth));
	assert(moveIsValid(move) || move==MoveNone);
	assert(scoreIsValid(score));
	assert(bound!=BoundNone);

	uint64_t key=posGetKey(pos);

	// Grab cluster.
	HTableKey hTableKey=ttHTableKeyFromPos(pos);
	TTCluster *cluster=htableGrab(tt, hTableKey);

	// Find entry to overwrite.
	TTEntry *entry, *replace=cluster->entries;
	unsigned int i, replaceScore=0; // Worst possible score.
	for(i=0,entry=cluster->entries;i<ttClusterSize;++i,++entry) {
		// If we find an exact match, simply reuse this entry.
		// We can also be certain that if this entry is unused, we will not find an
		// exact match in a later entry (otherwise said later entry would have
		// instead been written to this unused entry).
		if (ttEntryMatch(pos, entry) || ttEntryUnused(entry)) {
			// Set key (in case entry was previously unused).
			entry->keyUpper=(key>>48);

			// Update entry date (to reset age to 0).
			entry->date=searchGetDate();

			// Update move if we have one and it is from a deeper search (or no move already stored).
			if (!moveIsValid(entry->move) || (moveIsValid(move) && depth>=entry->depth))
				entry->move=move;

			// Update score, depth and bound if search was at least as deep as the entry depth.
			if (depth>=entry->depth) {
				entry->score=ttScoreIn(score, ply);
				entry->depth=depth;
				entry->bound=bound;
			}

			htableRelease(tt, hTableKey);
			return;
		}

		// Otherwise check if entry is better to use than replace.
		unsigned int entryScore=ttEntryFitness(searchDateToAge(entry->date), entry->depth, (entry->bound==BoundExact));
		if (entryScore>replaceScore) {
			replace=entry;
			replaceScore=entryScore;
		}
	}

	// Replace entry.
	replace->keyUpper=(key>>48);
	replace->move=move;
	replace->score=ttScoreIn(score, ply);
	replace->depth=depth;
	replace->bound=bound;
	replace->date=searchGetDate();

	htableRelease(tt, hTableKey);
}

unsigned int ttFull(void) {
	unsigned total=0;

	unsigned checked=0;
	size_t index, indexDelta=ttMaxClusters/1000;
	for(index=0;checked<1000;index+=indexDelta) {
		TTCluster *cluster=htableGrab(tt, index);

		unsigned entry;
		for(entry=0; entry<ttClusterSize && checked<1000; ++entry,++checked)
			total+=(!ttEntryUnused(&cluster->entries[entry]));

		htableRelease(tt, index);
	}

	return total;
}

////////////////////////////////////////////////////////////////////////////////
// Private functions.
////////////////////////////////////////////////////////////////////////////////

bool ttEntryMatch(const Pos *pos, const TTEntry *entry) {
	// Key match and move psueudo-legal?
	return (entry->keyUpper==(posGetKey(pos)>>48) && posMoveIsPseudoLegal(pos, entry->move));
}

bool ttEntryUnused(const TTEntry *entry) {
	return (entry->move==MoveInvalid);
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

HTableKey ttHTableKeyFromPos(const Pos *pos) {
	assert(pos!=NULL);

	STATICASSERT(HTableKeySize==32);
	return posGetKey(pos)&0xFFFFFFFFu; // Use lower 32 bits
}
