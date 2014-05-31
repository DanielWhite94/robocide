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
  movesstage_hash,
  movesstage_main,
}movesstage_t;

typedef struct
{
  move_t Moves[MOVES_MAX], *End;
  movesstage_t Stage;
  move_t HashMove;
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
  move_t BestMove;
}hash_t;
hash_t *SearchHashTable=NULL;
size_t SearchHashTableSize=0;

////////////////////////////////////////////////////////////////////////////////
// Private prototypes
////////////////////////////////////////////////////////////////////////////////

void SearchIDLoop(void *Data);
score_t SearchNode(node_t *Node);
static inline bool SearchIsTimeUp();
void SearchOutput(node_t *N, score_t Score);
void SearchScoreToStr(score_t Score, char Str[static 16]);
void SearchMovesInit(node_t *N, move_t HashMove);
move_t SearchMovesNext(moves_t *Moves);
void SearchSortMoves(moves_t *Moves);
static inline movescore_t SearchScoreMove(const pos_t *Pos, move_t Move);
void SearchHistoryUpdate(const node_t *N);
void SearchHistoryAge();
void SearchHistoryReset();
void SearchHashResize(int SizeMB);
void SearchHashFree();
void SearchHashReset();
move_t SearchHashRead(const node_t *N);
void SearchHashUpdate(const node_t *N);
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
  
  // Init hash table
  UCIOptionNewSpin("Hash", &SearchHashResize, 0, 16*1024, 16);
  UCIOptionNewButton("Clear Hash", &SearchHashReset);
  SearchHashResize(16);
  
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
  
  // Free hash table
  SearchHashFree();
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
  
  // Clear hash table
  SearchHashReset();
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
    score_t Score=SearchNode(&Node);
    
    // Early return? (i.e. out of 'time' and no moves searched)
    // If we are not using a hash table we cannot trust the move returned.
    // This is because if we haven't yet searched the previous-depth's best
    // move, we may choose an inferior earlier move.
    if (Node.PV[0]==MOVE_NULL || (SearchHashTable==NULL && SearchIsTimeUp()))
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

score_t SearchNode(node_t *N)
{
  // Sanity checks
  assert(-SCORE_INF<=N->Alpha && N->Alpha<N->Beta && N->Beta<=SCORE_INF);
  assert(N->InCheck==PosIsSTMInCheck(N->Pos));
  
  // Init
  N->PV[0]=MOVE_NULL;
  ++SearchNodeCount;
  score_t Alpha=N->Alpha;
  
  // Ply limit reached?
  if (N->Ply>=SEARCH_MAXPLY)
    return Evaluate(N->Pos);
  
  // Test for draws (and rare checkmates)
  if (N->Ply>=1 && PosIsDraw(N->Pos, N->Ply))
  {
    // In rare cases checkmate can be given on 100th half move
    if (N->InCheck && PosGetHalfMoveClock(N->Pos)==100 && !PosLegalMoveExist(N->Pos))
      return SCORE_MATEDIN(N->Ply);
    else
      return SCORE_DRAW;
  }

  // Standing pat (qsearch only)
  if (NODE_ISQ(N) && !N->InCheck)
  {
    score_t Eval=Evaluate(N->Pos);
    if (Eval>=N->Beta)
      return N->Beta;
    if (Eval>Alpha)
      Alpha=Eval;
  }
  
  // Check hash table
  move_t HashMove=(NODE_ISQ(N) ? MOVE_NULL : SearchHashRead(N));
  
  // Search moves
  score_t BestScore=-SCORE_INF;
  node_t Child;
  Child.Pos=N->Pos;
  Child.Ply=N->Ply+1;
  Child.Alpha=-N->Beta;
  Child.Beta=-Alpha;
  SearchMovesInit(N, HashMove);
  move_t Move;
  while((Move=SearchMovesNext(&N->Moves))!=MOVE_NULL)
  {
    // Search move
    if (!PosMakeMove(N->Pos, Move))
      continue;
    Child.InCheck=PosIsSTMInCheck(N->Pos);
    Child.Depth=N->Depth-1;
    if (!NODE_ISQ(N) && Child.InCheck)
      Child.Depth++; // Check extension
    score_t Score=-SearchNode(&Child);
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
        Child.Beta=-Alpha;
      }
    }
  }
  
  // Test for checkmate or stalemate
  if (BestScore==-SCORE_INF)
  {
    if (N->InCheck)
      return SCORE_MATEDIN(N->Ply); // We always try every move when in check
    else if (!NODE_ISQ(N) || !PosLegalMoveExist(N->Pos))
      return SCORE_DRAW;
    else
      return Alpha; // qsearch standing pat
  }
  
  // We now know the best move
  cutoff:
  assert(N->PV[0]!=MOVE_NULL);
  
  // Update history table
  SearchHistoryUpdate(N);
  
  // Update hash table
  if (!NODE_ISQ(N))
    SearchHashUpdate(N);
  
  // Return
  return BestScore;
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

void SearchMovesInit(node_t *N, move_t HashMove)
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
    N->Moves.End=N->Moves.Moves;
    N->Moves.Stage=movesstage_hash;
    N->Moves.HashMove=HashMove;
    *N->Moves.End=HashMove;
    N->Moves.End+=PosIsMovePseudoLegal(N->Pos, HashMove);
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
      case movesstage_hash:
        // Generate moves
        Moves->End=PosGenPseudoMoves(Moves->Pos, Moves->Moves);
        
        // Remove hash move
        for(Move=Moves->Moves;Move<Moves->End;++Move)
          if (*Move==Moves->HashMove)
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

void SearchHashResize(int SizeMB)
{
  // No hash table wanted?
  if (SizeMB<1)
  {
    SearchHashFree();
    return;
  }
  
  // Calculate greatest power of two number of entries we can fit in SizeMB
  uint64_t Entries=(((uint64_t)SizeMB)*1024llu*1024llu)/sizeof(hash_t);
  Entries=NextPowTwo64(Entries+1)/2;
  
  // Attempt to allocate table
  while(Entries>0)
  {
    hash_t *Ptr=realloc(SearchHashTable, Entries*sizeof(hash_t));
    if (Ptr!=NULL)
    {
      // Update table
      SearchHashTable=Ptr;
      SearchHashTableSize=Entries;
      
      // Clear entries
      SearchHashReset();
      
      return;
    }
    Entries/=2;
  }
  
  // Could not allocate
  SearchHashFree();
}

void SearchHashFree()
{
  free(SearchHashTable);
  SearchHashTable=NULL;
  SearchHashTableSize=0;
}

void SearchHashReset()
{
  memset(SearchHashTable, 0, SearchHashTableSize*sizeof(hash_t)); // HACK
}

move_t SearchHashRead(const node_t *N)
{
  if (SearchHashTable==NULL)
    return MOVE_NULL;
  
  int Index=(PosGetKey(N->Pos) & (SearchHashTableSize-1));
  hash_t *Entry=&SearchHashTable[Index];
  
  return Entry->BestMove;
}

void SearchHashUpdate(const node_t *N)
{
  assert(N->PV[0]!=MOVE_NULL);
  
  if (SearchHashTable==NULL)
    return;
  
  int Index=(PosGetKey(N->Pos) & (SearchHashTableSize-1));
  hash_t *Entry=&SearchHashTable[Index];
  
  Entry->BestMove=N->PV[0];
}

void SearchSetPonder(bool Ponder)
{
  SearchPonder=Ponder;
}
