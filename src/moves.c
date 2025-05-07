#include <assert.h>

#include "killers.h"
#include "moves.h"
#include "search.h"

////////////////////////////////////////////////////////////////////////////////
// Private prototypes.
////////////////////////////////////////////////////////////////////////////////

void movesSort(ScoredMove *start, ScoredMove *end); // Descending order (best move first).

////////////////////////////////////////////////////////////////////////////////
// Public functions.
////////////////////////////////////////////////////////////////////////////////

void movesInit(Moves *moves, const Pos *pos, Depth ply, MoveType type) {
	assert(type==MoveTypeQuiet || type==MoveTypeCapture || type==MoveTypeAny);
	moves->end=moves->next=moves->list;
	moves->stage=MovesStageTT;
	moves->ttMove=MoveInvalid;
	moves->pos=pos;
	moves->ply=ply;
	moves->allowed=moves->needed=type;
	moves->next=moves->list;
}

void movesRewind(Moves *moves, Move ttMove) {
	moves->stage=MovesStageTT;
	moves->next=moves->list;
	moves->ttMove=((moveIsValid(ttMove) && (posMoveGetType(moves->pos, ttMove)&moves->allowed)) ? ttMove : MoveInvalid);
}

Move movesNext(Moves *moves) {
	switch(moves->stage) {
		case MovesStageTT:
			// Update stage and next ptr ready for next call (at most one TT move).
			moves->stage=MovesStageGenCaptures;

			// Do we have a TT move?
			if (moves->ttMove!=MoveInvalid)
				return moves->ttMove;

			// Fall through.
		case MovesStageGenCaptures:
			// Do we need to generate any captures?
			if (moves->needed & MoveTypeCapture) {
				assert(moves->next==moves->list);
				assert(moves->next==moves->end);
				posGenPseudoMoves(moves, MoveTypeCapture);
				movesSort(moves->next, moves->end);
				moves->needed&=~MoveTypeCapture;
			}

			// Fall through.
			moves->stage=MovesStageCaptures;
		case MovesStageCaptures:
			// Return moves one at a time.
			while (moves->next<moves->end) {
				Move move=scoredMoveGetMove(*moves->next++);
				if (move!=moves->ttMove) // Exclude TT move as this is searched earlier.
					return move;
			}

			// Fall through.
			moves->stage=MovesStageKillers;
			moves->killersIndex=0;
		case MovesStageKillers:
			while(moves->killersIndex<KillersPerPly) {
				// Check if any killers left.
				Move move=killersGetN(moves->ply, moves->killersIndex++);
				if (move==MoveInvalid)
					break;

				// Hash move?
				if (move==moves->ttMove)
					continue;

				// Not pseudo-legal in this position?
				if (!posMoveIsPseudoLegal(moves->pos, move))
					continue;

				return move;
			}

			// Fall through (no need to update stage as next one is only temporary).
		case MovesStageGenQuiets:
			// No captures left, do we need to generate any quiets?
			if (moves->needed & MoveTypeQuiet) {
				assert(moves->next==moves->end);
				posGenPseudoMoves(moves, MoveTypeQuiet);
				movesSort(moves->next, moves->end);
				moves->needed&=~MoveTypeQuiet;
			}

			// Fall through.
			moves->stage=MovesStageQuiets;
		case MovesStageQuiets:
			// Return moves one at a time.
			while (moves->next<moves->end) {
				// Exclude TT and killer moves as these are searched earlier.
				Move move=scoredMoveGetMove(*moves->next++);
				if (move!=moves->ttMove && !killersMoveIsKiller(moves->ply, move))
					return move;
			}

			// No moves left
			return MoveInvalid;
		break;
	}

	assert(false);
	return MoveInvalid;
}

const Pos *movesGetPos(Moves *moves) {
	return moves->pos;
}

void movesPush(Moves *moves, Move move) {
	assert(moves->end>=moves->list && moves->end<moves->list+MovesMax);

	// Combine with score and add to list
	MoveScore score=searchScoreMove(moves->pos, move);
	*moves->end++=scoredMoveMake(score, move);
}

////////////////////////////////////////////////////////////////////////////////
// Private functions.
////////////////////////////////////////////////////////////////////////////////

void movesSort(ScoredMove *start, ScoredMove *end) {
	// Insertion sort - best move first.
	ScoredMove *ptr;
	for(ptr=start+1;ptr<end;++ptr) {
		ScoredMove temp=*ptr, *tempPtr;
		for(tempPtr=ptr-1;tempPtr>=start && scoredMoveCompGT(temp, *tempPtr);--tempPtr)
			*(tempPtr+1)=*tempPtr;
		*(tempPtr+1)=temp;
	}
}
