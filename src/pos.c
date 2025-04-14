#include <assert.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "attacks.h"
#include "eval.h"
#include "fen.h"
#include "pos.h"
#include "uci.h"
#include "util.h"

const CastRights CastRightsNone={
	.rookSq[ColourWhite][CastSideA]=SqInvalid,
	.rookSq[ColourWhite][CastSideH]=SqInvalid,
	.rookSq[ColourBlack][CastSideA]=SqInvalid,
	.rookSq[ColourBlack][CastSideH]=SqInvalid,
};

STATICASSERT(MoveBit<=16);
STATICASSERT(PieceBit<=4);
STATICASSERT(SqBit<=8);
typedef struct {
	Key key;
	uint64_t lastMove:16;
	uint64_t lastMoveWasPromo:1;
	uint64_t halfMoveNumber:15;
	uint64_t epSq:7;
	uint64_t capSq:7;
	uint64_t capPiece:4;
	uint64_t padding:10;
	CastRights castRights;
} PosData;

STATICASSERT(PieceBit<=8);
struct Pos {
	BB bbPiece[PieceNB];
	uint8_t array64[SqNB]; // entries are Pieces
	PosData *dataStart, *dataEnd, *data;
	BB bbColour[ColourNB], bbAll;
	Colour stm;
	unsigned int fullMoveNumber;
	Key pawnKey, matKey;
	VPair pstScore; // From white's POV
};

const char *posStartFEN="rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

Key posKeySTM, posKeyPiece[16][SqNB], posKeyEP[SqMax], posKeyCastling[SqMax];
Key posPawnKeyPiece[PieceNB][SqNB];
Key posMatKey[PieceNB];

////////////////////////////////////////////////////////////////////////////////
// Private prototypes.
////////////////////////////////////////////////////////////////////////////////

void posClean(Pos *pos);

void posPieceAdd(Pos *pos, Piece piece, Sq sq, bool skipMainKeyUpdate);
void posPieceRemove(Pos *pos, Sq sq, bool skipMainKeyUpdate);
void posPieceMove(Pos *pos, Sq fromSq, Sq toSq, bool skipMainKeyUpdate);
void posPieceMoveChange(Pos *pos, Sq fromSq, Sq toSq, Piece toPiece, bool skipMainKeyUpdate);

void posGenPseudoNormal(Moves *moves, BB allowed);
void posGenPseudoPawnMoves(Moves *moves, MoveType type);
void posGenPseudoCast(Moves *moves);

Key posComputeKey(const Pos *pos);
Key posComputePawnKey(const Pos *pos);
Key posComputeMatKey(const Pos *pos);
Key posRandKey(void);

bool posIsEPCap(const Pos *pos, Sq sq); // Is there a legal en-passent capture move to sq available?
bool posIsPiecePinned(const Pos *pos, BB occ, Colour atkColour, Sq pinnedSq, Sq victimSq);

bool posIsConsistent(const Pos *pos);

unsigned int matInfoShift(Piece piece);

bool posMoveIsPseudoLegalInternal(const Pos *pos, Move move);

bool posLegalMoveExistsPiece(const Pos *pos, PieceType type, BB allowed);

////////////////////////////////////////////////////////////////////////////////
// Public functions.
////////////////////////////////////////////////////////////////////////////////

void posInit(void) {
	// Hash keys
	utilRandSeed(1804289383);
	posKeySTM=posRandKey();
	memset(posKeyPiece, 0, PieceNB*SqNB*sizeof(Key));
	memset(posPawnKeyPiece, 0, PieceNB*SqNB*sizeof(Key));
	memset(posKeyEP, 0, SqMax*sizeof(Key));
	memset(posKeyCastling, 0, SqMax*sizeof(Key));
	unsigned int i;
	for(i=0;i<SqNB;++i) {
		posKeyPiece[PieceWPawn][i]=posRandKey();
		posKeyPiece[PieceWKnight][i]=posRandKey();
		posKeyPiece[PieceWBishopL][i]=posRandKey();
		posKeyPiece[PieceWBishopD][i]=posRandKey();
		posKeyPiece[PieceWRook][i]=posRandKey();
		posKeyPiece[PieceWQueen][i]=posRandKey();
		posKeyPiece[PieceWKing][i]=posRandKey();
		posKeyPiece[PieceBPawn][i]=posRandKey();
		posKeyPiece[PieceBKnight][i]=posRandKey();
		posKeyPiece[PieceBBishopL][i]=posRandKey();
		posKeyPiece[PieceBBishopD][i]=posRandKey();
		posKeyPiece[PieceBRook][i]=posRandKey();
		posKeyPiece[PieceBQueen][i]=posRandKey();
		posKeyPiece[PieceBKing][i]=posRandKey();

		posPawnKeyPiece[PieceWPawn][i]=posRandKey();
		posPawnKeyPiece[PieceBPawn][i]=posRandKey();
	}
	for(i=0;i<FileNB;++i)
		posKeyEP[sqMake(i,Rank3)]=posKeyEP[sqMake(i,Rank6)]=posRandKey();
	for(i=0; i<FileNB; ++i) {
		posKeyCastling[sqMake(i, Rank1)]=posRandKey();
		posKeyCastling[sqMake(i, Rank8)]=posRandKey();
	}

	memset(posMatKey, 0, PieceNB*sizeof(Key));
	Colour colour;
	PieceType pieceType;
	for(colour=ColourWhite; colour<=ColourBlack; ++colour)
		for(pieceType=PieceTypePawn; pieceType<=PieceTypeQueen; ++pieceType) {
			Piece piece=pieceMake(pieceType, colour);
			posMatKey[piece]=posRandKey();
		}
}

Pos *posNew(const char *gfen) {
	const size_t initialPosDataSize=64;

	// Create clean position.
	Pos *pos=malloc(sizeof(Pos));
	PosData *posData=malloc(initialPosDataSize*sizeof(PosData));
	if (pos==NULL || posData==NULL) {
		free(pos);
		free(posData);
		return NULL;
	}
	pos->dataStart=posData;
	pos->dataEnd=posData+initialPosDataSize;

	// If no FEN given use initial position.
	const char *fen=(gfen!=NULL ? gfen : posStartFEN);

	// Set to FEN.
	if (!posSetToFEN(pos, fen)) {
		posFree(pos);
		return NULL;
	}

	return pos;
}

Pos *posNewFromPos(const Pos *src) {
	assert(src!=NULL);

	Pos *pos=posNew(NULL);
	if (pos==NULL)
		return NULL;

	if (!posCopy(pos, src)) {
		posFree(pos);
		return NULL;
	}

	return pos;
}

void posFree(Pos *pos) {
	if (pos==NULL)
		return;
	free(pos->dataStart);
	free(pos);
}

bool posCopy(Pos *dest, const Pos *src) {
	assert(dest!=NULL);
	assert(src!=NULL);

	// Allocate more memory if needed
	size_t srcDataSize=(src->dataEnd-src->dataStart);
	size_t destDataSize=(dest->dataEnd-dest->dataStart);
	size_t newDataSize;
	PosData *newPosData;
	if (srcDataSize>destDataSize) {
		newDataSize=srcDataSize;
		newPosData=realloc(dest->dataStart, newDataSize*sizeof(PosData));
		if (newPosData==NULL) {
			free(newPosData);
			return false;
		}
	} else {
		newDataSize=destDataSize;
		newPosData=dest->dataStart;
	}

	// Set data
	*dest=*src;

	dest->dataStart=newPosData;
	dest->dataEnd=newPosData+newDataSize;
	size_t srcDataLen=(src->data-src->dataStart);
	dest->data=newPosData+srcDataLen;
	memcpy(dest->dataStart, src->dataStart, (srcDataLen+1)*sizeof(PosData));

	assert(posIsConsistent(dest));

	return true;
}

bool posSetToFEN(Pos *pos, const char *string) {
	// Parse FEN.
	Fen fen;
	if (string==NULL) {
		if (!fenRead(&fen, posStartFEN))
			return false;
	} else if (!fenRead(&fen, string))
		return false;

	// Set position to clean state.
	posClean(pos);

	// Set position to given FEN.
	Sq sq;
	for(sq=0;sq<SqNB;++sq)
		if (fen.array[sq]!=PieceNone)
			posPieceAdd(pos, fen.array[sq], sq, true);
	pos->stm=fen.stm;
	pos->fullMoveNumber=fen.fullMoveNumber;
	pos->data->halfMoveNumber=fen.halfMoveNumber;
	pos->data->castRights=fen.castRights;
	if (fen.epSq!=SqInvalid && posIsEPCap(pos, fen.epSq))
		pos->data->epSq=fen.epSq;
	pos->data->key=posComputeKey(pos);

	assert(posIsConsistent(pos));

	return true;
}

void posGetFEN(const Pos *pos, char string[static 128]) {
	assert(pos!=NULL);

	Fen fenData;
	fenFromPos(&fenData, pos);
	fenWrite(&fenData, string);
}

void posDraw(const Pos *pos) {
	// Header.
	uciWrite("Position:\n");

	// Board with pieces.
	int file, rank;
	for(rank=Rank8;rank>=Rank1;--rank) {
		uciWrite("%i|", (rank+8)-Rank8);
		for(file=FileA;file<=FileH;++file)
			uciWrite(" %c", pieceToChar(posGetPieceOnSq(pos, sqMake(file,rank))));
		uciWrite("\n");
	}
	uciWrite("   ----------------\n");
	uciWrite("  ");
	for(file=FileA;file<=FileH;++file)
		uciWrite(" %c", (file+'a')-FileA);
	uciWrite("\n");

	// Other information.
	uciWrite("STM: %s\n", colourToStr(posGetSTM(pos)));
	char castRightsStr[8];
	posCastRightsToStr(posGetCastRights(pos), castRightsStr);
	uciWrite("Castling rights: %s\n", castRightsStr);
	if (pos->data->epSq!=SqInvalid)
		uciWrite("EP-sq: %c%c\n", fileToChar(sqFile(pos->data->epSq)), rankToChar(sqRank(pos->data->epSq)));
	else
		uciWrite("EP-sq: -\n");
	uciWrite("Half move number: %u\n", pos->data->halfMoveNumber);
	uciWrite("Full move number: %u\n", pos->fullMoveNumber);
	uciWrite("Base hash key: %016"PRIxKey"\n", posGetKey(pos));
	uciWrite("Pawn hash key: %016"PRIxKey"\n", posGetPawnKey(pos));
	uciWrite("Material hash key: %016"PRIxKey"\n", posGetMatKey(pos));
	char fen[128];
	posGetFEN(pos, fen);
	uciWrite("FEN string: %s\n", fen);
	uciWrite("PST score: (%i,%i)\n", pos->pstScore.mg, pos->pstScore.eg);
}

Colour posGetSTM(const Pos *pos) {
	return pos->stm;
}

Piece posGetPieceOnSq(const Pos *pos, Sq sq) {
	assert(sqIsValid(sq));
	return pos->array64[sq];
}

BB posGetBBAll(const Pos *pos) {
	return pos->bbAll;
}

BB posGetBBColour(const Pos *pos, Colour colour) {
	assert(colourIsValid(colour));
	return pos->bbColour[colour];
}

BB posGetBBPiece(const Pos *pos, Piece piece) {
	assert(pieceIsValid(piece));
	return pos->bbPiece[piece];
}

unsigned int posGetPieceCount(const Pos *pos, Piece piece) {
	assert(pieceIsValid(piece));
	return bbPopCount(posGetBBPiece(pos, piece));
}

Sq posGetKingSq(const Pos *pos, Colour colour) {
	assert(colourIsValid(colour));
	return bbScanForward(posGetBBPiece(pos, pieceMake(PieceTypeKing, colour)));
}

unsigned int posGetHalfMoveNumber(const Pos *pos) {
	return pos->data->halfMoveNumber;
}

unsigned int posGetFullMoveNumber(const Pos *pos) {
	return pos->fullMoveNumber;
}

Key posGetKey(const Pos *pos) {
	return pos->data->key;
}

Key posGetPawnKey(const Pos *pos) {
	return pos->pawnKey;
}

Key posGetMatKey(const Pos *pos) {
	return pos->matKey;
}

CastRights posGetCastRights(const Pos *pos) {
	return pos->data->castRights;
}

Sq posGetEPSq(const Pos *pos) {
	return pos->data->epSq;
}

VPair posGetPstScore(const Pos *pos) {
	return pos->pstScore;
}

bool posMakeMove(Pos *pos, Move move) {
	assert(moveIsValid(move));

	// Does this move leave us in check?
	if (!posCanMakeMove(pos, move))
		return false;

	// Grab some move info now before we advance to next data entry.
	bool isCastlingA=posMoveIsCastlingA(pos, move);
	bool isCastlingH=posMoveIsCastlingH(pos, move);
	bool isCastling=isCastlingA|isCastlingH;

	Sq toSqTrue=posMoveGetToSqTrue(pos, move);

	// Use next data entry.
	if (pos->data+1>=pos->dataEnd) {
		// We need more space.
		size_t size=2*(pos->dataEnd-pos->dataStart);
		PosData *ptr=realloc(pos->dataStart, size*sizeof(PosData));
		if (ptr==NULL)
			return false;
		unsigned int dataOffset=pos->data-pos->dataStart;
		pos->dataStart=ptr;
		pos->dataEnd=ptr+size;
		pos->data=ptr+dataOffset;
	}
	++pos->data;

	// Update generic fields.
	Colour movingSide=posGetSTM(pos);
	Colour nonMovingSide=colourSwap(movingSide);

	pos->data->lastMove=move;
	pos->data->lastMoveWasPromo=false;
	pos->data->halfMoveNumber=(pos->data-1)->halfMoveNumber+1;
	pos->data->epSq=SqInvalid;
	pos->data->key=(pos->data-1)->key^posKeySTM^posKeyEP[(pos->data-1)->epSq];
	pos->data->castRights=(pos->data-1)->castRights;
	pos->data->capPiece=PieceNone;
	pos->data->capSq=SqInvalid;
	pos->fullMoveNumber+=(movingSide==ColourBlack); // Inc after black's move.
	pos->stm=nonMovingSide;

	assert(toSqTrue!=SqInvalid);

	pos->data->capPiece=(!isCastling ? posGetPieceOnSq(pos, toSqTrue) : PieceNone);
	pos->data->capSq=toSqTrue;

	Sq toSqRaw=moveGetToSqRaw(move);
	Sq fromSq=moveGetFromSq(move);

	Piece fromPiece=posGetPieceOnSq(pos, fromSq);

	// Special case for castling
	if (isCastling) {
		assert(fromPiece==pieceMake(PieceTypeKing, movingSide));
		assert(moveGetToPiece(move)==pieceMake(PieceTypeKing, movingSide));

		// Remove king (do it this way in case of strange Chess960 castling)
		posPieceRemove(pos, fromSq, false);

		// Move rook
		Sq rookFromSq=toSqRaw;
		Sq rookToSq=sqMake((isCastlingA ? FileD : FileF), (movingSide==ColourWhite ? Rank1 : Rank8));
		assert(posGetPieceOnSq(pos, rookFromSq)==pieceMake(PieceTypeRook, movingSide));

		if (rookFromSq!=rookToSq)
			posPieceMove(pos, rookFromSq, rookToSq, false);

		// Replace king in new position.
		assert(posGetPieceOnSq(pos, toSqTrue)==PieceNone);
		posPieceAdd(pos, pieceMake(PieceTypeKing, movingSide), toSqTrue, false);
	} else if (pieceGetType(fromPiece)==PieceTypePawn) {
		// Pawns are complicated so deserve a special case.

		// En-passent capture?
		bool isEP=(sqFile(fromSq)!=sqFile(toSqRaw) && pos->data->capPiece==PieceNone);
		if (isEP) {
			pos->data->capSq^=8;
			pos->data->capPiece=pieceMake(PieceTypePawn, nonMovingSide);
			assert(posGetPieceOnSq(pos, pos->data->capSq)==pos->data->capPiece);
		}

		// Capture?
		if (pos->data->capPiece!=PieceNone)
			// Remove piece.
			posPieceRemove(pos, pos->data->capSq, false);

		// Move the pawn, potentially promoting.
		Piece toPiece=moveGetToPiece(move);
		if (toPiece!=fromPiece) {
			pos->data->lastMoveWasPromo=true;
			posPieceMoveChange(pos, fromSq, toSqRaw, toPiece, false);
		} else
			posPieceMove(pos, fromSq, toSqRaw, false);

		// Pawn moves reset 50 move counter.
		pos->data->halfMoveNumber=0;

		// If double pawn move check set EP capture square (for next move).
		if (abs(((int)sqRank(toSqRaw))-((int)sqRank(fromSq)))==2) {
			Sq epSq=toSqRaw^8;
			if (posIsEPCap(pos, epSq)) {
				pos->data->epSq=epSq;
				pos->data->key^=posKeyEP[epSq];
			}
		}
	} else {
		// Standard piece move

		// Capture?
		if (pos->data->capPiece!=PieceNone) {
			// Remove piece.
			posPieceRemove(pos, toSqTrue, false);

			// Captures reset 50 move counter.
			pos->data->halfMoveNumber=0;
		}

		// Move non-pawn piece (i.e. no promotion to worry about).
		assert(posGetPieceOnSq(pos, toSqTrue)==PieceNone);
		posPieceMove(pos, fromSq, toSqTrue, false);
	}

	// Update castling rights
	if (pos->data->castRights.rookSq[movingSide][CastSideA]==fromSq || pieceGetType(fromPiece)==PieceTypeKing) {
		pos->data->key^=posKeyCastling[pos->data->castRights.rookSq[movingSide][CastSideA]];
		pos->data->castRights.rookSq[movingSide][CastSideA]=SqInvalid;
	}
	if (pos->data->castRights.rookSq[movingSide][CastSideH]==fromSq || pieceGetType(fromPiece)==PieceTypeKing) {
		pos->data->key^=posKeyCastling[pos->data->castRights.rookSq[movingSide][CastSideH]];
		pos->data->castRights.rookSq[movingSide][CastSideH]=SqInvalid;
	}
	if (pos->data->castRights.rookSq[nonMovingSide][CastSideA]==toSqRaw) {
		pos->data->key^=posKeyCastling[pos->data->castRights.rookSq[nonMovingSide][CastSideA]];
		pos->data->castRights.rookSq[nonMovingSide][CastSideA]=SqInvalid;
	}
	if (pos->data->castRights.rookSq[nonMovingSide][CastSideH]==toSqRaw) {
		pos->data->key^=posKeyCastling[pos->data->castRights.rookSq[nonMovingSide][CastSideH]];
		pos->data->castRights.rookSq[nonMovingSide][CastSideH]=SqInvalid;
	}

	assert(posIsConsistent(pos));

	return true;
}

bool posCanMakeMove(const Pos *pos, Move move) {
	// Sanity checks and special case.
	assert(moveIsValid(move));

	// Use local variables to simulate having made the move.
	Colour stm=moveGetColour(move);
	assert(stm==posGetSTM(pos));
	Colour xstm=colourSwap(stm);
	BB occ=posGetBBAll(pos);
	BB opp=posGetBBColour(pos, xstm);
	Sq fromSq=moveGetFromSq(move);
	Sq toSqRaw=moveGetToSqRaw(move);
	Sq toSqTrue=posMoveGetToSqTrue(pos, move);
	BB fromBB=bbSq(fromSq);
	BB toBB=bbSq(toSqTrue);
	Sq kingSq=posGetKingSq(pos, stm);

	if (fromSq==kingSq)
		kingSq=toSqTrue; // King move.
	occ&=~fromBB; // Move piece.
	occ|=toBB;
	opp&=~toBB; // Potentially capture opp piece (so it cannot attack us later).
	if (moveGetToPieceType(move)==PieceTypePawn && sqFile(fromSq)!=sqFile(toSqRaw) && posGetPieceOnSq(pos, toSqRaw)==PieceNone) {
		// En-passent capture.
		assert(pos->data->epSq==toSqRaw);
		occ^=bbSq(toSqRaw^8);
		opp^=bbSq(toSqRaw^8);
	}

	// Update occupancy to reflect rook movement if castling.
	if (posMoveIsCastling(pos, move)) {
		occ&=~bbSq(toSqRaw); // rook's from sq
		Sq rookToSq=sqMake((posMoveIsCastlingA(pos, move) ? FileD : FileF), (stm==ColourWhite ? Rank1 : Rank8));
		occ|=bbSq(rookToSq);
	}

	// Make a list of squares we need to ensure are unattacked.
	BB checkSquares=bbSq(kingSq);
	if (posMoveIsCastling(pos, move)) {
		checkSquares|=bbSq(fromSq);
		checkSquares|=bbBetween(fromSq, toSqTrue);
	}

	// Test for attacks to any of checkSquares.
	// Pawns are done setwise.
	BB oppPawns=(posGetBBPiece(pos, pieceMake(PieceTypePawn, xstm)) & opp);
	if (bbForwardOne(bbWingify(oppPawns), xstm) & checkSquares)
		return false;
	// Pieces are checked for each square in checkSquares (which usually only has a single bit set anyway).
	while(checkSquares) {
		Sq sq=bbScanReset(&checkSquares);

		// Knights.
		if (attacksKnight(sq) & opp & posGetBBPiece(pos, pieceMake(PieceTypeKnight, xstm)))
			return false;

		// Bishops and diagonal queen moves.
		if (attacksBishop(sq, occ) & opp &
		    (posGetBBPiece(pos, pieceMake(PieceTypeBishopL, xstm)) |
		     posGetBBPiece(pos, pieceMake(PieceTypeBishopD, xstm)) |
		     posGetBBPiece(pos, pieceMake(PieceTypeQueen, xstm))))
			return false;

		// Rooks and orthogonal queen moves.
		if (attacksRook(sq, occ) & opp &
		    (posGetBBPiece(pos, pieceMake(PieceTypeRook, xstm)) |
		     posGetBBPiece(pos, pieceMake(PieceTypeQueen, xstm))))
			return false;

		// King.
		if (attacksKing(sq) & opp & posGetBBPiece(pos, pieceMake(PieceTypeKing, xstm)))
			return false;
	}

	return true;
}

void posUndoMove(Pos *pos) {
	assert(pos->data>pos->dataStart);

	Move move=pos->data->lastMove;
	Colour nonMovingSide=posGetSTM(pos);
	Colour movingSide=colourSwap(nonMovingSide);
	assert(moveIsValid(move));

	// Update generic fields.
	pos->stm=movingSide;
	pos->fullMoveNumber-=(movingSide==ColourBlack);
	--pos->data; // do this here so that posMoveGetToSqTrue and posMoveIsCastling work correctly

	Sq fromSq=moveGetFromSq(move);
	Sq toSqRaw=moveGetToSqRaw(move);
	Sq toSqTrue=posMoveGetToSqTrue(pos, move);

	// If castling, remove rook here (to be safe in case of strange Chess960 castling)
	if (pos->data->castRights.rookSq[movingSide][CastSideA]==toSqRaw) {
		Sq rookToSq=sqMake(FileD, (movingSide==ColourWhite ? Rank1 : Rank8));
		posPieceRemove(pos, rookToSq, true);
	}
	if (pos->data->castRights.rookSq[movingSide][CastSideH]==toSqRaw) {
		Sq rookToSq=sqMake(FileF, (movingSide==ColourWhite ? Rank1 : Rank8));
		posPieceRemove(pos, rookToSq, true);
	}

	// Move piece back (potentially un-promoting).
	if ((pos->data+1)->lastMoveWasPromo)
		posPieceMoveChange(pos, toSqTrue, fromSq, pieceMake(PieceTypePawn, movingSide), true);
	else if (toSqTrue!=fromSq) // king doesn't always move when castling
		posPieceMove(pos, toSqTrue, fromSq, true);

	// Replace any captured piece.
	if ((pos->data+1)->capPiece!=PieceNone)
		posPieceAdd(pos, (pos->data+1)->capPiece, (pos->data+1)->capSq, true);

	// If castling replace the rook.
	if (posMoveIsCastling(pos, move)) {
		Sq rookFromSq=toSqRaw;
		posPieceAdd(pos, pieceMake(PieceTypeRook, movingSide), rookFromSq, true);
	}

	assert(posIsConsistent(pos));
}

bool posMakeNullMove(Pos *pos) {
	assert(!posIsSTMInCheck(pos));

	// Use next data entry.
	if (pos->data+1>=pos->dataEnd) {
		// We need more space.
		size_t size=2*(pos->dataEnd-pos->dataStart);
		PosData *ptr=realloc(pos->dataStart, size*sizeof(PosData));
		if (ptr==NULL)
			return false;
		unsigned int dataOffset=pos->data-pos->dataStart;
		pos->dataStart=ptr;
		pos->dataEnd=ptr+size;
		pos->data=ptr+dataOffset;
	}
	++pos->data;

	// Update generic fields.
	Colour movingSide=posGetSTM(pos);
	Colour nonMovingSide=colourSwap(movingSide);

	pos->data->lastMove=MoveInvalid;
	pos->data->lastMoveWasPromo=false;
	pos->data->halfMoveNumber=(pos->data-1)->halfMoveNumber+1;
	pos->data->epSq=SqInvalid;
	pos->data->key=(pos->data-1)->key^posKeySTM^posKeyEP[(pos->data-1)->epSq];
	pos->data->castRights=(pos->data-1)->castRights;
	pos->data->capPiece=PieceNone;
	pos->data->capSq=SqInvalid;
	pos->fullMoveNumber+=(movingSide==ColourBlack); // Inc after black's move.
	pos->stm=nonMovingSide;

	assert(posIsConsistent(pos));

	return true;
}

void posUndoNullMove(Pos *pos) {
	assert(pos->data>pos->dataStart);

	assert(pos->data->lastMove==MoveInvalid);
	Colour movingSide=colourSwap(posGetSTM(pos));

	// Update generic fields.
	pos->stm=movingSide;
	pos->fullMoveNumber-=(movingSide==ColourBlack);
	--pos->data;
}

void posGenPseudoMoves(Moves *moves, MoveType type) {
	assert(type==MoveTypeQuiet || type==MoveTypeCapture || type==MoveTypeAny);

	// Standard moves (no pawns or castling).
	BB occ=posGetBBAll(movesGetPos(moves));
	BB allowed=BBNone;
	if (type & MoveTypeQuiet)
		allowed|=~occ;
	if (type & MoveTypeCapture)
		allowed|=occ;
	posGenPseudoNormal(moves, allowed);

	// Pawns.
	posGenPseudoPawnMoves(moves, type);

	// Castling.
	if (type & MoveTypeQuiet)
		posGenPseudoCast(moves);
}

Move posGenLegalMove(const Pos *pos, MoveType type) {
	Moves moves;
	movesInit(&moves, pos, 0, type);
	Move move;
	while((move=movesNext(&moves))!=MoveInvalid)
		if (posCanMakeMove(pos, move))
			return move;
	return MoveInvalid;
}

bool posIsSqAttackedByColour(const Pos *pos, Sq sq, Colour colour) {
	assert(sqIsValid(sq));
	assert(colourIsValid(colour));
	BB occ=posGetBBAll(pos);

	// Pawns.
	if (bbForwardOne(bbWingify(posGetBBPiece(pos, pieceMake(PieceTypePawn, colour))), colour) & bbSq(sq))
		return true;

	// Knights.
	if (attacksKnight(sq) & posGetBBPiece(pos, pieceMake(PieceTypeKnight, colour)))
		return true;

	// Bishops.
	BB bishopSet=attacksBishop(sq, occ);
	if (bishopSet & (posGetBBPiece(pos, pieceMake(PieceTypeBishopL, colour)) |
	                 posGetBBPiece(pos, pieceMake(PieceTypeBishopD, colour))))
		return true;

	// Rooks.
	BB rookSet=attacksRook(sq, occ);
	if (rookSet & posGetBBPiece(pos, pieceMake(PieceTypeRook, colour)))
		return true;

	// Queens.
	if ((bishopSet | rookSet) & posGetBBPiece(pos, pieceMake(PieceTypeQueen, colour)))
		return true;

	// King.
	if (attacksKing(sq) & posGetBBPiece(pos, pieceMake(PieceTypeKing, colour)))
		return true;

	return false;
}

bool posIsSTMInCheck(const Pos *pos) {
	return posIsSqAttackedByColour(pos, posGetKingSq(pos, pos->stm), colourSwap(pos->stm));
}

bool posIsDraw(const Pos *pos) {
	// False positives are bad, false negatives are OK.

	// Repetition (2-fold).
	PosData *ptr, *endPtr=utilMax(pos->dataStart, pos->data-posGetHalfMoveNumber(pos));
	for(ptr=pos->data-2;ptr>=endPtr;ptr-=2)
		if (ptr->key==pos->data->key)
			return true;

	// 50-move rule.
	if (posGetHalfMoveNumber(pos)>=100)
		return true;

	// Insufficient material.
	if (evalGetMatType(pos)==EvalMatTypeDraw)
		return true;

	return false;
}

bool posIsMate(const Pos *pos) {
	return (posIsSTMInCheck(pos) && !posLegalMoveExists(pos, MoveTypeAny));
}

bool posIsStalemate(const Pos *pos) {
	return (!posIsSTMInCheck(pos) && !posLegalMoveExists(pos, MoveTypeAny));
}

bool posLegalMoveExists(const Pos *pos, MoveType type) {
	Colour stm=posGetSTM(pos);
	BB occ=posGetBBAll(pos);
	BB opp=posGetBBColour(pos, colourSwap(stm));
	BB allowed=BBNone; // Squares pieces can move to.
	if (type & MoveTypeQuiet) allowed|=~occ;
	if (type & MoveTypeCapture) allowed|=opp;

	// Pieces (ordered for speed).
	if (posLegalMoveExistsPiece(pos, PieceTypeKing, allowed) ||
	    posLegalMoveExistsPiece(pos, PieceTypeKnight, allowed) ||
	    posLegalMoveExistsPiece(pos, PieceTypeQueen, allowed) ||
	    posLegalMoveExistsPiece(pos, PieceTypeRook, allowed) ||
	    posLegalMoveExistsPiece(pos, PieceTypeBishopL, allowed) ||
	    posLegalMoveExistsPiece(pos, PieceTypeBishopD, allowed))
		goto success;

	// Pawns.
	Piece piece=pieceMake(PieceTypePawn, stm);
	BB pawns=posGetBBPiece(pos, piece);
	BB forwardPawns=(bbForwardOne(pawns, stm) & ~occ);
	int delta;
	BB set;

	if (type & MoveTypeQuiet) {
		// Pawns - standard move forward.
		delta=(stm==ColourWhite ? 8 : -8);
		set=(forwardPawns & ~(stm==ColourWhite ? bbRank(Rank8) : bbRank(Rank1)));
		while(set) {
			Sq toSq=bbScanReset(&set);
			Sq fromSq=toSq-delta;
			Move move=moveMake(fromSq, toSq, piece);
			if (posCanMakeMove(pos, move))
				goto success;
		}

		// Pawns - double first move.
		delta=(stm==ColourWhite ? 16 : -16);
		set=(bbForwardOne(forwardPawns, stm) & ~occ);
		set&=(stm==ColourWhite ? bbRank(Rank4) : bbRank(Rank5));
		while(set) {
			Sq toSq=bbScanReset(&set);
			Sq fromSq=toSq-delta;
			Move move=moveMake(fromSq, toSq, piece);
			if (posCanMakeMove(pos, move))
				goto success;
		}
	}

	if (type & MoveTypeCapture) {
		// Pawns - west captures.
		delta=(stm==ColourWhite ? 7 : -9);
		set=(bbWestOne(forwardPawns) & opp);
		while(set) {
			Sq toSq=bbScanReset(&set);
			Sq fromSq=toSq-delta;
			Piece toPiece=((sqRank(toSq)==Rank1 || sqRank(toSq)==Rank8) ? pieceMake(PieceTypeQueen, stm) : piece);
			Move move=moveMake(fromSq, toSq, toPiece);
			if (posCanMakeMove(pos, move))
				goto success;
		}

		// Pawns - east captures.
		delta=(stm==ColourWhite ? 9 : -7);
		set=(bbEastOne(forwardPawns) & opp);
		while(set) {
			Sq toSq=bbScanReset(&set);
			Sq fromSq=toSq-delta;
			Piece toPiece=((sqRank(toSq)==Rank1 || sqRank(toSq)==Rank8) ? pieceMake(PieceTypeQueen, stm) : piece);
			Move move=moveMake(fromSq, toSq, toPiece);
			if (posCanMakeMove(pos, move))
				goto success;
		}

		// Pawns - en-passent captures.
		if (pos->data->epSq!=SqInvalid) {
			Sq toSq=pos->data->epSq, fromSq;

			// Left capture.
			if (sqFile(pos->data->epSq)!=FileH && posGetPieceOnSq(pos, fromSq=sqEastOne(sqBackwardOne(toSq, stm)))==piece) {
				Move move=moveMake(fromSq, toSq, piece);
				if (posCanMakeMove(pos, move))
					goto success;
			}

			// Right capture.
			if (sqFile(pos->data->epSq)!=FileA && posGetPieceOnSq(pos, fromSq=sqWestOne(sqBackwardOne(toSq, stm)))==piece) {
				Move move=moveMake(fromSq, toSq, piece);
				if (posCanMakeMove(pos, move))
					goto success;
			}
		}
	}

	// No moves available.
	assert(posGenLegalMove(pos, type)==MoveInvalid);
	return false;

	// At least one move available.
	success:
	assert(posGenLegalMove(pos, type)!=MoveInvalid);
	return true;
}

bool posHasPieces(const Pos *pos, Colour colour) {
	assert(colourIsValid(colour));

	BB pawns=posGetBBPiece(pos, pieceMake(PieceTypePawn, colour));
	BB king=posGetBBPiece(pos, pieceMake(PieceTypeKing, colour));
	return (posGetBBColour(pos, colour) & ~(pawns | king))!=BBNone;
}

bool posMoveIsPseudoLegal(const Pos *pos, Move move) {
	bool result=posMoveIsPseudoLegalInternal(pos, move);
#	ifndef NDEBUG
	Moves moves;
	movesInit(&moves, pos, 0, MoveTypeAny);
	Move move2;
	bool trueResult=false;
	while((move2=movesNext(&moves))!=MoveInvalid)
		if (move2==move) {
			trueResult=true;
			break;
		}
	assert(result==trueResult);
#	endif
	return result;
}

MoveType posMoveGetType(const Pos *pos, Move move){
	// Castling? (special case to avoid worrying about wrong capPiece)
	if (posMoveIsCastling(pos, move))
		return MoveTypeQuiet;

	// Standard capture?
	Sq toSqRaw=moveGetToSqRaw(move);
	assert(toSqRaw==posMoveGetToSqTrue(pos, move)); // Should only differ in castling moves, but these are checked above
	Piece capPiece=posGetPieceOnSq(pos, toSqRaw);
	if (capPiece!=PieceNone)
		return MoveTypeCapture;

	// Promotion?
	Sq fromSq=moveGetFromSq(move);
	Piece fromPiece=posGetPieceOnSq(pos, fromSq);
	Piece toPiece=moveGetToPiece(move);
	if (fromPiece!=toPiece)
		return MoveTypeCapture;

	// En-passent capture?
	assert(capPiece==PieceNone);
	if (pieceGetType(fromPiece)==PieceTypePawn && sqFile(fromSq)!=sqFile(toSqRaw))
		return MoveTypeCapture;

	// Otherwise must be quiet.
	return MoveTypeQuiet;
}

bool posMoveIsPromotion(const Pos *pos, Move move) {
	Piece fromPiece=posGetPieceOnSq(pos, moveGetFromSq(move));
	bool result=(fromPiece!=moveGetToPiece(move));
	assert(!result || pieceGetType(fromPiece)==PieceTypePawn);
	return result;
}

bool posMoveIsCastling(const Pos *pos, Move move) {
	return posMoveIsCastlingA(pos, move)|posMoveIsCastlingH(pos, move);
}

bool posMoveIsCastlingA(const Pos *pos, Move move) {
	// We can simply check if we are moving into a castling rook
	assert(moveIsValid(move));
	return (pieceGetType(moveGetToPiece(move))==PieceTypeKing && moveGetToSqRaw(move)==pos->data->castRights.rookSq[posGetSTM(pos)][CastSideA]);
}

bool posMoveIsCastlingH(const Pos *pos, Move move) {
	// We can simply check if we are moving into a castling rook
	assert(moveIsValid(move));
	return (pieceGetType(moveGetToPiece(move))==PieceTypeKing && moveGetToSqRaw(move)==pos->data->castRights.rookSq[posGetSTM(pos)][CastSideH]);
}

Move posMoveFromStr(const Pos *pos, const char str[static 6]){
	Moves moves;
	movesInit(&moves, pos, 0, MoveTypeAny);
	Move move;
	while((move=movesNext(&moves))!=MoveInvalid)
		if (strcmp(str, POSMOVETOSTR(pos, move))==0)
			return move;
	return MoveInvalid;
}

void posMoveToStr(const Pos *pos, Move move, char str[static 6]) {
	// Special case for invalid moves.
	if (!moveIsValid(move)) {
		strcpy(str, "0000");
		return;
	}

	// Sanity checks.
	assert(posGetSTM(pos)==moveGetColour(move));

	// From/to squares.
	Sq fromSq=moveGetFromSq(move);
	Sq toSq=(uciGetChess960() ? moveGetToSqRaw(move) : posMoveGetToSqTrue(pos, move));

	// Promotion?
	Piece fromPiece=posGetPieceOnSq(pos, fromSq);
	assert(fromPiece!=PieceNone && pieceGetColour(fromPiece)==posGetSTM(pos));
	Piece toPiece=moveGetToPiece(move);
	bool isPromo=(fromPiece!=toPiece);
	assert(!isPromo || sqRank(toSq)==(posGetSTM(pos)==ColourWhite ? Rank8 : Rank1));

	// Create string
	str[0]=fileToChar(sqFile(fromSq));
	str[1]=rankToChar(sqRank(fromSq));
	str[2]=fileToChar(sqFile(toSq));
	str[3]=rankToChar(sqRank(toSq));
	str[4]=(isPromo ? pieceTypeToPromoChar(pieceGetType(toPiece)) : '\0');
	str[5]='\0';
}

Sq posMoveGetToSqTrue(const Pos *pos, Move move) {
	if (posMoveIsCastling(pos, move)) {
		File file=(moveGetToSqRaw(move)<moveGetFromSq(move) ? FileC : FileG);
		Rank rank=(moveGetColour(move)==ColourWhite ? Rank1 : Rank8);
		return sqMake(file, rank);
	} else
		return moveGetToSqRaw(move);
}

void posCastRightsToStr(CastRights castRights, char str[static 8]) {
	char temp[4];
	str[0]='\0';
	if (uciGetChess960()) {
		Colour colour;
		CastSide castSide;
		for(colour=ColourWhite; colour<=ColourBlack; ++colour) {
			for(castSide=CastSideA; castSide<=CastSideH; ++castSide) {
				Sq sq=castRights.rookSq[colour][castSide];
				if (sq!=SqInvalid) {
					char c=fileToChar(sqFile(sq));
					if (colour==ColourWhite)
						c=toupper(c);
					sprintf(temp, "%c", c);
					strcat(str, temp);
				}
			}
		}
	} else {
		if (castRights.rookSq[ColourWhite][CastSideH]!=SqInvalid)
			strcat(str, "K");
		if (castRights.rookSq[ColourWhite][CastSideA]!=SqInvalid)
			strcat(str, "Q");
		if (castRights.rookSq[ColourBlack][CastSideH]!=SqInvalid)
			strcat(str, "k");
		if (castRights.rookSq[ColourBlack][CastSideA]!=SqInvalid)
			strcat(str, "q");
	}

	if (str[0]=='\0')
		strcat(str, "-");
}

CastRights posCastRightsFromStr(const char *str, const Piece pieceArray[SqNB]) {
	CastRights castRights;
	castRights.rookSq[ColourWhite][CastSideA]=SqInvalid;
	castRights.rookSq[ColourWhite][CastSideH]=SqInvalid;
	castRights.rookSq[ColourBlack][CastSideA]=SqInvalid;
	castRights.rookSq[ColourBlack][CastSideH]=SqInvalid;

	const char *c=str;
	if (uciGetChess960()) {
		Sq kingSq[ColourNB]={[ColourWhite]=SqInvalid, [ColourBlack]=SqInvalid};
		Sq sq;
		for(sq=0; sq<SqNB; ++sq)
			if (pieceGetType(pieceArray[sq])==PieceTypeKing)
				kingSq[pieceGetColour(pieceArray[sq])]=sq;

		for(;;++c) {
			if (*c>='A' && *c<='H') {
				Sq rookSq=sqMake(fileFromChar(tolower(*c)), Rank1);
				if (pieceArray[rookSq]!=pieceMake(PieceTypeRook, ColourWhite))
					continue;

				CastSide castSide=(rookSq<kingSq[ColourWhite] ? CastSideA : CastSideH);
				castRights.rookSq[ColourWhite][castSide]=rookSq;
			} else if (*c>='a' && *c<='h') {
				Sq rookSq=sqMake(fileFromChar(*c), Rank8);
				if (pieceArray[rookSq]!=pieceMake(PieceTypeRook, ColourBlack))
					continue;

				CastSide castSide=(rookSq<kingSq[ColourBlack] ? CastSideA : CastSideH);
				castRights.rookSq[ColourBlack][castSide]=rookSq;
			} else
				return castRights;
		}
	} else {
		while(1)
			switch(*c++) {
				case 'K': castRights.rookSq[ColourWhite][CastSideH]=SqH1; break;
				case 'Q': castRights.rookSq[ColourWhite][CastSideA]=SqA1; break;
				case 'k': castRights.rookSq[ColourBlack][CastSideH]=SqH8; break;
				case 'q': castRights.rookSq[ColourBlack][CastSideA]=SqA8; break;
				default: return castRights; break;
			}
	}
}

void posMirror(Pos *pos) {
	// Create mirrored board and remove pieces from given Pos.
	Piece board[SqNB];
	Sq sq;
	for(sq=0;sq<SqNB;++sq) {
		board[sq]=posGetPieceOnSq(pos, sqMirror(sq));
		if (board[sq]!=PieceNone)
			posPieceRemove(pos, sqMirror(sq), true);
	}

	// Add pieces from mirrored board.
	for(sq=0;sq<SqNB;++sq)
		if (board[sq]!=PieceNone)
			posPieceAdd(pos, board[sq], sq, true);

	// Mirror other fields.
	if (pos->data->epSq!=SqInvalid)
		pos->data->epSq=sqMirror(pos->data->epSq);
	if (pos->data->capSq!=SqInvalid)
		pos->data->capSq=sqMirror(pos->data->capSq);

	CastRights oldCastRights=pos->data->castRights;
	pos->data->castRights.rookSq[ColourWhite][CastSideA]=(oldCastRights.rookSq[ColourWhite][CastSideH]!=SqInvalid ? sqMirror(oldCastRights.rookSq[ColourWhite][CastSideH]) : SqInvalid);
	pos->data->castRights.rookSq[ColourWhite][CastSideH]=(oldCastRights.rookSq[ColourWhite][CastSideA]!=SqInvalid ? sqMirror(oldCastRights.rookSq[ColourWhite][CastSideA]) : SqInvalid);
	pos->data->castRights.rookSq[ColourBlack][CastSideA]=(oldCastRights.rookSq[ColourBlack][CastSideH]!=SqInvalid ? sqMirror(oldCastRights.rookSq[ColourBlack][CastSideH]) : SqInvalid);
	pos->data->castRights.rookSq[ColourBlack][CastSideH]=(oldCastRights.rookSq[ColourBlack][CastSideA]!=SqInvalid ? sqMirror(oldCastRights.rookSq[ColourBlack][CastSideA]) : SqInvalid);

	// Update keys.
	pos->data->key=posComputeKey(pos);
	pos->pawnKey=posComputePawnKey(pos);
	pos->matKey=posComputeMatKey(pos);
}

void posFlip(Pos *pos) {
	// Create flipped board and remove pieces from given Pos.
	Piece board[SqNB];
	Sq sq;
	for(sq=0;sq<SqNB;++sq) {
		board[sq]=posGetPieceOnSq(pos, sqFlip(sq));
		if (board[sq]!=PieceNone) {
			PieceType pieceType=pieceGetType(board[sq]);
			Colour pieceColour=pieceGetColour(board[sq]);
			board[sq]=pieceMake(pieceType, colourSwap(pieceColour));
			posPieceRemove(pos, sqFlip(sq), true);
		}
	}

	// Add pieces from flipped board.
	for(sq=0;sq<SqNB;++sq)
		if (board[sq]!=PieceNone)
			posPieceAdd(pos, board[sq], sq, true);

	// Flip other fields.
	pos->stm=colourSwap(pos->stm);
	if (pos->data->epSq!=SqInvalid)
		pos->data->epSq=sqMirror(pos->data->epSq);
	if (pos->data->capSq!=SqInvalid)
		pos->data->capSq=sqMirror(pos->data->capSq);

	CastRights oldCastRights=pos->data->castRights;
	pos->data->castRights.rookSq[ColourWhite][CastSideA]=(oldCastRights.rookSq[ColourBlack][CastSideH]!=SqInvalid ? sqFlip(oldCastRights.rookSq[ColourBlack][CastSideH]) : SqInvalid);
	pos->data->castRights.rookSq[ColourWhite][CastSideH]=(oldCastRights.rookSq[ColourBlack][CastSideA]!=SqInvalid ? sqFlip(oldCastRights.rookSq[ColourBlack][CastSideA]) : SqInvalid);
	pos->data->castRights.rookSq[ColourBlack][CastSideA]=(oldCastRights.rookSq[ColourWhite][CastSideH]!=SqInvalid ? sqFlip(oldCastRights.rookSq[ColourWhite][CastSideH]) : SqInvalid);
	pos->data->castRights.rookSq[ColourBlack][CastSideH]=(oldCastRights.rookSq[ColourWhite][CastSideA]!=SqInvalid ? sqFlip(oldCastRights.rookSq[ColourWhite][CastSideA]) : SqInvalid);

	// Update keys.
	pos->data->key=posComputeKey(pos);
	pos->pawnKey=posComputePawnKey(pos);
	pos->matKey=posComputeMatKey(pos);
}

////////////////////////////////////////////////////////////////////////////////
// Private functions.
////////////////////////////////////////////////////////////////////////////////

void posClean(Pos *pos) {
	memset(pos->bbPiece, 0, PieceNB*sizeof(BB));
	memset(pos->array64, PieceNone, SqNB*sizeof(uint8_t));
	pos->bbColour[ColourWhite]=pos->bbColour[ColourBlack]=pos->bbAll=BBNone;
	pos->stm=ColourWhite;
	pos->fullMoveNumber=1;
	pos->pawnKey=0;
	pos->matKey=0;
	pos->pstScore=VPairZero;
	pos->data=pos->dataStart;
	pos->data->lastMove=MoveInvalid;
	pos->data->lastMoveWasPromo=false;
	pos->data->halfMoveNumber=0;
	pos->data->epSq=SqInvalid;
	pos->data->castRights=CastRightsNone;
	pos->data->capPiece=PieceNone;
	pos->data->capSq=SqInvalid;
	pos->data->key=0;
}

void posPieceAdd(Pos *pos, Piece piece, Sq sq, bool skipMainKeyUpdate) {
	// Sanity checks.
	assert(pieceIsValid(piece));
	assert(sqIsValid(sq));
	assert(posGetPieceOnSq(pos, sq)==PieceNone);

	// Update position.
	pos->bbPiece[piece]^=bbSq(sq);
	pos->bbColour[pieceGetColour(piece)]^=bbSq(sq);
	pos->bbAll^=bbSq(sq);
	pos->array64[sq]=piece;

	// Update hash keys.
	if (!skipMainKeyUpdate)
		pos->data->key^=posKeyPiece[piece][sq];
	pos->pawnKey^=posPawnKeyPiece[piece][sq];
	pos->matKey+=posMatKey[piece];

	// Update PST score.
	evalVPairAddTo(&pos->pstScore, &evalPST[piece][sq]);
}

void posPieceRemove(Pos *pos, Sq sq, bool skipMainKeyUpdate) {
	// Sanity checks.
	assert(sqIsValid(sq));
	assert(posGetPieceOnSq(pos, sq)!=PieceNone);

	// Update position.
	Piece piece=posGetPieceOnSq(pos, sq);
	pos->bbPiece[piece]^=bbSq(sq);
	pos->bbColour[pieceGetColour(piece)]^=bbSq(sq);
	pos->bbAll^=bbSq(sq);
	pos->array64[sq]=PieceNone;

	// Update hash keys.
	if (!skipMainKeyUpdate)
		pos->data->key^=posKeyPiece[piece][sq];
	pos->pawnKey^=posPawnKeyPiece[piece][sq];
	pos->matKey-=posMatKey[piece];

	// Update PST score.
	evalVPairSubFrom(&pos->pstScore, &evalPST[piece][sq]);
}

void posPieceMove(Pos *pos, Sq fromSq, Sq toSq, bool skipMainKeyUpdate) {
	// Sanity checks.
	assert(sqIsValid(fromSq) && sqIsValid(toSq));
	assert(toSq!=fromSq);
	assert(posGetPieceOnSq(pos, fromSq)!=PieceNone);
	assert(posGetPieceOnSq(pos, toSq)==PieceNone);

	// Update position.
	Piece piece=posGetPieceOnSq(pos, fromSq);
	pos->bbPiece[piece]^=bbSq(fromSq)^bbSq(toSq);
	pos->bbColour[pieceGetColour(piece)]^=bbSq(fromSq)^bbSq(toSq);
	pos->bbAll^=bbSq(fromSq)^bbSq(toSq);
	pos->array64[toSq]=piece;
	pos->array64[fromSq]=PieceNone;

	// Update hash keys.
	if (!skipMainKeyUpdate)
		pos->data->key^=posKeyPiece[piece][fromSq]^posKeyPiece[piece][toSq];
	pos->pawnKey^=posPawnKeyPiece[piece][fromSq]^posPawnKeyPiece[piece][toSq];

	// Update PST score.
	evalVPairSubFrom(&pos->pstScore, &evalPST[piece][fromSq]);
	evalVPairAddTo(&pos->pstScore, &evalPST[piece][toSq]);
}

void posPieceMoveChange(Pos *pos, Sq fromSq, Sq toSq, Piece toPiece, bool skipMainKeyUpdate) {
	// Sanity checks.
	assert(sqIsValid(fromSq) && sqIsValid(toSq));
	assert(toSq!=fromSq);
	assert(posGetPieceOnSq(pos, fromSq)!=PieceNone);
	assert(posGetPieceOnSq(pos, toSq)==PieceNone);
	assert(pieceIsValid(toPiece));
	assert(pieceGetColour(toPiece)==pieceGetColour(posGetPieceOnSq(pos, fromSq)));

	// Update position.
	posPieceRemove(pos, fromSq, skipMainKeyUpdate);
	posPieceAdd(pos, toPiece, toSq, skipMainKeyUpdate);
}

void posGenPseudoNormal(Moves *moves, BB allowed) {
	// Init.
#	define PUSH(m) movesPush(moves, (m))
	const Pos *pos=movesGetPos(moves);
	Colour stm=posGetSTM(pos);
	allowed&=~posGetBBColour(pos, stm); // Don't want to self-capture.
	BB occ=posGetBBAll(pos);

	// Loop over each piece type.
	PieceType type;
	for(type=PieceTypeKnight;type<=PieceTypeKing;++type) {
		// Loop over each piece of this type.
		Piece piece=pieceMake(type, stm);
		BB pieceSet=posGetBBPiece(pos, piece);
		while(pieceSet) {
			Sq fromSq=bbScanReset(&pieceSet);
			BB moveSet=(attacksPiece(piece, fromSq, occ) & allowed);
			while(moveSet)
				PUSH(moveMake(fromSq, bbScanReset(&moveSet), piece));
		}
	}
#	undef PUSH
}

void posGenPseudoPawnMoves(Moves *moves, MoveType type) {
#	define PUSH(m) movesPush(moves, (m))
	const Pos *pos=movesGetPos(moves);
	Colour stm=posGetSTM(pos);
	BB opp=posGetBBColour(pos, colourSwap(stm));
	BB empty=~posGetBBAll(pos);
	Piece piece=pieceMake(PieceTypePawn, stm);
	BB pawns=posGetBBPiece(pos, piece);
	BB forwardPawns=bbForwardOne(pawns, stm);
	BB backRanks=(bbRank(Rank1) | bbRank(Rank8));

	if (type & MoveTypeCapture) {
		BB set, set2;

		// Forward promotions.
		set=(forwardPawns & empty & backRanks);
		while(set) {
			Sq toSq=bbScanReset(&set);
			Sq fromSq=sqBackwardOne(toSq, stm);
			PUSH(moveMake(fromSq, toSq, pieceMake(PieceTypeQueen, stm)));
			PUSH(moveMake(fromSq, toSq, pieceMake(PieceTypeRook, stm)));
			PUSH(moveMake(fromSq, toSq, pieceMake(sqIsLight(toSq) ? PieceTypeBishopL : PieceTypeBishopD, stm)));
			PUSH(moveMake(fromSq, toSq, pieceMake(PieceTypeKnight, stm)));
		}

		// Capture left.
		set=bbWestOne(forwardPawns) & opp;
		set2=(set & backRanks);
		set&=~backRanks;
		while(set2) {
			Sq toSq=bbScanReset(&set2);
			Sq fromSq=sqEastOne(sqBackwardOne(toSq, stm));
			PUSH(moveMake(fromSq, toSq, pieceMake(PieceTypeQueen, stm)));
			PUSH(moveMake(fromSq, toSq, pieceMake(PieceTypeRook, stm)));
			PUSH(moveMake(fromSq, toSq, pieceMake(sqIsLight(toSq) ? PieceTypeBishopL : PieceTypeBishopD, stm)));
			PUSH(moveMake(fromSq, toSq, pieceMake(PieceTypeKnight, stm)));
		}
		while(set) {
			Sq toSq=bbScanReset(&set);
			Sq fromSq=sqEastOne(sqBackwardOne(toSq, stm));
			PUSH(moveMake(fromSq, toSq, piece));
		}

		// Capture right.
		set=bbEastOne(forwardPawns) & opp;
		set2=(set & backRanks);
		set&=~backRanks;
		while(set2) {
			Sq toSq=bbScanReset(&set2);
			Sq fromSq=sqWestOne(sqBackwardOne(toSq, stm));
			PUSH(moveMake(fromSq, toSq, pieceMake(PieceTypeQueen, stm)));
			PUSH(moveMake(fromSq, toSq, pieceMake(PieceTypeRook, stm)));
			PUSH(moveMake(fromSq, toSq, pieceMake(sqIsLight(toSq) ? PieceTypeBishopL : PieceTypeBishopD, stm)));
			PUSH(moveMake(fromSq, toSq, pieceMake(PieceTypeKnight, stm)));
		}
		while(set) {
			Sq toSq=bbScanReset(&set);
			Sq fromSq=sqWestOne(sqBackwardOne(toSq, stm));
			PUSH(moveMake(fromSq, toSq, piece));
		}

		// En-passent captures.
		if (pos->data->epSq!=SqInvalid) {
			Sq toSq=pos->data->epSq, fromSq;

			// Left capture.
			if (sqFile(pos->data->epSq)!=FileH && posGetPieceOnSq(pos, fromSq=sqEastOne(sqBackwardOne(toSq, stm)))==piece)
				PUSH(moveMake(fromSq, toSq, piece));

			// Right capture.
			if (sqFile(pos->data->epSq)!=FileA && posGetPieceOnSq(pos, fromSq=sqWestOne(sqBackwardOne(toSq, stm)))==piece)
				PUSH(moveMake(fromSq, toSq, piece));
		}
	}

	if (type & MoveTypeQuiet) {
		int delta=(stm==ColourWhite ? 8 : -8);
		BB allowed=(empty & ~backRanks);
		BB one=(forwardPawns & allowed);
		BB two=(bbForwardOne(one, stm) & allowed & (stm==ColourWhite ? bbRank(Rank4) : bbRank(Rank5)));

		// Standard move forward.
		while(one) {
			Sq toSq=bbScanReset(&one);
			PUSH(moveMake(toSq-delta, toSq, piece));
		}

		// Double first move.
		while(two) {
			Sq toSq=bbScanReset(&two);
			PUSH(moveMake(toSq-delta-delta, toSq, piece));
		}
	}
#	undef PUSH
}

void posGenPseudoCast(Moves *moves) {
#	define PUSH(m) movesPush(moves, (m))

	const Pos *pos=movesGetPos(moves);

	Colour stm=posGetSTM(pos);
	Sq kingFromSq=posGetKingSq(pos, stm);
	Rank backRank=(stm==ColourWhite ? Rank1 : Rank8);
	int castSide;
	for(castSide=CastSideA; castSide<=CastSideH; ++castSide) {
		if (pos->data->castRights.rookSq[stm][castSide]!=SqInvalid) {
			Sq rookFromSq=pos->data->castRights.rookSq[stm][castSide];
			Sq rookToSq=sqMake((castSide==CastSideA ? FileD : FileF), backRank);
			Sq kingToSq=sqMake((castSide==CastSideA ? FileC : FileG), backRank);

			assert(pieceGetType(posGetPieceOnSq(pos, rookFromSq))==PieceTypeRook);
			assert(pieceGetColour(posGetPieceOnSq(pos, rookFromSq))==stm);

			BB kingSpan=bbBetween(kingFromSq, kingToSq)|bbSq(kingToSq);
			BB rookSpan=bbBetween(rookFromSq, rookToSq)|bbSq(rookToSq);

			if (!(((kingSpan | rookSpan) & ~(bbSq(rookFromSq)|bbSq(kingFromSq)))&posGetBBAll(pos)))
				PUSH(moveMake(kingFromSq, rookFromSq, pieceMake(PieceTypeKing, stm)));
		}
	}

#	undef PUSH
}

Key posComputeKey(const Pos *pos) {
	// En-passent square and castling rights.
	Key key=posKeyEP[pos->data->epSq]^posKeyCastling[pos->data->castRights.rookSq[ColourWhite][CastSideA]]
	                                 ^posKeyCastling[pos->data->castRights.rookSq[ColourWhite][CastSideH]]
	                                 ^posKeyCastling[pos->data->castRights.rookSq[ColourBlack][CastSideA]]
	                                 ^posKeyCastling[pos->data->castRights.rookSq[ColourBlack][CastSideH]];

	// Colour.
	if (posGetSTM(pos)==ColourBlack)
		key^=posKeySTM;

	// Pieces.
	Sq sq;
	for(sq=0;sq<SqNB;++sq)
		key^=posKeyPiece[posGetPieceOnSq(pos, sq)][sq];

	return key;
}

Key posComputePawnKey(const Pos *pos) {
	// Only piece placement is important for pawn key.
	Key key=0;
	Sq sq;
	for(sq=0;sq<SqNB;++sq)
		key^=posPawnKeyPiece[posGetPieceOnSq(pos, sq)][sq];

	return key;
}

Key posComputeMatKey(const Pos *pos) {
	Key key=0;

	Colour colour;
	PieceType pieceType;
	for(colour=ColourWhite; colour<=ColourBlack; ++colour)
		for(pieceType=PieceTypePawn; pieceType<=PieceTypeQueen; ++pieceType) {
			Piece piece=pieceMake(pieceType, colour);
			unsigned count=posGetPieceCount(pos, piece);
			key+=count*posMatKey[piece];
		}

	return key;
}

Key posRandKey(void) {
	return utilRand64();
}

bool posIsEPCap(const Pos *pos, Sq sq) {
	// No pawn to capture? (i.e. bad FEN string)
	Colour stm=posGetSTM(pos);
	Colour xstm=colourSwap(stm);
	Piece victim=pieceMake(PieceTypePawn, xstm);
	Sq victimSq=sq^8;
	if (posGetPieceOnSq(pos, victimSq)!=victim)
		return false;

	// Simulate capturing the pawn.
	BB occ=posGetBBAll(pos);
	assert(occ&bbSq(victimSq)); // To make XOR trick work.
	occ^=bbSq(victimSq);

	// Check if capturing pawn(s) are pinned.
	Piece attacker=pieceMake(PieceTypePawn, stm);
	Sq kingSq=posGetKingSq(pos, stm);
	return ((sqFile(victimSq)!=FileA && posGetPieceOnSq(pos, sqWestOne(victimSq))==attacker && !posIsPiecePinned(pos, occ, xstm, sqWestOne(victimSq), kingSq)) ||
	        (sqFile(victimSq)!=FileH && posGetPieceOnSq(pos, sqEastOne(victimSq))==attacker && !posIsPiecePinned(pos, occ, xstm, sqEastOne(victimSq), kingSq)));
}

bool posIsPiecePinned(const Pos *pos, BB occ, Colour atkColour, Sq pinnedSq, Sq victimSq) {
	// Sanity checks.
	assert(posGetPieceOnSq(pos, pinnedSq)!=PieceNone);
	assert(atkColour==colourSwap(pieceGetColour(posGetPieceOnSq(pos, pinnedSq))));

	// Anything between victim and 'pinned' piece?
	BB between=bbBetween(pinnedSq, victimSq);
	if (between & occ)
		return false;

	// Test if the victim would be attacked if the 'pinned' piece is removed,
	assert(occ&bbSq(pinnedSq)); // To make XOR trick work.
	occ^=bbSq(pinnedSq);
	BB beyond=bbBeyond(victimSq, pinnedSq);
	int x1=sqFile(pinnedSq), y1=sqRank(pinnedSq);
	int x2=sqFile(victimSq), y2=sqRank(victimSq);
	if (x1==x2 || y1==y2) { // Horizontal/vertical.
		BB ray=(attacksRook(victimSq, occ) & beyond);
		if (ray & (posGetBBPiece(pos, pieceMake(PieceTypeRook, atkColour)) |
		           posGetBBPiece(pos, pieceMake(PieceTypeQueen, atkColour))))
			return true;
	} else if (x1+y1==x2+y2 || y1-x1==y2-x2) { // Major/minor diagonal.
		BB ray=(attacksBishop(victimSq, occ) & beyond);
		if (ray & (posGetBBPiece(pos, pieceMake(PieceTypeBishopL, atkColour)) |
		           posGetBBPiece(pos, pieceMake(PieceTypeBishopD, atkColour)) |
		           posGetBBPiece(pos, pieceMake(PieceTypeQueen, atkColour))))
			return true;
	}

	return false;
}

bool posIsConsistent(const Pos *pos) {
	char error[512];
	Sq sq;

	// Test bitboards are self consistent.
	BB wAll=BBNone, bAll=BBNone;
	PieceType type1, type2;
	for(type1=PieceTypePawn;type1<=PieceTypeKing;++type1) {
		for(type2=type1+1;type2<=PieceTypeKing;++type2) {
			BB w1=pos->bbPiece[pieceMake(type1, ColourWhite)];
			BB b1=pos->bbPiece[pieceMake(type1, ColourBlack)];
			BB w2=pos->bbPiece[pieceMake(type2, ColourWhite)];
			BB b2=pos->bbPiece[pieceMake(type2, ColourBlack)];
			if ((w1 & w2) || (w1 & b1) || (w1 & b2) || (w2 & b1) || (w2 & b2) || (b1 & b2)) {
				sprintf(error, "Error: Bitboards for piece types %i and %i intersect (in "
											 "some way, and some colour combo).\n", type1, type2);
				goto Error;
			}
		}
		wAll|=pos->bbPiece[pieceMake(type1, ColourWhite)];
		bAll|=pos->bbPiece[pieceMake(type1, ColourBlack)];
	}
	if (wAll!=pos->bbColour[ColourWhite] || bAll!=pos->bbColour[ColourBlack]) {
		strcpy(error, "Bitboard 'col[white]' or 'col[black]' error.\n");
		goto Error;
	}
	if ((wAll | bAll)!=pos->bbAll) {
		strcpy(error, "Bitboard 'all' error.\n");
		goto Error;
	}

	// Test array64 agrees with bitboards.
	for(sq=0; sq<SqNB; ++sq) {
		BB bb=bbSq(sq);
		Piece piece=pos->array64[sq];
		if (piece!=PieceNone) {
			if ((pos->bbPiece[piece] & bb)==BBNone) {
				sprintf(error, "Piece %i in array64 for %c%c, but not set in bitboards.\n", piece, fileToChar(sqFile(sq)), rankToChar(sqRank(sq)));
				goto Error;
			}
		} else {
			if ((pos->bbAll & bb)!=BBNone) {
				sprintf(error, "%c%c empty in array64, but set in bitboards.\n", fileToChar(sqFile(sq)), rankToChar(sqRank(sq)));
				goto Error;
			}
		}
	}

	// Test we have exactly 1 king of each colour.
	int wKingCount=bbPopCount(pos->bbPiece[PieceWKing]);
	int bKingCount=bbPopCount(pos->bbPiece[PieceBKing]);
	if (wKingCount!=1 || bKingCount!=1) {
		sprintf(error, "bad king counts: (white,black)=(%i,%i).\n", wKingCount, bKingCount);
		goto Error;
	}

	// Test correct bishop pieces are correct (either light or dark).
	for(sq=0;sq<SqNB;++sq) {
		if ((posGetPieceOnSq(pos, sq)==PieceWBishopL || posGetPieceOnSq(pos, sq)==PieceBBishopL) && !sqIsLight(sq)) {
			sprintf(error, "Light bishop on dark square %c%c.\n", fileToChar(sqFile(sq)), rankToChar(sqRank(sq)));
			goto Error;
		}
		if ((posGetPieceOnSq(pos, sq)==PieceWBishopD || posGetPieceOnSq(pos, sq)==PieceBBishopD) && sqIsLight(sq)) {
			sprintf(error, "Dark bishop on light square %c%c.\n", fileToChar(sqFile(sq)), rankToChar(sqRank(sq)));
			goto Error;
		}
	}

	// Test EP square is valid.
	if (pos->data->epSq!=SqInvalid && !posIsEPCap(pos, pos->data->epSq)) {
		sprintf(error, "Position has invalid ep capture sq %c%c.\n",
						fileToChar(sqFile(pos->data->epSq)), rankToChar(sqRank(pos->data->epSq)));
		goto Error;
	}

	// Test hash keys match.
	Key trueKey=posComputeKey(pos);
	Key key=posGetKey(pos);
	if (key!=trueKey) {
		sprintf(error, "Current key is %016"PRIxKey" while true key is %016"PRIxKey".\n",
						key, trueKey);
		goto Error;
	}

	// Test pawn hash keys match.
	Key truePawnKey=posComputePawnKey(pos);
	Key pawnKey=posGetPawnKey(pos);
	if (pawnKey!=truePawnKey) {
		sprintf(error, "Current pawn key is %016"PRIxKey" while true key is %016"PRIxKey".\n",
						pawnKey, truePawnKey);
		goto Error;
	}

	// Test mat hash keys match.
	Key trueMatKey=posComputeMatKey(pos);
	Key matKey=posGetMatKey(pos);
	if (matKey!=trueMatKey) {
		sprintf(error, "Current mat key is %016"PRIxKey" while true key is %016"PRIxKey".\n",
						matKey, trueMatKey);
		goto Error;
	}

	// Test PST score is accurate.
	VPair truePstScore=evalComputePstScore(pos);
	if (pos->pstScore.mg!=truePstScore.mg || pos->pstScore.eg!=truePstScore.eg) {
		sprintf(error, "Current pst score is (%i,%i) while true is (%i,%i).\n",
						pos->pstScore.mg, pos->pstScore.eg, truePstScore.mg, truePstScore.eg);
		goto Error;
	}

	return true;

	Error:
#	ifndef NDEBUG
	uciWrite("---------------------------------\n");
	uciWrite("posIsConsistent() failed:\n");
	uciWrite(error);
	posDraw(pos);
	uciWrite("---------------------------------\n");
#	endif
	return false;
}

unsigned int matInfoShift(Piece piece) {
	assert(pieceIsValid(piece));
	return 4*(2*pieceGetType(piece)+pieceGetColour(piece));
}

bool posMoveIsPseudoLegalInternal(const Pos *pos, Move move) {
	// Special cases.
	if (!moveIsValid(move))
		return false;

	// We can now assume that move was generated by one of the posGenX()
	// functions (just not necessarily for the current position).
	// Hence we can assume piece movements are valid (although they may be blocked
	// in the current position).

	// Is move of correct colour for stm?
	Colour stm=posGetSTM(pos);
	Piece toPiece=moveGetToPiece(move);
	if (pieceGetColour(toPiece)!=stm)
		return false;

	// Friendly capture? (deferred for king movements due to castling, see below)
	Sq toSqTrue=posMoveGetToSqTrue(pos, move);
	Piece capPiece=(pieceGetType(toPiece)!=PieceTypeKing ? posGetPieceOnSq(pos, toSqTrue) : PieceNone);
	if (capPiece!=PieceNone && pieceGetColour(capPiece)==stm)
		return false;

	// King capture?
	if (pieceGetType(capPiece)==PieceTypeKing)
		return false;

	// Bad moving piece?
	Sq fromSq=moveGetFromSq(move);
	Piece fromPiece=posGetPieceOnSq(pos, fromSq);
	if (fromPiece==PieceNone || pieceGetColour(fromPiece)!=stm)
		return false;

	// Piece type specific logic.
	BB occ=posGetBBAll(pos);
	unsigned int dX=abs(((int)sqFile(fromSq))-((int)sqFile(toSqTrue)));
	int dY=abs(((int)sqRank(fromSq))-((int)sqRank(toSqTrue)));
	switch(pieceGetType(fromPiece)) {
		case PieceTypePawn: {
			// Valid rank movement?
			int colourDelta=(stm==ColourWhite ? 1 : -1);
			int rankDelta=sqRank(toSqTrue)-sqRank(fromSq);
			if (rankDelta!=colourDelta &&
			    (rankDelta!=2*colourDelta || (sqRank(fromSq)!=Rank2 && sqRank(fromSq)!=Rank7) ||
			     posGetPieceOnSq(pos, (fromSq+toSqTrue)/2)!=PieceNone))
				return false;

			// Valid file movement?
			// The next line was derived from the following equivalent expression:
			// (!(dX==0 && capPiece==PieceNone) && !(dX==1 && (capPiece!=PieceNone || toSqTrue==pos->data->epSq)));
			if ((dX!=0 || capPiece!=PieceNone) && (dX!=1 || (capPiece==PieceNone && toSqTrue!=pos->data->epSq)))
				return false;

			// Valid to-piece?
			PieceType toPieceType=pieceGetType(toPiece);
			if (sqRank(toSqTrue)==Rank1 || sqRank(toSqTrue)==Rank8)
				return (toPieceType>=PieceTypeKnight && toPieceType<=PieceTypeQueen);
			else
				return (toPieceType==PieceTypePawn);
		} break;
		case PieceTypeKnight:
			return (((dX==2 && dY==1) || (dX==1 && dY==2)) && fromPiece==toPiece);
		break;
		case PieceTypeBishopL:
		case PieceTypeBishopD:
			return (dX==dY && fromPiece==toPiece && (bbBetween(fromSq, toSqTrue) & occ)==BBNone);
		break;
		case PieceTypeRook:
			return ((dX==0 || dY==0) && fromPiece==toPiece && (bbBetween(fromSq, toSqTrue) & occ)==BBNone);
		break;
		case PieceTypeQueen:
			return ((dX==0 || dY==0 || dX==dY) && fromPiece==toPiece && (bbBetween(fromSq, toSqTrue) & occ)==BBNone);
		break;
		case PieceTypeKing: {
			// King cannot change.
			if (fromPiece!=toPiece)
				return false;

			// Castling requires extra tests.
			Sq toSqRaw=moveGetToSqRaw(move);
			Piece toSqPiece=posGetPieceOnSq(pos, toSqRaw);

			CastSide castSide;
			if (toSqRaw==pos->data->castRights.rookSq[posGetSTM(pos)][CastSideA])
				castSide=CastSideA;
			else if (toSqRaw==pos->data->castRights.rookSq[posGetSTM(pos)][CastSideH])
				castSide=CastSideH;
			else {
				// Check friendly captures here as we skipped them for the king above
				if (toSqPiece!=PieceNone && pieceGetColour(toSqPiece)==stm)
					return false;

				// Standard move (we still have to check movement in this case as move may have been generated as castling, but there is no rook in this position)
				return (dX<=1 && dY<=1);
			}

			assert(toSqPiece==pieceMake(PieceTypeRook, posGetSTM(pos)));

			Sq rookToSq=sqMake((castSide==CastSideA ? FileD : FileF), (posGetSTM(pos)==ColourWhite ? Rank1 : Rank8));
			BB kingSpan=bbBetween(fromSq, toSqTrue)|bbSq(toSqTrue);
			BB rookSpan=bbBetween(toSqRaw, rookToSq)|bbSq(rookToSq);
			if ((((kingSpan | rookSpan) & ~(bbSq(toSqRaw)|bbSq(fromSq)))&posGetBBAll(pos)))
				return false;

			return true;
		} break;
		default:
			return false;
		break;
	}
	return false;
}

bool posLegalMoveExistsPiece(const Pos *pos, PieceType type, BB allowed) {
	assert(pieceTypeIsValid(type) && type!=PieceTypePawn);

	Colour stm=posGetSTM(pos);
	BB occ=posGetBBAll(pos);
	Piece piece=pieceMake(type, stm);
	BB set=posGetBBPiece(pos, piece);
	while(set) {
		Sq fromSq=bbScanReset(&set);
		BB attacks=(attacksPieceType(type, fromSq, occ) & allowed);
		while(attacks) {
			Sq toSq=bbScanReset(&attacks);
			Move move=moveMake(fromSq, toSq, piece);
			if (posCanMakeMove(pos, move))
				return true;
		}
	}
	return false;
}
