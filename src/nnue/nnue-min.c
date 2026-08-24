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

const unsigned nnueSimdValuesPerVector=sizeof(SimdVector)/sizeof(int16_t);
const unsigned nnueSimdIterCount=NnueAccumulatorSize/nnueSimdValuesPerVector;

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
	_Alignas(SimdAlignment) int16_t weightsAccum[NnuePieceNB][SqNB][NnueAccumulatorSize]; // [Piece][PieceSq][index]
	_Alignas(SimdAlignment) int16_t biasAccum[NnueAccumulatorSize];

	_Alignas(SimdAlignment) int16_t weightsOutput[2*NnueAccumulatorSize];
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
	NnueNet *net=utilAlignedMalloc(sizeof(NnueNet), SimdAlignment);
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
	utilAlignedFree(net);
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

bool nnueAccumulatorCalcRequiredMakeMove(const Pos *pos, Move move) {
	assert(pos!=NULL);

	// Check for a king moving across the centre line
	if (moveGetToPieceType(move)==PieceTypeKing) {
		Sq fromSq=moveGetFromSq(move);
		Sq toSq=moveGetToSqRaw(move); // this isn't quite right in the case of castling but it won't affect the result of this function
		if ((sqFile(fromSq)<=FileD && sqFile(toSq)>=FileE) ||
		    (sqFile(fromSq)>=FileE && sqFile(toSq)<=FileD))
			return true;
	}

	return false;
}

void nnueAccumulatorCalc(const NnueNet *net, const Pos *pos, NnueAccumulator *accum) {
	assert(net!=NULL);
	assert(pos!=NULL);
	assert(accum!=NULL);

	// Input layer (accumulators)
	memcpy(accum->values[ColourWhite], net->biasAccum, NnueAccumulatorSize*sizeof(int16_t));
	memcpy(accum->values[ColourBlack], net->biasAccum, NnueAccumulatorSize*sizeof(int16_t));

	BB occ=posGetBBAll(pos);
	while(occ!=BBNone) {
		Sq sq=bbScanReset(&occ);
		Piece p=posGetPieceOnSq(pos, sq);
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

	const SimdVector *valuesW=(const SimdVector *)nnueAccumulatorGetFeatureWeights(net, kingSqW, ColourWhite, sq, nnueP);
	const SimdVector *valuesB=(const SimdVector *)nnueAccumulatorGetFeatureWeights(net, kingSqB, ColourBlack, sq, nnueP);
	SimdVector *accW=(SimdVector *)accum->values[ColourWhite];
	SimdVector *accB=(SimdVector *)accum->values[ColourBlack];

	for(unsigned i=0; i<nnueSimdIterCount; ++i) {
		accW[i]=simdAddEpi16(accW[i], valuesW[i]);
		accB[i]=simdAddEpi16(accB[i], valuesB[i]);
	}
}

void nnueAccumulatorRemove(NnueAccumulator *accum, const NnueNet *net, const Pos *pos, Sq sq, Piece piece) {
	assert(accum!=NULL);
	assert(net!=NULL);
	assert(pos!=NULL);
	assert(piece!=PieceNone);

	Sq kingSqW=posGetKingSq(pos, ColourWhite);
	Sq kingSqB=posGetKingSq(pos, ColourBlack);
	Piece nnueP=nnuePieceFromPiece(piece);

	const SimdVector *valuesW=(const SimdVector *)nnueAccumulatorGetFeatureWeights(net, kingSqW, ColourWhite, sq, nnueP);
	const SimdVector *valuesB=(const SimdVector *)nnueAccumulatorGetFeatureWeights(net, kingSqB, ColourBlack, sq, nnueP);
	SimdVector *accW=(SimdVector *)accum->values[ColourWhite];
	SimdVector *accB=(SimdVector *)accum->values[ColourBlack];

	for(unsigned i=0; i<nnueSimdIterCount; ++i) {
		accW[i]=simdSubEpi16(accW[i], valuesW[i]);
		accB[i]=simdSubEpi16(accB[i], valuesB[i]);
	}
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

	// Output layer (transform step is combined into this where we encode the stm and clamp the values)
	Colour xtm=colourSwap(stm);

	const SimdVector *valuesSTM=(const SimdVector *)accum->values[stm];
	const SimdVector *valuesXTM=(const SimdVector *)accum->values[xtm];
	const SimdVector *weightsSTM=(const SimdVector *)(net->weightsOutput);
	const SimdVector *weightsXTM=(const SimdVector *)(net->weightsOutput+NnueAccumulatorSize);

	const SimdVector vecMin=simdSetZero();
	const SimdVector vecMax=simdSetEpi16(nnueMinNetworkQA);

	SimdVector outputLayer=simdSetZero();

	for(unsigned i=0; i<nnueSimdIterCount; ++i) {
		SimdVector input, t;

		input=simdMinEpi16(simdMaxEpi16(valuesSTM[i], vecMin), vecMax);
		t=simdMulloEpi16(input, weightsSTM[i]);
		t=simdMAddEpi16(t, input);
		outputLayer=simdAddEpi32(outputLayer, t);

		input=simdMinEpi16(simdMaxEpi16(valuesXTM[i], vecMin), vecMax);
		t=simdMulloEpi16(input, weightsXTM[i]);
		t=simdMAddEpi16(t, input);
		outputLayer=simdAddEpi32(outputLayer, t);
	}

	int unsquared=simdHAddEpi32(outputLayer)/nnueMinNetworkQA+net->biasOutput;
	Score output=((unsquared*nnueMinNetworkScale)/(nnueMinNetworkQA*nnueMinNetworkQB));

	// Debugging
	if (verbose)
		printf("Output layer: %i\n", (int)output);

	return output;
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
