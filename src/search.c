#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "eval.h"
#include "htable.h"
#include "main.h"
#include "moves.h"
#include "search.h"
#include "see.h"
#include "threads.h"
#include "types.h"
#include "uci.h"
#include "util.h"

////////////////////////////////////////////////////////////////////////////////
// 
////////////////////////////////////////////////////////////////////////////////

#define SEARCH_MAXPLY 128
unsigned long long int SearchNodeCount;
bool SearchInfinite, SearchStopFlag;
ms_t SearchStartTime, SearchEndTime;
thread_t *SearchThread=NULL;

#define MOVESCORE_WIDTH 48
#define MOVESCORE_MAX (((movescore_t)1)<<MOVESCORE_WIDTH)
#define HISTORY_MAX (((movescore_t)1)<<(MOVESCORE_WIDTH-7)) // see SearchScoreMove()
movescore_t SearchHistory[16][64];

TUNECONST int SearchNullReduction=1;
TUNECONST int SearchIIDMin=2;
TUNECONST int SearchIIDReduction=3;

bool SearchPonder=true;

typedef enum
{
  nodetype_invalid=0,
  nodetype_lower=1,
  nodetype_upper=2,
  nodetype_exact=3,
}nodetype_t;

typedef struct
{
  pos_t *Pos; // *
  int Depth, Ply; // *
  nodetype_t Type; // #
  score_t Alpha, Beta; // *
  bool InCheck; // *
  move_t Move; // #
  score_t Score; // #
  // * - search functions should not modify these entries
  // # - search functions should ensure these are set correctly before returning (even if only say Move==MOVE_NONE)
}node_t;
#define NODE_ISQ(N) ((N)->Depth<1)
#define NODE_ISPV(N) ((N)->Beta-(N)->Alpha>1)

// transposition table entry - 128 bits
typedef struct
{
  hkey_t Key;
  move_t Move;
  score_t Score;
  uint8_t Depth;
  uint8_t Type:2; // score bound type
  uint8_t Date:6; // SearchAge at the time the entry was read/written, used to calculate entry age.
  uint16_t Dummy; // Padding/reserved for future use
}tte_t;

#define TT_CLUSTERSIZE (4u)
typedef struct
{
  tte_t Entries[TT_CLUSTERSIZE];
}ttcluster_t;
htable_t *SearchTT=NULL;
const size_t SearchTTDefaultSizeMB=16;
unsigned int SearchAge;
#define SEARCH_MAXAGE (64u)

////////////////////////////////////////////////////////////////////////////////
// Private prototypes
////////////////////////////////////////////////////////////////////////////////

void SearchIDLoop(void *Data);
score_t SearchNode(node_t *N);
score_t SearchQNode(node_t *N);
void SearchNodeInternal(node_t *Node);
void SearchQNodeInternal(node_t *Node);
static inline bool SearchIsTimeUp();
void SearchOutput(node_t *N);
void SearchScoreToStr(score_t Score, int Type, char Str[static 32]);
void SearchHistoryUpdate(const node_t *N);
void SearchHistoryAge();
void SearchHistoryClear();
bool SearchTTRead(node_t *N, move_t *Move);
void SearchTTWrite(const node_t *N);
static inline bool SearchTTMatch(const node_t *N, const tte_t *TTE);
static inline score_t SearchTTToScore(score_t S, int Ply);
static inline score_t SearchScoreToTT(score_t S, int Ply);
void SearchSetPonder(bool Ponder);
void SearchSetPonderWrapper(bool Ponder, void *Dummy);
static inline bool SearchIsZugzwang(const node_t *N);
#ifdef TUNE
void SearchSetValue(int Value, void *UserData);
#endif
void SearchNodePreCheck(node_t *N);
void SearchNodePostCheck(const node_t *PostN, const node_t *PreN);
bool SearchInteriorRecog(node_t *N);
bool SearchInteriorRecogKNNvK(node_t *N);
unsigned int SearchDateToAge(unsigned int Date);

////////////////////////////////////////////////////////////////////////////////
// Public functions
////////////////////////////////////////////////////////////////////////////////

bool SearchInit()
{
  // Create worker thread
  SearchThread=ThreadCreate();
  if (SearchThread==NULL)
    return false;
  
  // Setup TT table
  ttcluster_t NullEntry;
  unsigned int I;
  for(I=0;I<TT_CLUSTERSIZE;++I)
  {
    NullEntry.Entries[I].Key=0;
    NullEntry.Entries[I].Move=MOVE_INVALID;
    NullEntry.Entries[I].Score=SCORE_INVALID;
    NullEntry.Entries[I].Depth=0;
    NullEntry.Entries[I].Type=nodetype_invalid;
    NullEntry.Entries[I].Date=SEARCH_MAXAGE-1;
  }
  SearchTT=HTableNew(sizeof(ttcluster_t), &NullEntry, SearchTTDefaultSizeMB);
  if (SearchTT==NULL)
    mainFatalError("Error: Could not allocate transposition table.\n");
  UCIOptionNewSpin("Hash", &HTableResizeInterface, SearchTT, 1, 16*1024, SearchTTDefaultSizeMB);
  UCIOptionNewButton("Clear Hash", &HTableClearInterface, SearchTT);
  
  // Set all structures to clean state
  SearchClear();
  
  // Init pondering
  UCIOptionNewCheck("Ponder", &SearchSetPonderWrapper, NULL, SearchPonder);
  
  // Setup callbacks for tuning values
# ifdef TUNE
  UCIOptionNewSpin("NullReduction", &SearchSetValue, &SearchNullReduction, 0, 8, SearchNullReduction);
  UCIOptionNewSpin("IIDMin", &SearchSetValue, &SearchIIDMin, 0, 32, SearchIIDMin);
  UCIOptionNewSpin("IIDReduction", &SearchSetValue, &SearchIIDReduction, 0, 32, SearchIIDReduction);
# endif
  
  return true;
}

void SearchQuit()
{
  // If searching, signal to stop and wait until done
  SearchStop();
  
  // Free the worker thread
  ThreadFree(SearchThread);
  
  // Free TT table
  HTableFree(SearchTT);
  SearchTT=NULL;
}

void SearchThink(const pos_t *SrcPos, ms_t StartTime, ms_t SearchTime, bool Infinite, bool Ponder)
{
  // Make sure we finish previous search
  SearchStop();
  ThreadWaitReady(SearchThread);
  
  // Prepare for search
  pos_t *Pos=PosCopy(SrcPos);
  if (Pos==NULL)
    return;
  SearchNodeCount=0;
  SearchInfinite=(Infinite || Ponder);
  SearchStopFlag=false;
  SearchStartTime=StartTime;
  SearchEndTime=StartTime+SearchTime;
  SearchAge=(SearchAge+1)%SEARCH_MAXAGE;
  
  // Set away worker
  ThreadRun(SearchThread, &SearchIDLoop, (void *)Pos);
}

void SearchStop()
{
  // Signal for search to stop
  SearchStopFlag=true;
  
  // Wait until actually finished
  ThreadWaitReady(SearchThread);
}

void SearchClear()
{
  // Clear history tables
  SearchHistoryClear();
  
  // Clear TT table
  HTableClear(SearchTT);
  
  // Reset search age
  SearchAge=0;
}

void SearchPonderHit()
{
  SearchInfinite=false;
}

movescore_t SearchScoreMove(const pos_t *Pos, move_t Move)
{
  movescore_t Score=0;
  
  // Sort first by captured/promotion piece (most valuable first)
  piece_t FromPiece=PosGetPieceOnSq(Pos, MOVE_GETFROMSQ(Move));
  piece_t ToPieceType=PIECE_TYPE(MOVE_ISPROMO(Move) ? MOVE_GETPROMO(Move) : FromPiece);
  piece_t CapturedPieceType=(MOVE_ISEP(Move) ? pawn : PIECE_TYPE(PosGetPieceOnSq(Pos, MOVE_GETTOSQ(Move))));
  int Delta=(CapturedPieceType+ToPieceType-PIECE_TYPE(FromPiece));
  assert(Delta>=0 && Delta<16);
  Score+=Delta*8*HISTORY_MAX;
  
  // Sort second by capturing piece (least valuable first)
  Score+=(8-ToPieceType)*HISTORY_MAX;
  
  // Further sort using history tables
  assert(SearchHistory[FromPiece][MOVE_GETTOSQ(Move)]<HISTORY_MAX);
  Score+=SearchHistory[FromPiece][MOVE_GETTOSQ(Move)];
  
  assert(Score>=0 && Score<MOVESCORE_MAX);
  return Score;
}

////////////////////////////////////////////////////////////////////////////////
// Private functions
////////////////////////////////////////////////////////////////////////////////

void SearchIDLoop(void *Data)
{
  // Make node structure
  node_t Node;
  Node.Pos=(pos_t *)Data;
  Node.Ply=0;
  Node.Alpha=-SCORE_INF;
  Node.Beta=SCORE_INF;
  Node.InCheck=PosIsSTMInCheck(Node.Pos);
  
  // Loop increasing search depth until we run out of 'time'
  move_t BestMove=MOVE_INVALID;
  for(Node.Depth=1;Node.Depth<SEARCH_MAXPLY;++Node.Depth)
  {
    // Search
    SearchNode(&Node);
    
    // No moves searched? (i.e. out of 'time')
    if (Node.Move==MOVE_INVALID)
      break;
    
    // Update bestmove
    BestMove=Node.Move;
    
    // Output info
    SearchOutput(&Node);
    
    // Time to end?
    if (SearchIsTimeUp())
      break;
  }
  
  // Grab best move (and potentially ponder move)
  if (BestMove==MOVE_INVALID)
  {
    // Try TT
    SearchTTRead(&Node, &BestMove);
    
    // Otherwise simply use some legal move
    if (BestMove==MOVE_INVALID)
      BestMove=PosGenLegalMove(Node.Pos);
  }
  
  // Send best move and ponder move to GUI
  char Str[8];
  PosMoveToStr(BestMove, Str);
  
  move_t PonderMove=MOVE_INVALID;
  if (SearchPonder && MOVE_ISVALID(BestMove))
  {
    PosMakeMove(Node.Pos, BestMove);
    SearchTTRead(&Node, &PonderMove);
    PosUndoMove(Node.Pos);
  }
  
  if (MOVE_ISVALID(PonderMove))
  {
    char Str2[8];
    PosMoveToStr(PonderMove, Str2);
    printf("bestmove %s ponder %s\n", Str, Str2);
  }
  else
    printf("bestmove %s\n", Str);
  
  // Free position
  PosFree(Node.Pos);
}

score_t SearchNode(node_t *N)
{
# ifndef NDEBUG
  // Save node_t structure for post-checks
  node_t PreN=*N;
  
  // Pre-checks
  SearchNodePreCheck(N);
# endif
  
  // Call main search function
  SearchNodeInternal(N);
  
  // Post-checks
# ifndef NDEBUG
  SearchNodePostCheck(N, &PreN);
# endif
  
  return N->Score;
}

score_t SearchQNode(node_t *N)
{
# ifndef NDEBUG
  // Save node_t structure for post-checks
  node_t PreN=*N;
  
  // Pre-checks
  SearchNodePreCheck(N);
  assert(NODE_ISQ(N));
# endif
  
  // Call main search function
  SearchQNodeInternal(N);
  
  // Post-checks
# ifndef NDEBUG
  SearchNodePostCheck(N, &PreN);
# endif
  
  return N->Score;
}

void SearchNodeInternal(node_t *N)
{
  // Q node? (or ply limit reached)
  if (NODE_ISQ(N) || N->Ply>=SEARCH_MAXPLY)
  {
    SearchQNode(N);
    return;
  }
  
  // Node begins
  ++SearchNodeCount;
  
  // Interior node recogniser (also handles draws)
  if (N->Ply>0 && SearchInteriorRecog(N))
    return;
  
  // Check TT table
  move_t TTMove=MOVE_INVALID;
  if (SearchTTRead(N, &TTMove))
    return;
  
  // Null move pruning
  node_t Child;
  Child.Pos=N->Pos;
  Child.Ply=N->Ply+1;
  if (!NODE_ISPV(N) && SearchNullReduction>0 && N->Depth>1+SearchNullReduction &&
      !SCORE_ISMATE(N->Beta) && !SearchIsZugzwang(N) && Evaluate(N->Pos)>=N->Beta)
  {
    assert(!N->InCheck);
    
    PosMakeMove(N->Pos, MOVE_NONE);
    Child.InCheck=false;
    Child.Depth=N->Depth-1-SearchNullReduction;
    Child.Alpha=-N->Beta;
    Child.Beta=1-N->Beta;
    score_t Score=-SearchNode(&Child);
    PosUndoMove(N->Pos);
    
    if (Score>=N->Beta)
    {
      N->Type=nodetype_lower;
      N->Move=MOVE_NONE;
      N->Score=N->Beta;
      return;
    }
  }
  
  // Internal iterative deepening
  int Depth=N->Depth;
  if (SearchIIDReduction>0 && N->Depth>=SearchIIDMin && NODE_ISPV(N) && TTMove==MOVE_INVALID)
  {
    int k=(N->Depth-SearchIIDMin)/SearchIIDReduction;
    Depth=N->Depth-k*SearchIIDReduction;
    
    assert(Depth>=SearchIIDMin && Depth<=N->Depth);
    assert((N->Depth-Depth)%SearchIIDReduction==0);
  }
  
  // Begin IID loop
  moves_t Moves;
  MovesInit(&Moves, N->Pos, true);
  MovesRewind(&Moves, TTMove);
  do
  {
    assert(Depth>=0 && Depth<=N->Depth);
    
    // Prepare to search current depth
    score_t Alpha=N->Alpha;
    N->Score=SCORE_INVALID;
    N->Type=nodetype_upper;
    N->Move=MOVE_INVALID;
    Child.Alpha=-N->Beta;
    Child.Beta=-Alpha;
    move_t Move;
    while((Move=MovesNext(&Moves))!=MOVE_INVALID)
    {
      // Make move (might leave us in check, if so skip)
      if (!PosMakeMove(N->Pos, Move))
        continue;
      
      // PVS search
      Child.InCheck=PosIsSTMInCheck(N->Pos);
      Child.Depth=Depth-!Child.InCheck; // Check extension
      score_t Score;
      if (Alpha>N->Alpha)
      {
        // We have found a good move, try zero window search
        assert(Child.Alpha==Child.Beta-1);
        Score=-SearchNode(&Child);
        
        // Research?
        if (Score>Alpha && Score<N->Beta)
        {
          Child.Alpha=-N->Beta;
          Score=-SearchNode(&Child);
          Child.Alpha=Child.Beta-1;
        }
      }
      else
      {
        // Full window search
        assert(Child.Alpha==-N->Beta);
        Score=-SearchNode(&Child);
      }
      
      // Undo move
      PosUndoMove(N->Pos);
      
      // Out of time? (previous search result is invalid)
      if (SearchIsTimeUp())
      {
        // Not yet started N->Depth search?
        if (Depth<N->Depth)
        {
          N->Type=nodetype_invalid;
          N->Move=MOVE_INVALID;
          N->Score=SCORE_INVALID;
          return;
        }
        
        // Node type is tricky as we haven't yet searched all moves
        N->Type&=~nodetype_upper;
        if (N->Type==nodetype_invalid)
        {
          N->Move=MOVE_INVALID;
          N->Score=SCORE_INVALID;
          return;
        }
        
        // We may have useful info, update TT
        SearchTTWrite(N);
          
        return;
      }
      
      // Better move?
      if (Score>N->Score)
      {
        // Update best score and move
        N->Score=Score;
        N->Move=Move;
        
        // Cutoff?
        if (Score>=N->Beta)
        {
          N->Type=nodetype_lower;
          goto cutoff;
        }
        
        // Update alpha
        if (Score>Alpha)
        {
          Alpha=Score;
          Child.Alpha=-Alpha-1;
          Child.Beta=-Alpha;
          N->Type=nodetype_exact;
        }
      }
    }
    
    // Test for checkmate or stalemate
    if (N->Score==SCORE_INVALID)
    {
      N->Type=nodetype_exact;
      N->Move=MOVE_NONE;
      if (N->InCheck)
      {
        assert(PosIsMate(N->Pos));
        N->Score=SCORE_MATEDIN(N->Ply);
      }
      else
      {
        assert(PosIsStalemate(N->Pos));
        N->Score=SCORE_DRAW;
      }
      return;
    }
    
    cutoff:
    MovesRewind(&Moves, N->Move);
    
    // Continue onto next depth or done
  }while((Depth+=SearchIIDReduction)<=N->Depth);
  
  // We now know the best move
  assert(MOVE_ISVALID(N->Move));
  assert(SCORE_ISVALID(N->Score));
  assert(N->Type!=nodetype_invalid);
  
  // Update history table
  SearchHistoryUpdate(N);
  
  // Update TT table
  SearchTTWrite(N);
  
  return;
}

void SearchQNodeInternal(node_t *N)
{
  // Init
  ++SearchNodeCount;
  score_t Alpha=N->Alpha;
  
  // Interior node recogniser (also handles draws)
  if (SearchInteriorRecog(N))
    return;
  
  // Standing pat (when not in check)
  if (!N->InCheck)
  {
    score_t Eval=Evaluate(N->Pos);
    if (Eval>=N->Beta)
    {
      N->Type=nodetype_lower;
      N->Move=MOVE_NONE;
      N->Score=N->Beta;
      return;
    }
    else if (Eval>Alpha)
      Alpha=Eval;
  }
  
  // Search moves
  node_t Child;
  N->Type=nodetype_upper;
  N->Move=MOVE_NONE;
  N->Score=SCORE_INVALID;
  Child.Pos=N->Pos;
  Child.Depth=N->Depth;
  Child.Ply=N->Ply+1;
  Child.Alpha=-N->Beta;
  Child.Beta=-Alpha;
  moves_t Moves;
  MovesInit(&Moves, N->Pos, N->InCheck);
  MovesRewind(&Moves, MOVE_INVALID);
  move_t Move;
  bool NoLegalMove=true;
  while((Move=MovesNext(&Moves))!=MOVE_INVALID)
  {
    // Search move
    if (!PosMakeMove(N->Pos, Move))
      continue;
    Child.InCheck=PosIsSTMInCheck(N->Pos);
    score_t Score=-SearchQNode(&Child);
    PosUndoMove(N->Pos);
    
    // Out of time? (previous search result is invalid)
    if (SearchIsTimeUp())
    {
      // Node type is tricky as we haven't yet searched all moves
      N->Type&=~nodetype_upper;
      
      // HACK
      if (N->Move==MOVE_NONE)
        N->Move=MOVE_INVALID;
      
      return;
    }
    
    // We have a legal move
    NoLegalMove=false;
    
    // Better move?
    if (Score>Alpha)
    {
      // Update alpha
      Alpha=Score;
      Child.Beta=-Alpha;
      N->Type=nodetype_exact;
      N->Move=Move;
      
      // Cutoff?
      if (Score>=N->Beta)
      {
        N->Type=nodetype_lower;
        goto cutoff;
      }
    }
  }
  
  // Test for checkmate or stalemate
  if (NoLegalMove)
  {
    if (N->InCheck)
    {
      // We always try every move when in check
      assert(PosIsMate(N->Pos));
      N->Type=nodetype_exact;
      N->Score=SCORE_MATEDIN(N->Ply);
      return;
    }
    else if (!PosLegalMoveExist(N->Pos))
    {
      assert(PosIsStalemate(N->Pos));
      N->Type=nodetype_exact;
      N->Score=SCORE_DRAW;
      return;
    }
    else
      // else there are quiet moves available, assume one is at least as good as standing pat
      N->Type=nodetype_lower;
  }
  
  // We now know the best move
  cutoff:
  N->Score=Alpha;
  assert(N->Type!=nodetype_invalid);
  assert(SCORE_ISVALID(N->Score));
  
  return;
}

static inline bool SearchIsTimeUp()
{
  if (SearchStopFlag)
    return true;
  
  if (SearchInfinite || (SearchNodeCount&1023)!=0 || TimeGet()<SearchEndTime)
    return false;
  
  SearchStopFlag=true;
  return true;
}

void SearchOutput(node_t *N)
{
  assert(SCORE_ISVALID(N->Score));
  assert(N->Move!=MOVE_INVALID);
  assert(N->Type!=nodetype_invalid);
  
  // Various bits of data
  ms_t Time=TimeGet()-SearchStartTime;
  char Str[32];
  SearchScoreToStr(N->Score, N->Type, Str);
  printf("info depth %u score %s nodes %llu time %llu", N->Depth, Str,
         SearchNodeCount, (unsigned long long int)Time);
  if (Time>0)
    printf(" nps %llu", (SearchNodeCount*1000llu)/Time);
# ifndef NDEBUG
  printf(" rawscore %i", (int)N->Score);
# endif
  
  // PV (extracted from TT, mostly)
  printf(" pv");
  int Ply;
  move_t Move=N->Move;
  for(Ply=0;;++Ply)
  {
    // Terminate PV if any of the following are true:
    // * no move
    // * drawn position (we don't want infinite PVs in case of repetition)
    // * the move is not legal (we check this last to avoid undo logic)
    if (Move==MOVE_INVALID || (Ply>0 && PosIsDraw(N->Pos, Ply)) || !PosMakeMove(N->Pos, Move))
      break;
    
    // Print move
    PosMoveToStr(Move, Str);
    printf(" %s", Str);
    
    // Read next move from TT
    Move=MOVE_INVALID;
    if (SearchTTRead(N, &Move))
      Move=N->Move; // to make sure we collect move in case of cutoff
  }
  
  // Return position to initial state
  for(;Ply>0;--Ply)
    PosUndoMove(N->Pos);
  
  printf("\n");
}

void SearchScoreToStr(score_t Score, int Type, char Str[static 32])
{
  // Basic score (either in centipawns or distance to mate)
  if (SCORE_ISMATE(Score))
    sprintf(Str, "mate %i", ((Score<0) ? -SCORE_MATEDIST(Score) : SCORE_MATEDIST(Score)));
  else
    sprintf(Str, "cp %i", SCORE_VALUE(Score));
  
  // Upper/lowerbound?
  if (Type==nodetype_lower)
    strcat(Str, " lowerbound");
  if (Type==nodetype_upper)
    strcat(Str, " upperbound");
}

void SearchHistoryUpdate(const node_t *N)
{
  // Only consider non-capture moves
  move_t Move=N->Move;
  if (!PosIsMoveCapture(N->Pos, Move))
  {
    // Increment count in table
    piece_t FromPiece=PosGetPieceOnSq(N->Pos, MOVE_GETFROMSQ(Move));
    movescore_t *Counter=&SearchHistory[FromPiece][MOVE_GETTOSQ(Move)];
    *Counter+=(((movescore_t)1)<<N->Depth);
    
    // Overflow? (not a literal overflow, but beyond desired range)
    if (*Counter>=HISTORY_MAX)
      SearchHistoryAge();
  }
}

void SearchHistoryAge()
{
  int I, J;
  for(I=0;I<16;++I)
    for(J=0;J<64;++J)
      SearchHistory[I][J]/=2;
}

void SearchHistoryClear()
{
  memset(SearchHistory, 0, sizeof(SearchHistory));
}

bool SearchTTRead(node_t *N, move_t *Move)
{
  // Grab cluster
  hkey_t Key=PosGetKey(N->Pos);
  ttcluster_t *Cluster=HTableGrab(SearchTT, Key);
  
  // Look for match
  unsigned int I;
  tte_t *TTE=&Cluster->Entries[0];
  for(I=0;I<TT_CLUSTERSIZE;++I,++TTE)
    if (SearchTTMatch(N, TTE))
    {
      // Update entry date
      TTE->Date=SearchAge;
      
      // Cutoff possible?
      score_t Score=SearchTTToScore(TTE->Score, N->Ply);
      if (TTE->Depth>=N->Depth &&
          ((TTE->Type==nodetype_exact) ||
           ((TTE->Type & nodetype_lower) && Score>=N->Beta) ||
           ((TTE->Type & nodetype_upper) && Score<=N->Alpha)))
      {
        N->Move=TTE->Move;
        N->Type=TTE->Type;
        N->Score=Score;
        
        HTableRelease(SearchTT, Key);
        return true;
      }
      
      // If no cutoff at least pass back the stored move
      *Move=TTE->Move;
      
      HTableRelease(SearchTT, Key);
      return false;
    }
  
  // No match
  HTableRelease(SearchTT, Key);
  return false;
}

void SearchTTWrite(const node_t *N)
{
  // Sanity checks
  assert(N->Move!=MOVE_INVALID);
  assert(SCORE_ISVALID(N->Score));
  assert(N->Type!=nodetype_invalid);
  
  // Grab cluster
  hkey_t Key=PosGetKey(N->Pos);
  ttcluster_t *Cluster=HTableGrab(SearchTT, Key);
  
  // Find entry to overwrite
  // based on the following factors, in order:
  // * unused - if entry is unused no harm using it
  // * age - prefer replacing older entries over new ones
  // * depth - perfer replacing shallower entries over deeper ones
  // * exact - prefer exact scores to upper- or lower-bounds
# define REPSCORE(AGE,DEPTH,TYPE) (2*SEARCH_MAXAGE*2*256*((TYPE)==nodetype_invalid)+2*256*((int)(AGE))-2*((int)(DEPTH))-((TYPE)==nodetype_exact))
  
  tte_t *Replace, *TTE;
  int ReplaceScore=REPSCORE(0, 255, nodetype_exact)-1; // worst possible score - 1
  Replace=TTE=&Cluster->Entries[0];
  unsigned int I;
  for(I=0;I<TT_CLUSTERSIZE;++I,++TTE)
  {
    // If we find an exact match, simply reuse this entry
    if (SearchTTMatch(N, TTE))
    {
      // Update move if we have one and it is from a deeper search (or no move already stored)
      if (N->Move!=MOVE_NONE && (N->Depth>=TTE->Depth || !MOVE_ISVALID(TTE->Move)))
        TTE->Move=N->Move;
      
      // Update score, depth and type if search was at least as deep as the entry depth
      if (N->Depth>=TTE->Depth)
      {
        TTE->Score=SearchScoreToTT(N->Score, N->Ply);
        TTE->Depth=N->Depth;
        TTE->Type=N->Type;
      }
      
      // Update entry date to current date to reset age to 0
      TTE->Date=SearchAge;
      
      HTableRelease(SearchTT, Key);
      return;
    }
    
    // Is TTE better to use than Replace?
    int TTEScore=REPSCORE(SearchDateToAge(TTE->Date), TTE->Depth, TTE->Type);
    if (TTEScore>ReplaceScore)
    {
      Replace=TTE;
      ReplaceScore=TTEScore;
    }
  }
# undef REPSCORE
  
  // Replace entry
  Replace->Key=PosGetKey(N->Pos);
  Replace->Move=N->Move;
  Replace->Score=SearchScoreToTT(N->Score, N->Ply);
  Replace->Depth=N->Depth;
  Replace->Type=N->Type;
  Replace->Date=SearchAge;
  
  HTableRelease(SearchTT, Key);
}

static inline bool SearchTTMatch(const node_t *N, const tte_t *TTE)
{
  // Key match and move psueudo-legal?
  return (TTE->Key==PosGetKey(N->Pos) && PosIsMovePseudoLegal(N->Pos, TTE->Move));
}

static inline score_t SearchTTToScore(score_t S, int Ply)
{
  if (SCORE_ISMATE(S))
    return (S>0 ? S-Ply : S+Ply); // Adjust to distance from root [to mate]
  else
    return S;
}

static inline score_t SearchScoreToTT(score_t S, int Ply)
{
  if (SCORE_ISMATE(S))
    return (S>0 ? S+Ply : S-Ply); // Adjust to distance from this node [to mate]
  else
    return S;
}

void SearchSetPonder(bool Ponder)
{
  SearchPonder=Ponder;
}

void SearchSetPonderWrapper(bool Ponder, void *Dummy)
{
  SearchSetPonder(Ponder);
}

static inline bool SearchIsZugzwang(const node_t *N)
{
  return (N->InCheck || !PosHasPieces(N->Pos, PosGetSTM(N->Pos)));
}

#ifdef TUNE
void SearchSetValue(int Value, void *UserData)
{
  // Set value
  *((int *)UserData)=Value;
  
  // Clear now-invalid TT and history etc.
  SearchClear();
}
#endif

void SearchNodePreCheck(node_t *N)
{
  // Check preset values are sensible
  assert(PosIsConsistent(N->Pos));
  assert(N->Depth>=0);
  assert(N->Ply>=0);
  assert(-SCORE_INF<=N->Alpha && N->Alpha<N->Beta && N->Beta<=SCORE_INF);
  assert(N->InCheck==PosIsSTMInCheck(N->Pos));
  
  // Set other values to invalid to detect errors in post-checks
  N->Type=nodetype_invalid;
  N->Move=MOVE_INVALID;
  N->Score=SCORE_INVALID;
}

void SearchNodePostCheck(const node_t *PostN, const node_t *PreN)
{
  // Check position, depth, ply and incheck are unchanged
  assert(PostN->Pos==PreN->Pos);
  assert(PostN->Depth==PreN->Depth);
  assert(PostN->Ply==PreN->Ply);
  assert(PostN->InCheck==PreN->InCheck);
  
  // Check type, move and score have been set and are sensible
  if (SCORE_ISVALID(PostN->Score))
  {
    assert(PostN->Type==nodetype_lower || PostN->Type==nodetype_upper || PostN->Type==nodetype_exact);
    assert(PostN->Move!=MOVE_INVALID);
  }
  else
  {
    assert(SearchIsTimeUp());
    assert(PostN->Type==nodetype_invalid);
    assert(PostN->Move==MOVE_INVALID);
  }
}

bool SearchInteriorRecog(node_t *N)
{
  // Sanity checks
  assert(N->Alpha>=-SCORE_INF && N->Alpha<N->Beta && N->Beta<=SCORE_INF);
  assert(N->Ply>0);
  
  // Test for draws by rule (and rare checkmates)
  if (PosIsDraw(N->Pos, N->Ply))
  {
    N->Type=nodetype_exact;
    N->Move=MOVE_NONE;
    
    // In rare cases checkmate can be given on 100th half move
    if (N->InCheck && PosGetHalfMoveClock(N->Pos)==100 && !PosLegalMoveExist(N->Pos))
    {
      assert(PosIsMate(N->Pos));
      N->Score=SCORE_MATEDIN(N->Ply);
    }
    else
      N->Score=SCORE_DRAW;
    
    return true;
  }
  
  // Special recognizers
  switch(EvalGetMatType(N->Pos))
  {
    case evalmattype_KNNvK: if (SearchInteriorRecogKNNvK(N)) return true; break;
    default:
      // No handler for this combination
    break;
  }
  
  return false;
}

bool SearchInteriorRecogKNNvK(node_t *N)
{
  // The defender simply has to avoid mate-in-1 (and can always do so trivially)
  col_t DefSide=(PosPieceCount(N->Pos, wknight)>0 ? black : white);
  if (PosGetSTM(N->Pos)==DefSide && (!N->InCheck || PosLegalMoveExist(N->Pos)))
  {
    assert(!PosIsMate(N->Pos));
    N->Type=nodetype_exact;
    N->Move=MOVE_NONE;
    N->Score=SCORE_DRAW;
    return true;
  }
  
  return false;
}

unsigned int SearchDateToAge(unsigned int Date)
{
  return (Date<=SearchAge ? SearchAge-Date : 64+SearchAge-Date);
}
