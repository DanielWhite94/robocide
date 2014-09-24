#include <assert.h>
#include <stdint.h>
#include <stdlib.h>

#include "htable.h"
#include "main.h"
#include "search.h"
#include "tt.h"
#include "uci.h"
#include "util.h"

// Transposition table entry - 128 bits.
STATICASSERT(MoveBit<=16);
STATICASSERT(ScoreBit<=16);
STATICASSERT(BoundBit<=2);
STATICASSERT(DateBit<=6);
typedef struct
{
  uint64_t key;
  uint16_t move;
  int16_t score;
  uint8_t depth;
  uint8_t bound:2;
  uint8_t date:6; // Search date at the time the entry was last read/written, used to calculate entry age.
  uint16_t dummy; // Padding.
}TTEntry;

// Group ttClusterSize number of entries into each 'bin'.
// When reading from the tt we loop over all entries in the relevant bin, when
// writing we choose the 'least useful' entry to replace.
#define ttClusterSize (4u)
typedef struct
{
  TTEntry entries[ttClusterSize];
}TTCluster;
HTable *tt=NULL;
const size_t ttDefaultSizeMb=16;
const size_t ttMaxSizeMb=1024*1024; // 1tb

////////////////////////////////////////////////////////////////////////////////
// Private prototypes
////////////////////////////////////////////////////////////////////////////////

bool ttEntryMatch(const Pos *pos, const TTEntry *entry);
bool ttEntryUnused(const TTEntry *entry);
unsigned int ttEntryFitness(unsigned int age, unsigned int depth, bool exact);
Score ttScoreOut(Score score, unsigned int ply);
Score ttScoreIn(Score score, unsigned int ply);

////////////////////////////////////////////////////////////////////////////////
// Public functions
////////////////////////////////////////////////////////////////////////////////

void ttInit(void)
{
  // Setup tt as a HTable.
  TTCluster nullEntry;
  unsigned int i;
  for(i=0;i<ttClusterSize;++i)
  {
    nullEntry.entries[i].key=0;
    nullEntry.entries[i].move=MoveInvalid;
    nullEntry.entries[i].score=ScoreInvalid;
    nullEntry.entries[i].depth=0;
    nullEntry.entries[i].bound=BoundNone;
    nullEntry.entries[i].date=DateMax-1;
  }
  tt=htableNew(sizeof(TTCluster), &nullEntry, ttDefaultSizeMb);
  if (tt==NULL)
    mainFatalError("Error: Could not allocate transposition table.\n");
    
  // Add uci options to change size and clear.
  uciOptionNewSpin("Hash", &htableResizeInterface, tt, 1, ttMaxSizeMb, ttDefaultSizeMb);
  uciOptionNewButton("Clear Hash", &htableClearInterface, tt);
}

void ttQuit(void)
{
  htableFree(tt);
  tt=NULL;
}

void ttClear(void)
{
  htableClear(tt);
}

bool ttRead(const Pos *pos, unsigned int ply, Move *move, unsigned int *depth, Score *score, Bound *bound)
{
  // Grab cluster
  uint64_t key=posGetKey(pos);
  TTCluster *cluster=htableGrab(tt, key);
  
  // Loop over entries in cluster looking for a match.
  unsigned int i;
  TTEntry *entry;
  for(i=0,entry=cluster->entries;i<ttClusterSize;++i,++entry)
    if (ttEntryMatch(pos, entry))
    {
      // Update entry date (to reset age to 0).
      entry->date=searchGetDate();
      
      // Extract information.
      *move=entry->move;
      *depth=entry->depth;
      *score=ttScoreOut(entry->score, ply);
      *bound=entry->bound;
      
      htableRelease(tt, key);
      return true;
    }
  
  // No match.
  htableRelease(tt, key);
  return false;
}

Move ttReadMove(const Pos *pos)
{
  Move move=MoveInvalid;
  unsigned int dummyDepth;
  Score dummyScore;
  Bound dummyBound;
  ttRead(pos, 0, &move, &dummyDepth, &dummyScore, &dummyBound);
  return move;
}

void ttWrite(const Pos *pos, unsigned int ply, unsigned int depth, Move move, Score score, Bound bound)
{
  // Sanity checks.
  assert(moveIsValid(move) || move==MoveNone);
  assert(scoreIsValid(score));
  assert(bound!=BoundNone);
  
  // Grab cluster.
  uint64_t key=posGetKey(pos);
  TTCluster *cluster=htableGrab(tt, key);
  
  // Find entry to overwrite.
  TTEntry *entry, *replace=cluster->entries;
  unsigned int i, replaceScore=0; // Worst possible score.
  for(i=0,entry=cluster->entries;i<ttClusterSize;++i,++entry)
  {
    // If we find an exact match, simply reuse this entry.
    // We can also be certain that if this entry is unused, we will not find an
    // exact match in a later entry (otherwise said later entry would have
    // instead been written to this unused entry).
    if (ttEntryMatch(pos, entry) || ttEntryUnused(entry))
    {
      // Set key (in case entry was previously unused).
      entry->key=key;
      
      // Update entry date (to reset age to 0).
      entry->date=searchGetDate();
      
      // Update move if we have one and it is from a deeper search (or no move already stored).
      if (!moveIsValid(entry->move) || (moveIsValid(move) && depth>=entry->depth))
        entry->move=move;
      
      // Update score, depth and bound if search was at least as deep as the entry depth.
      if (depth>=entry->depth)
      {
        entry->score=ttScoreIn(score, ply);
        entry->depth=depth;
        entry->bound=bound;
      }
      
      htableRelease(tt, key);
      return;
    }
    
    // Otherwise check if entry is better to use than replace.
    unsigned int entryScore=ttEntryFitness(searchDateToAge(entry->date), entry->depth, entry->bound==BoundExact);
    if (entryScore>replaceScore)
    {
      replace=entry;
      replaceScore=entryScore;
    }
  }
  
  // Replace entry.
  replace->key=key;
  replace->move=move;
  replace->score=ttScoreIn(score, ply);
  replace->depth=depth;
  replace->bound=bound;
  replace->date=searchGetDate();
  
  htableRelease(tt, key);
}

////////////////////////////////////////////////////////////////////////////////
// Private functions
////////////////////////////////////////////////////////////////////////////////

bool ttEntryMatch(const Pos *pos, const TTEntry *entry)
{
  // Key match and move psueudo-legal?
  return (entry->key==posGetKey(pos) && posMoveIsPseudoLegal(pos, entry->move));
}

bool ttEntryUnused(const TTEntry *entry)
{
  return (entry->move==MoveInvalid);
}

unsigned int ttEntryFitness(unsigned int age, unsigned int depth, bool exact)
{
  // Evaluate how 'fit' an entry is to be replaced.
  // Based on the following factors, in order:
  // * Match/unused - if entry is a match or unused it is immediately chosen.
  // * Age - prefer replacing older entries over new ones.
  // * Depth - prefer replacing shallower entries over deeper ones.
  // * Bound - prefer exact bounds to upper- or lower-bounds.
  assert(exact==0 || exact==1);
  assert(depth<256);
  return 2*256*age+2*(255-depth)+(1-exact);
}

Score ttScoreOut(Score score, unsigned int ply)
{
  if (scoreIsMate(score))
    return (score>0 ? score-ply : score+ply); // Adjust to distance from root [to mate].
  else
    return score;
}

Score ttScoreIn(Score score, unsigned int ply)
{
  if (scoreIsMate(score))
    return (score>0 ? score+ply : score-ply); // Adjust to distance from this node [to mate].
  else
    return score;
}
