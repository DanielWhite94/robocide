#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "eval.h"
#include "search.h"
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

typedef uint64_t movescore_t;
#define MOVESCORE_WIDTH 64
#define HISTORY_MAX (((movescore_t)1)<<(MOVESCORE_WIDTH-8)) // see SearchScoreMove()
movescore_t SearchHistory[16][64];

TUNECONST int SearchNullReduction=1;
TUNECONST int SearchIIDMin=2;
TUNECONST int SearchIIDReduction=3;

bool SearchPonder=true;

typedef enum
{
  movesstage_tt,
  movesstage_main
}movesstage_t;

typedef struct
{
  move_t Moves[MOVES_MAX], *Next, *End;
  movesstage_t Stage;
  move_t TTMove;
  const pos_t *Pos;
  move_t * (*Gen)(const pos_t *Pos, move_t *Moves);
}moves_t;

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
  moves_t Moves;
  // * - search functions should not modify these entries
  // # - search functions should ensure these are set correctly before returning (even if only say Move==MOVE_NONE)
}node_t;
#define NODE_ISQ(N) ((N)->Depth<1)
#define NODE_ISPV(N) ((N)->Beta-(N)->Alpha>1)

typedef struct
{
  hkey_t Key;
  move_t Move;
  score_t Score;
  uint8_t Depth;
  uint8_t Type;
  uint16_t Dummy;
}tt_t;
tt_t *SearchTT=NULL;
size_t SearchTTSize=0;

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
static inline void SearchMovesInit(node_t *N, move_t TTMove);
static inline void SearchMovesRewind(node_t *N, move_t TTMove);
move_t SearchMovesNext(moves_t *Moves);
void SearchSortMoves(moves_t *Moves);
static inline movescore_t SearchScoreMove(const pos_t *Pos, move_t Move);
void SearchHistoryUpdate(const node_t *N);
void SearchHistoryAge();
void SearchHistoryReset();
void SearchTTResize(int SizeMB);
void SearchTTResizeWrapper(int SizeMB, void *Dummy);
void SearchTTFree();
void SearchTTReset();
void SearchTTResetWrapper(void *Dummy);
bool SearchTTRead(node_t *N, move_t *Move);
void SearchTTWrite(const node_t *N);
static inline bool SearchTTMatch(const node_t *N, const tt_t *TTE);
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

////////////////////////////////////////////////////////////////////////////////
// Public functions
////////////////////////////////////////////////////////////////////////////////

bool SearchInit()
{
  // Create worker thread
  SearchThread=ThreadCreate();
  if (SearchThread==NULL)
    return false;
  
  // Init history tables
  SearchHistoryReset();
  
  // Init TT table
  UCIOptionNewSpin("Hash", &SearchTTResizeWrapper, NULL, 0, 16*1024, 16);
  UCIOptionNewButton("Clear Hash", &SearchTTResetWrapper, NULL);
  SearchTTResize(16);
  
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
  SearchTTFree();
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

void SearchReset()
{
  // Clear history tables
  SearchHistoryReset();
  
  // Clear TT table
  SearchTTReset();
}

void SearchPonderHit()
{
  SearchInfinite=false;
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
  SearchMovesInit(N, TTMove);
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
    while((Move=SearchMovesNext(&N->Moves))!=MOVE_INVALID)
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
    
    // Update TTMove and rewind move pointer
    SearchMovesRewind(N, N->Move);
    
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
  SearchMovesInit(N, MOVE_INVALID);
  move_t Move;
  bool NoLegalMove=true;
  while((Move=SearchMovesNext(&N->Moves))!=MOVE_INVALID)
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
    SearchTTRead(N, &Move);
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
    sprintf(Str, "cp %i", Score);
  
  // Upper/lowerbound?
  if (Type==nodetype_lower)
    strcat(Str, " lowerbound");
  if (Type==nodetype_upper)
    strcat(Str, " upperbound");
}

static inline void SearchMovesInit(node_t *N, move_t TTMove)
{
  N->Moves.Next=N->Moves.End=N->Moves.Moves;
  N->Moves.Stage=movesstage_tt;
  N->Moves.TTMove=TTMove;
  N->Moves.Pos=N->Pos;
  N->Moves.Gen=((!NODE_ISQ(N) || N->InCheck) ? &PosGenPseudoMoves : &PosGenPseudoCaptures);
}

static inline void SearchMovesRewind(node_t *N, move_t TTMove)
{
  N->Moves.Next=N->Moves.Moves;
  N->Moves.Stage=movesstage_tt;
  N->Moves.TTMove=TTMove;
}

move_t SearchMovesNext(moves_t *Moves)
{
  switch(Moves->Stage)
  {
    case movesstage_tt:
      // Update stage ready for next call
      Moves->Stage=movesstage_main;
      
      // Do we have a TT move?
      if (MOVE_ISVALID(Moves->TTMove))
        return Moves->TTMove;
      
      // Fall through to generate/choose a move
    case movesstage_main:
      // Do we need to generate any moves? (we might have already done this)
      if (Moves->Gen!=NULL)
      {
        assert(Moves->Next==Moves->Moves);
        Moves->End=Moves->Gen(Moves->Pos, Moves->Moves);
        Moves->Gen=NULL;
        SearchSortMoves(Moves);
      }
      
      // Choose move
      while(Moves->Next<Moves->End)
        if (*Moves->Next++!=Moves->TTMove)
          return *(Moves->Next-1);
      
      // No moves left
      return MOVE_INVALID;
    break;
  }
  
  assert(false);
  return MOVE_INVALID;
}

void SearchSortMoves(moves_t *Moves)
{
  // Calculate scores
  movescore_t Scores[MOVES_MAX], *ScorePtr;
  move_t *MovePtr;
  for(MovePtr=Moves->Moves,ScorePtr=Scores;MovePtr<Moves->End;++MovePtr)
    *ScorePtr++=SearchScoreMove(Moves->Pos, *MovePtr);
  
  // Sort (best move first)
  for(MovePtr=Moves->Moves+1,ScorePtr=Scores+1;MovePtr<Moves->End;++MovePtr,++ScorePtr)
  {
    move_t TempMove=*MovePtr, *TempMovePtr;
    movescore_t TempScore=*ScorePtr, *TempScorePtr;
    for(TempMovePtr=MovePtr-1,TempScorePtr=ScorePtr-1;TempScore>*TempScorePtr && TempScorePtr>=Scores;--TempMovePtr,--TempScorePtr)
    {
      *(TempScorePtr+1)=*TempScorePtr;
      *(TempMovePtr+1)=*TempMovePtr;
    }
    *(TempScorePtr+1)=TempScore;
    *(TempMovePtr+1)=TempMove;
  }
}

static inline movescore_t SearchScoreMove(const pos_t *Pos, move_t Move)
{
  movescore_t Score=0;
  
  // Sort first by captured/promotion piece (most valuable first)
  piece_t FromPiece=PosGetPieceOnSq(Pos, MOVE_GETFROMSQ(Move));
  piece_t ToPiece=(MOVE_ISPROMO(Move) ? MOVE_GETPROMO(Move) : FromPiece);
  piece_t CapturedPiece=(MOVE_ISEP(Move) ? pawn : PIECE_TYPE(PosGetPieceOnSq(Pos, MOVE_GETTOSQ(Move))));
  Score+=(CapturedPiece+PIECE_TYPE(ToPiece)-PIECE_TYPE(FromPiece))*8*HISTORY_MAX;
  
  // Sort second by capturing piece (least valuable first)
  Score+=(8-PIECE_TYPE(ToPiece))*HISTORY_MAX;
  
  // Further sort using history tables
  Score+=SearchHistory[FromPiece][MOVE_GETTOSQ(Move)];
  
  return Score;
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

void SearchHistoryReset()
{
  memset(SearchHistory, 0, sizeof(SearchHistory));
}

void SearchTTResize(int SizeMB)
{
  // No TT wanted?
  if (SizeMB<1)
  {
    SearchTTFree();
    return;
  }
  
  // Calculate greatest power of two number of entries we can fit in SizeMB
  uint64_t Entries=(((uint64_t)SizeMB)*1024llu*1024llu)/sizeof(tt_t);
  Entries=NextPowTwo64(Entries+1)/2;
  
  // Attempt to allocate table
  while(Entries>0)
  {
    tt_t *Ptr=realloc(SearchTT, Entries*sizeof(tt_t));
    if (Ptr!=NULL)
    {
      // Update table
      SearchTT=Ptr;
      SearchTTSize=Entries;
      
      // Clear entries
      SearchTTReset();
      
      return;
    }
    Entries/=2;
  }
  
  // Could not allocate
  SearchTTFree();
}

void SearchTTResizeWrapper(int SizeMB, void *Dummy)
{
  SearchTTResize(SizeMB);
}

void SearchTTFree()
{
  free(SearchTT);
  SearchTT=NULL;
  SearchTTSize=0;
}

void SearchTTReset()
{
  memset(SearchTT, 0, SearchTTSize*sizeof(tt_t)); // HACK
}

void SearchTTResetWrapper(void *Dummy)
{
  SearchTTReset();
}

bool SearchTTRead(node_t *N, move_t *Move)
{
  // No TT?
  if (SearchTT==NULL)
    return false;
  
  // Grab entry
  int Index=(PosGetKey(N->Pos) & (SearchTTSize-1));
  tt_t *TTE=&SearchTT[Index];
  
  // Match?
  if (!SearchTTMatch(N, TTE))
    return false;
  
  
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
    return true;
  }
  
  // If no cutoff at least pass back the stored move
  *Move=TTE->Move;
  
  return false;
}

void SearchTTWrite(const node_t *N)
{
  // Sanity checks
  assert(N->Move!=MOVE_INVALID);
  assert(SCORE_ISVALID(N->Score));
  assert(N->Type!=nodetype_invalid);
  
  // No TT?
  if (SearchTT==NULL)
    return;
  
  // Grab entry
  int Index=(PosGetKey(N->Pos) & (SearchTTSize-1));
  tt_t *TTE=&SearchTT[Index];
  
  // Replace/update?
  if (!SearchTTMatch(N, TTE) || N->Depth>=TTE->Depth)
  {
    TTE->Key=PosGetKey(N->Pos);
    TTE->Move=N->Move;
    TTE->Score=SearchScoreToTT(N->Score, N->Ply);
    TTE->Depth=N->Depth;
    TTE->Type=N->Type;
  }
}

static inline bool SearchTTMatch(const node_t *N, const tt_t *TTE)
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
  SearchReset();
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
  
  return false;
}
