#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "eval.h"
#include "search.h"
#include "threads.h"
#include "types.h"

////////////////////////////////////////////////////////////////////////////////
// 
////////////////////////////////////////////////////////////////////////////////

#define SEARCH_MAXPLY 128
unsigned long long int SearchNodeCount;
bool SearchInfinite, SearchStopFlag;
ms_t SearchStartTime, SearchEndTime;
thread_t *SearchThread=NULL;

typedef int movescore_t;

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

////////////////////////////////////////////////////////////////////////////////
// Private prototypes
////////////////////////////////////////////////////////////////////////////////

void SearchIDLoop(void *Data);
score_t SearchNode(node_t *Node);
inline bool SearchIsTimeUp();
void SearchOutput(node_t *N, score_t Score);
void SearchScoreToStr(score_t Score, char Str[static 16]);
void SearchMovesInit(node_t *N, move_t HashMove);
move_t SearchMovesNext(moves_t *Moves);
void SearchSortMoves(moves_t *Moves);
inline movescore_t SearchScoreMove(const pos_t *Pos, move_t Move);

////////////////////////////////////////////////////////////////////////////////
// Public functions
////////////////////////////////////////////////////////////////////////////////

bool SearchInit()
{
  // Create worker thread
  SearchThread=ThreadCreate();
  if (SearchThread==NULL)
    return false;
  
  return true;
}

void SearchQuit()
{
  // If searching, signal to stop and wait until done
  SearchStop();
  
  // Free the worker thread
  ThreadFree(SearchThread);
}

void SearchThink(const pos_t *SrcPos, ms_t StartTime, ms_t SearchTime, bool Infinite)
{
  // Make sure we finish previous search
  SearchStop();
  ThreadWaitReady(SearchThread);
  
  // Prepare for search
  pos_t *Pos=PosCopy(SrcPos);
  if (Pos==NULL)
    return;
  SearchNodeCount=0;
  SearchInfinite=Infinite;
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
    
    // Time to end?
    if (SearchIsTimeUp())
      break;
    
    // Update bestmove
    BestMove=Node.PV[0];
    
    // Output info
    SearchOutput(&Node, Score);
  }
  
  // Send best move
  char Str[8];
  PosMoveToStr(BestMove, Str);
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
    return SCORE_DRAW;
  
  // Test for draws (and rare checkmates)
  if (N->Ply>=1 && PosIsDraw(N->Pos))
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
  
  // Search moves
  score_t BestScore=-SCORE_INF;
  node_t Child;
  Child.Pos=N->Pos;
  Child.Depth=N->Depth-1;
  Child.Ply=N->Ply+1;
  Child.Alpha=-N->Beta;
  Child.Beta=-Alpha;
  SearchMovesInit(N, MOVE_NULL);
  move_t Move;
  while((Move=SearchMovesNext(&N->Moves))!=MOVE_NULL)
  {
    // Search move
    if (!PosMakeMove(N->Pos, Move))
      continue;
    Child.InCheck=PosIsSTMInCheck(N->Pos);
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
  
  // Return
  return BestScore;
}

inline bool SearchIsTimeUp()
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

inline movescore_t SearchScoreMove(const pos_t *Pos, move_t Move)
{
  movescore_t Score=0;
  
  /* Sort first by captured/promotion piece (most valuable first) */
  piece_t FromPiece=PIECE_TYPE(PosGetPieceOnSq(Pos, MOVE_GETFROMSQ(Move)));
  piece_t ToPiece=(MOVE_ISPROMO(Move) ? PIECE_TYPE(MOVE_GETPROMO(Move)) : FromPiece);
  piece_t CapturedPiece=(MOVE_ISEP(Move) ? pawn : PIECE_TYPE(PosGetPieceOnSq(Pos, MOVE_GETTOSQ(Move))));
  Score+=(CapturedPiece+ToPiece-FromPiece)*8;
  
  /* Sort second by capturing piece (least valuable first) */
  Score+=(8-ToPiece);
  
  return Score;
}
