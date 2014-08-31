#include <assert.h>

#include "eval.h"
#include "history.h"
#include "main.h"
#include "score.h"
#include "search.h"
#include "thread.h"
#include "tt.h"
#include "tune.h"
#include "uci.h"

#define SearchMaxPly 128

const MoveScore MoveScoreMax=(((MoveScore)1)<<MoveScoreBit);

Thread *searchThread=NULL;

typedef struct
{
  // Ssearch functions should not modify these entries:
  Pos *pos;
  unsigned int depth, ply;
  Score alpha, beta;
  bool inCheck;
  // Search functions should ensure these are set correctly before returning:
  Move move;
  Score score;
  Bound bound;
}Node;

unsigned long long int searchNodeCount; // Number of nodes entered since begining of last search.
bool searchInfiniteFlag, searchStopFlag;
TimeMs searchStartTime, searchEndTime;
unsigned int searchDate; // Incremented after each SearchThink() call.

TUNECONST int searchNullReduction=1;
TUNECONST int searchIIDMin=2;
TUNECONST int searchIIDReduction=3;

bool searchPonder=true;

////////////////////////////////////////////////////////////////////////////////
// Private prototypes
////////////////////////////////////////////////////////////////////////////////

void searchIDLoop(void *posPtr);
Score searchNode(Node *node);
Score searchQNode(Node *node);
void searchNodeInternal(Node *node);
void searchQNodeInternal(Node *node);
bool searchIsTimeUp(void);
void searchOutput(Node *node);
bool searchIsZugzwang(const Node *node);
void searchNodePreCheck(Node *node);
void searchNodePostCheck(const Node *preNode, const Node *postNode);
bool searchInteriorRecog(Node *node);
bool searchInteriorRecogKNNvK(Node *node);
bool searchNodeIsPV(const Node *node);
bool searchNodeIsQ(const Node *node);
void searchInterfacePonder(void *dummy, bool ponder);
#ifdef TUNE
void searchInterfaceValue(void *ptr, int value);
#endif

////////////////////////////////////////////////////////////////////////////////
// Public functions
////////////////////////////////////////////////////////////////////////////////

void searchInit(void)
{
  // Create worker thread.
  searchThread=threadNew();
  if (searchThread==NULL)
    mainFatalError("Error: Could not start search worker thread.\n");
  
  // Set all structures to clean state.
  searchClear();
  
  // Init pondering option.
  uciOptionNewCheck("Ponder", &searchInterfacePonder, NULL, searchPonder);
  
  // Setup callbacks for tuning values.
# ifdef TUNE
  uciOptionNewSpin("NullReduction", &searchInterfaceValue, &searchNullReduction, 0, 8, searchNullReduction);
  uciOptionNewSpin("IIDMin", &searchInterfaceValue, &searchIIDMin, 0, 32, searchIIDMin);
  uciOptionNewSpin("IIDReduction", &searchInterfaceValue, &searchIIDReduction, 0, 32, searchIIDReduction);
# endif
}

void searchQuit(void)
{
  // If searching, signal to stop and wait until done.
  searchStop();
  
  // Free the worker thread.
  threadFree(searchThread);
}

void searchThink(const Pos *srcPos, TimeMs startTime, TimeMs searchTime, bool infinite, bool ponder)
{
  // Make sure we are not already searching.
  searchStop();
  
  // Prepare for search.
  Pos *pos=posCopy(srcPos);
  if (pos==NULL)
    return;
  searchNodeCount=0;
  searchInfiniteFlag=(infinite || ponder);
  searchStopFlag=false;
  searchStartTime=startTime;
  searchEndTime=startTime+searchTime;
  searchDate=(searchDate+1)%DateMax;
  
  // Set away worker
  threadRun(searchThread, &searchIDLoop, (void *)pos);
}

void searchStop(void)
{
  // Signal for search to stop.
  searchStopFlag=true;
  
  // Wait until actually finished.
  threadWaitReady(searchThread);
}

void searchClear(void)
{
  // Clear history tables.
  historyClear();
  
  // Clear transpostion table.
  ttClear();
  
  // Reset search date.
  searchDate=0;
}

void searchPonderHit(void)
{
  searchInfiniteFlag=false;
}

MoveScore searchScoreMove(const Pos *pos, Move move)
{
  // Sanity checks.
  assert(moveIsValid(move));
  
  // Extract move info.
  Sq fromSq=moveGetFromSq(move);
  Sq toSq=moveGetToSq(move);
  Piece fromPiece=posGetPieceOnSq(pos, fromSq);
  PieceType fromPieceType=pieceGetType(fromPiece);
  PieceType toPieceType=moveGetToPieceType(move);
  PieceType capturedPieceType=pieceGetType(posGetPieceOnSq(pos, toSq));
  
  // Sort by MVV-LVA
  if (capturedPieceType==PieceTypeNone && fromPieceType==PieceTypePawn &&
      sqFile(fromSq)!=sqFile(toSq))
    capturedPieceType=PieceTypePawn; // En-passent capture.
  MoveScore score=(capturedPieceType+toPieceType-fromPieceType)*8+(8-toPieceType);
  score<<=HistoryBit;
  
  // Further sort using history tables
  assert(historyGet(fromPiece, toSq)<HistoryMax);
  score+=historyGet(fromPiece, toSq);
  
  assert(score<MoveScoreMax);
  return score;
}

unsigned int searchGetDate(void)
{
  return searchDate;
}

unsigned int searchDateToAge(unsigned int date)
{
  return (date<=searchDate ? searchDate-date : DateMax+searchDate-date);
}

////////////////////////////////////////////////////////////////////////////////
// Private functions
////////////////////////////////////////////////////////////////////////////////

void searchIDLoop(void *posPtr)
{
  // Make node structure for root node.
  Node node;
  node.pos=(Pos *)posPtr;
  node.ply=0;
  node.alpha=-ScoreInf;
  node.beta=ScoreInf;
  node.inCheck=posIsSTMInCheck(node.pos);
  
  // Loop increasing search depth until we run out of 'time'.
  Move bestMove=MoveInvalid;
  for(node.depth=1;node.depth<SearchMaxPly;++node.depth)
  {
    // Search
    searchNode(&node);
    
    // No moves searched? (i.e. out of time/nodes/etc.).
    if (node.move==MoveInvalid)
      break;
    
    // Update bestmove
    bestMove=node.move;
    
    // Output info
    searchOutput(&node);
    
    // Time to end?
    if (searchIsTimeUp())
      break;
  }
  
  // Grab best move (and potentially ponder move)
  if (!moveIsValid(bestMove))
  {
    // Try TT.
    bestMove=ttReadMove(node.pos);
    
    // Otherwise simply use some legal move.
    if (bestMove==MoveInvalid)
      bestMove=posGenLegalMove(node.pos);
  }
  
  // If in pondering mode try to extract ponder move
  Move ponderMove=MoveInvalid;
  if (searchPonder && moveIsValid(bestMove))
  {
    posMakeMove(node.pos, bestMove);
    ponderMove=ttReadMove(node.pos);
    posUndoMove(node.pos);
  }
  
  // Send best move (and potentially ponder move) to GUI
  char str[8];
  posMoveToStr(node.pos, bestMove, str);
  if (moveIsValid(ponderMove))
  {
    char str2[8];
    posMoveToStr(node.pos, ponderMove, str2);
    uciWrite("bestmove %s ponder %s\n", str, str2);
  }
  else
    uciWrite("bestmove %s\n", str);
  
  // Free position
  posFree(node.pos);
  
  // Age history table.
  historyAge();
}

Score searchNode(Node *node)
{
# ifndef NDEBUG
  // Save node_t structure for post-checks.
  Node preNode=*node;
  
  // Pre-checks.
  searchNodePreCheck(node);
# endif
  
  // Call main search function
  searchNodeInternal(node);
  
  // Post-checks
# ifndef NDEBUG
  searchNodePostCheck(&preNode, node);
# endif
  
  return node->score;
}

Score searchQNode(Node *node)
{
# ifndef NDEBUG
  // Save node_t structure for post-checks.
  Node preNode=*node;
  
  // Pre-checks.
  searchNodePreCheck(node);
  assert(searchNodeIsQ(node));
# endif
  
  // Call main search function
  searchQNodeInternal(node);
  
  // Post-checks
# ifndef NDEBUG
  searchNodePostCheck(&preNode, node);
# endif
  
  return node->score;
}

void searchNodeInternal(Node *node)
{
  // Q node? (or ply limit reached).
  if (searchNodeIsQ(node) || node->ply>=SearchMaxPly)
  {
    searchQNode(node);
    return;
  }
  
  // Node begins.
  ++searchNodeCount;
  
  // Interior node recogniser (also handles draws).
  if (node->ply>0 && searchInteriorRecog(node))
    return;
  
  // Check transposition table.
  Move ttMove=MoveInvalid;
  unsigned int ttDepth;
  Score ttScore;
  Bound ttBound;
  if (ttRead(node->pos, node->ply, &ttMove, &ttDepth, &ttScore, &ttBound) && ttDepth>=node->depth)
  {
    // Check for cutoff.
    if (ttBound==BoundExact ||
        (ttBound==BoundLower && (ttScore>=node->beta)) ||
        (ttBound==BoundUpper && (ttScore<=node->alpha)))
    {
      node->bound=ttBound;
      node->move=ttMove;
      node->score=ttScore;
      return;
    }
  }
  
  // Null move pruning.
  Node child;
  child.pos=node->pos;
  child.ply=node->ply+1;
  if (!searchNodeIsPV(node) && searchNullReduction>0 && node->depth>1+searchNullReduction &&
      !scoreIsMate(node->beta) && !searchIsZugzwang(node) && evaluate(node->pos)>=node->beta)
  {
    assert(!node->inCheck);
    
    posMakeMove(node->pos, MoveNone);
    child.inCheck=false;
    child.depth=node->depth-1-searchNullReduction;
    child.alpha=-node->beta;
    child.beta=1-node->beta;
    Score score=-searchNode(&child);
    posUndoMove(node->pos);
    
    if (score>=node->beta)
    {
      node->bound=BoundLower;
      node->move=MoveNone;
      node->score=node->beta;
      return;
    }
  }
  
  // Internal iterative deepening.
  unsigned int depth=node->depth;
  if (searchIIDReduction>0 && node->depth>=searchIIDMin && searchNodeIsPV(node) && !moveIsValid(ttMove))
  {
    unsigned int k=(node->depth-searchIIDMin)/searchIIDReduction;
    depth=node->depth-k*searchIIDReduction;
    
    assert(depth>=searchIIDMin && depth<=node->depth);
    assert((node->depth-depth)%searchIIDReduction==0);
  }
  
  // Begin IID loop.
  Moves moves;
  movesInit(&moves, node->pos, true);
  movesRewind(&moves, ttMove);
  do
  {
    assert(depth>=0 && depth<=node->depth);
    
    // Prepare to search current depth.
    Score alpha=node->alpha;
    node->score=ScoreInvalid;
    node->bound=BoundUpper;
    node->move=MoveInvalid;
    child.alpha=-node->beta;
    child.beta=-alpha;
    Move move;
    while((move=movesNext(&moves))!=MoveInvalid)
    {
      // Make move (might leave us in check, if so skip).
      if (!posMakeMove(node->pos, move))
        continue;
      
      // PVS search
      child.inCheck=posIsSTMInCheck(node->pos);
      child.depth=depth-!child.inCheck; // Check extension.
      Score score;
      if (alpha>node->alpha)
      {
        // We have found a good move, try zero window search.
        assert(child.alpha==child.beta-1);
        score=-searchNode(&child);
        
        // Research?
        if (score>alpha && score<node->beta)
        {
          child.alpha=-node->beta;
          score=-searchNode(&child);
          child.alpha=child.beta-1;
        }
      }
      else
      {
        // Full window search.
        assert(child.alpha==-node->beta);
        score=-searchNode(&child);
      }
      
      // Undo move.
      posUndoMove(node->pos);
      
      // Out of time? (previous search result is invalid).
      if (searchIsTimeUp())
      {
        // Not yet started node->depth search?
        if (depth<node->depth)
        {
          node->bound=BoundNone;
          node->move=MoveInvalid;
          node->score=ScoreInvalid;
          return;
        }
        
        // Node type is tricky as we haven't yet searched all moves.
        node->bound&=~BoundUpper;
        if (node->bound==BoundNone)
        {
          node->move=MoveInvalid;
          node->score=ScoreInvalid;
          return;
        }
        
        // We may have useful info, update TT.
        ttWrite(node->pos, node->ply, node->depth, node->move, node->score, node->bound);
          
        return;
      }
      
      // Better move?
      if (score>node->score)
      {
        // Update best score and move.
        node->score=score;
        node->move=move;
        
        // Cutoff?
        if (score>=node->beta)
        {
          node->bound=BoundLower;
          goto cutoff;
        }
        
        // Update alpha.
        if (score>alpha)
        {
          alpha=score;
          child.alpha=-alpha-1;
          child.beta=-alpha;
          node->bound=BoundExact;
        }
      }
    }
    
    // Test for checkmate or stalemate.
    if (node->score==ScoreInvalid)
    {
      node->bound=BoundExact;
      node->move=MoveNone;
      if (node->inCheck)
      {
        assert(posIsMate(node->pos));
        node->score=scoreMatedIn(node->ply);
      }
      else
      {
        assert(posIsStalemate(node->pos));
        node->score=ScoreDraw;
      }
      return;
    }
    
    cutoff:
    movesRewind(&moves, node->move);
    
    // Continue onto next depth or done.
  }while((depth+=searchIIDReduction)<=node->depth);
  
  // We now know the best move.
  assert(node->move!=MoveInvalid);
  assert(scoreIsValid(node->score));
  assert(node->bound!=BoundNone);
  
  // Update history table.
  if (posMoveIsQuiet(node->pos, node->move))
  {
    Piece fromPiece=moveGetToPiece(node->move);
    assert(fromPiece==posGetPieceOnSq(node->pos, moveGetFromSq(node->move))); // Could only disagree if move is promotion, but these are classed as captures.
    Sq toSq=moveGetToSq(node->move);
    historyInc(fromPiece, toSq, node->depth);
  }
  
  // Update transposition table.
  ttWrite(node->pos, node->ply, node->depth, node->move, node->score, node->bound);
  
  return;
}

void searchQNodeInternal(Node *node)
{
  // Init.
  ++searchNodeCount;
  Score alpha=node->alpha;
  
  // Interior node recogniser (also handles draws).
  if (searchInteriorRecog(node))
    return;
  
  // Standing pat (when not in check).
  if (!node->inCheck)
  {
    Score eval=evaluate(node->pos);
    if (eval>=node->beta)
    {
      node->bound=BoundLower;
      node->move=MoveNone;
      node->score=node->beta;
      return;
    }
    else if (eval>alpha)
      alpha=eval;
  }
  
  // Search moves.
  Node child;
  node->bound=BoundUpper;
  node->move=MoveNone;
  node->score=ScoreInvalid;
  child.pos=node->pos;
  child.depth=node->depth;
  child.ply=node->ply+1;
  child.alpha=-node->beta;
  child.beta=-alpha;
  Moves moves;
  movesInit(&moves, node->pos, node->inCheck);
  movesRewind(&moves, MoveInvalid);
  Move move;
  bool noLegalMove=true;
  while((move=movesNext(&moves))!=MoveInvalid)
  {
    // Search move.
    if (!posMakeMove(node->pos, move))
      continue;
    child.inCheck=posIsSTMInCheck(node->pos);
    Score score=-searchQNode(&child);
    posUndoMove(node->pos);
    
    // Out of time? (previous search result is invalid).
    if (searchIsTimeUp())
    {
      // Node type is tricky as we haven't yet searched all moves.
      node->bound&=~BoundUpper;
      
      // HACK.
      if (node->move==MoveNone)
        node->move=MoveInvalid;
      
      return;
    }
    
    // We have a legal move.
    noLegalMove=false;
    
    // Better move?
    if (score>alpha)
    {
      // Update alpha.
      alpha=score;
      child.beta=-alpha;
      node->bound=BoundExact;
      node->move=move;
      
      // Cutoff?
      if (score>=node->beta)
      {
        node->bound=BoundLower;
        goto cutoff;
      }
    }
  }
  
  // Test for checkmate or stalemate.
  if (noLegalMove)
  {
    if (node->inCheck)
    {
      // We always try every move when in check.
      assert(posIsMate(node->pos));
      node->bound=BoundExact;
      node->score=scoreMatedIn(node->ply);
      return;
    }
    else if (!posLegalMoveExists(node->pos))
    {
      assert(posIsStalemate(node->pos));
      node->bound=BoundExact;
      node->score=ScoreDraw;
      return;
    }
    else
      // Else there are quiet moves available, assume one is at least as good as standing pat.
      node->bound=BoundLower;
  }
  
  // We now know the best move.
  cutoff:
  node->score=alpha;
  assert(node->bound!=BoundNone);
  assert(scoreIsValid(node->score));
  
  return;
}

bool searchIsTimeUp(void)
{
  if (searchStopFlag)
    return true;
  
  if (searchInfiniteFlag || (searchNodeCount&1023)!=0 || timeGet()<searchEndTime)
    return false;
  
  searchStopFlag=true;
  return true;
}

void searchOutput(Node *node)
{
  assert(scoreIsValid(node->score));
  assert(node->move!=MoveInvalid);
  assert(node->bound!=BoundNone);
  
  // Various bits of data
  TimeMs time=timeGet()-searchStartTime;
  char str[32];
  scoreToStr(node->score, node->bound, str);
  uciWrite("info depth %u score %s nodes %llu time %llu", node->depth, str, searchNodeCount, (unsigned long long int)time);
  if (time>0)
    uciWrite(" nps %llu", (searchNodeCount*1000llu)/time);
# ifndef NDEBUG
  uciWrite(" rawscore %i", (int)node->score);
# endif
  
  // PV (extracted from TT, mostly)
  uciWrite(" pv");
  unsigned int ply=0;
  Move move=node->move;
  while(move!=MoveInvalid)
  {
    // Compute move string before we make the move.
    posMoveToStr(node->pos, move, str);
    
    // Make move.
    if (!posMakeMove(node->pos, move))
      break;
    ++ply;
    
    // Print move string.
    uciWrite(" %s", str);
    
    // Draw? (don't want infinite PVs in case of repetition).
    if (posIsDraw(node->pos, ply))
      break;
    
    // Read next move from TT.
    move=ttReadMove(node->pos);
  }
  
  // Return position to initial state.
  for(;ply>0;--ply)
    posUndoMove(node->pos);
  
  uciWrite("\n");
}

bool searchIsZugzwang(const Node *node)
{
  return (node->inCheck || !posHasPieces(node->pos, posGetSTM(node->pos)));
}

void searchNodePreCheck(Node *node)
{
  // Check preset values are sensible.
  assert(node->depth>=0);
  assert(node->ply>=0);
  assert(-ScoreInf<=node->alpha && node->alpha<node->beta && node->beta<=ScoreInf);
  assert(node->inCheck==posIsSTMInCheck(node->pos));
  
  // Set other values to invalid to detect errors in post-checks.
  node->bound=BoundNone;
  node->move=MoveInvalid;
  node->score=ScoreInvalid;
}

void searchNodePostCheck(const Node *preNode, const Node *postNode)
{
  // Check position, depth, ply and incheck are unchanged.
  assert(postNode->pos==preNode->pos);
  assert(postNode->depth==preNode->depth);
  assert(postNode->ply==preNode->ply);
  assert(postNode->inCheck==preNode->inCheck);
  
  // Check type, move and score have been set and are sensible.
  if (scoreIsValid(postNode->score))
  {
    assert(postNode->bound==BoundLower || postNode->bound==BoundUpper || postNode->bound==BoundExact);
    assert(postNode->move!=MoveInvalid);
  }
  else
  {
    assert(searchIsTimeUp());
    assert(postNode->bound==BoundNone);
    assert(postNode->move==MoveInvalid);
  }
}

bool searchInteriorRecog(Node *node)
{
  // Sanity checks
  assert(node->alpha>=-ScoreInf && node->alpha<node->beta && node->beta<=ScoreInf);
  assert(node->ply>0);
  
  // Test for draws by rule (and rare checkmates)
  if (posIsDraw(node->pos, node->ply))
  {
    node->move=MoveNone;
    node->bound=BoundExact;
    
    // In rare cases checkmate can be given on 100th half move
    if (node->inCheck && posGetHalfMoveNumber(node->pos)==100 && !posLegalMoveExists(node->pos))
    {
      assert(posIsMate(node->pos));
      node->score=scoreMatedIn(node->ply);
    }
    else
      node->score=ScoreDraw;
    
    return true;
  }
  
  // Special recognizers
  switch(evalGetMatType(node->pos))
  {
    case EvalMatTypeKNNvK: if (searchInteriorRecogKNNvK(node)) return true; break;
    default:
      // No handler for this combination
    break;
  }
  
  return false;
}

bool searchInteriorRecogKNNvK(Node *node)
{
  // The defender simply has to avoid mate-in-1 (and can always do so trivially)
  Colour defender=(posGetPieceCount(node->pos, PieceWKnight)>0 ? ColourBlack : ColourWhite);
  if (posGetSTM(node->pos)==defender && (!node->inCheck || posLegalMoveExists(node->pos)))
  {
    assert(!posIsMate(node->pos));
    node->move=MoveNone;
    node->score=ScoreDraw;
    node->bound=BoundExact;
    return true;
  }
  
  return false;
}

bool searchNodeIsPV(const Node *node)
{
  return (node->beta-node->alpha>1);
}

bool searchNodeIsQ(const Node *node)
{
  return node->depth<1;
}

void searchInterfacePonder(void *dummy, bool ponder)
{
  searchPonder=ponder;
}

#ifdef TUNE
void searchInterfaceValue(void *ptr, int value)
{
  // Set value
  *((int *)ptr)=value;
  
  // Clear now-invalid tt and history etc.
  searchClear();
}
#endif
