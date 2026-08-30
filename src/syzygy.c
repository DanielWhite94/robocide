#include <assert.h>

#include "syzygy.h"
#include "syzygy/tbprobe.h"
#include "uci.h"

typedef struct {
	uint64_t white;
	uint64_t black;
	uint64_t kings;
	uint64_t queens;
	uint64_t rooks;
	uint64_t bishops;
	uint64_t knights;
	uint64_t pawns;
	unsigned rule50;
	unsigned castling;
	unsigned ep;
	bool turn;
} SyzygyPosData;

////////////////////////////////////////////////////////////////////////////////
// Private prototypes
////////////////////////////////////////////////////////////////////////////////

void syzygyGetPosData(SyzygyPosData *data, const Pos *pos);
unsigned syzygyGetPosCastling(const Pos *pos); // Set of TB_CASTLING_K etc.

void syzygySetSyzygyPath(void *userData, const char *value);

Move syzygyResultToMove(unsigned result, const Pos *pos);
SyzygyWdl syzygyResultToWdl(unsigned result);

////////////////////////////////////////////////////////////////////////////////
// Public functions
////////////////////////////////////////////////////////////////////////////////

void syzygyInit(void) {
	// Add UCI options
	uciOptionNewString("SyzygyPath", syzygySetSyzygyPath, NULL, "<empty>");

	// (defer initialising until we are given a path via setoption)
}

void syzygyQuit(void) {
	if (TB_LARGEST>0)
		tb_free();
}

SyzygyWdl syzygyProbeWdl(const Pos *pos) {
	assert(pos!=NULL);

	// Too many pieces for any of the tables we have available?
	if (bbPopCount(posGetBBAll(pos))>TB_LARGEST)
		return SyzygyWdlError;

	// Lookup value
	SyzygyPosData pd;
	syzygyGetPosData(&pd, pos);
	unsigned result=tb_probe_wdl(pd.white, pd.black, pd.kings, pd.queens, pd.rooks, pd.bishops, pd.knights, pd.pawns, pd.rule50, pd.castling, pd.ep, pd.turn);

	// Convert result
	switch(result) {
		case TB_LOSS:
			return SyzygyWdlLoss;
		break;
		case TB_BLESSED_LOSS:
		case TB_DRAW:
		case TB_CURSED_WIN:
			return SyzygyWdlDraw;
		break;
		case TB_WIN:
			return SyzygyWdlWin;
		break;
		case TB_RESULT_FAILED:
			return SyzygyWdlError;
		break;
	}

	assert(false);
	return SyzygyWdlError;
}

Move syzygyProbeRoot(const Pos *pos, SyzygyWdl *wdl, int *dtz, SyzygyMove *moves) {
	assert(pos!=NULL);

	// Too many pieces for any of the tables we have available?
	if (bbPopCount(posGetBBAll(pos))>TB_LARGEST)
		return MoveInvalid;

	// Lookup value
	SyzygyPosData pd;
	syzygyGetPosData(&pd, pos);

	unsigned resultsArray[TB_MAX_MOVES];
	unsigned *results=resultsArray;
	unsigned result=tb_probe_root(pd.white, pd.black, pd.kings, pd.queens, pd.rooks, pd.bishops, pd.knights, pd.pawns, pd.rule50, pd.castling, pd.ep, pd.turn, results);

	if (result==TB_RESULT_STALEMATE || result==TB_RESULT_CHECKMATE || result==TB_RESULT_FAILED)
		return MoveInvalid;

	// Set WDL and DTZ if needed
	if (wdl!=NULL)
		*wdl=syzygyResultToWdl(result);

	if (dtz!=NULL)
		*dtz=TB_GET_DTZ(result);

	// Fill moves array if needed
	if (moves!=NULL) {
		while(*results!=TB_RESULT_FAILED) {
			moves->move=syzygyResultToMove(*results, pos);
			moves->wdl=syzygyResultToWdl(*results);
			moves->dtz=TB_GET_DTZ(*results);
			++results;
			++moves;
		}
		moves->move=MoveInvalid;
		moves->wdl=SyzygyWdlError;
		moves->dtz=256;
	}

	// Extract best move and return it
	return syzygyResultToMove(result, pos);
}

const char *syzygyWdlStr[4]={
	[SyzygyWdlError]="error",
	[SyzygyWdlWin]="win",
	[SyzygyWdlLoss]="loss",
	[SyzygyWdlDraw]="draw",
};
const char *syzygyWdlToStr(SyzygyWdl wdl) {
	assert(wdl<SyzygyWdlNB);
	return syzygyWdlStr[wdl];
}

////////////////////////////////////////////////////////////////////////////////
// Private functions
////////////////////////////////////////////////////////////////////////////////

void syzygyGetPosData(SyzygyPosData *data, const Pos *pos) {
	assert(data!=NULL);
	assert(pos!=NULL);

	data->white=posGetBBColour(pos, ColourWhite);
	data->black=posGetBBColour(pos, ColourBlack);
	data->kings=(posGetBBPiece(pos, PieceWKing)|posGetBBPiece(pos, PieceBKing));
	data->queens=(posGetBBPiece(pos, PieceWQueen)|posGetBBPiece(pos, PieceBQueen));
	data->rooks=(posGetBBPiece(pos, PieceWRook)|posGetBBPiece(pos, PieceBRook));
	data->bishops=(posGetBBPiece(pos, PieceWBishop)|posGetBBPiece(pos, PieceBBishop));
	data->knights=(posGetBBPiece(pos, PieceWKnight)|posGetBBPiece(pos, PieceBKnight));
	data->pawns=(posGetBBPiece(pos, PieceWPawn)|posGetBBPiece(pos, PieceBPawn));
	data->rule50=posGetHalfMoveNumber(pos);
	data->castling=syzygyGetPosCastling(pos);
	data->ep=(posGetEPSq(pos)!=SqInvalid ? posGetEPSq(pos) : 0);
	data->turn=(posGetSTM(pos)==ColourWhite);
}

unsigned syzygyGetPosCastling(const Pos *pos) {
	STATICASSERT(TB_CASTLING_Q==((1u)<<1));
	STATICASSERT(TB_CASTLING_K==((1u)<<0));
	STATICASSERT(TB_CASTLING_q==((1u)<<3));
	STATICASSERT(TB_CASTLING_k==((1u)<<2));
	CastRights castling=posGetCastRights(pos);
	return (((castling.rookSq[ColourWhite][CastSideA]!=SqInvalid)<<1)|
	        ((castling.rookSq[ColourWhite][CastSideH]!=SqInvalid)<<0)|
	        ((castling.rookSq[ColourBlack][CastSideA]!=SqInvalid)<<3)|
	        ((castling.rookSq[ColourBlack][CastSideH]!=SqInvalid)<<2));
}

void syzygySetSyzygyPath(void *userData, const char *value) {
	assert(userData==NULL);
	assert(value!=NULL);

	// Free previous initialisation if needed
	if (TB_LARGEST>0)
		tb_free();

	// Initialise
	if (!tb_init(value)) {
		uciWrite("info string Failed to load Syzygy tables at '%s'\n", value);
		return;
	}

	uciWrite("info string Loaded Syzygy tables at '%s' up to size %u\n", value, TB_LARGEST);
}

Move syzygyResultToMove(unsigned result, const Pos *pos) {
	assert(pos!=NULL);

	Sq fromSq=TB_GET_FROM(result);
	Sq toSq=TB_GET_TO(result);
	Piece toPiece=posGetPieceOnSq(pos, fromSq);
	switch(TB_GET_PROMOTES(result)) {
		case TB_PROMOTES_NONE:
		break;
		case TB_PROMOTES_QUEEN:
			toPiece=pieceMake(PieceTypeQueen, pieceGetColour(toPiece));
		break;
		case TB_PROMOTES_ROOK:
			toPiece=pieceMake(PieceTypeRook, pieceGetColour(toPiece));
		break;
		case TB_PROMOTES_BISHOP:
			toPiece=pieceMake(PieceTypeBishop, pieceGetColour(toPiece));
		break;
		case TB_PROMOTES_KNIGHT:
			toPiece=pieceMake(PieceTypeKnight, pieceGetColour(toPiece));
		break;
	}

	return moveMake(fromSq, toSq, toPiece);
}

SyzygyWdl syzygyResultToWdl(unsigned result) {
	switch(TB_GET_WDL(result)) {
		case TB_LOSS:
			return SyzygyWdlLoss;
		break;
		case TB_BLESSED_LOSS:
		case TB_DRAW:
		case TB_CURSED_WIN:
			return SyzygyWdlDraw;
		break;
		case TB_WIN:
			return SyzygyWdlWin;
		break;
		case TB_RESULT_FAILED:
			return SyzygyWdlError;
		break;
	}

	assert(false);
	return SyzygyWdlError;
}
