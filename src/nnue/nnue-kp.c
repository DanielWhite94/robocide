#ifdef NNUE_ARCH_KP

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nnue-kp.h"
#include "util.h"

const uint32_t nnueFileHeaderVersion=0xF4B8E1D9; // randomly generated - hopefully it doesn't clash with anything out in the wild
const uint32_t nnueFileHeaderHash=0xF4B8E1D9; // use this as extra version bits instead of a hash for now

#define NnueLayerHidden1Size 32
#define NnueLayerHidden2Size 32

typedef enum {
	// This differs from the standard Piece definition in a few ways:
	// * There is no None member
	// * No differentiation between light/dark square bishops
	// * Single (non-coloured) king (would be redundant having both as friendly king position is already encoded in the input layer)
	NnuePieceWPawn,
	NnuePieceWKnight,
	NnuePieceWBishop,
	NnuePieceWRook,
	NnuePieceWQueen,
	NnuePieceBPawn,
	NnuePieceBKnight,
	NnuePieceBBishop,
	NnuePieceBRook,
	NnuePieceBQueen,
	NnuePieceKing,
	NnuePieceNB,
} NnuePiece;

struct NnueNet {
	int16_t weightsAccum[SqNB][NnuePieceNB][SqNB][NnueAccumulatorSize]; // [KingSq][Piece][PieceSq][index]
	int16_t biasAccum[NnueAccumulatorSize];

	int8_t weightsHidden1[NnueLayerHidden1Size][2*NnueAccumulatorSize];
	int32_t biasHidden1[NnueLayerHidden1Size];

	int8_t weightsHidden2[NnueLayerHidden2Size][NnueLayerHidden1Size];
	int32_t biasHidden2[NnueLayerHidden2Size];

	int8_t weightsOutput[NnueLayerHidden2Size];
	int32_t biasOutput;
};

////////////////////////////////////////////////////////////////////////////////
// Private prototypes
////////////////////////////////////////////////////////////////////////////////

NnuePiece nnuePieceFromPiece(Piece p);
NnuePiece nnuePieceSwapColour(NnuePiece p);

////////////////////////////////////////////////////////////////////////////////
// Public functions
////////////////////////////////////////////////////////////////////////////////

NnueNet *nnueNetNew(const NnueNet *src) {
	// Allocate memory
	NnueNet *net=malloc(sizeof(NnueNet));
	if (net==NULL)
		return NULL;

	// If given a source net copy it, otherwise initialise all values to zero
	if (src!=NULL)
		memcpy(net, src, sizeof(NnueNet));
	else
		memset(net, 0, sizeof(NnueNet));

	return net;
}

void nnueNetFree(NnueNet *net) {
	// NULL check
	if (net==NULL)
		return;

	// Free memory
	free(net);
}

NnueNet *nnueNetLoad(const char *path) {
	assert(path!=NULL);

	// Init
	NnueNet *net=NULL;
	FILE *file=NULL;

	// Allocate network
	net=nnueNetNew(NULL);
	if (net==NULL)
		goto error;

	// Open file
	file=fopen(path, "r");
	if (file==NULL)
		goto error;

	// Read header
	uint32_t fileHeaderVersion;
	uint32_t fileHeaderHash;
	uint32_t descLen;
	if (fread(&fileHeaderVersion, sizeof(fileHeaderVersion), 1, file)!=1 ||
	    fread(&fileHeaderHash, sizeof(fileHeaderHash), 1, file)!=1 ||
	    fread(&descLen, sizeof(descLen), 1, file)!=1)
		goto error;

	// Verify header version and hash
	if (fileHeaderVersion!=nnueFileHeaderVersion || fileHeaderHash!=nnueFileHeaderHash)
		goto error;

	// Skip past description string
	char temp[64];
	while(descLen>0) {
		size_t readCount=fread(temp, 1, (descLen<64 ? descLen : 64), file);
		if (readCount==0)
			goto error;
		descLen-=readCount;
	}

	// Read data
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
	if (file!=NULL)
		fclose(file);
	free(net);
	return NULL;
}

bool nnueNetSave(const NnueNet *net, const char *path) {
	assert(net!=NULL);
	assert(path!=NULL);

	// Open file
	FILE *file=fopen(path, "w");
	if (file==NULL)
		return false;

	// Write header
	const char *desc="Stockfish HalfKP style feature set: 45056->256*2->32->32->1.";
	uint32_t descLen=strlen(desc);
	if (fwrite(&nnueFileHeaderVersion, sizeof(nnueFileHeaderVersion), 1, file)!=1 ||
	    fwrite(&nnueFileHeaderHash, sizeof(nnueFileHeaderHash), 1, file)!=1 ||
	    fwrite(&descLen, sizeof(descLen), 1, file)!=1 ||
	    fwrite(desc, descLen, 1, file)!=1)
		goto error;

	// Write data
	if (fwrite(net->weightsAccum, sizeof(net->weightsAccum), 1, file)!=1 ||
	    fwrite(net->biasAccum, sizeof(net->biasAccum), 1, file)!=1 ||
	    fwrite(net->weightsHidden1, sizeof(net->weightsHidden1), 1, file)!=1 ||
	    fwrite(net->biasHidden1, sizeof(net->biasHidden1), 1, file)!=1 ||
	    fwrite(net->weightsHidden2, sizeof(net->weightsHidden2), 1, file)!=1 ||
	    fwrite(net->biasHidden2, sizeof(net->biasHidden2), 1, file)!=1 ||
	    fwrite(net->weightsOutput, sizeof(net->weightsOutput), 1, file)!=1 ||
	    fwrite(&net->biasOutput, sizeof(net->biasOutput), 1, file)!=1)
		goto error;

	// Close file
	fclose(file);

	return true;
	error:
	fclose(file);
	return false;
}

NnueNet *nnueNetNewMaterial(void) {
	// Create 'empty' net
	NnueNet *net=nnueNetNew(NULL);

	// Input layer (accumulator)
	for(Sq kingSq=0; kingSq<SqNB; ++kingSq) {
		for(Sq pieceSq=0; pieceSq<SqNB; ++pieceSq) {
			const int f=3*100;
			net->weightsAccum[kingSq][NnuePieceWPawn][pieceSq][0]=1*f;
			net->weightsAccum[kingSq][NnuePieceBPawn][pieceSq][0]=-1*f;
			net->weightsAccum[kingSq][NnuePieceWKnight][pieceSq][0]=3*f;
			net->weightsAccum[kingSq][NnuePieceBKnight][pieceSq][0]=-3*f;
			net->weightsAccum[kingSq][NnuePieceWBishop][pieceSq][0]=3*f;
			net->weightsAccum[kingSq][NnuePieceBBishop][pieceSq][0]=-3*f;
			net->weightsAccum[kingSq][NnuePieceWRook][pieceSq][0]=5*f;
			net->weightsAccum[kingSq][NnuePieceBRook][pieceSq][0]=-5*f;
			net->weightsAccum[kingSq][NnuePieceWQueen][pieceSq][0]=9*f;
			net->weightsAccum[kingSq][NnuePieceBQueen][pieceSq][0]=-9*f;
		}
	}

	// Hidden layer 1
	net->weightsHidden1[0][0]=64;
	net->weightsHidden1[1][NnueAccumulatorSize]=64;

	// Hidden layer 2
	net->weightsHidden2[0][0]=64;
	net->weightsHidden2[1][1]=64;

	// Output layer
	net->weightsOutput[0]=20;
	net->weightsOutput[1]=-20;

	return net;
}

NnueNet *nnueNetNewPST(void) {
	// Create 'empty' net
	NnueNet *net=nnueNetNew(NULL);

	// Input layer (accumulator)
	for(Sq kingSq=0; kingSq<SqNB; ++kingSq) {
		for(Sq pieceSq=0; pieceSq<SqNB; ++pieceSq) {
			unsigned i=pieceSq;
			int d=2;

			// White
			net->weightsAccum[kingSq][NnuePieceWPawn][pieceSq][i]=evalPST[PieceWPawn][pieceSq].mg/d;
			net->weightsAccum[kingSq][NnuePieceWKnight][pieceSq][i]=evalPST[PieceWKnight][pieceSq].mg/d;
			net->weightsAccum[kingSq][NnuePieceWBishop][pieceSq][i]=evalPST[PieceWBishopL][pieceSq].mg/d;
			net->weightsAccum[kingSq][NnuePieceWRook][pieceSq][i]=evalPST[PieceWRook][pieceSq].mg/d;
			net->weightsAccum[kingSq][NnuePieceWQueen][pieceSq][i]=evalPST[PieceWQueen][pieceSq].mg/d;

			// Black
			net->weightsAccum[kingSq][NnuePieceBPawn][pieceSq][i]=evalPST[PieceBPawn][pieceSq].mg/d;
			net->weightsAccum[kingSq][NnuePieceBKnight][pieceSq][i]=evalPST[PieceBKnight][pieceSq].mg/d;
			net->weightsAccum[kingSq][NnuePieceBBishop][pieceSq][i]=evalPST[PieceBBishopL][pieceSq].mg/d;
			net->weightsAccum[kingSq][NnuePieceBRook][pieceSq][i]=evalPST[PieceBRook][pieceSq].mg/d;
			net->weightsAccum[kingSq][NnuePieceBQueen][pieceSq][i]=evalPST[PieceBQueen][pieceSq].mg/d;

			// King is always opponent king (see NnuePiece definition)
			net->weightsAccum[kingSq][NnuePieceKing][pieceSq][i]=evalPST[PieceBKing][pieceSq].mg/d;
		}
	}

	// Hidden layer 1
	assert(SqNB<=NnueAccumulatorSize);
	for(unsigned i=0; i<SqNB; ++i) {
		net->weightsHidden1[i/(2*SqNB/NnueLayerHidden1Size)][i]=32;
		net->weightsHidden1[i/(2*SqNB/NnueLayerHidden1Size)+NnueLayerHidden1Size/2][i+NnueAccumulatorSize]=32;
	}

	// Hidden layer 2
	assert(NnueLayerHidden1Size==NnueLayerHidden2Size);
	for(unsigned i=0; i<NnueLayerHidden2Size/2; ++i) {
		net->weightsHidden2[i][i]=64;
		net->weightsHidden2[i+NnueLayerHidden2Size/2][i+NnueLayerHidden2Size/2]=64;
	}

	// Output layer
	for(unsigned i=0; i<NnueLayerHidden2Size/2; ++i) {
		net->weightsOutput[i]=20;
		net->weightsOutput[i+NnueLayerHidden2Size/2]=-20;
	}

	return net;
}

bool nnueNetFeatureSetIsSimple(void) {
	return false; // features depend on king position
}

void nnueAccumulatorCalc(const NnueNet *net, const Pos *pos, NnueAccumulator *accum) {
	assert(net!=NULL);
	assert(pos!=NULL);
	assert(accum!=NULL);

	// Input layer (accumulators)
	memcpy(accum->values[ColourWhite], net->biasAccum, NnueAccumulatorSize*sizeof(int16_t));
	memcpy(accum->values[ColourBlack], net->biasAccum, NnueAccumulatorSize*sizeof(int16_t));

	for(Sq sq=0; sq<SqNB; ++sq) {
		Piece p=posGetPieceOnSq(pos, sq);
		if (p!=PieceNone)
			nnueAccumulatorAdd(accum, net, pos, sq, p);
	}
}

bool nnueAccumulatorIsEqual(const NnueAccumulator *a, const NnueAccumulator *b) {
	assert(a!=NULL);
	assert(b!=NULL);

	return (memcmp(a->values, b->values, sizeof(a->values))==0);
}

void nnueAccumulatorCopy(NnueAccumulator *dest, const NnueAccumulator *src) {
	assert(dest!=NULL);
	assert(src!=NULL);

	memcpy(dest->values, src->values, sizeof(dest->values));
}
void nnueAccumulatorDebug(const NnueAccumulator *accum) {
	assert(accum!=NULL);

	nnueLayerDebug16(accum->values[ColourWhite], NnueAccumulatorSize);
	nnueLayerDebug16(accum->values[ColourBlack], NnueAccumulatorSize);
}

void nnueAccumulatorAdd(NnueAccumulator *accum, const NnueNet *net, const Pos *pos, Sq sq, Piece piece) {
	assert(accum!=NULL);
	assert(net!=NULL);
	assert(pos!=NULL);
	assert(piece!=PieceNone);

	Sq kingSqW=posGetKingSq(pos, ColourWhite);
	Sq kingSqB=posGetKingSq(pos, ColourBlack);
	Piece nnueP=nnuePieceFromPiece(piece);

	if (sq!=kingSqW)
		for(unsigned i=0; i<NnueAccumulatorSize; ++i)
			accum->values[ColourWhite][i]+=net->weightsAccum[kingSqW][nnueP][sq][i];

	if (sq!=kingSqB)
		for(unsigned i=0; i<NnueAccumulatorSize; ++i)
			accum->values[ColourBlack][i]+=net->weightsAccum[sqFlip(kingSqB)][nnuePieceSwapColour(nnueP)][sqFlip(sq)][i];
}

void nnueAccumulatorRemove(NnueAccumulator *accum, const NnueNet *net, const Pos *pos, Sq sq, Piece piece) {
	assert(accum!=NULL);
	assert(net!=NULL);
	assert(pos!=NULL);
	assert(piece!=PieceNone);

	Sq kingSqW=posGetKingSq(pos, ColourWhite);
	Sq kingSqB=posGetKingSq(pos, ColourBlack);
	Piece nnueP=nnuePieceFromPiece(piece);

	if (sq!=kingSqW)
		for(unsigned i=0; i<NnueAccumulatorSize; ++i)
			accum->values[ColourWhite][i]-=net->weightsAccum[kingSqW][nnueP][sq][i];

	if (sq!=kingSqB)
		for(unsigned i=0; i<NnueAccumulatorSize; ++i)
			accum->values[ColourBlack][i]-=net->weightsAccum[sqFlip(kingSqB)][nnuePieceSwapColour(nnueP)][sqFlip(sq)][i];
}

void nnueAccumulatorMove(NnueAccumulator *accum, const NnueNet *net, const Pos *pos, Sq fromSq, Piece fromPiece, Sq toSq, Piece toPiece) {
	assert(accum!=NULL);
	assert(net!=NULL);
	assert(pos!=NULL);
	assert(fromPiece!=PieceNone);
	assert(toPiece!=PieceNone);

	nnueAccumulatorRemove(accum, net, pos, fromSq, fromPiece);
	nnueAccumulatorAdd(accum, net, pos, toSq, toPiece);
}

Score nnueEvaluateNetAccum(Colour stm, const NnueNet *net, const NnueAccumulator *accum, bool verbose) {
	assert(net!=NULL);
	assert(accum!=NULL);

	// Debugging
	if (verbose) {
		printf("Accumulator:\n");
		nnueLayerDebug16(accum->values[ColourWhite], NnueAccumulatorSize);
		nnueLayerDebug16(accum->values[ColourBlack], NnueAccumulatorSize);
	}

	// Transform step (encode stm, reduce, make 8 bit, clamping to [0, 127])
	Colour xtm=colourSwap(stm);

	int8_t inputLayer[2*NnueAccumulatorSize];
	for(unsigned i=0; i<NnueAccumulatorSize; ++i) {
		inputLayer[i]=nnueCReLU(accum->values[stm][i]);
		inputLayer[i+NnueAccumulatorSize]=nnueCReLU(accum->values[xtm][i]);
	}

	// Debugging
	if (verbose) {
		printf("Input layer:\n");
		nnueLayerDebug8(inputLayer, 2*NnueAccumulatorSize);
	}

	// First hidden layer
	int32_t hiddenLayer1[NnueLayerHidden1Size];
	memcpy(hiddenLayer1, net->biasHidden1, sizeof(hiddenLayer1));

	for(unsigned i=0; i<NnueLayerHidden1Size; ++i)
		for(unsigned j=0; j<2*NnueAccumulatorSize; ++j)
			hiddenLayer1[i]+=nnueMul(inputLayer[j], net->weightsHidden1[i][j]);

	for(unsigned i=0; i<NnueLayerHidden1Size; ++i)
		hiddenLayer1[i]=nnueCReLU(hiddenLayer1[i]);

	// Debugging
	if (verbose) {
		printf("Hidden layer 1:\n");
		nnueLayerDebug32(hiddenLayer1, NnueLayerHidden1Size);
	}

	// Second hidden layer
	int32_t hiddenLayer2[NnueLayerHidden2Size];
	memcpy(hiddenLayer2, net->biasHidden2, sizeof(hiddenLayer2));

	for(unsigned i=0; i<NnueLayerHidden2Size; ++i)
		for(unsigned j=0; j<NnueLayerHidden1Size; ++j)
			hiddenLayer2[i]+=nnueMul(hiddenLayer1[j], net->weightsHidden2[i][j]);

	for(unsigned i=0; i<NnueLayerHidden2Size; ++i)
		hiddenLayer2[i]=nnueCReLU(hiddenLayer2[i]);

	// Debugging
	if (verbose) {
		printf("Hidden layer 2:\n");
		nnueLayerDebug32(hiddenLayer2, NnueLayerHidden2Size);
	}

	// Output layer
	int32_t outputLayer=net->biasOutput;
	for(unsigned i=0; i<NnueLayerHidden2Size; ++i)
		outputLayer+=nnueMul(hiddenLayer2[i], net->weightsOutput[i]);

	// Debugging
	if (verbose)
		printf("Output layer: %lli\n", (long long int)outputLayer);

	return outputLayer;
}

////////////////////////////////////////////////////////////////////////////////
// Private functions
////////////////////////////////////////////////////////////////////////////////

NnuePiece nnuePieceFromPiece(Piece p) {
	switch(p) {
		case PieceNone:
			assert(false);
			return NnuePieceNB;
		break;
		case PieceWPawn:
			return NnuePieceWPawn;
		break;
		case PieceWKnight:
			return NnuePieceWKnight;
		break;
		case PieceWBishopL:
		case PieceWBishopD:
			return NnuePieceWBishop;
		break;
		case PieceWRook:
			return NnuePieceWRook;
		break;
		case PieceWQueen:
			return NnuePieceWQueen;
		break;
		case PieceWKing:
			return NnuePieceKing;
		break;
		case PieceBPawn:
			return NnuePieceBPawn;
		break;
		case PieceBKnight:
			return NnuePieceBKnight;
		break;
		case PieceBBishopL:
		case PieceBBishopD:
			return NnuePieceBBishop;
		break;
		case PieceBRook:
			return NnuePieceBRook;
		break;
		case PieceBQueen:
			return NnuePieceBQueen;
		break;
		case PieceBKing:
			return NnuePieceKing;
		break;
		case PieceNB:
			assert(false);
			return NnuePieceNB;
		break;
	}

	assert(false);
	return NnuePieceNB;
}

NnuePiece nnuePieceSwapColour(NnuePiece p) {
	assert(p<NnuePieceNB);

	if (p==NnuePieceKing)
		return NnuePieceKing;

	return (p<NnuePieceBPawn) ? (p-NnuePieceWPawn+NnuePieceBPawn) : (p-NnuePieceBPawn+NnuePieceWPawn);
}

#endif
