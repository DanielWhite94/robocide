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

////////////////////////////////////////////////////////////////////////////////
// Private prototypes
////////////////////////////////////////////////////////////////////////////////

void SearchIDLoop(void *Data);
score_t SearchNode(pos_t *Pos, unsigned int Depth, unsigned int Ply,
                   score_t Alpha, score_t Beta, move_t *PV, bool InCheck);
score_t SearchQNode(pos_t *Pos, unsigned int Ply, score_t Alpha, score_t Beta, bool InCheck);
inline bool SearchIsTimeUp();
void SearchOutput(pos_t *Pos, unsigned int Depth, score_t Score, const move_t *PV);
void SearchScoreToStr(score_t Score, char Str[static 16]);

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
  // Grab position
  pos_t *Pos=(pos_t *)Data;
  bool InCheck=PosIsSTMInCheck(Pos);
  
  // Choose a legal move in case we don't even complete a single ply search
  move_t BestMove=PosGenLegalMove(Pos);
  
  // Setup the PV array
  move_t PV[SEARCH_MAXPLY]={0};
  PV[0]=BestMove;
  
  // Loop increasing search depth until we run out of 'time'
  unsigned int Depth;
  for(Depth=1;Depth<SEARCH_MAXPLY;++Depth)
  {
    // Search
    score_t Score=SearchNode(Pos, Depth, 1, -SCORE_INF, SCORE_INF, PV, InCheck);
    
    // Early return? (i.e. out of 'time' and no moves searched)
    if (PV[0]==MOVE_NULL)
      break;
    
    // Update bestmove
    BestMove=PV[0];
    
    // Output info
    SearchOutput(Pos, Depth, Score, PV);
    
    // Time to end?
    if (SearchIsTimeUp())
      break;
  }
  
  // Send best move
  char Str[8];
  PosMoveToStr(BestMove, Str);
  printf("bestmove %s\n", Str);
  
  // Free position
  PosFree(Pos);
}

score_t SearchNode(pos_t *Pos, unsigned int Depth, unsigned int Ply,
                   score_t Alpha, score_t Beta, move_t *PV, bool InCheck)
{
  // Sanity checks
  assert(-SCORE_INF<=Alpha && Alpha<Beta && Beta<=SCORE_INF);
  assert(InCheck==PosIsSTMInCheck(Pos));
  
  // Set null move in PV in case we return before updating the PV
  PV[0]=MOVE_NULL;
  
  // Out of depth (or ply limit reached)?
  if (Depth<1 || Ply>=SEARCH_MAXPLY)
    return SearchQNode(Pos, Ply, Alpha, Beta, InCheck);
  
  // Increase node counter (do this after qsearch to avoid double counting)
  ++SearchNodeCount;
  
  // Test for draws (and rare checkmates)
  if (Ply>=1 && PosIsDraw(Pos))
  {
    // In rare cases checkmate can be given on 100th half move
    if (InCheck && PosGetHalfMoveClock(Pos)==100 && !PosLegalMoveExist(Pos))
      return SCORE_MATEDIN(Ply);
    else
      return SCORE_DRAW;
  }
  
  // Search moves
  score_t BestScore=-SCORE_INF;
  move_t SubPV[SEARCH_MAXPLY];
  move_t Moves[MOVES_MAX], *Move;
  move_t *End=PosGenPseudoMoves(Pos, Moves);
  for(Move=Moves;Move<End;++Move)
  {
    // Search move
    if (!PosMakeMove(Pos, *Move))
      continue;
    bool GivesCheck=PosIsSTMInCheck(Pos);
    score_t Score=-SearchNode(Pos, Depth-1, Ply+1, -Beta, -Alpha, SubPV, GivesCheck);
    PosUndoMove(Pos);
    
    // Out of time? (previous search result is invalid)
    if (SearchIsTimeUp())
      return BestScore;
    
    // Better move?
    if (Score>BestScore)
    {
      // Update best score and PV
      BestScore=Score;
      PV[0]=*Move;
      move_t *PVPtr=PV+1, *SubPVPtr=SubPV;
      do
      {
        *PVPtr++=*SubPVPtr;
      }while(*SubPVPtr++!=MOVE_NULL);
      
      // Cutoff?
      if (Score>=Beta)
        goto cutoff;
      
      // Update alpha
      if (Score>Alpha)
        Alpha=Score;
    }
  }
  
  // Test for checkmate or stalemate
  if (BestScore==-SCORE_INF)
    return (InCheck ? SCORE_MATEDIN(Ply) : SCORE_DRAW);
  
  // We now know the best move
  cutoff:
  
  // Return
  return BestScore;
}

score_t SearchQNode(pos_t *Pos, unsigned int Ply, score_t Alpha, score_t Beta, bool InCheck)
{
  // Sanity checks
  assert(-SCORE_INF<=Alpha && Alpha<Beta && Beta<=SCORE_INF);
  assert(InCheck==PosIsSTMInCheck(Pos));
  
  // Increase node counter
  ++SearchNodeCount;
  
  // Draw?
  if (PosIsDraw(Pos))
    return SCORE_DRAW;
  
  // Standing pat
  if (!InCheck)
  {
    score_t Eval=Evaluate(Pos);
    if (Eval>=Beta)
      return Beta;
    if (Eval>Alpha)
      Alpha=Eval;
  }
  
  // Search moves
  move_t Moves[MOVES_MAX], *Move;
  move_t *End=(InCheck ? PosGenPseudoMoves(Pos, Moves) :
                         PosGenPseudoCaptures(Pos, Moves));
  bool NoLegalMove=true;
  for(Move=Moves;Move<End;++Move)
  {
    // Search move
    if (!PosMakeMove(Pos, *Move))
      continue;
    bool GivesCheck=PosIsSTMInCheck(Pos);
    score_t Score=-SearchQNode(Pos, Ply+1, -Beta, -Alpha, GivesCheck);
    PosUndoMove(Pos);
    
    // Out of time? (previous search result is invalid)
    if (SearchIsTimeUp())
      return Alpha;
    
    // Cutoff?
    if (Score>=Beta)
      return Beta;
    
    // New best move?
    if (Score>Alpha)
      Alpha=Score;
    
    // We have found a legal move
    NoLegalMove=false;
  }
  
  // Checkmate/stalemate?
  if (NoLegalMove)
  {
    if (InCheck)
      return SCORE_MATEDIN(Ply);
    else if (!PosLegalMoveExist(Pos))
      return SCORE_DRAW;
  }
  
  return Alpha;
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

void SearchOutput(pos_t *Pos, unsigned int Depth, score_t Score, const move_t *PV)
{
  ms_t Time=TimeGet()-SearchStartTime;
  char Str[16];
  SearchScoreToStr(Score, Str);
  printf("info depth %u score %s nodes %llu time %llu", Depth, Str,
         SearchNodeCount, (unsigned long long int)Time);
  if (Time>0)
    printf(" nps %llu", (SearchNodeCount*1000llu)/Time);
  printf(" pv");
  const move_t *Move;
  for(Move=PV;*Move!=MOVE_NULL;++Move)
  {
    PosMoveToStr(*Move, Str);
    if (!PosMakeMove(Pos, *Move))
      break;
    printf(" %s", Str);
  }
  for(--Move;Move>=PV;--Move)
    PosUndoMove(Pos);
  printf("\n");
}

void SearchScoreToStr(score_t Score, char Str[static 16])
{
  if (SCORE_ISMATE(Score))
    sprintf(Str, "mate %i", ((Score<0) ? -SCORE_MATEDIST(Score) : SCORE_MATEDIST(Score)));
  else
    sprintf(Str, "cp %i", Score);
}
