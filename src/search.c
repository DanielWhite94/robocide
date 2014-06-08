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

bool SearchPonder=true;

typedef enum
{
  movesstage_tt,
  movesstage_main,
}movesstage_t;

typedef struct
{
  move_t Moves[MOVES_MAX], *End;
  movesstage_t Stage;
  move_t TTMove;
  const pos_t *Pos;
}moves_t;

typedef struct
{
  pos_t *Pos;
  int Depth, Ply;
  score_t Alpha, Beta;
  bool InCheck;
  move_t PV[SEARCH_MAXPLY];
  moves_t Moves;
}node_t;
#define NODE_ISQ(N) ((N)->Depth<1)

typedef struct
{
  hkey_t Key;
  move_t Move;
  score_t Score;
  uint8_t Depth;
  uint8_t Type;
  uint16_t Dummy;
}tt_t;
#define NODETYPE_NONE (0u)
#define NODETYPE_LOWER (1u)
#define NODETYPE_UPPER (2u)
#define NODETYPE_EXACT (NODETYPE_LOWER | NODETYPE_UPPER)
tt_t *SearchTT=NULL;
size_t SearchTTSize=0;

////////////////////////////////////////////////////////////////////////////////
// Private prototypes
////////////////////////////////////////////////////////////////////////////////

void SearchIDLoop(void *Data);
score_t SearchPVNode(node_t *Node);
score_t SearchZWNode(node_t *Node);
score_t SearchQNode(node_t *Node);
static inline bool SearchIsTimeUp();
void SearchOutput(node_t *N, score_t Score);
void SearchScoreToStr(score_t Score, char Str[static 16]);
void SearchMovesInit(node_t *N, move_t TTMove);
move_t SearchMovesNext(moves_t *Moves);
void SearchSortMoves(moves_t *Moves);
static inline movescore_t SearchScoreMove(const pos_t *Pos, move_t Move);
void SearchHistoryUpdate(const node_t *N);
void SearchHistoryAge();
void SearchHistoryReset();
void SearchTTResize(int SizeMB);
void SearchTTFree();
void SearchTTReset();
bool SearchTTRead(const node_t *N, move_t *Move, score_t *Score);
void SearchTTWrite(const node_t *N, score_t Score);
static inline bool SearchTTMatch(const node_t *N, const tt_t *TTE);
static inline score_t SearchTTToScore(score_t S, int Ply);
static inline score_t SearchScoreToTT(score_t S, int Ply);
void SearchSetPonder(bool Ponder);

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
  UCIOptionNewSpin("Hash", &SearchTTResize, 0, 16*1024, 16);
  UCIOptionNewButton("Clear Hash", &SearchTTReset);
  SearchTTResize(16);
  
  // Init pondering
  UCIOptionNewCheck("Ponder", &SearchSetPonder, SearchPonder);
  
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
  
  // Choose a legal move in case we don't even complete a single ply search
  move_t BestMove=PosGenLegalMove(Node.Pos);
  
  // Setup the PV array
  Node.PV[0]=BestMove;
  Node.PV[1]=MOVE_NULL;
  
  // Loop increasing search depth until we run out of 'time'
  for(Node.Depth=1;Node.Depth<SEARCH_MAXPLY;++Node.Depth)
  {
    // Search
    score_t Score=SearchPVNode(&Node);
    
    // Early return? (i.e. out of 'time' and no moves searched)
    // If we are not using a TT we cannot trust the move returned.
    // This is because if we haven't yet searched the previous-depth's best
    // move, we may choose an inferior earlier move.
    if (Node.PV[0]==MOVE_NULL || (SearchTT==NULL && SearchIsTimeUp()))
      break;
    
    // Update bestmove
    BestMove=Node.PV[0];
    
    // Output info
    SearchOutput(&Node, Score);
    
    // Time to end?
    if (SearchIsTimeUp())
      break;
  }
  
  // Send best move (and potentially ponder move)
  char Str[8];
  PosMoveToStr(BestMove, Str);
  if (SearchPonder && Node.PV[1]!=MOVE_NULL)
  {
    char Str2[8];
    PosMoveToStr(Node.PV[1], Str2);
    printf("bestmove %s ponder %s\n", Str, Str2);
  }
  else
    printf("bestmove %s\n", Str);
  
  // Free position
  PosFree(Node.Pos);
}

score_t SearchPVNode(node_t *N)
{
  // Sanity checks
  assert(-SCORE_INF<=N->Alpha && N->Alpha<N->Beta && N->Beta<=SCORE_INF);
  assert(N->InCheck==PosIsSTMInCheck(N->Pos));
  assert(N->Ply>=0);
  
  // Set null move now in case of early return
  N->PV[0]=MOVE_NULL;
  
  // Q node? (or ply limit reached)
  if (NODE_ISQ(N) || N->Ply>=SEARCH_MAXPLY)
    return SearchQNode(N);
  
  // Node begins
  ++SearchNodeCount;
  
  // Test for draws (and rare checkmates)
  if (N->Ply>0 && PosIsDraw(N->Pos, N->Ply))
  {
    // In rare cases checkmate can be given on 100th half move
    if (N->InCheck && PosGetHalfMoveClock(N->Pos)==100 && !PosLegalMoveExist(N->Pos))
    {
      assert(PosIsMate(N->Pos));
      return SCORE_MATEDIN(N->Ply);
    }
    else
      return SCORE_DRAW;
  }
  
  // Check TT table
  move_t TTMove=MOVE_NULL;
  score_t TTScore;
  if (SearchTTRead(N, &TTMove, &TTScore))
  {
    N->PV[0]=TTMove;
    N->PV[1]=MOVE_NULL;
    return TTScore;
  }
  
  // Search moves
  score_t Alpha=N->Alpha;
  score_t BestScore=SCORE_NONE;
  node_t Child;
  Child.Pos=N->Pos;
  Child.Ply=N->Ply+1;
  Child.Alpha=-N->Beta;
  Child.Beta=-Alpha;
  SearchMovesInit(N, TTMove);
  move_t Move;
  while((Move=SearchMovesNext(&N->Moves))!=MOVE_NULL)
  {
    // Search move
    if (!PosMakeMove(N->Pos, Move))
      continue;
    Child.InCheck=PosIsSTMInCheck(N->Pos);
    Child.Depth=N->Depth-!Child.InCheck; // Check extension
    
    // PVS search
    score_t Score;
    if (Alpha>N->Alpha)
    {
      // We have found a good move, try zero window search
      assert(Child.Alpha==Child.Beta-1);
      Score=-SearchZWNode(&Child);
      
      // Research?
      if (Score>Alpha && Score<N->Beta)
      {
        Child.Alpha=-N->Beta;
        Score=-SearchPVNode(&Child);
        Child.Alpha=Child.Beta-1;
      }
    }
    else
    {
      assert(Child.Alpha==-N->Beta);
      Score=-SearchPVNode(&Child);
    }
    
    PosUndoMove(N->Pos);
    
    // Out of time? (previous search result is invalid)
    if (SearchIsTimeUp())
      return BestScore;
    
    // Better move?
    if (Score>BestScore)
    {
      // Update best score and PV
      BestScore=Score;
      N->PV[0]=Move;
      move_t *PVPtr=N->PV+1, *SubPVPtr=Child.PV;
      do
      {
        *PVPtr++=*SubPVPtr;
      }while(*SubPVPtr++!=MOVE_NULL);
      
      // Cutoff?
      if (Score>=N->Beta)
        goto cutoff;
      
      // Update alpha
      if (Score>Alpha)
      {
        Alpha=Score;
        Child.Alpha=-Alpha-1;
        Child.Beta=-Alpha;
      }
    }
  }
  
  // Test for checkmate or stalemate [shouldn't really happen in root]
  if (BestScore==SCORE_NONE)
  {
    if (N->InCheck)
    {
      assert(PosIsMate(N->Pos));
      return SCORE_MATEDIN(N->Ply);
    }
    else
    {
      assert(PosIsStalemate(N->Pos));
      return SCORE_DRAW;
    }
  }
  
  // We now know the best move
  cutoff:
  assert(N->PV[0]!=MOVE_NULL);
  
  // Update history table
  SearchHistoryUpdate(N);
  
  // Update TT table
  SearchTTWrite(N, BestScore);
  
  // Return
  return BestScore;
}

score_t SearchZWNode(node_t *N)
{
  // Sanity checks
  assert(-SCORE_INF<=N->Alpha && N->Alpha+1==N->Beta && N->Beta<=SCORE_INF);
  assert(N->InCheck==PosIsSTMInCheck(N->Pos));
  assert(N->Ply>=1);
  
  // Q node? (or ply limit reached)
  if (NODE_ISQ(N) || N->Ply>=SEARCH_MAXPLY)
    return SearchQNode(N);
  
  // Node begins
  ++SearchNodeCount;
  
  // Test for draws (and rare checkmates)
  if (PosIsDraw(N->Pos, N->Ply))
  {
    // In rare cases checkmate can be given on 100th half move
    if (N->InCheck && PosGetHalfMoveClock(N->Pos)==100 && !PosLegalMoveExist(N->Pos))
    {
      assert(PosIsMate(N->Pos));
      return SCORE_MATEDIN(N->Ply);
    }
    else
      return SCORE_DRAW;
  }
  
  // Check TT table
  move_t TTMove=MOVE_NULL;
  score_t TTScore;
  if (SearchTTRead(N, &TTMove, &TTScore))
    return TTScore;
  
  // Search moves
  score_t BestScore=SCORE_NONE;
  N->PV[0]=MOVE_NULL;
  node_t Child;
  Child.Pos=N->Pos;
  Child.Ply=N->Ply+1;
  Child.Alpha=-N->Beta;
  Child.Beta=-N->Alpha;
  SearchMovesInit(N, TTMove);
  move_t Move;
  while((Move=SearchMovesNext(&N->Moves))!=MOVE_NULL)
  {
    // Search move
    if (!PosMakeMove(N->Pos, Move))
      continue;
    Child.InCheck=PosIsSTMInCheck(N->Pos);
    Child.Depth=N->Depth-!Child.InCheck; // Check extension
    score_t Score=-SearchZWNode(&Child);
    PosUndoMove(N->Pos);
    
    // Out of time? (previous search result is invalid)
    if (SearchIsTimeUp())
      return BestScore;
    
    // Better move?
    if (Score>BestScore)
    {
      // Update best score and move
      BestScore=Score;
      N->PV[0]=Move;
      
      // Cutoff?
      if (Score>=N->Beta)
        goto cutoff;
    }
  }
  
  // Test for checkmate or stalemate
  if (BestScore==SCORE_NONE)
  {
    if (N->InCheck)
    {
      assert(PosIsMate(N->Pos));
      return SCORE_MATEDIN(N->Ply);
    }
    else
    {
      assert(PosIsStalemate(N->Pos));
      return SCORE_DRAW;
    }
  }
  
  // We now know the best move
  cutoff:
  assert(N->PV[0]!=MOVE_NULL);
  
  // Update history table
  SearchHistoryUpdate(N);
  
  // Update TT table
  SearchTTWrite(N, BestScore);
  
  // Return
  return BestScore;
}

score_t SearchQNode(node_t *N)
{
  // Sanity checks
  assert(-SCORE_INF<=N->Alpha && N->Alpha<N->Beta && N->Beta<=SCORE_INF);
  assert(N->InCheck==PosIsSTMInCheck(N->Pos));
  assert(NODE_ISQ(N));
  assert(N->Ply>=1);
  
  // Init
  ++SearchNodeCount;
  score_t Alpha=N->Alpha;
  
  // Test for draws (and rare checkmates)
  if (PosIsDraw(N->Pos, N->Ply))
  {
    // In rare cases checkmate can be given on 100th half move (however unlikely
    // this actually occurs in q-search...)
    if (N->InCheck && PosGetHalfMoveClock(N->Pos)==100 && !PosLegalMoveExist(N->Pos))
      return SCORE_MATEDIN(N->Ply);
    else
      return SCORE_DRAW;
  }

  // Standing pat (when not in check)
  if (!N->InCheck)
  {
    score_t Eval=Evaluate(N->Pos);
    if (Eval>=N->Beta)
      return N->Beta;
    if (Eval>Alpha)
      Alpha=Eval;
  }
  
  // Search moves
  node_t Child;
  Child.Pos=N->Pos;
  Child.Depth=N->Depth-1;
  Child.Ply=N->Ply+1;
  Child.Alpha=-N->Beta;
  Child.Beta=-Alpha;
  SearchMovesInit(N, MOVE_NULL);
  move_t Move;
  bool NoLegalMove=true;
  while((Move=SearchMovesNext(&N->Moves))!=MOVE_NULL)
  {
    // Search move
    if (!PosMakeMove(N->Pos, Move))
      continue;
    Child.InCheck=PosIsSTMInCheck(N->Pos);
    score_t Score=-SearchQNode(&Child);
    PosUndoMove(N->Pos);
    
    // Out of time? (previous search result is invalid)
    if (SearchIsTimeUp())
      return Alpha;
    
    // We have a legal move
    NoLegalMove=false;
    
    // Better move?
    if (Score>Alpha)
    {
      // Update alpha
      Alpha=Score;
      Child.Beta=-Alpha;
      
      // Cutoff?
      if (Score>=N->Beta)
        goto cutoff;
    }
  }
  
  // Test for checkmate or stalemate
  if (NoLegalMove)
  {
    if (N->InCheck)
    {
      // We always try every move when in check
      assert(PosIsMate(N->Pos));
      return SCORE_MATEDIN(N->Ply);
    }
    else if (!PosLegalMoveExist(N->Pos))
    {
      assert(PosIsStalemate(N->Pos));
      return SCORE_DRAW;
    }
    else
      // else there are quiet moves available, assume one is good
      return Alpha;
  }
  
  // We now know the best move
  cutoff:
  assert(!NoLegalMove);
  
  // Return
  return Alpha;
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

void SearchOutput(node_t *N, score_t Score)
{
  ms_t Time=TimeGet()-SearchStartTime;
  char Str[16];
  SearchScoreToStr(Score, Str);
  printf("info depth %u score %s nodes %llu time %llu", N->Depth, Str,
         SearchNodeCount, (unsigned long long int)Time);
  if (Time>0)
    printf(" nps %llu", (SearchNodeCount*1000llu)/Time);
  printf(" pv");
  const move_t *Move;
  for(Move=N->PV;*Move!=MOVE_NULL;++Move)
  {
    PosMoveToStr(*Move, Str);
    if (!PosMakeMove(N->Pos, *Move))
      break;
    printf(" %s", Str);
  }
  for(--Move;Move>=N->PV;--Move)
    PosUndoMove(N->Pos);
  printf("\n");
}

void SearchScoreToStr(score_t Score, char Str[static 16])
{
  if (SCORE_ISMATE(Score))
    sprintf(Str, "mate %i", ((Score<0) ? -SCORE_MATEDIST(Score) : SCORE_MATEDIST(Score)));
  else
    sprintf(Str, "cp %i", Score);
}

void SearchMovesInit(node_t *N, move_t TTMove)
{
  if (NODE_ISQ(N))
  {
    // Generate moves
    if (N->InCheck)
      N->Moves.End=PosGenPseudoMoves(N->Pos, N->Moves.Moves);
    else
      N->Moves.End=PosGenPseudoCaptures(N->Pos, N->Moves.Moves);
    N->Moves.Stage=movesstage_main;
    N->Moves.Pos=N->Pos;
    
    // Score and sort moves
    SearchSortMoves(&N->Moves);
  }
  else
  {
    assert(TTMove==MOVE_NULL || PosIsMovePseudoLegal(N->Pos, TTMove));
    
    N->Moves.End=N->Moves.Moves;
    N->Moves.Stage=movesstage_tt;
    N->Moves.TTMove=TTMove;
    *N->Moves.End=TTMove;
    N->Moves.End+=(TTMove!=MOVE_NULL);
    N->Moves.Pos=N->Pos;
  }
}

move_t SearchMovesNext(moves_t *Moves)
{
  while(1)
  {
    // If we already have a move waiting, return it
    if (Moves->End>Moves->Moves)
      return *--Moves->End;
    
    // Otherwise move to next stage
    move_t *Move;
    switch(Moves->Stage)
    {
      case movesstage_tt:
        // Generate moves
        Moves->End=PosGenPseudoMoves(Moves->Pos, Moves->Moves);
        
        // Remove TT move
        for(Move=Moves->Moves;Move<Moves->End;++Move)
          if (*Move==Moves->TTMove)
          {
            *Move=*--Moves->End;
            break;
          }
        
        // Score and sort
        SearchSortMoves(Moves);
        
        // Update stage
        Moves->Stage=movesstage_main;
      break;
      case movesstage_main:
        return MOVE_NULL; // No more moves
      break;
    }
  }
}

void SearchSortMoves(moves_t *Moves)
{
  // Calculate scores
  movescore_t Scores[MOVES_MAX], *ScorePtr;
  move_t *MovePtr;
  for(MovePtr=Moves->Moves,ScorePtr=Scores;MovePtr<Moves->End;++MovePtr)
    *ScorePtr++=SearchScoreMove(Moves->Pos, *MovePtr);
  
  // Sort (best move last)
  for(MovePtr=Moves->Moves+1,ScorePtr=Scores+1;MovePtr<Moves->End;++MovePtr,++ScorePtr)
  {
    move_t TempMove=*MovePtr, *TempMovePtr;
    movescore_t TempScore=*ScorePtr, *TempScorePtr;
    for(TempMovePtr=MovePtr-1,TempScorePtr=ScorePtr-1;TempScore<*TempScorePtr && TempScorePtr>=Scores;--TempMovePtr,--TempScorePtr)
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
  
  /* Sort first by captured/promotion piece (most valuable first) */
  piece_t FromPiece=PosGetPieceOnSq(Pos, MOVE_GETFROMSQ(Move));
  piece_t ToPiece=(MOVE_ISPROMO(Move) ? MOVE_GETPROMO(Move) : FromPiece);
  piece_t CapturedPiece=(MOVE_ISEP(Move) ? pawn : PIECE_TYPE(PosGetPieceOnSq(Pos, MOVE_GETTOSQ(Move))));
  Score+=(CapturedPiece+PIECE_TYPE(ToPiece)-PIECE_TYPE(FromPiece))*8*HISTORY_MAX;
  
  /* Sort second by capturing piece (least valuable first) */
  Score+=(8-PIECE_TYPE(ToPiece))*HISTORY_MAX;
  
  /* Further sort using history tables */
  Score+=SearchHistory[FromPiece][MOVE_GETTOSQ(Move)];
  
  return Score;
}

void SearchHistoryUpdate(const node_t *N)
{
  /* Only consider non-capture moves */
  move_t Move=N->PV[0];
  if (!PosIsMoveCapture(N->Pos, Move))
  {
    /* Increment count in table */
    piece_t FromPiece=PosGetPieceOnSq(N->Pos, MOVE_GETFROMSQ(Move));
    movescore_t *Counter=&SearchHistory[FromPiece][MOVE_GETTOSQ(Move)];
    *Counter+=(((movescore_t)1)<<N->Depth);
    
    /* Overflow? (not a literal overflow, but beyond desired range) */
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

bool SearchTTRead(const node_t *N, move_t *Move, score_t *Score)
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
  
  // Set move and score
  *Move=TTE->Move;
  *Score=SearchScoreToTT(TTE->Score, N->Ply);
  
  // Cutoff possible?
  if (TTE->Depth>=N->Depth &&
      ((TTE->Type==NODETYPE_EXACT) ||
       ((TTE->Type & NODETYPE_LOWER) && *Score>=N->Beta) ||
       ((TTE->Type & NODETYPE_UPPER) && *Score<=N->Alpha)))
    return true;
  
  return false;
}

void SearchTTWrite(const node_t *N, score_t Score)
{
  // Sanity checks
  assert(N->PV[0]!=MOVE_NULL);
  assert(SCORE_ISVALID(Score));
  
  // No TT?
  if (SearchTT==NULL)
    return;
  
  // Grab entry
  int Index=(PosGetKey(N->Pos) & (SearchTTSize-1));
  tt_t *TTE=&SearchTT[Index];
  
  // Replace/update?
  if (!SearchTTMatch(N, TTE) || N->Depth>=TTE->Depth)
  {
    // Find node type
    uint8_t Type=NODETYPE_NONE;
    if (Score>N->Alpha)
      Type|=NODETYPE_LOWER;
    if (Score<N->Beta)
      Type|=NODETYPE_UPPER;
    
    // Update entry
    TTE->Key=PosGetKey(N->Pos);
    TTE->Move=N->PV[0];
    TTE->Score=SearchScoreToTT(Score, N->Ply);
    TTE->Depth=N->Depth;
    TTE->Type=Type;
  }
}

static inline bool SearchTTMatch(const node_t *N, const tt_t *TTE)
{
  // Key match?
  if (TTE->Key!=PosGetKey(N->Pos))
    return false;
  
  // TT move pseudo-legal?
  if (TTE->Move!=MOVE_NULL && !PosIsMovePseudoLegal(N->Pos, TTE->Move))
    return false;
  
  return true;
}

static inline score_t SearchTTToScore(score_t S, int Ply)
{
  if (SCORE_ISMATE(S))
    return (S>0 ? S+Ply : S-Ply); // Adjust to distance from root [to mate]
  else
    return S;
}

static inline score_t SearchScoreToTT(score_t S, int Ply)
{
  if (SCORE_ISMATE(S))
    return (S>0 ? S-Ply : S+Ply); // Adjust to distance from this node [to mate]
  else
    return S;
}

void SearchSetPonder(bool Ponder)
{
  SearchPonder=Ponder;
}
