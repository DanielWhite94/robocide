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

Score netEvaluateRaw(const Pos *pos) {
	// TODO: this
	return 0;
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
