#include <assert.h>
#include <stdlib.h>

#include "attacks.h"
#include "bitbase.h"
#include "eval.h"
#include "history.h"
#include "killers.h"
#include "main.h"
#include "score.h"
#include "search.h"
#include "see.h"
#include "thread.h"
#include "tt.h"
#include "tune.h"
#include "uci.h"
#include "util.h"

#define SearchMaxPly 128

const MoveScore MoveScoreMax=(((MoveScore)1)<<MoveScoreBit);

Thread *searchThread=NULL;

typedef struct {
	// Search functions should not modify these entries:
	Pos *pos;
	Depth depth, ply;
	Score alpha, beta;
	bool inCheck;
	// Search functions should ensure these are set correctly before returning:
	Score score;
	Bound bound;
} Node;

unsigned long long int searchNodeCount; // Number of nodes entered since beginning of last search.
unsigned long long int searchNodeNext; // Node count at which we should next check the time.
bool searchStopFlag;
Lock *searchActivity=NULL; // Once reached depth limit, search will wait for this before printing bestmove command.
Pos *searchPos=NULL;
TimeMs searchEndTime;
TimeMs searchNextRegularOutputTime;
bool searchShowCurrmove;
SearchLimit searchLimit;
unsigned int searchDate; // Incremented after each searchThink() call.
bool searchOutput;

TUNECONST int searchNullReduction=1;
TUNECONST int searchIIDMin=2;
TUNECONST int searchIIDReduction=3;
TUNECONST bool searchHistoryHeuristic=true;
TUNECONST bool searchKillersHeuristic=true;
TUNECONST int searchLmrReduction=1;
TUNECONST int searchLmrReductionDepthLimit=3;
TUNECONST int searchLmrReductionMoveLimit=4;

bool searchPonder=true;

////////////////////////////////////////////////////////////////////////////////
// Private prototypes.
////////////////////////////////////////////////////////////////////////////////

void searchThinkClear(void);
void searchStopInternal(void); // indicate search should stop asap - sets stop flag and updates activity lock. Can be called by worker thread (as opposed to external version searchStop).

void searchIDLoop(void *posPtr);

Score searchNode(Node *node, Move *bestMove);
Score searchQNode(Node *node);
void searchNodeInternal(Node *node, Move *bestMove);
void searchQNodeInternal(Node *node);

bool searchIsTimeUp(void);

void searchOutputRegular(void); // Regular infomation such as hashfull and nps.
void searchOutputDepthPre(Node *node); // Called at begining of searching a new depth.
void searchOutputDepthPost(Node *node, Move bestMove); // Called at end of searching a particular depth.

bool searchIsZugzwang(const Node *node);

void searchNodePreCheck(Node *node);
void searchNodePostCheck(const Node *preNode, const Node *postNode);

bool searchInteriorRecog(Node *node);
bool searchInteriorRecogBlocked(Node *node);
bool searchInteriorRecogKNNvK(Node *node);
bool searchInteriorRecogKPvK(Node *node);
bool searchInteriorRecogKBPvK(Node *node);

BB searchFill(PieceType type, BB init, BB occ, BB target);

bool searchNodeIsPV(const Node *node);
bool searchNodeIsQ(const Node *node);

void searchInterfacePonder(void *dummy, bool ponder);

#ifdef TUNE
void searchInterfaceSpinValue(void *ptr, long long value);
void searchInterfaceCheckValue(void *ptr, bool value);
#endif

////////////////////////////////////////////////////////////////////////////////
// Public functions.
////////////////////////////////////////////////////////////////////////////////

void searchInit(void) {
	// Create worker thread and lock.
	searchThread=threadNew();
	if (searchThread==NULL)
		mainFatalError("Error: Could not start search worker thread.\n");
	searchActivity=lockNew(0);
	if (searchActivity==NULL)
		mainFatalError("Error: Could not init lock for search.\n");

	// Create position
	searchPos=posNew(NULL);
	if (searchPos==NULL)
		mainFatalError("Error: Could not create position for search.\n");

	// Set all structures to clean state.
	searchClear();

	// Set searchThink fields for first search
	searchThinkClear();

	// Init pondering option.
	uciOptionNewCheck("Ponder", &searchInterfacePonder, NULL, searchPonder);

	// Setup callbacks for tuning values.
# ifdef TUNE
	uciOptionNewSpin("NullReduction", &searchInterfaceSpinValue, &searchNullReduction, 0, 8, searchNullReduction);
	uciOptionNewSpin("IIDMin", &searchInterfaceSpinValue, &searchIIDMin, 0, 32, searchIIDMin);
	uciOptionNewSpin("IIDReduction", &searchInterfaceSpinValue, &searchIIDReduction, 0, 32, searchIIDReduction);
	uciOptionNewCheck("HistoryHeuristic", &searchInterfaceCheckValue, &searchHistoryHeuristic, searchHistoryHeuristic);
	uciOptionNewCheck("KillersHeuristic", &searchInterfaceCheckValue, &searchKillersHeuristic, searchKillersHeuristic);
	uciOptionNewSpin("LmrReduction", &searchInterfaceSpinValue, &searchLmrReduction, 0, 32, searchLmrReduction);
	uciOptionNewSpin("LmrReductionDepthLimit", &searchInterfaceSpinValue, &searchLmrReductionDepthLimit, 0, 32, searchLmrReductionDepthLimit);
	uciOptionNewSpin("LmrReductionMoveLimit", &searchInterfaceSpinValue, &searchLmrReductionMoveLimit, 0, 256, searchLmrReductionMoveLimit);
# endif
}

void searchQuit(void) {
	// If searching, signal to stop and wait until done.
	searchStopAndWait();

	// Free position
	posFree(searchPos);

	// Free the worker thread and lock;
	threadFree(searchThread);
	lockFree(searchActivity);
}

void searchThink(const Pos *srcPos, const SearchLimit *limit, bool output) {
	// Make sure we are not already searching (and if we are, set stop flag and wait until finished).
	searchStopAndWait();

	// Sanity checks
	assert(searchNodeNext==1);
	assert(searchShowCurrmove==false);
	assert(searchEndTime==TimeMsInvalid);
	assert(searchNextRegularOutputTime==0);

	// Prepare for search.
	if (!posCopy(searchPos, srcPos))
		return;

	searchStopFlag=false;
	searchNodeCount=0;
	searchLimit=*limit;
	searchLimit.searchMovesNext=searchLimit.searchMoves+(limit->searchMovesNext-limit->searchMoves);
	searchOutput=output;

	while (lockTryWait(searchActivity)) ; // Reset to 0.

	// Decide how to use our time.
	if (searchLimit.nodes==0)
		searchLimit.nodes=~0; // To avoid an extra searchLimit.nodes!=0 check in searchIsTimeUp().

	TimeMs searchTime=TimeMsInvalid;
	if (searchLimit.totalTime!=TimeMsInvalid || searchLimit.incTime!=TimeMsInvalid) {
		if (searchLimit.totalTime==TimeMsInvalid)
			searchLimit.totalTime=0;
		if (searchLimit.incTime==TimeMsInvalid)
			searchLimit.incTime=0;
		if (searchLimit.movesToGo==0)
			searchLimit.movesToGo=15;
		TimeMs maxTime=searchLimit.totalTime-20;
		searchTime=utilMin(searchTime, searchLimit.totalTime/searchLimit.movesToGo+searchLimit.incTime);
		searchTime=utilMin(searchTime, maxTime);
	}
	if (searchLimit.moveTime!=TimeMsInvalid)
		searchTime=utilMin(searchTime, searchLimit.moveTime);
	if (searchTime!=TimeMsInvalid)
		searchEndTime=searchLimit.startTime+searchTime;

	// Set away worker
	threadRun(searchThread, &searchIDLoop, NULL);
}

void searchStopAndWait(void) {
	// Signal for search to stop.
	searchLimit.infinite=false;
	searchStopInternal();

	// Wait until actually finished.
	searchWait();
}

void searchWait(void) {
	threadWaitReady(searchThread);
}

unsigned long long int searchBenchmark(const Pos *pos, Depth depth) {
	// Set search limit to given depth.
	SearchLimit limit;
	searchLimitInit(&limit, 0);
	searchLimitSetDepth(&limit, depth);

	// Search and wait to complete.
	searchThink(pos, &limit, false);
	searchWait();

	// Return node count.
	return searchNodeCount;
}

void searchClear(void) {
	// Clear history tables.
	historyClear();

	// Clear transposition table.
	ttClear();

	// Clear killer moves.
	killersClear();

	// Reset search date.
	searchDate=0;
}

void searchPonderHit(void) {
	searchLimit.infinite=false;
	lockPost(searchActivity);
}

MoveScore searchScoreMove(const Pos *pos, Move move) {
	// Sanity checks.
	assert(moveIsValid(move));

	// Extract move info.
	Sq fromSq=moveGetFromSq(move);
	Sq toSqTrue=posMoveGetToSqTrue(pos, move);
	Piece fromPiece=posGetPieceOnSq(pos, fromSq);
	PieceType fromPieceType=pieceGetType(fromPiece);
	PieceType toPieceType=moveGetToPieceType(move);
	PieceType capturedPieceType=pieceGetType(posGetPieceOnSq(pos, toSqTrue));

	// Sort by MVV-LVA
	if (capturedPieceType==PieceTypeNone && fromPieceType==PieceTypePawn &&
			sqFile(fromSq)!=sqFile(toSqTrue))
		capturedPieceType=PieceTypePawn; // En-passent capture.
	MoveScore score=(capturedPieceType+toPieceType-fromPieceType)*8+(8-toPieceType);
	score<<=HistoryBit;

	// Further sort using history tables
	if (searchHistoryHeuristic)
		score+=historyGet(fromPiece, toSqTrue);

	assert(score<MoveScoreMax);
	return score;
}

unsigned int searchGetDate(void) {
	return searchDate;
}

unsigned int searchDateToAge(unsigned int date) {
	return (date<=searchDate ? searchDate-date : DateMax+searchDate-date);
}

void searchLimitInit(SearchLimit *limit, TimeMs startTime) {
	assert(startTime!=TimeMsInvalid);
	limit->infinite=false;
	limit->startTime=startTime;
	limit->totalTime=TimeMsInvalid;
	limit->incTime=TimeMsInvalid;
	limit->moveTime=TimeMsInvalid;
	limit->movesToGo=0;
	limit->depth=DepthMax-1;
	limit->nodes=0;
	limit->searchMovesNext=limit->searchMoves;
}

void searchLimitSetInfinite(SearchLimit *limit, bool infinite) {
	limit->infinite=infinite;
}

void searchLimitSetTotalTime(SearchLimit *limit, TimeMs totalTime) {
	limit->totalTime=totalTime;
}

void searchLimitSetIncTime(SearchLimit *limit, TimeMs incTime) {
	limit->incTime=incTime;
}

void searchLimitSetMoveTime(SearchLimit *limit, TimeMs moveTime) {
	limit->moveTime=moveTime;
}

void searchLimitSetDepth(SearchLimit *limit, Depth depth) {
	limit->depth=utilMin(depth, DepthMax-1);
}

void searchLimitSetNodes(SearchLimit *limit, unsigned long long int nodes) {
	limit->nodes=nodes;
}

void searchLimitSetMovesToGo(SearchLimit *limit, unsigned int movesToGo) {
	limit->movesToGo=movesToGo;
}

void searchLimitAddMove(SearchLimit *limit, const Pos *pos, Move move) {
	if (posCanMakeMove(pos, move))
		*limit->searchMovesNext++=move;
}

////////////////////////////////////////////////////////////////////////////////
// Private functions.
////////////////////////////////////////////////////////////////////////////////

void searchThinkClear(void) {
	searchNodeNext=1;
	searchShowCurrmove=false;
	searchEndTime=TimeMsInvalid;
	searchNextRegularOutputTime=0;
	searchDate=(searchDate+1)%DateMax;
}

void searchStopInternal(void) {
	// Set flag to indicate to return asap
	searchStopFlag=true;

	// Update activity lock
	lockPost(searchActivity);
}

void searchIDLoop(void *userData) {
	assert(userData==NULL);

	// Make node structure for root node.
	Node node;
	node.pos=searchPos;
	node.ply=0;
	node.alpha=-ScoreInf;
	node.beta=ScoreInf;
	node.inCheck=posIsSTMInCheck(node.pos);

	// Loop, increasing search depth until we run out of 'time'.
	Move bestMove=MoveInvalid;
	for(node.depth=1;node.depth<=searchLimit.depth;++node.depth) {
		// After 1s start showing 'currmove' info.
		if (searchOutput && timeGet()>=searchLimit.startTime+1000)
			searchShowCurrmove=true;

		// Output pre info.
		searchOutputDepthPre(&node);

		// Search
		Move tempBestMove;
		searchNode(&node, &tempBestMove);

		// No info found? (out of time/nodes/etc.).
		if (node.bound==BoundNone)
			break;

		// Update bestMove
		bestMove=tempBestMove;

		// Output post info.
		searchOutputDepthPost(&node, bestMove);

		// Time to end?
		if (searchIsTimeUp())
			break;
	}

	// Ensure we have a legal bestMove
	if (!moveIsValid(bestMove) || !posCanMakeMove(searchPos, bestMove)) {
		if (searchLimit.searchMovesNext>searchLimit.searchMoves)
			bestMove=searchLimit.searchMoves[0];
		else
			bestMove=posGenLegalMove(searchPos, MoveTypeAny);
	}

	// If in pondering mode try to extract ponder move.
	Move ponderMove=MoveInvalid;
	if (searchPonder && moveIsValid(bestMove)) {
		assert(posCanMakeMove(searchPos, bestMove));
		posMakeMove(searchPos, bestMove);
		ponderMove=ttReadMove(searchPos, 1);
		if (!moveIsValid(ponderMove) || !posCanMakeMove(searchPos, ponderMove))
			ponderMove=posGenLegalMove(searchPos, MoveTypeAny);
		posUndoMove(searchPos);
	}

	// This is to handle infinite mode - wait until told to stop.
	while(searchLimit.infinite)
		lockWait(searchActivity);

	// Send best move (and potentially ponder move) to GUI.
	if (searchOutput) {
		char str[8];
		posMoveToStr(searchPos, bestMove, str);
		if (moveIsValid(ponderMove)) {
			posMakeMove(searchPos, bestMove);
			uciWrite("bestmove %s ponder %s\n", str, POSMOVETOSTR(searchPos, ponderMove));
			posUndoMove(searchPos);
		} else
			uciWrite("bestmove %s\n", str);
	}

	// Age history table.
	if (searchHistoryHeuristic)
		historyAge();

	// Clear killers (do here to avoid having to spend time at start of next search).
	if (searchKillersHeuristic)
		killersClear();

	// Reset searchThink fields for next search
	searchThinkClear();
}

Score searchNode(Node *node, Move *bestMove) {
# ifndef NDEBUG
	// Save node_t structure for post-checks.
	Node preNode=*node;

	// Pre-checks.
	searchNodePreCheck(node);
# endif

	// Call main search function.
	searchNodeInternal(node, bestMove);

# ifndef NDEBUG
	// Post-checks.
	searchNodePostCheck(&preNode, node);
# endif

	return node->score;
}

Score searchQNode(Node *node) {
# ifndef NDEBUG
	// Save node_t structure for post-checks.
	Node preNode=*node;

	// Pre-checks.
	searchNodePreCheck(node);
	assert(searchNodeIsQ(node));
# endif

	// Call main search function.
	searchQNodeInternal(node);

# ifndef NDEBUG
	// Post-checks.
	searchNodePostCheck(&preNode, node);
# endif

	return node->score;
}

void searchNodeInternal(Node *node, Move *bestMove) {
	Move tempChildMove;
	*bestMove=MoveInvalid;

	// Q node?
	if (searchNodeIsQ(node)) {
		searchQNode(node);
		return;
	}

	// Ply limit reached?
	if (node->ply>=DepthMax) {
		node->bound=BoundExact;
		node->score=evaluate(node->pos);
		return;
	}

	// Node begins.
	++searchNodeCount;

	// Mate distance pruning.
	if (node->ply>0) {
		Score matedIn=scoreMatedIn(node->ply);
		if (matedIn>=node->beta) {
			node->bound=BoundLower;
			node->score=matedIn;
			return;
		}

		Score mateIn=scoreMateIn(node->ply);
		if (mateIn<=node->alpha) {
			node->bound=BoundUpper;
			node->score=mateIn;
			return;
		}
	}

	// Interior node recogniser (also handles draws).
	if (node->ply>0 && searchInteriorRecog(node))
		return;

	// Check transposition table.
	Move ttMove=MoveInvalid;
	unsigned int ttDepth;
	Score ttScore;
	Bound ttBound;
	if (ttRead(node->pos, node->ply, &ttMove, &ttDepth, &ttScore, &ttBound)) {
		// Sanity checks.
		assert(moveIsValid(ttMove));
		assert(scoreIsValid(ttScore));
		assert(ttBound!=BoundNone);

		// Check for cutoff.
		if (ttDepth>=node->depth &&
		    (ttBound==BoundExact ||
		    (ttBound==BoundLower && (ttScore>=node->beta)) ||
		    (ttBound==BoundUpper && (ttScore<=node->alpha)))) {
			node->bound=ttBound;
			node->score=ttScore;
			*bestMove=ttMove;
			return;
		}
	}

	// Null move pruning.
	Node child;
	child.pos=node->pos;
	child.ply=node->ply+1;
	if (!searchNodeIsPV(node) && searchNullReduction>0 && node->depth>1+searchNullReduction &&
	    !scoreIsMate(node->beta) && !searchIsZugzwang(node) && evaluate(node->pos)>=node->beta) {
		assert(!node->inCheck);

		posMakeMove(node->pos, MoveNone);
		child.inCheck=false;
		child.depth=node->depth-1-searchNullReduction;
		child.alpha=-node->beta;
		child.beta=1-node->beta;
		Score score=-searchNode(&child, &tempChildMove);
		posUndoMove(node->pos);

		if (score>=node->beta) {
			node->bound=BoundLower;
			node->score=node->beta;
			return;
		}
	}

	// Internal iterative deepening.
	if (searchIIDReduction>0 && node->depth>=searchIIDMin && node->depth>searchIIDReduction && searchNodeIsPV(node) && !moveIsValid(ttMove)) {
		// No hash move available - search current node but with a reduced depth to obtain a good guess at the best move.
		Node child=*node;
		child.depth-=searchIIDReduction;
		assert(ttMove==MoveInvalid);
		searchNode(&child, &ttMove);
	}

	// Move loop.
	Moves moves;
	movesInit(&moves, node->pos, node->ply, MoveTypeAny);
	movesRewind(&moves, ttMove);
	Score alpha=node->alpha;
	node->score=ScoreInvalid;
	node->bound=BoundNone;
	child.alpha=-node->beta;
	child.beta=-alpha;
	Move move;
	unsigned moveNumber=0;
	while((move=movesNext(&moves))!=MoveInvalid) {
		// If we are the root ensure this move is one that was specified (if any restriction given)
		if (node->ply==0 && searchLimit.searchMovesNext>searchLimit.searchMoves) {
			Move *movePtr;
			for(movePtr=searchLimit.searchMoves; movePtr!=searchLimit.searchMovesNext; ++movePtr)
				if (move==*movePtr)
					break;
			if (movePtr==searchLimit.searchMovesNext)
				continue;
		}

		// Find move string for UCI output.
		char moveStr[8]; // Only used if root node.
		if (searchShowCurrmove && node->ply==0)
			posMoveToStr(node->pos, move, moveStr); // Must do this before making the move.

		// Make move (might leave us in check, if so skip).
		MoveType moveType=posMoveGetType(node->pos, move);
		if (!posMakeMove(node->pos, move))
			continue;
		++moveNumber;

		// 'currmove' UCI output.
		if (searchShowCurrmove && node->ply==0)
			uciWrite("info depth %u currmove %s currmovenumber %u\n", node->depth, moveStr, moveNumber);

		// Calculate child values.
		child.inCheck=posIsSTMInCheck(node->pos);

		// Calculate search depth.
		int extension=0, reduction=0;
		extension+=child.inCheck; // Check extension;
		if (extension==0 && !node->inCheck && !child.inCheck && !searchNodeIsPV(node) && node->depth>=searchLmrReductionDepthLimit && moveType==MoveTypeQuiet && moveNumber>searchLmrReductionMoveLimit)
			reduction+=searchLmrReduction; // Late-move-reductions.
		child.depth=node->depth-1+extension-reduction;

		// PVS search
		Score score;
		if (alpha>node->alpha) {
			// We have found a good move, try zero window search.
			assert(child.alpha==child.beta-1);
			score=-searchNode(&child, &tempChildMove);

			// Research?
			if (score>alpha && score<node->beta) {
				child.alpha=-node->beta;
				child.depth=node->depth-1+extension;
				score=-searchNode(&child, &tempChildMove);
				child.alpha=child.beta-1;
			}
		} else {
			// Full window search.
			assert(child.alpha==-node->beta);
			score=-searchNode(&child, &tempChildMove);
		}

		// Undo move.
		posUndoMove(node->pos);

		// Out of time? (previous search result is invalid).
		if (searchIsTimeUp()) {
			// No moves searched?
			if (node->bound==BoundNone) {
				node->bound=BoundNone;
				node->score=ScoreInvalid;
				return;
			}
			assert(scoreIsValid(node->score));
			assert(moveIsValid(*bestMove));

			// We may have useful info, update TT.
			ttWrite(node->pos, node->ply, node->depth, *bestMove, node->score, node->bound);

			return;
		}

		// Better move?
		if (score>node->score) {
			// Update best score and move.
			node->score=score;
			*bestMove=move;

			// Alpha improvement?
			if (score>alpha) {
				// We can trust the score as a lowerbound.
				node->bound|=BoundLower;

				// Cutoff?
				if (score>=node->beta) {
					// Update killers.
					if (searchKillersHeuristic && posMoveGetType(node->pos, *bestMove)==MoveTypeQuiet)
						killersCutoff(node->ply, *bestMove);

					goto cutoff;
				}

				// Update values.
				alpha=score;
				child.alpha=-alpha-1;
				child.beta=-alpha;
			}
		}
	}

	// Test for checkmate or stalemate.
	if (node->score==ScoreInvalid) {
		node->bound=BoundExact;
		if (node->inCheck) {
			assert(posIsMate(node->pos));
			node->score=scoreMatedIn(node->ply);
		} else {
			assert(posIsStalemate(node->pos));
			node->score=ScoreDraw;
		}
		return;
	}

	node->bound|=BoundUpper; // We have searched all moves.

	// Single legal move in root?
	// (continue searching anyway if in infinite/pondering mode)
	if (node->ply==0 && moveNumber==1 && !searchLimit.infinite)
		searchStopInternal();

	cutoff:

	// We now know the best move.
	assert(moveIsValid(*bestMove));
	assert(scoreIsValid(node->score));
	assert(node->bound!=BoundNone);

	// Update history table.
	if (searchHistoryHeuristic && posMoveGetType(node->pos, *bestMove)==MoveTypeQuiet) {
		Piece fromPiece=moveGetToPiece(*bestMove);
		assert(fromPiece==posGetPieceOnSq(node->pos, moveGetFromSq(*bestMove))); // Could only disagree if move is promotion, but these are classed as captures.
		Sq toSq=moveGetToSqRaw(*bestMove);
		historyInc(fromPiece, toSq, node->depth);
	}

	// Update transposition table.
	ttWrite(node->pos, node->ply, node->depth, *bestMove, node->score, node->bound);

	return;
}

void searchQNodeInternal(Node *node) {
	// Ply limit reached?
	if (node->ply>=DepthMax) {
		node->bound=BoundExact;
		node->score=evaluate(node->pos);
		return;
	}

	// Init.
	++searchNodeCount;
	Score alpha=node->alpha;

	// Interior node recogniser (also handles draws).
	if (searchInteriorRecog(node))
		return;

	// Standing pat (when not in check).
	if (!node->inCheck) {
		Score eval=evaluate(node->pos);
		if (eval>=node->beta) {
			node->bound=BoundLower;
			node->score=node->beta;
			return;
		} else if (eval>alpha)
			alpha=eval;
	}

	// Search moves.
	Node child;
	node->bound=BoundLower;
	node->score=alpha;
	child.pos=node->pos;
	child.depth=node->depth;
	child.ply=node->ply+1;
	child.alpha=-node->beta;
	child.beta=-alpha;
	Moves moves;
	movesInit(&moves, node->pos, 0, (node->inCheck ? MoveTypeAny : MoveTypeCapture));
	Move move;
	bool noLegalMove=true;
	while((move=movesNext(&moves))!=MoveInvalid) {
		// If SEE is negative, skip move.
		if (!node->inCheck && !posMoveIsPromotion(node->pos, move) && seeSign(node->pos, moveGetFromSq(move), posMoveGetToSqTrue(node->pos, move))<0)
			continue;

		// Search move.
		if (!posMakeMove(node->pos, move))
			continue;
		child.inCheck=posIsSTMInCheck(node->pos);
		Score score=-searchQNode(&child);
		posUndoMove(node->pos);

		// Out of time? (previous search result is invalid).
		if (searchIsTimeUp())
			return;

		// We have a legal move.
		noLegalMove=false;

		// Better move?
		if (score>alpha) {
			// Update alpha.
			alpha=score;
			child.beta=-alpha;

			// Cutoff?
			if (score>=node->beta)
				goto cutoff;
		}
	}

	if (node->inCheck) {
		// If in check we search all moves so score is exact.
		node->bound|=BoundUpper;

		// If no legal moves, checkmate.
		if (noLegalMove) {
			assert(posIsMate(node->pos));
			assert(node->bound==BoundExact);
			node->score=scoreMatedIn(node->ply);
			return;
		}
	}

	// We now know the best move.
	cutoff:
	node->score=alpha;
	assert(node->bound!=BoundNone);
	assert(scoreIsValid(node->score));

	return;
}

bool searchIsTimeUp(void) {
	// If stop flag is set we are expected to quit as soon as possible.
	if (searchStopFlag)
		return true;

	// Check node count.
	if (searchNodeCount>=searchLimit.nodes)
		goto timeup;

	// Time to check the real clock?
	if (searchNodeCount>=searchNodeNext) {
		// Is time up? (want to return asap).
		TimeMs currTime=timeGet();
		if (currTime>=searchEndTime)
			goto timeup;

		// Print regular debugging information every so often.
		if (currTime>=searchNextRegularOutputTime) {
			searchOutputRegular();
			searchNextRegularOutputTime=currTime+1000;
		}

		// Update searchNodeNext to check again in the future.
		if (currTime>searchLimit.startTime) {
			// Aim to check again 50% through our remaining time for this move.
			// So if, for example, we allocated 16s for the current search, and have
			// already used 12s, we aim to check again at 14s (12+(16-12)/2).
			// We use the node counter and previous nps as a rough timer to avoid
			// checking the real time too often.
			TimeMs timeDelay=64*utilMin(searchEndTime-currTime, 2*1000); // We /128 later to avoid losing accuracy. Also limit to 1s.
			unsigned long long int nodeDelay=(searchNodeCount*timeDelay)/(128*(currTime-searchLimit.startTime));
			searchNodeNext=searchNodeCount+nodeDelay;
		} else
			// No time passed yet since we started searching, check again later.
			searchNodeNext*=2;
	}

	return false;

	timeup:
	searchStopInternal();

	return true;
}

void searchOutputRegular(void) {
	if (!searchOutput)
		return;

	TimeMs time=timeGet()-searchLimit.startTime;
	uciWrite("info nodes %llu time %llu", (unsigned long long int)searchNodeCount, (unsigned long long int)time);
	if (time>0)
		uciWrite(" nps %llu", (searchNodeCount*1000llu)/time);
	uciWrite(" hashfull %u\n", ttFull());
}

void searchOutputDepthPre(Node *node) {
	if (!searchOutput)
		return;

	uciWrite("info depth %u\n", (unsigned int)node->depth);
}

void searchOutputDepthPost(Node *node, Move bestMove) {
	assert(scoreIsValid(node->score));
	assert(node->bound!=BoundNone);

	if (!searchOutput)
		return;

	// Various bits of data
	TimeMs time=timeGet()-searchLimit.startTime;
	char str[32];
	scoreToStr(node->score, node->bound, str);
	uciWrite("info depth %u score %s nodes %llu time %llu", (unsigned int)node->depth, str, searchNodeCount, (unsigned long long int)time);
	if (time>0)
		uciWrite(" nps %llu", (searchNodeCount*1000llu)/time);

	// PV (extracted from TT)
	uciWrite(" pv");
	unsigned int ply=0;
	while(1) {
		// Read move from TT.
		Move move=ttReadMove(node->pos, ply);
		if (move==MoveInvalid)
			break;

		// Compute move string before we make the move.
		posMoveToStr(node->pos, move, str);

		// Make move.
		if (!posMakeMove(node->pos, move))
			break;
		++ply;

		// Print move string.
		uciWrite(" %s", str);

		// Draw? (don't want infinite PVs in case of repetition).
		if (posIsDraw(node->pos))
			break;
	}

	// Return position to initial state.
	for(;ply>0;--ply)
		posUndoMove(node->pos);

	uciWrite("\n");
}

bool searchIsZugzwang(const Node *node) {
	Colour stm=posGetSTM(node->pos);

	// If stm is in check or has no non-pawn pieces, could be zugzwang.
	if (node->inCheck || !posHasPieces(node->pos, stm))
		return true;

	// Test stm has sufficient mobility.
	const unsigned mobilityLimit=4;
	unsigned mobility=0;
	BB occ=posGetBBAll(node->pos);
	BB notFriendly=~posGetBBColour(node->pos, stm);
	BB opp=posGetBBColour(node->pos, colourSwap(stm));

	// Forward pawn moves.
	BB pawns=posGetBBPiece(node->pos, pieceMake(PieceTypePawn, stm));
	BB pawnsForward=bbForwardOne(pawns, stm);
	BB pawnMoves=pawnsForward & ~occ;
	mobility+=bbPopCount(pawnMoves);
	if (mobility>mobilityLimit)
		return false;

	// Pawn captures.
	BB pawnAtks=bbWingify(pawnsForward) & opp;
	mobility+=bbPopCount(pawnAtks);
	if (mobility>mobilityLimit)
		return false;

	// Pieces.
	Piece piece=pieceMake(PieceTypeKnight, stm);
	Piece endPiece=pieceMake(PieceTypeKing, stm);
	for(; piece<endPiece; ++piece) {
		BB pieceSet=posGetBBPiece(node->pos, piece);
		while(pieceSet) {
			Sq sq=bbScanReset(&pieceSet);
			BB attacks=attacksPiece(piece, sq, occ) & notFriendly;
			mobility+=bbPopCount(attacks);
			if (mobility>mobilityLimit)
				return false;
		}
	}

	return true;
}

void searchNodePreCheck(Node *node) {
	// Check preset values are sensible.
	assert(depthIsValid(node->depth));
	assert(depthIsValid(node->ply));
	assert(-ScoreInf<=node->alpha && node->alpha<node->beta && node->beta<=ScoreInf);
	assert(node->inCheck==posIsSTMInCheck(node->pos));

	// Set other values to invalid to detect errors in post-checks.
	node->bound=BoundNone;
	node->score=ScoreInvalid;
}

void searchNodePostCheck(const Node *preNode, const Node *postNode) {
	// Check position, depth, ply and incheck are unchanged.
	assert(postNode->pos==preNode->pos);
	assert(postNode->depth==preNode->depth);
	assert(postNode->ply==preNode->ply);
	assert(postNode->inCheck==preNode->inCheck);

	// Check type, move and score have been set and are sensible.
	if (scoreIsValid(postNode->score))
		assert(postNode->bound==BoundLower || postNode->bound==BoundUpper || postNode->bound==BoundExact);
	else
		assert(postNode->bound==BoundNone);
}

bool searchInteriorRecog(Node *node) {
	// Sanity checks.
	assert(node->alpha>=-ScoreInf && node->alpha<node->beta && node->beta<=ScoreInf);
	assert(depthIsValid(node->ply));

	// Test for draws by rule (and rare checkmates).
	if (posIsDraw(node->pos)) {
		node->bound=BoundExact;

		// In rare cases checkmate can be given on 100th half move.
		if (node->inCheck && posGetHalfMoveNumber(node->pos)==100 && !posLegalMoveExists(node->pos, MoveTypeAny)) {
			assert(posIsMate(node->pos));
			node->score=scoreMatedIn(node->ply);
		} else
			node->score=ScoreDraw;

		return true;
	}

	// Blocked positions.
	if (node->beta<=ScoreDraw && searchInteriorRecogBlocked(node)) {
		node->score=ScoreDraw;
		node->bound=BoundLower;
		return true;
	}

	// Special material combination recognizers.
	switch(evalGetMatType(node->pos)) {
		case EvalMatTypeKNNvK: if (searchInteriorRecogKNNvK(node)) return true; break;
		case EvalMatTypeKPvK: if (searchInteriorRecogKPvK(node)) return true; break;
		case EvalMatTypeKBPvK: if (searchInteriorRecogKBPvK(node)) return true; break;
		default:
			// No handler for this combination.
		break;
	}

	return false;
}

bool searchInteriorRecogBlocked(Node *node) {
	// Attempt to detect if the side to move (the defender) can maintain the
	// current pawn structure and simply shuffle a piece, hence giving at least a
	// draw.
	const Pos *pos=node->pos;
	Colour def=posGetSTM(pos);
	Colour atk=colourSwap(def);
	BB occ=posGetBBAll(pos);
	BB atkPawns=posGetBBPiece(pos, pieceMake(PieceTypePawn, atk));
	BB atkPawnStops=bbForwardOne(atkPawns, atk);
	BB atkPawnAtks=bbWingify(atkPawnStops);
	BB defOcc=posGetBBColour(pos, def);
	BB defPawns=posGetBBPiece(pos, pieceMake(PieceTypePawn, def));
	BB defKing=posGetBBPiece(pos, pieceMake(PieceTypeKing, def));

	// Can any of attacker's pawns potentially move, either by:
	// * Moving forward if they are not blocked by a same-coloured pawn or one of the defender's pieces.
	// * Capturing if the defender has any pieces on target squares.
	if (((atkPawnStops&~(defOcc|atkPawns)) | (atkPawnAtks&defOcc))!=BBNone)
		return false;

	// Now test if any of the attacker's pieces can reach any of the defender's
	// blockers (or king).
	// Note: We overestimate the power of the attacker. It is assumed that he
	// can always manoeuvre his own non-pawn pieces out of the way when making
	// moves.
	// The following position is an example of where this logic fails:
	//   7k/8/6p1/5pPp/2p1pP1P/1pPpP2R/1P1P4/2B1K3 b - - 0 1
	// While it is obvious the bishop on c1 is as blocked as the pawns are, the
	// code below will still think that the rook can make it to a4 and attack the
	// defender's pawns.
	BB atkInfluence=atkPawnAtks; // Set of squares attacker can reach in any number of steps.
	BB blockers=(atkPawnStops & defOcc); // A blocker is a defender piece that is necessary and cannot move - it is holding back the attacker's pawns.
	BB target=(blockers|defKing); // If the attacker can reach one of these (i.e. attack it), the fortress may be breached.
	BB fillOcc=(blockers | atkPawns); // We don't mind if some of defender's pieces are captured - just the blockers and king are important.
	PieceType type;
	for(type=PieceTypeKnight;type<=PieceTypeQueen;++type) {
		// Find 'attack fill' for all pieces of the current type.
		// This is a bitboard with 1s for all squares the pieces can move to with
		// any number of steps, but without moving from an 'occupied' square.
		BB attackers=posGetBBPiece(pos, pieceMake(type, atk));
		BB fill=searchFill(type, attackers, fillOcc, target);
		if ((fill & target)!=BBNone)
			return false;
		atkInfluence|=fill;
	}

	// Calculate squares defender attacks (and hence where attacker king can not
	// walk).
	// Note: We underestimate the power of the defender. We do not consider
	// attacks from non-blocker pieces which could put further restrictions on the
	// attacker's king. However we do not yet know if said king could attack any
	// of said pieces (and hence we do not know if we can trust their attacks to
	// exist).
	assert((blockers & atkInfluence)==BBNone); // Check blockers are not attackable.
	BB defAttacks=BBNone;
	BB set=blockers;
	while(set) {
		Sq sq=bbScanReset(&set);
		defAttacks|=attacksPiece(posGetPieceOnSq(pos, sq), sq, occ);
	}

	// King fill.
	BB atkKing=posGetBBPiece(pos, pieceMake(PieceTypeKing, atk));
	BB fill=searchFill(PieceTypeKing, atkKing, (defAttacks | atkPawns), target);
	if ((fill & target)!=BBNone)
		return false;
	atkInfluence|=fill;

	// Finally ensure the defender has a legal move which does not disturb the fortress.
	// As we know the current piece placement holds the fortress, we just need to
	// test that one of the defender's pieces has a reversible move available
	// (i.e. a non-pawn,  not a capture, not castling). This implies the piece can
	// shuffle between the two squares in question.
	BB mobile=(defOcc & ~(defPawns | blockers | atkInfluence)); // Pieces which the defender can potentially shuffle.
	BB safe=~(occ | atkInfluence);
	while(mobile) {
		Sq sq=bbScanReset(&mobile);
		BB attacks=attacksPiece(posGetPieceOnSq(pos, sq), sq, occ);
		if ((attacks & safe)!=BBNone)
			return true;
	}

	// Defender may have to move a blocker or make a capture, potentially
	// distrupting the fortress. Let search deal with this.
	return false;
}

bool searchInteriorRecogKNNvK(Node *node) {
	// The defender simply has to avoid mate-in-1 (and can always do so trivially).
	Colour defender=(posGetPieceCount(node->pos, PieceWKnight)>0 ? ColourBlack : ColourWhite);
	if (posGetSTM(node->pos)==defender && (!node->inCheck || posLegalMoveExists(node->pos, MoveTypeAny))) {
		assert(!posIsMate(node->pos));
		node->score=ScoreDraw;
		node->bound=BoundExact;
		return true;
	}

	return false;
}

bool searchInteriorRecogKPvK(Node *node) {
	BitBaseResult result=bitbaseProbe(node->pos);
	if (result==BitBaseResultDraw) {
		node->score=ScoreDraw;
		node->bound=BoundExact;
		return true;
	}

	// We could return a win here but prefer to let evaluation guide us to
	// shortest win.
	return false;
}

bool searchInteriorRecogKBPvK(Node *node) {
	// KBPvK (wrong rook pawns).
	// Triggered when all pawns are on a- or h-file, the bishops do not control the
	// queening square, and the defending king is on, or can reach, the queening square.

	// Sanity checks.
	Colour atkCol=(posGetPieceCount(node->pos, PieceWPawn)>0 ? ColourWhite : ColourBlack);
	BB pawns=posGetBBPiece(node->pos, pieceMake(PieceTypePawn, atkCol));
	assert(pawns!=BBNone);
	assert((posGetPieceCount(node->pos, pieceMake(PieceTypeBishopL, atkCol))>0) ^
	       (posGetPieceCount(node->pos, pieceMake(PieceTypeBishopD, atkCol))>0));

	// Ensure all pawns are on wrong rook file.
	bool bishopIsLight=(posGetPieceCount(node->pos, pieceMake(PieceTypeBishopL, atkCol))>0);
	BB wrongFile=((bishopIsLight^(atkCol==ColourWhite)) ? bbFile(FileA) : bbFile(FileH));
	if (pawns & ~wrongFile)
	  return false; // At least one other pawn.
	assert((pawns & wrongFile)==pawns);

	// Is defending king on, or within reach of, the queening square?
	Sq defKingSq=posGetKingSq(node->pos, colourSwap(atkCol));
	BB promoBB=(wrongFile & bbRank(atkCol==ColourWhite ? Rank8 : Rank1));
	if ((bbSq(defKingSq) | attacksKing(defKingSq)) & promoBB) {
		// We can be sure it is a draw.
		node->score=ScoreDraw;
		node->bound=BoundExact;
		return true;
	}

	return false;
}

BB searchFill(PieceType type, BB init, BB occ, BB target) {
	assert(type>=PieceTypeKnight && type<=PieceTypeKing);

	BB fill=init;
	BB done=occ;
	BB todo=init;
	while(todo!=BBNone) {
		assert((done & todo)==BBNone);

		// Choose a new square to generate the moves for.
		Sq sq=bbScanReset(&todo);

		// Mark this square as done.
		done|=bbSq(sq);

		// Hit target?
		BB attacks=attacksPieceType(type, sq, occ);
		if ((attacks & target)!=BBNone)
			return attacks;

		// Add attacks from this square to 'todo' list (if not already done).
		todo|=(attacks & ~done);
		fill|=attacks;
	}

	return fill;
}

bool searchNodeIsPV(const Node *node) {
	return (node->beta-node->alpha>1);
}

bool searchNodeIsQ(const Node *node) {
	return node->depth<1;
}

void searchInterfacePonder(void *dummy, bool ponder) {
	searchPonder=ponder;
}

#ifdef TUNE
void searchInterfaceSpinValue(void *ptr, long long value) {
	// Set value.
	*((int *)ptr)=value;

	// Clear now-invalid TT and history etc.
	searchClear();
}

void searchInterfaceCheckValue(void *ptr, bool value) {
	// Set value.
	*((bool *)ptr)=value;

	// Clear now-invalid TT and history etc.
	searchClear();
}
#endif
