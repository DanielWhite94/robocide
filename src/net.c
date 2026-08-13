#include <assert.h>
#include <string.h>

#include "eval.h"
#include "net.h"

struct Net {
	int16_t weightsAccum[SqNB][NetPieceNB][SqNB][256]; // [KingSq][Piece][PieceSq][index]
	int16_t biasAccum[256];

	int8_t weightsHidden1[32][512];
	int32_t biasHidden1[32];

	int8_t weightsHidden2[32][32];
	int32_t biasHidden2[32];

	int8_t weightsOutput[32];
	int32_t biasOutput;
};

typedef enum {
	NetTuneScoreInvalid,
	NetTuneScoreWhiteWin,
	NetTuneScoreBlackWin,
	NetTuneScoreDraw,
} NetTuneScore;

const char NetTuneScoreStr[][8]={
	[NetTuneScoreInvalid]="???",
	[NetTuneScoreWhiteWin]="1-0",
	[NetTuneScoreBlackWin]="0-1",
	[NetTuneScoreDraw]="1/2-1/2",
};

STATICASSERT(PieceNB<=16);
typedef struct {
	BB occ;
	uint8_t pieces[16]; // a pair per byte in order from a1-h8
	uint8_t stm:1;
	uint8_t score:2;
	uint8_t padding:5;
} NetTunePosition;

struct NetTunePositions {
	NetTunePosition *array;
	size_t next, size;
};

Net *net=NULL; // holds the currently loaded network

////////////////////////////////////////////////////////////////////////////////
// Private prototypes
////////////////////////////////////////////////////////////////////////////////

void netAccumulatorAddRaw(NetAccumulator *accum, const Net *net, Sq kingSqW, Sq kingSqB, Sq sq, Piece piece);

NetPiece netPieceFromPiece(Piece p); // expects an actual piece (not PieceNone)
NetPiece netPieceSwapColour(NetPiece p); // leaves NetPieceKing unchanged

int32_t netMul(int8_t a, int8_t b); // exists to avoid bugs where variables are not cast to a larger type before a multiplication
int32_t netCReLU(int32_t x);

const char *netTuneScoreToStr(NetTuneScore score);

void netTunePositionSetFromPos(NetTunePosition *position, const Pos *pos);
Piece netTunePositionGetPiece(const NetTunePosition *pos, unsigned n);
void netTunePositionDebug(const NetTunePosition *pos);

void netTunePositionAccumulatorCalc(const Net *net, const NetTunePosition *pos, NetAccumulator *accum);

void netTuneLayerDebug8(const int8_t *values, size_t count);
void netTuneLayerDebug16(const int16_t *values, size_t count);
void netTuneLayerDebug32(const int32_t *values, size_t count);

////////////////////////////////////////////////////////////////////////////////
// Public functions
////////////////////////////////////////////////////////////////////////////////

void netInit(void) {
	assert(net==NULL);

	// Load network
	net=netNewPST();
}

void netQuit(void) {
	netFree(net);
}

const Net *netGet(void) {
	return net;
}

Net *netNew(void) {
	// Allocate memory
	Net *net=malloc(sizeof(Net));
	if (net==NULL)
		return NULL;

	// Initialise all values to zero
	memset(net, 0, sizeof(Net));

	return net;
}

void netFree(Net *net) {
	// NULL check
	if (net==NULL)
		return;

	// Free memory
	free(net);
}

Net *netLoad(const char *path) {
	assert(path!=NULL);

	// Init
	Net *net=NULL;
	FILE *file=NULL;

	// Allocate network
	net=netNew();
	if (net==NULL)
		goto error;

	// Open file
	file=fopen(path, "w");
	if (file==NULL)
		goto error;

	// Write data
	if (fread(net->weightsAccum, sizeof(net->weightsAccum), 1, file)!=1 ||
	    fread(net->biasAccum, sizeof(net->biasAccum), 1, file)!=1 ||
	    fread(net->weightsHidden1, sizeof(net->weightsHidden1), 1, file)!=1 ||
	    fread(net->biasHidden1, sizeof(net->biasHidden1), 1, file)!=1 ||
	    fread(net->weightsHidden2, sizeof(net->weightsHidden2), 1, file)!=1 ||
	    fread(net->biasHidden2, sizeof(net->biasHidden2), 1, file)!=1 ||
	    fread(net->weightsOutput, sizeof(net->weightsOutput), 1, file)!=1 ||
	    fread(&net->biasOutput, sizeof(net->biasOutput), 1, file)!=1)
		goto error;

	// Close file
	fclose(file);

	return net;

	// Error handling
	error:
	fclose(file);
	free(net);
	return NULL;
}

bool netSave(const Net *net, const char *path) {
	assert(net!=NULL);
	assert(path!=NULL);

	// Open file
	FILE *file=fopen(path, "w");
	if (file==NULL)
		return false;

	// Write data
	if (fwrite(net->weightsAccum, sizeof(net->weightsAccum), 1, file)!=1 ||
	    fwrite(net->biasAccum, sizeof(net->biasAccum), 1, file)!=1 ||
	    fwrite(net->weightsHidden1, sizeof(net->weightsHidden1), 1, file)!=1 ||
	    fwrite(net->biasHidden1, sizeof(net->biasHidden1), 1, file)!=1 ||
	    fwrite(net->weightsHidden2, sizeof(net->weightsHidden2), 1, file)!=1 ||
	    fwrite(net->biasHidden2, sizeof(net->biasHidden2), 1, file)!=1 ||
	    fwrite(net->weightsOutput, sizeof(net->weightsOutput), 1, file)!=1 ||
	    fwrite(&net->biasOutput, sizeof(net->biasOutput), 1, file)!=1) {
		fclose(file);
		return false;
	}

	// Close file
	fclose(file);

	return true;
}

Net *netNewMaterial(void) {
	// Create 'empty' net
	Net *net=netNew();

	// Input layer (accumulator)
	for(Sq kingSq=0; kingSq<64; ++kingSq) {
		for(Sq pieceSq=0; pieceSq<64; ++pieceSq) {
			const int f=3*100;
			net->weightsAccum[kingSq][NetPieceWPawn][pieceSq][0]=1*f;
			net->weightsAccum[kingSq][NetPieceBPawn][pieceSq][0]=-1*f;
			net->weightsAccum[kingSq][NetPieceWKnight][pieceSq][0]=3*f;
			net->weightsAccum[kingSq][NetPieceBKnight][pieceSq][0]=-3*f;
			net->weightsAccum[kingSq][NetPieceWBishop][pieceSq][0]=3*f;
			net->weightsAccum[kingSq][NetPieceBBishop][pieceSq][0]=-3*f;
			net->weightsAccum[kingSq][NetPieceWRook][pieceSq][0]=5*f;
			net->weightsAccum[kingSq][NetPieceBRook][pieceSq][0]=-5*f;
			net->weightsAccum[kingSq][NetPieceWQueen][pieceSq][0]=9*f;
			net->weightsAccum[kingSq][NetPieceBQueen][pieceSq][0]=-9*f;
		}
	}

	// Hidden layer 1
	net->weightsHidden1[0][0]=64;
	net->weightsHidden1[1][256]=64;

	// Hidden layer 2
	net->weightsHidden2[0][0]=64;
	net->weightsHidden2[1][1]=64;

	// Output layer
	net->weightsOutput[0]=20;
	net->weightsOutput[1]=-20;

	return net;
}

Net *netNewPST(void) {
	// Create 'empty' net
	Net *net=netNew();

	// Input layer (accumulator)
	for(Sq kingSq=0; kingSq<64; ++kingSq) {
		for(Sq pieceSq=0; pieceSq<64; ++pieceSq) {
			unsigned i=pieceSq;
			int d=2;

			// White
			net->weightsAccum[kingSq][NetPieceWPawn][pieceSq][i]=evalPST[PieceWPawn][pieceSq].mg/d;
			net->weightsAccum[kingSq][NetPieceWKnight][pieceSq][i]=evalPST[PieceWKnight][pieceSq].mg/d;
			net->weightsAccum[kingSq][NetPieceWBishop][pieceSq][i]=evalPST[PieceWBishopL][pieceSq].mg/d;
			net->weightsAccum[kingSq][NetPieceWRook][pieceSq][i]=evalPST[PieceWRook][pieceSq].mg/d;
			net->weightsAccum[kingSq][NetPieceWQueen][pieceSq][i]=evalPST[PieceWQueen][pieceSq].mg/d;

			// Black
			net->weightsAccum[kingSq][NetPieceBPawn][pieceSq][i]=evalPST[PieceBPawn][pieceSq].mg/d;
			net->weightsAccum[kingSq][NetPieceBKnight][pieceSq][i]=evalPST[PieceBKnight][pieceSq].mg/d;
			net->weightsAccum[kingSq][NetPieceBBishop][pieceSq][i]=evalPST[PieceBBishopL][pieceSq].mg/d;
			net->weightsAccum[kingSq][NetPieceBRook][pieceSq][i]=evalPST[PieceBRook][pieceSq].mg/d;
			net->weightsAccum[kingSq][NetPieceBQueen][pieceSq][i]=evalPST[PieceBQueen][pieceSq].mg/d;

			// NetPiece has single colour-agnostic king as we transform the opponents king to always be black (the friendly king is encoded in kingSq)
			net->weightsAccum[kingSq][NetPieceKing][pieceSq][i]=evalPST[PieceBKing][pieceSq].mg/d;
		}
	}

	// Hidden layer 1
	for(unsigned i=0; i<64; ++i) {
		net->weightsHidden1[i/4][i]=32;
		net->weightsHidden1[i/4+16][i+256]=32;
	}

	// Hidden layer 2
	for(unsigned i=0; i<16; ++i) {
		net->weightsHidden2[i][i]=64;
		net->weightsHidden2[i+16][i+16]=64;
	}

	// Output layer
	for(unsigned i=0; i<16; ++i) {
		net->weightsOutput[i]=20;
		net->weightsOutput[i+16]=-20;
	}

	return net;
}

Score netEvaluate(const Pos *pos) {
	assert(net!=NULL);
	assert(pos!=NULL);

	return netEvaluateNet(pos, net);
}

Score netEvaluateNet(const Pos *pos, const Net *net) {
	assert(pos!=NULL);
	assert(net!=NULL);

	// Pass cached accumulator values into netEvaluateNetAccum to do the heavy lifting
	return netEvaluateNetAccum(posGetSTM(pos), net, posGetNetAccum(pos), false);
}

Score netEvaluateNetAccum(Colour stm, const Net *net, const NetAccumulator *accum, bool verbose) {
	assert(net!=NULL);
	assert(accum!=NULL);

	// Debugging
	if (verbose) {
		printf("Accumulator:\n");
		netTuneLayerDebug16(accum->values[ColourWhite], 256);
		netTuneLayerDebug16(accum->values[ColourBlack], 256);
	}

	// Transform step (encode stm, reduce, make 8 bit, clamping to [0, 127])
	Colour xtm=colourSwap(stm);

	int8_t inputLayer[512];
	for(unsigned i=0; i<256; ++i) {
		inputLayer[i]=netCReLU(accum->values[stm][i]);
		inputLayer[i+256]=netCReLU(accum->values[xtm][i]);
	}

	// Debugging
	if (verbose) {
		printf("Input layer:\n");
		netTuneLayerDebug8(inputLayer, 512);
	}

	// First hidden layer
	int32_t hiddenLayer1[32];
	memcpy(hiddenLayer1, net->biasHidden1, sizeof(hiddenLayer1));

	for(unsigned i=0; i<32; ++i)
		for(unsigned j=0; j<512; ++j)
			hiddenLayer1[i]+=netMul(inputLayer[j], net->weightsHidden1[i][j]);

	for(unsigned i=0; i<32; ++i)
		hiddenLayer1[i]=netCReLU(hiddenLayer1[i]);

	// Debugging
	if (verbose) {
		printf("Hidden layer 1:\n");
		netTuneLayerDebug32(hiddenLayer1, 32);
	}

	// Second hidden layer
	int32_t hiddenLayer2[32];
	memcpy(hiddenLayer2, net->biasHidden2, sizeof(hiddenLayer2));

	for(unsigned i=0; i<32; ++i)
		for(unsigned j=0; j<32; ++j)
			hiddenLayer2[i]+=netMul(hiddenLayer1[j], net->weightsHidden2[i][j]);

	for(unsigned i=0; i<32; ++i)
		hiddenLayer2[i]=netCReLU(hiddenLayer2[i]);

	// Debugging
	if (verbose) {
		printf("Hidden layer 2:\n");
		netTuneLayerDebug32(hiddenLayer2, 32);
	}

	// Output layer
	int32_t outputLayer=net->biasOutput;
	for(unsigned i=0; i<32; ++i)
		outputLayer+=netMul(hiddenLayer2[i], net->weightsOutput[i]);

	// Debugging
	if (verbose)
		printf("Output layer: %lli\n", (long long int)outputLayer);

	return outputLayer;
}

void netAccumulatorCalc(const Net *net, const Pos *pos, NetAccumulator *accum) {
	assert(net!=NULL);
	assert(pos!=NULL);
	assert(accum!=NULL);

	// Input layer (accumulators)
	memcpy(accum->values[ColourWhite], net->biasAccum, 256*sizeof(int16_t));
	memcpy(accum->values[ColourBlack], net->biasAccum, 256*sizeof(int16_t));

	for(Sq sq=0; sq<SqNB; ++sq) {
		Piece p=posGetPieceOnSq(pos, sq);
		if (p!=PieceNone)
			netAccumulatorAdd(accum, net, pos, sq, p);
	}
}

bool netAccumulatorIsEqual(const NetAccumulator *a, const NetAccumulator *b) {
	assert(a!=NULL);
	assert(b!=NULL);

	return (memcmp(a->values, b->values, sizeof(a->values))==0);
}

void netAccumulatorCopy(NetAccumulator *dest, const NetAccumulator *src) {
	assert(dest!=NULL);
	assert(src!=NULL);

	memcpy(dest->values, src->values, sizeof(dest->values));
}

void netAccumulatorDebug(const NetAccumulator *accum) {
	assert(accum!=NULL);

	netTuneLayerDebug16(accum->values[ColourWhite], 256);
	netTuneLayerDebug16(accum->values[ColourBlack], 256);
}

void netAccumulatorAdd(NetAccumulator *accum, const Net *net, const Pos *pos, Sq sq, Piece piece) {
	assert(accum!=NULL);
	assert(net!=NULL);
	assert(pos!=NULL);
	assert(piece!=PieceNone);

	Sq kingSqW=posGetKingSq(pos, ColourWhite);
	Sq kingSqB=posGetKingSq(pos, ColourBlack);

	netAccumulatorAddRaw(accum, net, kingSqW, kingSqB, sq, piece);
}

void netAccumulatorRemove(NetAccumulator *accum, const Net *net, const Pos *pos, Sq sq, Piece piece) {
	assert(accum!=NULL);
	assert(net!=NULL);
	assert(pos!=NULL);
	assert(piece!=PieceNone);

	Sq kingSqW=posGetKingSq(pos, ColourWhite);
	Sq kingSqB=posGetKingSq(pos, ColourBlack);

	Piece netP=netPieceFromPiece(piece);

	if (sq!=kingSqW)
		for(unsigned i=0; i<256; ++i)
			accum->values[ColourWhite][i]-=net->weightsAccum[kingSqW][netP][sq][i];

	if (sq!=kingSqB)
		for(unsigned i=0; i<256; ++i)
			accum->values[ColourBlack][i]-=net->weightsAccum[sqFlip(kingSqB)][netPieceSwapColour(netP)][sqFlip(sq)][i];
}

void netAccumulatorMove(NetAccumulator *accum, const Net *net, const Pos *pos, Sq fromSq, Piece fromPiece, Sq toSq, Piece toPiece) {
	assert(accum!=NULL);
	assert(net!=NULL);
	assert(pos!=NULL);
	assert(fromPiece!=PieceNone);
	assert(toPiece!=PieceNone);

	netAccumulatorRemove(accum, net, pos, fromSq, fromPiece);
	netAccumulatorAdd(accum, net, pos, toSq, toPiece);
}

NetTunePositions *netTunePositionsNew(size_t count) {
	// Allocate memory
	NetTunePositions *positions=malloc(sizeof(NetTunePositions));
	NetTunePosition *array=(count>0 ? malloc(sizeof(NetTunePosition)*count) : NULL);
	if (positions==NULL || (count>0 && array==NULL))
		return NULL;

	// Set fields
	positions->array=array;
	positions->next=0;
	positions->size=count;

	return positions;
}

void netTunePositionsFree(NetTunePositions *positions) {
	// NULL check
	if (positions==NULL)
		return;

	// Free memory
	free(positions->array);
	free(positions);
}

NetTunePositions *netTunePositionsLoadEpd(const char *path) {
	// Init variables
	NetTunePositions *positions=NULL;
	FILE *file=NULL;
	Pos *pos=NULL;

	// Open EPD file for reading
	file=fopen(path, "r");
	if (file==NULL)
		goto error;

	// Allocate NetTunePositions object
	unsigned lineCount=utilFileCountLines(file); // do an initial pass over the file to count the number of lines so we can allocate our positions object all at once
	positions=netTunePositionsNew(lineCount);
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

		NetTuneScore result=NetTuneScoreInvalid;
		if (strcmp(resultStr, "1-0")==0)
			result=NetTuneScoreWhiteWin;
		else if (strcmp(resultStr, "1/2-1/2")==0)
			result=NetTuneScoreDraw;
		else if (strcmp(resultStr, "0-1")==0)
			result=NetTuneScoreBlackWin;
		else
			continue;

		// Setup position
		if (!posSetToFEN(pos, fenStr))
			continue;

		// Add position to array
		assert(positions->next<positions->size);

		netTunePositionSetFromPos(&positions->array[positions->next], pos);
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
	netTunePositionsFree(positions);
	return NULL;
}

////////////////////////////////////////////////////////////////////////////////
// Private functions
////////////////////////////////////////////////////////////////////////////////

void netAccumulatorAddRaw(NetAccumulator *accum, const Net *net, Sq kingSqW, Sq kingSqB, Sq sq, Piece piece) {
	assert(accum!=NULL);
	assert(net!=NULL);

	Piece netP=netPieceFromPiece(piece);

	if (sq!=kingSqW)
		for(unsigned i=0; i<256; ++i)
			accum->values[ColourWhite][i]+=net->weightsAccum[kingSqW][netP][sq][i];

	if (sq!=kingSqB)
		for(unsigned i=0; i<256; ++i)
			accum->values[ColourBlack][i]+=net->weightsAccum[sqFlip(kingSqB)][netPieceSwapColour(netP)][sqFlip(sq)][i];
}

NetPiece netPieceFromPiece(Piece p) {
	switch(p) {
		case PieceNone:
			assert(false);
			return NetPieceNB;
		break;
		case PieceWPawn:
			return NetPieceWPawn;
		break;
		case PieceWKnight:
			return NetPieceWKnight;
		break;
		case PieceWBishopL:
		case PieceWBishopD:
			return NetPieceWBishop;
		break;
		case PieceWRook:
			return NetPieceWRook;
		break;
		case PieceWQueen:
			return NetPieceWQueen;
		break;
		case PieceWKing:
			return NetPieceKing;
		break;
		case PieceBPawn:
			return NetPieceBPawn;
		break;
		case PieceBKnight:
			return NetPieceBKnight;
		break;
		case PieceBBishopL:
		case PieceBBishopD:
			return NetPieceBBishop;
		break;
		case PieceBRook:
			return NetPieceBRook;
		break;
		case PieceBQueen:
			return NetPieceBQueen;
		break;
		case PieceBKing:
			return NetPieceKing;
		break;
		case PieceNB:
			assert(false);
			return NetPieceNB;
		break;
	}

	assert(false);
	return NetPieceNB;
}

NetPiece netPieceSwapColour(NetPiece p) {
	assert(p<NetPieceNB);

	if (p==NetPieceKing)
		return NetPieceKing;

	return (p<NetPieceBPawn) ? (p-NetPieceWPawn+NetPieceBPawn) : (p-NetPieceBPawn+NetPieceWPawn);
}

int32_t netMul(int8_t a, int8_t b) {
	return ((int32_t)a)*((int32_t)b);
}

int32_t netCReLU(int32_t x) {
	x/=64;
	return (x<=0 ? 0 : (x<=127 ? x : 127));
}

const char *netTuneScoreToStr(NetTuneScore score) {
	assert(score<4);
	return NetTuneScoreStr[score];
}

void netTunePositionSetFromPos(NetTunePosition *position, const Pos *pos) {
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
	position->score=NetTuneScoreInvalid; // this will be set after by the caller
}

Piece netTunePositionGetPiece(const NetTunePosition *pos, unsigned n) {
	assert(pos!=NULL);
	assert(n<32);

	return ((pos->pieces[n/2]>>(n%2==0 ? 0 : 4)) & 0xF);
}

void netTunePositionDebug(const NetTunePosition *pos) {
	// Bitscan loop to find pieces
	Piece array[SqNB];
	assert(PieceNone==0);
	memset(array, 0, sizeof(array));

	unsigned i=0;
	BB occ=pos->occ;
	while(occ!=BBNone) {
		Sq sq=bbScanReset(&occ);
		Piece p=netTunePositionGetPiece(pos, i);
		assert(p!=PieceNone && p<PieceNB);

		array[sq]=p;

		++i;
	}

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
	printf("Result = %s\n", netTuneScoreToStr(pos->score));
}

void netTunePositionAccumulatorCalc(const Net *net, const NetTunePosition *pos, NetAccumulator *accum) {
	assert(net!=NULL);
	assert(pos!=NULL);
	assert(accum!=NULL);

	// Find kingSqW and kingSqB
	Sq kingSqW=SqInvalid;
	Sq kingSqB=SqInvalid;

	unsigned i=0;
	BB occ=pos->occ;
	while(occ!=BBNone) {
		Sq sq=bbScanReset(&occ);
		Piece p=netTunePositionGetPiece(pos, i);
		assert(p!=PieceNone && p<PieceNB);

		if (p==PieceWKing)
			kingSqW=sq;
		if (p==PieceBKing)
			kingSqB=sq;

		++i;
	}

	// Input layer (accumulators)
	memcpy(accum->values[ColourWhite], net->biasAccum, 256*sizeof(int16_t));
	memcpy(accum->values[ColourBlack], net->biasAccum, 256*sizeof(int16_t));

	i=0;
	occ=pos->occ;
	while(occ!=BBNone) {
		Sq sq=bbScanReset(&occ);
		Piece p=netTunePositionGetPiece(pos, i);
		assert(p!=PieceNone && p<PieceNB);

		netAccumulatorAddRaw(accum, net, kingSqW, kingSqB, sq, p);

		++i;
	}
}

void netTuneLayerDebug8(const int8_t *values, size_t count) {
	assert(values!=NULL);

	for(unsigned i=0; i<count; ++i) {
		if (i%16==0)
			printf("    ");
		printf("%4i ", values[i]);
		if (i%16==15)
			printf("\n");
	}
}

void netTuneLayerDebug16(const int16_t *values, size_t count) {
	assert(values!=NULL);

	for(unsigned i=0; i<count; ++i) {
		if (i%16==0)
			printf("    ");
		printf("%6i ", values[i]);
		if (i%16==15)
			printf("\n");
	}
}

void netTuneLayerDebug32(const int32_t *values, size_t count) {
	assert(values!=NULL);

	for(unsigned i=0; i<count; ++i) {
		if (i%16==0)
			printf("    ");
		printf("%8i ", values[i]);
		if (i%16==15)
			printf("\n");
	}
}
