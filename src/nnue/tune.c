#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../pos.h"
#include "../util.h"
#include "tune.h"

typedef enum {
	NnueTuneScoreInvalid,
	NnueTuneScoreWhiteWin,
	NnueTuneScoreBlackWin,
	NnueTuneScoreDraw,
} NnueTuneScore;

const char NnueTuneScoreStr[][8]={
	[NnueTuneScoreInvalid]="???",
	[NnueTuneScoreWhiteWin]="1-0",
	[NnueTuneScoreBlackWin]="0-1",
	[NnueTuneScoreDraw]="1/2-1/2",
};

STATICASSERT(PieceNB<=16);
typedef struct {
	BB occ;
	uint8_t pieces[16]; // a pair per byte in order from a1-h8
	uint8_t stm:1;
	uint8_t score:2;
	uint8_t padding:5;
} NnueTunePosition;

struct NnueTunePositions {
	NnueTunePosition *array;
	size_t next, size;
};

////////////////////////////////////////////////////////////////////////////////
// Private prototypes
////////////////////////////////////////////////////////////////////////////////

void nnueTunePositionFromPos(NnueTunePosition *position, const Pos *pos);
void nnueTunePositionToPos(const NnueTunePosition *position, Pos *pos); // note: this only sets the bare minimum for our tuning purposes (a call to posIsConsistent would fail)
void nnueTunePositionToArray(const NnueTunePosition *position, Piece array[SqNB]);
Piece nnueTunePositionGetPiece(const NnueTunePosition *pos, unsigned n);
void nnueTunePositionDebug(const NnueTunePosition *pos);

const char *nnueTuneScoreToStr(NnueTuneScore score);
double nnueTuneScoreToWinProb(NnueTuneScore score);

double nnueTuneComputeSigmoid(double s);

////////////////////////////////////////////////////////////////////////////////
// Public functions
////////////////////////////////////////////////////////////////////////////////

NnueTunePositions *nnueTunePositionsNew(size_t count) {
	// Allocate memory
	NnueTunePositions *positions=malloc(sizeof(NnueTunePositions));
	NnueTunePosition *array=(count>0 ? malloc(sizeof(NnueTunePosition)*count) : NULL);
	if (positions==NULL || (count>0 && array==NULL))
		return NULL;

	// Set fields
	positions->array=array;
	positions->next=0;
	positions->size=count;

	return positions;
}

void nnueTunePositionsFree(NnueTunePositions *positions) {
	// NULL check
	if (positions==NULL)
		return;

	// Free memory
	free(positions->array);
	free(positions);
}

NnueTunePositions *nnueTunePositionsLoadEpd(const char *path) {
	// Init variables
	NnueTunePositions *positions=NULL;
	FILE *file=NULL;
	Pos *pos=NULL;

	// Open EPD file for reading
	file=fopen(path, "r");
	if (file==NULL)
		goto error;

	// Allocate NnueTunePositions object
	unsigned lineCount=utilFileCountLines(file); // do an initial pass over the file to count the number of lines so we can allocate our positions object all at once
	positions=nnueTunePositionsNew(lineCount);
	if (positions==NULL)
		goto error;

	// Create scratch position and coefficients to use to find evaluation coefficients
	pos=posNew(NULL);

	// Read file one line at a time
	char line[1024];
	while(fgets(line, 1024, file)!=NULL) {
		// Try to parse FEN string and game result
		char *c9pos=strstr(line, " c9 \"");
		if (c9pos==NULL)
			continue;
		*c9pos='\0';

		char *fenStr=line;
		char *resultStr=c9pos+5;
		char *resultStrEndPos=strstr(resultStr, "\"");
		if (resultStrEndPos==NULL)
			continue;
		*resultStrEndPos='\0';

		NnueTuneScore result=NnueTuneScoreInvalid;
		if (strcmp(resultStr, "1-0")==0)
			result=NnueTuneScoreWhiteWin;
		else if (strcmp(resultStr, "1/2-1/2")==0)
			result=NnueTuneScoreDraw;
		else if (strcmp(resultStr, "0-1")==0)
			result=NnueTuneScoreBlackWin;
		else
			continue;

		// Setup position
		if (!posSetToFEN(pos, fenStr))
			continue;

		// Add position to array
		assert(positions->next<positions->size);

		nnueTunePositionFromPos(&positions->array[positions->next], pos);
		positions->array[positions->next].score=result;

		++positions->next;
	}

	// Tidy up
	posFree(pos);
	fclose(file);

	return positions;

	// Error handling
	error:
	posFree(pos);
	if (file!=NULL)
		fclose(file);
	nnueTunePositionsFree(positions);
	return NULL;
}

////////////////////////////////////////////////////////////////////////////////
// Private functions
////////////////////////////////////////////////////////////////////////////////

void nnueTunePositionFromPos(NnueTunePosition *position, const Pos *pos) {
	assert(position!=NULL);
	assert(pos!=NULL);

	// Occ is a direct copy
	BB occ=position->occ=posGetBBAll(pos);

	// Create piece list
	memset(position->pieces, 0, sizeof(position->pieces));
	unsigned i=0;
	while(occ!=BBNone) {
		Sq sq=bbScanReset(&occ);
		Piece p=posGetPieceOnSq(pos, sq);
		assert(p!=PieceNone && p<PieceNB && PieceNB<=16);
		position->pieces[i/2]|=(i%2==0 ? p : (p<<4));
		++i;
	}

	// Misc fields
	position->stm=posGetSTM(pos);
	position->score=NnueTuneScoreInvalid; // this will be set after by the caller
}

void nnueTunePositionToPos(const NnueTunePosition *position, Pos *pos) {
	assert(position!=NULL);
	assert(pos!=NULL);

	// Convert NnueTunePosition into a piece array and then to a Pos
	Piece array[SqNB];
	nnueTunePositionToArray(position, array);
	posSetToArray(pos, position->stm, array, true);
}

void nnueTunePositionToArray(const NnueTunePosition *position, Piece array[SqNB]) {
	assert(position!=NULL);

	assert(PieceNone==0);
	memset(array, 0, sizeof(Piece)*SqNB);

	unsigned i=0;
	BB occ=position->occ;
	while(occ!=BBNone) {
		Sq sq=bbScanReset(&occ);
		Piece p=nnueTunePositionGetPiece(position, i);
		assert(p!=PieceNone && p<PieceNB);

		array[sq]=p;

		++i;
	}
}

Piece nnueTunePositionGetPiece(const NnueTunePosition *pos, unsigned n) {
	assert(pos!=NULL);
	assert(n<32);

	return ((pos->pieces[n/2]>>(n%2==0 ? 0 : 4)) & 0xF);
}

void nnueTunePositionDebug(const NnueTunePosition *pos) {
	// Find pieces
	Piece array[SqNB];
	nnueTunePositionToArray(pos, array);

	// Print board and other fields
	int file, rank;
	for(rank=Rank8;rank>=Rank1;--rank) {
		printf("%c|", rankToChar(rank));
		for(file=FileA;file<=FileH;++file)
			printf(" %c", pieceToChar(array[sqMake(file,rank)]));
		printf("\n");
	}
	printf("   ----------------\n");
	printf("  ");
	for(file=FileA;file<=FileH;++file)
		printf(" %c", fileToChar(file));
	printf("\n");
	printf("STM = %s\n", colourToStr(pos->stm));
	printf("Result = %s\n", nnueTuneScoreToStr(pos->score));
}

const char *nnueTuneScoreToStr(NnueTuneScore score) {
	assert(score<4);
	return NnueTuneScoreStr[score];
}

double nnueTuneScoreToWinProb(NnueTuneScore score) {
	switch(score) {
		case NnueTuneScoreInvalid:
			assert(false);
			return 0.5;
		break;
		case NnueTuneScoreWhiteWin:
			return 1.0;
		break;
		case NnueTuneScoreBlackWin:
			return 0.0;
		break;
		case NnueTuneScoreDraw:
			return 0.5;
		break;
	}

	assert(false);
	return 0.5;
}

double nnueTuneComputeSigmoid(double s) {
	// Convert evaluation/search score s into a logistic win/draw/loss value in the range [0,1]
	const double k=0.01222392421;
	return 1.0/(1.0+pow(2.0, -k*s));
}
