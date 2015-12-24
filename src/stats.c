#ifdef STATS

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "stats.h"
#include "uci.h"

#define statsSizeP ((size_t)(8+1))
#define statsSizeN ((size_t)(2+1+1))
#define statsSizeB ((size_t)(1+1+1)) // treat bishops on different colours separately
#define statsSizeR ((size_t)(2+1+1))
#define statsSizeQ ((size_t)(1+1+1))

typedef struct {
	size_t index;
	uint64_t count;
} StatsPair;

size_t statsTallySize=(statsSizeP*statsSizeN*statsSizeB*statsSizeB*statsSizeR*statsSizeQ)*(statsSizeP*statsSizeN*statsSizeB*statsSizeB*statsSizeR*statsSizeQ);
uint64_t *statsTally=NULL;

////////////////////////////////////////////////////////////////////////////////
// Private prototypes.
////////////////////////////////////////////////////////////////////////////////

ssize_t statsMakeIndex(unsigned wp, unsigned wn, unsigned wbl, unsigned wbd,unsigned wr,unsigned wq, unsigned bp, unsigned bn, unsigned bbl, unsigned bbd,unsigned br,unsigned bq);

int statsCompare(const void *aPtr, const void *bPtr);

////////////////////////////////////////////////////////////////////////////////
// Public functions.
////////////////////////////////////////////////////////////////////////////////

bool statsInit(void) {
	assert(statsTally==NULL);

	statsTally=malloc(sizeof(uint64_t)*statsTallySize);
	if (statsTally==NULL)
		return false;

	statsClear();

	return true;
}

void statsQuit(void) {
	assert(statsTally!=NULL);

	free(statsTally);
	statsTally=NULL;
}

void statsClear(void) {
	assert(statsTally!=NULL);

	memset(statsTally, 0, sizeof(uint64_t)*statsTallySize);
}

bool statsAdd(const Pos *pos) {
	assert(statsTally!=NULL);

	// Get piece counts.
	size_t wp=posGetPieceCount(pos, PieceWPawn);
	size_t wn=posGetPieceCount(pos, PieceWKnight);
	size_t wbl=posGetPieceCount(pos, PieceWBishopL);
	size_t wbd=posGetPieceCount(pos, PieceWBishopD);
	size_t wr=posGetPieceCount(pos, PieceWRook);
	size_t wq=posGetPieceCount(pos, PieceWQueen);
	size_t bp=posGetPieceCount(pos, PieceBPawn);
	size_t bn=posGetPieceCount(pos, PieceBKnight);
	size_t bbl=posGetPieceCount(pos, PieceBBishopL);
	size_t bbd=posGetPieceCount(pos, PieceBBishopD);
	size_t br=posGetPieceCount(pos, PieceBRook);
	size_t bq=posGetPieceCount(pos, PieceBQueen);

	// Find index.
	ssize_t index=statsMakeIndex(wp, wn, wbl, wbd, wr, wq, bp, bn, bbl, bbd, br, bq);
	if (index<0)
		return false;

	// Increase tally.
	statsTally[index]++;

	return true;
}

bool statsWrite(const char *path) {
	FILE *file=fopen(path, "w");
	if (file==NULL)
		return false;

	size_t written=fwrite(statsTally, sizeof(uint64_t), statsTallySize, file);

	fclose(file);

	return (written==statsTallySize);
}

bool statsRead(const char *path) {
	uciWrite("Opening file...\n");
	FILE *file=fopen(path, "r");
	if (file==NULL)
		return false;

	uciWrite("\n");
	StatsPair *data=malloc(sizeof(StatsPair)*statsTallySize);
	if (data==NULL)
		return false;

	uciWrite("Loading entries...\n");
	size_t index;
	for(index=0;index<statsTallySize;++index) {
		uint64_t count;
		size_t readCount=fread(&count, sizeof(uint64_t), 1, file);
		if (readCount!=1)
			return false;

		data[index].index=index;
		data[index].count=count;
	}

	uciWrite("Sorting entries...\n");
	qsort(data, statsTallySize, sizeof(StatsPair), &statsCompare);

	uciWrite("Displaying entries...\n");
	size_t i;
	for(i=0;i<20;++i) {
		uciWrite("%3u %10llu", (unsigned) i, (unsigned long long int)data[i].count);

		size_t index=data[i].index;
		uciWrite(" bq=%u", (unsigned) (index%statsSizeQ));
		index/=statsSizeQ;
		uciWrite(" br=%u", (unsigned) (index%statsSizeR));
		index/=statsSizeR;
		uciWrite(" bbd=%u", (unsigned) (index%statsSizeB));
		index/=statsSizeB;
		uciWrite(" bbl=%u", (unsigned) (index%statsSizeB));
		index/=statsSizeB;
		uciWrite(" bn=%u", (unsigned) (index%statsSizeN));
		index/=statsSizeN;
		uciWrite(" bp=%u", (unsigned) (index%statsSizeP));
		index/=statsSizeP;
		uciWrite(" wq=%u", (unsigned) (index%statsSizeQ));
		index/=statsSizeQ;
		uciWrite(" wr=%u", (unsigned) (index%statsSizeR));
		index/=statsSizeR;
		uciWrite(" wbd=%u", (unsigned) (index%statsSizeB));
		index/=statsSizeB;
		uciWrite(" wbl=%u", (unsigned) (index%statsSizeB));
		index/=statsSizeB;
		uciWrite(" wn=%u", (unsigned) (index%statsSizeN));
		index/=statsSizeN;
		uciWrite(" wp=%u", (unsigned) (index%statsSizeP));

		uciWrite("\n");
	}

	free(data);

	fclose(file);

	return true;
}

////////////////////////////////////////////////////////////////////////////////
// Private functions.
////////////////////////////////////////////////////////////////////////////////

ssize_t statsMakeIndex(unsigned wp, unsigned wn, unsigned wbl, unsigned wbd,unsigned wr,unsigned wq, unsigned bp, unsigned bn, unsigned bbl, unsigned bbd,unsigned br,unsigned bq) {
	if (wp>=statsSizeP || wn>=statsSizeN || wbl>=statsSizeB || wbd>=statsSizeB || wr>=statsSizeR || wq>=statsSizeQ ||
	    bp>=statsSizeP || bn>=statsSizeN || bbl>=statsSizeB || bbd>=statsSizeB || br>=statsSizeR || bq>=statsSizeQ)
	    return -1;

	return ((((((((((wp*statsSizeN+wn)*statsSizeB+wbl)*statsSizeB+wbd)*statsSizeR+wr)*statsSizeQ+wq)*statsSizeP+bp)*statsSizeN+bn)*statsSizeB+bbl)*statsSizeB+bbd)*statsSizeR+br)*statsSizeQ+bq;
}

int statsCompare(const void *aPtr, const void *bPtr) {
	const StatsPair *pairA=(const StatsPair *)aPtr;
	const StatsPair *pairB=(const StatsPair *)bPtr;

	// Reversed here to sort in descending order.
	if (pairA->count<pairB->count)
		return +1;
	else if (pairA->count>pairB->count)
		return -1;
	else
		return 0;
}

#endif

