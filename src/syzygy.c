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

void syzygySetSyzygyPath(void *userData, const char *value);

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

const char *syzygyWdlStr[4]={
	[SyzygyWdlError]="error",
	[SyzygyWdlWin]="win",
	[SyzygyWdlLoss]="loss",
	[SyzygyWdlDraw]="draw",
};
const char *syzygyWdlToStr(SyzygyWdl wdl) {
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
	data->bishops=(posGetBBPiece(pos, PieceWBishopL)|posGetBBPiece(pos, PieceBBishopL)|posGetBBPiece(pos, PieceWBishopD)|posGetBBPiece(pos, PieceBBishopD));
	data->knights=(posGetBBPiece(pos, PieceWKnight)|posGetBBPiece(pos, PieceBKnight));
	data->pawns=(posGetBBPiece(pos, PieceWPawn)|posGetBBPiece(pos, PieceBPawn));
	data->rule50=posGetHalfMoveNumber(pos);
	data->castling=0; // TODO: this (using TB_CASTLING_K etc.). However, in reality never going to matter in a real game. Also needs thought for Chess960 in case where castling is still a possibility)
	data->ep=(posGetEPSq(pos)!=SqInvalid ? posGetEPSq(pos) : 0);
	data->turn=(posGetSTM(pos)==ColourWhite);
}

void syzygySetSyzygyPath(void *userData, const char *value) {
	assert(userData==NULL);
	assert(value!=NULL);

	// Free previous initialisation if needed
	if (TB_LARGEST>0)
		tb_free();

	// Initialise
	if (!tb_init(value)) {
		uciWrite("Failed to load Syzygy tables at '%s'\n", value);
		return;
	}	

	uciWrite("Loaded Syzygy tables at '%s' up to size %u\n", value, TB_LARGEST);
}
