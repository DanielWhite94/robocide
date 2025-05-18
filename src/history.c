#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "history.h"

const History HistoryMax=(((History)1)<<HistoryBit);

History history[PieceNB][SqNB];

void historyInc(Piece fromPiece, Sq toSq, unsigned int depth) {
	assert(pieceIsValid(fromPiece));
	assert(sqIsValid(toSq));

	// Increment count in table.
	History *counter=&history[fromPiece][toSq];
	*counter+=(((History)1)<<utilMin(depth, HistoryBit-1));

	// Overflow? (not a literal overflow, but beyond desired range).
	if (*counter>=HistoryMax)
		historyAge();
	assert(*counter<HistoryMax);
}

History historyGet(Piece fromPiece, Sq toSq) {
	assert(pieceIsValid(fromPiece));
	assert(sqIsValid(toSq));
	assert(history[fromPiece][toSq]<HistoryMax);
	return history[fromPiece][toSq];
}

void historyAge(void) {
	unsigned int i, j;
	for(i=0;i<PieceNB;++i)
		for(j=0;j<SqNB;++j)
			history[i][j]/=2;
}

void historyClear(void) {
	memset(history, 0, sizeof(history));
}

bool historyImport(const char *path) {
	// Open file
	FILE *file=fopen(path, "r");
	if (file==NULL)
		return false;

	// Read data
	size_t s=sizeof(History);
	size_t n=PieceNB*SqNB;
	size_t read=fread(history, s, n, file);

	// Close file
	fclose(file);

	return (read==n);
}

bool historyExport(const char *path) {
	// Open file
	FILE *file=fopen(path, "w");
	if (file==NULL)
		return false;

	// Write data
	size_t s=sizeof(History);
	size_t n=PieceNB*SqNB;
	size_t written=fwrite(history, s, n, file);

	// Close file
	fclose(file);

	return (written==n);
}
