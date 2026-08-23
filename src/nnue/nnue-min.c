#ifdef NNUE_ARCH_MIN

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nnue-min.h"
#include "util.h"

const uint32_t nnueFileHeaderVersion=0x4F9E2B8C; // randomly generated - hopefully it doesn't clash with anything out in the wild
const uint32_t nnueFileHeaderHash=0xA3D7B29E; // use this as extra version bits instead of a hash for now

#define nnueMinNetworkScale 340
#define nnueMinNetworkQA 101
#define nnueMinNetworkQB 160

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

	int16_t weightsOutput[2*NnueAccumulatorSize];
	int16_t biasOutput;
};

////////////////////////////////////////////////////////////////////////////////
// Private prototypes
////////////////////////////////////////////////////////////////////////////////

const int16_t *nnueAccumulatorGetFeatureWeights(const NnueNet *net, Sq kingSq, Colour c, Sq sq, NnuePiece p);

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
	const char *desc="Even simpler Piece-Square feature set with a no hidden layer: (768->64)*2->1.";
	uint32_t descLen=strlen(desc);
	if (fwrite(&nnueFileHeaderVersion, sizeof(nnueFileHeaderVersion), 1, file)!=1 ||
	    fwrite(&nnueFileHeaderHash, sizeof(nnueFileHeaderHash), 1, file)!=1 ||
	    fwrite(&descLen, sizeof(descLen), 1, file)!=1 ||
	    fwrite(desc, descLen, 1, file)!=1)
		goto error;

	// Write data
	if (fwrite(net->weightsAccum, sizeof(net->weightsAccum), 1, file)!=1 ||
	    fwrite(net->biasAccum, sizeof(net->biasAccum), 1, file)!=1 ||
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

bool nnueNetFeatureSetIsSimple(void) {
	return false; // due to mirroring of POV king if on EGFH files
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
	const int16_t *values;
	Piece nnueP=nnuePieceFromPiece(piece);

	values=nnueAccumulatorGetFeatureWeights(net, kingSqW, ColourWhite, sq, nnueP);
	for(unsigned i=0; i<NnueAccumulatorSize; ++i)
		accum->values[ColourWhite][i]+=values[i];

	values=nnueAccumulatorGetFeatureWeights(net, kingSqB, ColourBlack, sq, nnueP);
	for(unsigned i=0; i<NnueAccumulatorSize; ++i)
		accum->values[ColourBlack][i]+=values[i];
}

void nnueAccumulatorRemove(NnueAccumulator *accum, const NnueNet *net, const Pos *pos, Sq sq, Piece piece) {
	assert(accum!=NULL);
	assert(net!=NULL);
	assert(pos!=NULL);
	assert(piece!=PieceNone);

	Sq kingSqW=posGetKingSq(pos, ColourWhite);
	Sq kingSqB=posGetKingSq(pos, ColourBlack);
	const int16_t *values;
	Piece nnueP=nnuePieceFromPiece(piece);

	values=nnueAccumulatorGetFeatureWeights(net, kingSqW, ColourWhite, sq, nnueP);
	for(unsigned i=0; i<NnueAccumulatorSize; ++i)
		accum->values[ColourWhite][i]-=values[i];

	values=nnueAccumulatorGetFeatureWeights(net, kingSqB, ColourBlack, sq, nnueP);
	for(unsigned i=0; i<NnueAccumulatorSize; ++i)
		accum->values[ColourBlack][i]-=values[i];
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

	int16_t inputLayer[2*NnueAccumulatorSize];
	for(unsigned i=0; i<NnueAccumulatorSize; ++i) {
		inputLayer[i]=nnueClamp(accum->values[stm][i], 0, nnueMinNetworkQA);
		inputLayer[i+NnueAccumulatorSize]=nnueClamp(accum->values[xtm][i], 0, nnueMinNetworkQA);
	}

	// Debugging
	if (verbose) {
		printf("Input layer:\n");
		nnueLayerDebug16(inputLayer, 2*NnueAccumulatorSize);
	}

	// Output layer
	int32_t outputLayer=0;
	for(unsigned i=0; i<2*NnueAccumulatorSize; ++i) {
		int16_t t=nnueMul(inputLayer[i], net->weightsOutput[i]); // important we throw away the high bits and only keep the lower 16
		outputLayer+=nnueMul(inputLayer[i], t);
	}

	int unsquared=outputLayer/nnueMinNetworkQA+net->biasOutput;
	outputLayer=((unsquared*nnueMinNetworkScale)/(nnueMinNetworkQA*nnueMinNetworkQB));

	// Debugging
	if (verbose)
		printf("Output layer: %i\n", (int)outputLayer);

	return outputLayer;
}

////////////////////////////////////////////////////////////////////////////////
// Private functions
////////////////////////////////////////////////////////////////////////////////

const int16_t *nnueAccumulatorGetFeatureWeights(const NnueNet *net, Sq kingSq, Colour c, Sq sq, NnuePiece p) {
	assert(net!=NULL);

	// Mirror if friendly king is on right half of the board (EFGH files)
	if (sqFile(kingSq)>FileD)
		sq=sqMirror(sq);

	// Adjust so always from white's POV
	if (c==ColourBlack) {
		p=nnuePieceSwapColour(p);
		sq=sqFlip(sq);
	}

	// Return array of weights
	return net->weightsAccum[p][sq];
}

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

#endif
