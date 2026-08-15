#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nnue-ps.h"
#include "util.h"

#define NnueLayerHiddenSize 16

typedef enum {
	// This differs from the standard Piece definition in a few ways:
	// * There is no None member
	// * No differentiation between light/dark square bishops
	NnuePieceWPawn,
	NnuePieceWKnight,
	NnuePieceWBishop,
	NnuePieceWRook,
	NnuePieceWQueen,
	NnuePieceWKing,
	NnuePieceBPawn,
	NnuePieceBKnight,
	NnuePieceBBishop,
	NnuePieceBRook,
	NnuePieceBQueen,
	NnuePieceBKing,
	NnuePieceNB,
} NnuePiece;

struct NnueNet {
	int16_t weightsAccum[NnuePieceNB][SqNB][NnueAccumulatorSize]; // [Piece][PieceSq][index]
	int16_t biasAccum[NnueAccumulatorSize];

	int8_t weightsHidden[NnueLayerHiddenSize][2*NnueAccumulatorSize];
	int32_t biasHidden[NnueLayerHiddenSize];

	int8_t weightsOutput[NnueLayerHiddenSize];
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

	// Read data
	if (fread(net->weightsAccum, sizeof(net->weightsAccum), 1, file)!=1 ||
	    fread(net->biasAccum, sizeof(net->biasAccum), 1, file)!=1 ||
	    fread(net->weightsHidden, sizeof(net->weightsHidden), 1, file)!=1 ||
	    fread(net->biasHidden, sizeof(net->biasHidden), 1, file)!=1 ||
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

	// Write data
	if (fwrite(net->weightsAccum, sizeof(net->weightsAccum), 1, file)!=1 ||
	    fwrite(net->biasAccum, sizeof(net->biasAccum), 1, file)!=1 ||
	    fwrite(net->weightsHidden, sizeof(net->weightsHidden), 1, file)!=1 ||
	    fwrite(net->biasHidden, sizeof(net->biasHidden), 1, file)!=1 ||
	    fwrite(net->weightsOutput, sizeof(net->weightsOutput), 1, file)!=1 ||
	    fwrite(&net->biasOutput, sizeof(net->biasOutput), 1, file)!=1) {
		fclose(file);
		return false;
	}

	// Close file
	fclose(file);

	return true;
}

NnueNet *nnueNetNewMaterial(void) {
	// Create 'empty' net
	NnueNet *net=nnueNetNew(NULL);

	// Input layer (accumulator)
	for(Sq pieceSq=0; pieceSq<64; ++pieceSq) {
		const int f=3*100;
		net->weightsAccum[NnuePieceWPawn][pieceSq][0]=1*f;
		net->weightsAccum[NnuePieceBPawn][pieceSq][0]=-1*f;
		net->weightsAccum[NnuePieceWKnight][pieceSq][0]=3*f;
		net->weightsAccum[NnuePieceBKnight][pieceSq][0]=-3*f;
		net->weightsAccum[NnuePieceWBishop][pieceSq][0]=3*f;
		net->weightsAccum[NnuePieceBBishop][pieceSq][0]=-3*f;
		net->weightsAccum[NnuePieceWRook][pieceSq][0]=5*f;
		net->weightsAccum[NnuePieceBRook][pieceSq][0]=-5*f;
		net->weightsAccum[NnuePieceWQueen][pieceSq][0]=9*f;
		net->weightsAccum[NnuePieceBQueen][pieceSq][0]=-9*f;
	}

	// Hidden layer
	net->weightsHidden[0][0]=64;
	net->weightsHidden[1][NnueAccumulatorSize]=64;

	// Output layer
	net->weightsOutput[0]=20;
	net->weightsOutput[1]=-20;

	return net;
}

NnueNet *nnueNetNewPST(void) {
	// Create 'empty' net
	NnueNet *net=nnueNetNew(NULL);

	// Input layer (accumulator)
	for(Sq pieceSq=0; pieceSq<SqNB; ++pieceSq) {
		unsigned i=pieceSq;
		int d=2;

		// White
		net->weightsAccum[NnuePieceWPawn][pieceSq][i]=evalPST[PieceWPawn][pieceSq].mg/d;
		net->weightsAccum[NnuePieceWKnight][pieceSq][i]=evalPST[PieceWKnight][pieceSq].mg/d;
		net->weightsAccum[NnuePieceWBishop][pieceSq][i]=evalPST[PieceWBishopL][pieceSq].mg/d;
		net->weightsAccum[NnuePieceWRook][pieceSq][i]=evalPST[PieceWRook][pieceSq].mg/d;
		net->weightsAccum[NnuePieceWQueen][pieceSq][i]=evalPST[PieceWQueen][pieceSq].mg/d;
		net->weightsAccum[NnuePieceWKing][pieceSq][i]=evalPST[PieceWKing][pieceSq].mg/d;

		// Black
		net->weightsAccum[NnuePieceBPawn][pieceSq][i]=evalPST[PieceBPawn][pieceSq].mg/d;
		net->weightsAccum[NnuePieceBKnight][pieceSq][i]=evalPST[PieceBKnight][pieceSq].mg/d;
		net->weightsAccum[NnuePieceBBishop][pieceSq][i]=evalPST[PieceBBishopL][pieceSq].mg/d;
		net->weightsAccum[NnuePieceBRook][pieceSq][i]=evalPST[PieceBRook][pieceSq].mg/d;
		net->weightsAccum[NnuePieceBQueen][pieceSq][i]=evalPST[PieceBQueen][pieceSq].mg/d;
		net->weightsAccum[NnuePieceBKing][pieceSq][i]=evalPST[PieceBKing][pieceSq].mg/d;
	}

	// Hidden layer
	assert(SqNB<=NnueAccumulatorSize);
	for(unsigned i=0; i<SqNB; ++i) {
		net->weightsHidden[i/(2*SqNB/NnueLayerHiddenSize)][i]=32;
		net->weightsHidden[i/(2*SqNB/NnueLayerHiddenSize)+NnueLayerHiddenSize/2][i+NnueAccumulatorSize]=32;
	}

	// Output layer
	for(unsigned i=0; i<NnueLayerHiddenSize/2; ++i) {
		net->weightsOutput[i]=20;
		net->weightsOutput[i+NnueLayerHiddenSize/2]=-20;
	}

	return net;
}

bool nnueNetFeatureSetIsSimple(void) {
	return true;
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

	Piece nnueP=nnuePieceFromPiece(piece);

	for(unsigned i=0; i<NnueAccumulatorSize; ++i)
		accum->values[ColourWhite][i]+=net->weightsAccum[nnueP][sq][i];

	for(unsigned i=0; i<NnueAccumulatorSize; ++i)
		accum->values[ColourBlack][i]+=net->weightsAccum[nnuePieceSwapColour(nnueP)][sqFlip(sq)][i];
}

void nnueAccumulatorRemove(NnueAccumulator *accum, const NnueNet *net, const Pos *pos, Sq sq, Piece piece) {
	assert(accum!=NULL);
	assert(net!=NULL);
	assert(pos!=NULL);
	assert(piece!=PieceNone);

	Piece nnueP=nnuePieceFromPiece(piece);

	for(unsigned i=0; i<NnueAccumulatorSize; ++i)
		accum->values[ColourWhite][i]-=net->weightsAccum[nnueP][sq][i];

	for(unsigned i=0; i<NnueAccumulatorSize; ++i)
		accum->values[ColourBlack][i]-=net->weightsAccum[nnuePieceSwapColour(nnueP)][sqFlip(sq)][i];
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

	// Hidden layer
	int32_t hiddenLayer[NnueLayerHiddenSize];
	memcpy(hiddenLayer, net->biasHidden, sizeof(hiddenLayer));

	for(unsigned i=0; i<NnueLayerHiddenSize; ++i)
		for(unsigned j=0; j<2*NnueAccumulatorSize; ++j)
			hiddenLayer[i]+=nnueMul(inputLayer[j], net->weightsHidden[i][j]);

	for(unsigned i=0; i<NnueLayerHiddenSize; ++i)
		hiddenLayer[i]=nnueCReLU(hiddenLayer[i]);

	// Debugging
	if (verbose) {
		printf("Hidden layer:\n");
		nnueLayerDebug32(hiddenLayer, NnueLayerHiddenSize);
	}

	// Output layer
	int32_t outputLayer=net->biasOutput;
	for(unsigned i=0; i<NnueLayerHiddenSize; ++i)
		outputLayer+=nnueMul(hiddenLayer[i], net->weightsOutput[i]);

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
			return NnuePieceWKing;
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
			return NnuePieceBKing;
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

	return (p<NnuePieceBPawn) ? (p-NnuePieceWPawn+NnuePieceBPawn) : (p-NnuePieceBPawn+NnuePieceWPawn);
}
