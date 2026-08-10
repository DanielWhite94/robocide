#include <assert.h>
#include <string.h>

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

////////////////////////////////////////////////////////////////////////////////
// Private prototypes
////////////////////////////////////////////////////////////////////////////////

NetPiece netPieceFromPiece(Piece p); // expects an actual piece (not PieceNone)
NetPiece netPieceSwapColour(NetPiece p); // leaves NetPieceKing unchanged

int32_t netMul(int8_t a, int8_t b); // exists to avoid bugs where variables are not cast to a larger type before a multiplication

////////////////////////////////////////////////////////////////////////////////
// Public functions
////////////////////////////////////////////////////////////////////////////////

void netInit(void) {
}

void netQuit(void) {
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

Score netEvaluateNet(const Pos *pos, const Net *net) {
	assert(pos!=NULL);
	assert(net!=NULL);

	// Input layer (accumulators)
	NetAccumulator accum;
	netAccumulatorCalc(net, pos, &accum);

	// Rest of the network
	return netEvaluateNetAccum(pos, net, &accum);
}


Score netEvaluateNetAccum(const Pos *pos, const Net *net, const NetAccumulator *accum) {
	assert(pos!=NULL);
	assert(net!=NULL);
	assert(accum!=NULL);

	// Transform step (encode stm, make 8 bit, clamping to 127)
	Colour stm=posGetSTM(pos);
	Colour xtm=colourSwap(stm);

	int8_t inputLayer[512];
	for(unsigned i=0; i<256; ++i) {
		inputLayer[i]=(accum->values[stm][i]<=127 ? accum->values[stm][i] : 127);
		inputLayer[i+256]=(accum->values[xtm][i]<=127 ? accum->values[xtm][i] : 127);
	}

	// First hidden layer
	int32_t hiddenLayer1[32];
	memcpy(hiddenLayer1, net->biasHidden1, sizeof(hiddenLayer1));

	for(unsigned i=0; i<32; ++i) {
		for(unsigned j=0; j<512; ++j) {
			hiddenLayer1[i]+=netMul(inputLayer[j], net->weightsHidden1[i][j]);
		}
	}

	for(unsigned i=0; i<32; ++i) {
		hiddenLayer1[i]/=64;
		hiddenLayer1[i]=(hiddenLayer1[i]<127 ? hiddenLayer1[i] : 127);
	}

	// Second hidden layer
	int32_t hiddenLayer2[32];
	memcpy(hiddenLayer2, net->biasHidden2, sizeof(hiddenLayer2));

	for(unsigned i=0; i<32; ++i) {
		for(unsigned j=0; j<32; ++j) {
			hiddenLayer2[i]+=netMul(hiddenLayer1[j], net->weightsHidden1[i][j]);
		}
	}

	for(unsigned i=0; i<32; ++i) {
		hiddenLayer2[i]/=64;
		hiddenLayer2[i]=(hiddenLayer2[i]<127 ? hiddenLayer2[i] : 127);
	}

	for(unsigned i=0; i<32; ++i) {
		hiddenLayer2[i]/=64;
		hiddenLayer2[i]=(hiddenLayer2[i]<127 ? hiddenLayer2[i] : 127);
	}

	// Output layer
	int32_t outputLayer=net->biasOutput;
	for(unsigned i=0; i<32; ++i)
		outputLayer+=netMul(hiddenLayer1[i], net->weightsOutput[i]);

	outputLayer/=16;

	return outputLayer;
}

void netAccumulatorCalc(const Net *net, const Pos *pos, NetAccumulator *accum) {
	assert(net!=NULL);
	assert(pos!=NULL);
	assert(accum!=NULL);

	// Input layer (accumulators)
	memcpy(accum->values[ColourWhite], net->biasAccum, 256*sizeof(int16_t));
	memcpy(accum->values[ColourBlack], net->biasAccum, 256*sizeof(int16_t));

	Sq kingSqW=posGetKingSq(pos, ColourWhite);
	Sq kingSqB=posGetKingSq(pos, ColourBlack);

	for(Sq sq=0; sq<SqNB; ++sq) {
		Piece p=posGetPieceOnSq(pos, sq);
		if (p==PieceNone)
			continue;
		Piece netP=netPieceFromPiece(p);

		if (sq!=kingSqW) {
			for(unsigned i=0; i<256; ++i)
				accum->values[ColourWhite][i]+=net->weightsAccum[kingSqW][netP][sq][i];
		}

		if (sq!=kingSqB) {
			for(unsigned i=0; i<256; ++i)
				accum->values[ColourBlack][i]+=net->weightsAccum[sqFlip(kingSqB)][netPieceSwapColour(netP)][sqFlip(sq)][i];
		}
	}

	for(unsigned i=0; i<256; ++i) {
		if (accum->values[ColourWhite][i]<0)
			accum->values[ColourWhite][i]=0;
		if (accum->values[ColourBlack][i]<0)
			accum->values[ColourBlack][i]=0;
	}
}

////////////////////////////////////////////////////////////////////////////////
// Private functions
////////////////////////////////////////////////////////////////////////////////

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
