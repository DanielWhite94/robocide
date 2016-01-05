#include <assert.h>
#include <stdint.h>
#include <stdlib.h>

#include "attacks.h"
#include "bb.h"
#include "bitbase.h"
#include "colour.h"
#include "robocide.h"
#include "square.h"
#include "util.h"

// Pack 64 results into single array entry (a mask of places where if the black
// king stands the position is a win for white).
STATICASSERT(BitBaseResultBit<=1);
uint64_t *bitbase=NULL;

typedef enum {
	BitBaseResultFullInvalid,
	BitBaseResultFullUnknown,
	BitBaseResultFullDraw,
	BitBaseResultFullWin,
} BitBaseResultFull;

////////////////////////////////////////////////////////////////////////////////
// Private prototypes.
////////////////////////////////////////////////////////////////////////////////

void bitbaseGen(void);

BitBaseResultFull bitbaseComputeStaticResult(Sq pawnSq, Sq wKingSq, Colour stm, Sq bKingSq);
BitBaseResultFull bitbaseComputeDynamicResult(const BitBaseResultFull *array, Sq pawnSq, Sq wKingSq, Colour stm, Sq bKingSq);

BitBaseResult bitbaseProbeRaw(Sq pawnSq, Sq wKingSq, Sq bKingSq, Colour stm); // Assumes the pawn is white (hence moving north).

unsigned int bitbaseIndex(File pawnFile, Rank pawnRank, Sq wKingSq, Colour stm);
unsigned int bitbaseIndexFull(File pawnFile, Rank pawnRank, Sq wKingSq, Colour stm, Sq bKingSq);

////////////////////////////////////////////////////////////////////////////////
// Public functions.
////////////////////////////////////////////////////////////////////////////////

void bitbaseInit(void) {
	// Allocate memory.
	bitbase=malloc((FileNB/2)*RankNB*SqNB*ColourNB*sizeof(uint64_t));
	if (bitbase==NULL)
		mainFatalError("Error: Could not allocate memory for KPvK bitbase.\n");

	// Generate bitbase.
	bitbaseGen();
}

void bitbaseQuit(void) {
	// Free memory.
	free(bitbase);
	bitbase=NULL;
}

BitBaseResult bitbaseProbe(const Pos *pos) {
	// Sanity checks.
	assert(bbPopCount(posGetBBAll(pos))==3);
	assert(posGetBBPiece(pos, PieceWPawn)!=BBNone || posGetBBPiece(pos, PieceBPawn)!=BBNone);

	// Adjust so as if white has the pawn.
	Colour attacker=(posGetBBPiece(pos, PieceWPawn)!=BBNone ? ColourWhite : ColourBlack);
	Sq pawnSq, wKingSq, bKingSq;
	Colour stm;
	if (attacker==ColourWhite) {
		pawnSq=*posGetPieceListStart(pos, PieceWPawn);
		wKingSq=posGetKingSq(pos, ColourWhite);
		bKingSq=posGetKingSq(pos, ColourBlack);
		stm=posGetSTM(pos);
	} else {
		// Flip squares vertically, swap king colours and swap side to move.
		pawnSq=sqFlip(*posGetPieceListStart(pos, PieceBPawn));
		wKingSq=sqFlip(posGetKingSq(pos, ColourBlack));
		bKingSq=sqFlip(posGetKingSq(pos, ColourWhite));
		stm=colourSwap(posGetSTM(pos));
	}

	// Probe.
	return bitbaseProbeRaw(pawnSq, wKingSq, bKingSq, stm);
}

////////////////////////////////////////////////////////////////////////////////
// Private functions.
////////////////////////////////////////////////////////////////////////////////

void bitbaseGen(void) {
	// Allocate array to use while generating bitbase.
	BitBaseResultFull *array=malloc((FileNB/2)*RankNB*SqNB*ColourNB*SqNB*sizeof(BitBaseResultFull));
	if (array==NULL)
		mainFatalError("Error: Could not allocate memory for generating KPvK bitbase.\n");

	// Mark positions which are obviously won/drawn/invalid (otherwise mark as unknown).
	Sq wKingSq, bKingSq;
	Colour stm;
	File pawnFile;
	Rank pawnRank;
	for(pawnFile=FileA;pawnFile<=FileD;++pawnFile)
		for(pawnRank=Rank8;pawnRank>=Rank2;--pawnRank) {
			Sq pawnSq=sqMake(pawnFile, pawnRank);

			for(wKingSq=0;wKingSq<SqNB;++wKingSq)
				for(stm=0;stm<ColourNB;++stm)
					for(bKingSq=0;bKingSq<SqNB;++bKingSq) {
						unsigned int index=bitbaseIndexFull(pawnFile, pawnRank, wKingSq, stm, bKingSq);
						array[index]=bitbaseComputeStaticResult(pawnSq, wKingSq, stm, bKingSq);
					}
		}

	// Loop over each pawn file and ranks in backwards order (from 7 to 2).
	// We can do this as different files are independent and, for example, rank 5
	// positions do not depend on any rank 4 positions.
	for(pawnFile=FileA;pawnFile<=FileD;++pawnFile)
		for(pawnRank=Rank7;pawnRank>=Rank2;--pawnRank) {
			Sq pawnSq=sqMake(pawnFile, pawnRank);

			// Compute position results based on child positions.
			bool change;
			do {
				change=false;

				for(wKingSq=0;wKingSq<SqNB;++wKingSq)
					for(stm=0;stm<ColourNB;++stm)
						for(bKingSq=0;bKingSq<SqNB;++bKingSq) {
							// Position already solved?
							unsigned int index=bitbaseIndexFull(pawnFile, pawnRank, wKingSq, stm, bKingSq);
							if (array[index]!=BitBaseResultFullUnknown)
								continue;

							// Try to compute result and update change flag if successful.
							BitBaseResultFull result=bitbaseComputeDynamicResult(array, pawnSq, wKingSq, stm, bKingSq);
							change|=((array[index]=result)!=BitBaseResultFullUnknown);
						}
			} while(change);

			// Update global array.
			// Any positions left 'unknown' are draws (although neither side can 'force' it per se).
			for(wKingSq=0;wKingSq<SqNB;++wKingSq)
				for(stm=0;stm<ColourNB;++stm) {
					unsigned int index=bitbaseIndex(pawnFile, pawnRank, wKingSq, stm);
					bitbase[index]=0;
					for(bKingSq=0;bKingSq<SqNB;++bKingSq) {
						unsigned int fullIndex=bitbaseIndexFull(pawnFile, pawnRank, wKingSq, stm, bKingSq);
						STATICASSERT(BitBaseResultWin==1);
						if (array[fullIndex]==BitBaseResultFullWin)
							bitbase[index]|=(1llu<<bKingSq);
					}
				}
		}

	// Verify counts.
#	ifndef NDEBUG
	unsigned countTotal=0, countWin=0, countDraw=0, countInvalid=0, countUnknown=0;
	for(pawnFile=FileA;pawnFile<=FileD;++pawnFile)
		for(pawnRank=Rank2;pawnRank<=Rank7;++pawnRank)
			for(wKingSq=0;wKingSq<SqNB;++wKingSq)
				for(stm=0;stm<ColourNB;++stm)
					for(bKingSq=0;bKingSq<SqNB;++bKingSq) {
						unsigned int fullIndex=bitbaseIndexFull(pawnFile, pawnRank, wKingSq, stm, bKingSq);
						switch(array[fullIndex]) {
							case BitBaseResultFullWin:
								++countWin;
							break;
							case BitBaseResultFullDraw:
								++countDraw;
							break;
							case BitBaseResultFullInvalid:
								++countInvalid;
							break;
							case BitBaseResultFullUnknown:
								++countUnknown;
							break;
						}
						++countTotal;
					}
	assert(countTotal==196608);
	assert(countWin==111282);
	assert(countInvalid==30932);
	assert(countDraw+countUnknown==54394);
#	endif

	// Free working array.
	free(array);
}

BitBaseResultFull bitbaseComputeStaticResult(Sq pawnSq, Sq wKingSq, Colour stm, Sq bKingSq) {
	// Sanity checks.
	assert(sqIsValid(pawnSq) && sqFile(pawnSq)<=FileD);
	assert(sqIsValid(wKingSq));
	assert(colourIsValid(stm));
	assert(sqIsValid(bKingSq));

	// Init.
	BB wKingAtks=attacksKing(wKingSq);
	BB bKingAtks=attacksKing(bKingSq);
	BB pawnAtks=attacksPawn(pawnSq, ColourWhite);
	BB wKingBB=bbSq(wKingSq);
	BB bKingBB=bbSq(bKingSq);
	BB pawnBB=bbSq(pawnSq);
	BB occ=(pawnBB | wKingBB | bKingBB);

	// If any pieces occupy the same square, or the side not on move is in check, invalid position.
	if (pawnSq==wKingSq || pawnSq==bKingSq || wKingSq==bKingSq || // Pieces overlap.
	    sqRank(pawnSq)==Rank1 || // Pawn on rank 1.
	    (wKingAtks & bKingBB)!=BBNone || // Kings are adjacent.
	    (stm==ColourWhite && (pawnAtks & bKingBB)!=BBNone)) // White to move, black king attacked by pawn.
		return BitBaseResultFullInvalid;

	// If pawn can promote without capture, win.
	if (sqRank(pawnSq)==Rank7 && stm==ColourWhite) {
		Sq promoSq=sqNorth(pawnSq,1);
		if (promoSq!=wKingSq && promoSq!=bKingSq && // Promotion square empty.
		    ((bbSq(promoSq) & bKingAtks)==BBNone || (bbSq(promoSq) & wKingAtks)!=BBNone)) // Promotion square is safe.
			return BitBaseResultFullWin;
	}

	// If black can capture pawn, draw.
	bool pawnAttacked=((bKingAtks & pawnBB)!=BBNone);
	bool pawnDefended=((wKingAtks & pawnBB)!=BBNone);
	if (stm==ColourBlack && pawnAttacked && !pawnDefended)
	    return BitBaseResultFullDraw;

	// If 'pawn' is on 8th rank, win (we have already shown that it cannot be captured).
	if (sqRank(pawnSq)==Rank8)
		return BitBaseResultFullWin;

	// If no moves available for stm, draw (stalemate).
	if (stm==ColourWhite) {
		BB safe=~(bKingAtks | occ);
		if ((wKingAtks & safe)==BBNone && // No king moves.
		    (sqNorth(pawnSq,1)==wKingSq || sqNorth(pawnSq,1)==bKingSq)) // No pawn moves.
			return BitBaseResultFullDraw;
	} else {
		BB safe=~(wKingAtks | pawnAtks | occ); // We already check if black can capture pawn, hence assume defended.
		if ((bKingAtks & safe)==BBNone)
			return BitBaseResultFullDraw;
	}

	// Unable to statically evaluate the position.
	return BitBaseResultFullUnknown;
}

BitBaseResultFull bitbaseComputeDynamicResult(const BitBaseResultFull *array, Sq pawnSq, Sq wKingSq, Colour stm, Sq bKingSq) {
	// Sanity checks.
	assert(sqIsValid(pawnSq) && sqFile(pawnSq)<=FileD && sqRank(pawnSq)>=Rank2 && sqRank(pawnSq)<=Rank7);
	assert(sqIsValid(wKingSq));
	assert(colourIsValid(stm));
	assert(sqIsValid(bKingSq));

	// If white to move and any children are wins, win. If white to move and
	// all children are draws, draw.
	// If black to move and any children are draws, draw. If black to move
	// and all children are wins, win.
	File pawnFile=sqFile(pawnSq);
	Rank pawnRank=sqRank(pawnSq);
	Colour xstm=colourSwap(stm);
	if (stm==ColourWhite) {
		bool allDraws=true;

		// King moves.
		BB set=attacksKing(wKingSq);
		while(set) {
			unsigned int newIndex=bitbaseIndexFull(pawnFile, pawnRank, bbScanReset(&set), xstm, bKingSq);
			switch(array[newIndex]) {
				case BitBaseResultFullInvalid: break; // Invalid move.
				case BitBaseResultFullUnknown: allDraws=false; break;
				case BitBaseResultFullDraw: break;
				case BitBaseResultFullWin: return BitBaseResultFullWin; break;
			}
		}

		// Standard pawn move forward.
		// We do not need to test for moving to 8th rank or into either of the kings as such positions will be marked already.
		unsigned int newIndex=bitbaseIndexFull(pawnFile, pawnRank+1, wKingSq, xstm, bKingSq);
		bool singlePushOk=true;
		switch(array[newIndex]) {
			case BitBaseResultFullInvalid: singlePushOk=false; break; // Invalid move.
			case BitBaseResultFullUnknown: allDraws=false; break;
			case BitBaseResultFullDraw: break;
			case BitBaseResultFullWin: return BitBaseResultFullWin; break;
		}

		// Double pawn move.
		// Again we do not need to check for moving into either of the kings.
		if (pawnRank==Rank2 && singlePushOk) {
			unsigned int newIndex=bitbaseIndexFull(pawnFile, pawnRank+2, wKingSq, xstm, bKingSq);
			switch(array[newIndex]) {
				case BitBaseResultFullInvalid: break; // Invalid move.
				case BitBaseResultFullUnknown: allDraws=false; break;
				case BitBaseResultFullDraw: break;
				case BitBaseResultFullWin: return BitBaseResultFullWin; break;
			}
		}

		return (allDraws ? BitBaseResultFullDraw : BitBaseResultFullUnknown);
	} else {
		// King moves.
		bool allWins=true;
		BB set=attacksKing(bKingSq);
		while(set) {
			unsigned int newIndex=bitbaseIndexFull(pawnFile, pawnRank, wKingSq, xstm, bbScanReset(&set));
			switch(array[newIndex]) {
				case BitBaseResultFullInvalid: break; // Invalid move.
				case BitBaseResultFullUnknown: allWins=false; break;
				case BitBaseResultFullDraw: return BitBaseResultFullDraw; break;
				case BitBaseResultFullWin: break;
			}
		}

		return (allWins ? BitBaseResultFullWin : BitBaseResultFullUnknown);
	}

	assert(false);
	return BitBaseResultFullInvalid;
}

BitBaseResult bitbaseProbeRaw(Sq pawnSq, Sq wKingSq, Sq bKingSq, Colour stm) {
	// Sanity checks.
	assert(sqIsValid(pawnSq));
	assert(sqIsValid(wKingSq));
	assert(sqIsValid(bKingSq));
	assert(colourIsValid(stm));

	// Adjust so pawn is in left half of board (on files A, B, C or D).
	File pawnFile=sqFile(pawnSq);
	Rank pawnRank=sqRank(pawnSq);
	if (pawnFile>FileD) {
		pawnFile=fileMirror(pawnFile);
		wKingSq=sqMirror(wKingSq);
		bKingSq=sqMirror(bKingSq);
	}

	// Probe.
	unsigned int index=bitbaseIndex(pawnFile, pawnRank, wKingSq, stm);
	return (bitbase[index]>>bKingSq)&1;
}

unsigned int bitbaseIndex(File pawnFile, Rank pawnRank, Sq wKingSq, Colour stm) {
	assert(pawnFile>=FileA && pawnFile<=FileD);
	assert(pawnRank>=Rank2 && pawnRank<=Rank7);
	assert(sqIsValid(wKingSq));
	assert(colourIsValid(stm));
	return ((pawnFile*RankNB+pawnRank)*SqNB+wKingSq)*ColourNB+stm;
}

unsigned int bitbaseIndexFull(File pawnFile, Rank pawnRank, Sq wKingSq, Colour stm, Sq bKingSq) {
	assert(pawnFile>=FileA && pawnFile<=FileD);
	assert(rankIsValid(pawnRank));
	assert(sqIsValid(wKingSq));
	assert(colourIsValid(stm));
	assert(sqIsValid(bKingSq));
	return (((pawnFile*RankNB+pawnRank)*SqNB+wKingSq)*ColourNB+stm)*SqNB+bKingSq;
}
