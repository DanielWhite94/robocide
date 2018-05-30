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

struct Pos {
	BB bbPiece[PieceNB];
	uint8_t array64[SqNB]; // One entry per square pointing to location in pieceList.
	Sq pieceList[PieceNB*16]; // 16 entries per piece.
	uint8_t pieceListNext[PieceNB]; // Gives next empty slot in pieceList array.
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
Key posMatKey[PieceNB*16];

////////////////////////////////////////////////////////////////////////////////
// Private prototypes.
////////////////////////////////////////////////////////////////////////////////

void posClean(Pos *pos);

void posPieceAdd(Pos *pos, Piece piece, Sq sq);
void posPieceRemove(Pos *pos, Sq sq);
void posPieceMove(Pos *pos, Sq fromSq, Sq toSq);
void posPieceMoveChange(Pos *pos, Sq fromSq, Sq toSq, Piece toPiece);

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

	posMatKey[0]=0; // For empty squares.
	for(i=1;i<PieceNB*16;++i)
		posMatKey[i]=posRandKey();
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
			posPieceAdd(pos, fen.array[sq], sq);
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
	return (pos->array64[sq]>>4);
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
	return (posGetPieceListEnd(pos, piece)-posGetPieceListStart(pos, piece));
}

Sq posGetKingSq(const Pos *pos, Colour colour) {
	assert(colourIsValid(colour));
	return *posGetPieceListStart(pos, pieceMake(PieceTypeKing, colour));
}

const Sq *posGetPieceListStart(const Pos *pos, Piece piece) {
	assert(pieceIsValid(piece));
	return &pos->pieceList[piece<<4];
}

const Sq *posGetPieceListEnd(const Pos *pos, Piece piece) {
	assert(pieceIsValid(piece));
	return &pos->pieceList[pos->pieceListNext[piece]];
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

MatInfo posGetMatInfo(const Pos *pos) {
	// Grab piece offsets and subtract 'base' to give literal piece counts.
	const MatInfo *pieceListNext=((const MatInfo *)pos->pieceListNext);
	MatInfo white=pieceListNext[ColourWhite]-0x7060504030201000llu;
	MatInfo black=pieceListNext[ColourBlack]-0xF0E0D0C0B0A09080llu;

	// Interleave white and black into a single 64 bit integer (we only need 4
	// bits per piece)
	return ((black<<4) | white);
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
	assert(moveIsValid(move) || move==MoveNone);

	// Does this move leave us in check?
	if (!posCanMakeMove(pos, move))
		return false;

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
	Sq fromSq=moveGetFromSq(move);
	Sq toSq=moveGetToSq(move);
	pos->data->lastMove=move;
	pos->data->lastMoveWasPromo=false;
	pos->data->halfMoveNumber=(pos->data-1)->halfMoveNumber+1;
	pos->data->epSq=SqInvalid;
	pos->data->key=(pos->data-1)->key^posKeySTM^posKeyEP[(pos->data-1)->epSq];
	pos->data->castRights=(pos->data-1)->castRights;
	pos->data->capPiece=posGetPieceOnSq(pos, toSq);
	pos->data->capSq=toSq;
	pos->fullMoveNumber+=(pos->stm==ColourBlack); // Inc after black's move.
	pos->stm=colourSwap(pos->stm);

	if (move!=MoveNone) {
		Piece fromPiece=posGetPieceOnSq(pos, fromSq);

		// Update castling rights
		if (pos->data->castRights.rookSq[colourSwap(pos->stm)][CastSideA]==fromSq || pieceGetType(fromPiece)==PieceTypeKing)
			pos->data->castRights.rookSq[colourSwap(pos->stm)][CastSideA]=SqInvalid;
		if (pos->data->castRights.rookSq[colourSwap(pos->stm)][CastSideH]==fromSq || pieceGetType(fromPiece)==PieceTypeKing)
			pos->data->castRights.rookSq[colourSwap(pos->stm)][CastSideH]=SqInvalid;
		if (pos->data->castRights.rookSq[pos->stm][CastSideA]==toSq)
			pos->data->castRights.rookSq[pos->stm][CastSideA]=SqInvalid;
		if (pos->data->castRights.rookSq[pos->stm][CastSideH]==toSq)
			pos->data->castRights.rookSq[pos->stm][CastSideH]=SqInvalid;

		switch(pieceGetType(fromPiece)) {
			case PieceTypePawn: {
				// Pawns are complicated so deserve a special case.

				// En-passent capture?
				bool isEP=(sqFile(fromSq)!=sqFile(toSq) && pos->data->capPiece==PieceNone);
				if (isEP) {
					pos->data->capSq^=8;
					pos->data->capPiece=pieceMake(PieceTypePawn, pos->stm);
					assert(posGetPieceOnSq(pos, pos->data->capSq)==pos->data->capPiece);
				}

				// Capture?
				if (pos->data->capPiece!=PieceNone)
					// Remove piece.
					posPieceRemove(pos, pos->data->capSq);

				// Move the pawn, potentially promoting.
				Piece toPiece=moveGetToPiece(move);
				if (toPiece!=fromPiece) {
					pos->data->lastMoveWasPromo=true;
					posPieceMoveChange(pos, fromSq, toSq, toPiece);
				} else
					posPieceMove(pos, fromSq, toSq);

				// Pawn moves reset 50 move counter.
				pos->data->halfMoveNumber=0;

				// If double pawn move check set EP capture square (for next move).
				if (abs(((int)sqRank(toSq))-((int)sqRank(fromSq)))==2) {
					Sq epSq=toSq^8;
					if (posIsEPCap(pos, epSq))
						pos->data->epSq=epSq;
				}
			} break;
			case PieceTypeKing:
				// Castling.
				if (sqFile(toSq)==sqFile(fromSq)+2)
					posPieceMove(pos, toSq+1, toSq-1); // Kingside.
				else if (sqFile(toSq)==sqFile(fromSq)-2)
					posPieceMove(pos, toSq-2, toSq+1); // Queenside.

				// Fall through to move king.
			default:
				// Capture?
				if (pos->data->capPiece!=PieceNone) {
					// Remove piece.
					posPieceRemove(pos, toSq);

					// Captures reset 50 move counter.
					pos->data->halfMoveNumber=0;
				}

				// Move non-pawn piece (i.e. no promotion to worry about).
				posPieceMove(pos, fromSq, toSq);
			break;
		}

		// Update key.
		pos->data->key^=posKeyEP[pos->data->epSq];
		unsigned i, j;
		for(i=ColourWhite; i<=ColourBlack; ++i)
			for(j=CastSideA; j<=CastSideH; ++j) {
				pos->data->key^=posKeyCastling[(pos->data-1)->castRights.rookSq[i][j]];
				pos->data->key^=posKeyCastling[pos->data->castRights.rookSq[i][j]];
			}
	}

	assert(posIsConsistent(pos));

	return true;
}

bool posCanMakeMove(const Pos *pos, Move move) {
	// Sanity checks and special case.
	assert(moveIsValid(move) || move==MoveNone);
	if (move==MoveNone)
		return true;

	// Use local variables to simulate having made the move.
	Colour stm=moveGetColour(move);
	assert(stm==posGetSTM(pos));
	Colour xstm=colourSwap(stm);
	BB occ=posGetBBAll(pos);
	BB opp=posGetBBColour(pos, xstm);
	Sq fromSq=moveGetFromSq(move);
	Sq toSq=moveGetToSq(move);
	BB fromBB=bbSq(fromSq);
	BB toBB=bbSq(toSq);
	Sq kingSq=posGetKingSq(pos, stm);

	if (fromSq==kingSq)
		kingSq=toSq; // King move.
	occ&=~fromBB; // Move piece.
	occ|=toBB;
	opp&=~toBB; // Potentially capture opp piece (so it cannot attack us later).
	if (moveGetToPieceType(move)==PieceTypePawn && sqFile(fromSq)!=sqFile(toSq) && posGetPieceOnSq(pos, toSq)==PieceNone) {
		// En-passent capture.
		assert(pos->data->epSq==toSq);
		occ^=bbSq(toSq^8);
		opp^=bbSq(toSq^8);
	}

	// Make a list of squares we need to ensure are unattacked.
	BB checkSquares=bbSq(kingSq);
	if (moveIsCastling(move))
		checkSquares|=fromBB|bbSq((toSq+fromSq)/2);

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
	Move move=pos->data->lastMove;
	assert(moveIsValid(move) || move==MoveNone);

	// Update generic fields.
	pos->stm=colourSwap(pos->stm);
	pos->fullMoveNumber-=(pos->stm==ColourBlack);

	if (move!=MoveNone) {
		Sq fromSq=moveGetFromSq(move);
		Sq toSq=moveGetToSq(move);
		Piece toPiece=moveGetToPiece(move);

		// Move piece back (potentially un-promoting).
		if (pos->data->lastMoveWasPromo)
			posPieceMoveChange(pos, toSq, fromSq, pieceMake(PieceTypePawn, pos->stm));
		else
			posPieceMove(pos, toSq, fromSq);

		// Replace any captured piece.
		if (pos->data->capPiece!=PieceNone)
			posPieceAdd(pos, pos->data->capPiece, pos->data->capSq);

		// If castling replace the rook.
		if (pieceGetType(toPiece)==PieceTypeKing) {
			if (toSq==fromSq+2)
				posPieceMove(pos, toSq-1, toSq+1); // Kingside.
			else if (toSq==fromSq-2)
				posPieceMove(pos, toSq+1, toSq-2); // Queenside.
		}
	}

	// Discard data.
	--pos->data;

	assert(posIsConsistent(pos));
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

bool posIsDraw(const Pos *pos, unsigned int ply) {
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
	return ((posGetMatInfo(pos) & matInfoMakeMaskColour(colour) & ~(matInfoMakeMaskPieceType(PieceTypePawn) | matInfoMakeMaskPieceType(PieceTypeKing)))!=0);
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
	// Standard capture?
	Sq toSq=moveGetToSq(move);
	Piece capPiece=posGetPieceOnSq(pos, toSq);
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
	if (pieceGetType(fromPiece)==PieceTypePawn && sqFile(fromSq)!=sqFile(toSq))
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
	// We can simply check if we are moving into a castling rook
	return (moveGetToSq(move)==pos->data->castRights.rookSq[posGetSTM(pos)][CastSideA] ||
	        moveGetToSq(move)==pos->data->castRights.rookSq[posGetSTM(pos)][CastSideH]);
}

Move posMoveFromStr(const Pos *pos, const char str[static 6]){
	Moves moves;
	movesInit(&moves, pos, 0, MoveTypeAny);
	Move move;
	while((move=movesNext(&moves))!=MoveInvalid) {
		char genStr[8];
		posMoveToStr(pos, move, genStr);
		if (!strcmp(str, genStr))
			return move;
	}
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
	Sq toSq=moveGetToSq(move);

	// Special case for non-chess 960 castling
	if (!uciGetChess960() && posMoveIsCastling(pos, move)) {
		// We normally send as king captures rook, but here we need kings true final position
		File file=(toSq<fromSq ? FileC : FileG);
		Rank rank=(moveGetColour(move)==ColourWhite ? Rank1 : Rank8);
		toSq=sqMake(file, rank);
	}

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

unsigned int matInfoGetPieceCount(MatInfo info, Piece piece) {
	assert(pieceIsValid(piece));
	return (info>>matInfoShift(piece))&15;
}

MatInfo matInfoMake(Piece piece, unsigned int count) {
	assert(pieceIsValid(piece));
	assert(count<16);
	return ((MatInfo)count)<<matInfoShift(piece);
}

MatInfo matInfoMakeMaskPiece(Piece piece) {
	assert(pieceIsValid(piece));
	return matInfoMake(piece, 15);
}

MatInfo matInfoMakeMaskPieceType(PieceType type) {
	assert(pieceTypeIsValid(type));
	return matInfoMakeMaskPiece(pieceMake(type, ColourWhite))|matInfoMakeMaskPiece(pieceMake(type, ColourBlack));
}

MatInfo matInfoMakeMaskColour(Colour colour) {
	assert(colourIsValid(colour));
	return matInfoMakeMaskPiece(pieceMake(PieceTypePawn, colour))|
           matInfoMakeMaskPiece(pieceMake(PieceTypeKnight, colour))|
           matInfoMakeMaskPiece(pieceMake(PieceTypeBishopL, colour))|
           matInfoMakeMaskPiece(pieceMake(PieceTypeBishopD, colour))|
           matInfoMakeMaskPiece(pieceMake(PieceTypeRook, colour))|
           matInfoMakeMaskPiece(pieceMake(PieceTypeQueen, colour))|
           matInfoMakeMaskPiece(pieceMake(PieceTypeKing, colour));
}

void posCastRightsToStr(CastRights castRights, char str[static 8]) {
	char temp[4];
	str[0]='\0';
	if (uciGetChess960()) {
		Colour colour;
		CastSide castSide;
		for(colour=ColourWhite; colour<=ColourBlack; ++colour) {
			for(castSide=CastSideA; castSide<=CastSideH; ++castSide) {
				Sq sq=castRights.rookSq[ColourWhite][CastSideA];
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

CastRights posCastRightsFromStr(const char *str) {
	CastRights castRights;
	castRights.rookSq[ColourWhite][CastSideA]=SqInvalid;
	castRights.rookSq[ColourWhite][CastSideH]=SqInvalid;
	castRights.rookSq[ColourBlack][CastSideA]=SqInvalid;
	castRights.rookSq[ColourBlack][CastSideH]=SqInvalid;

	const char *c=str;
	if (uciGetChess960()) {
		CastSide nextCastSide[ColourNB]={[ColourWhite]=CastSideA, [ColourBlack]=CastSideA};
		while(1) {
			if (*c>='A' && *c<='H')
				castRights.rookSq[ColourWhite][nextCastSide[ColourWhite]++]=sqMake(fileFromChar(tolower(*c)), Rank1);
			else if (*c>='a' && *c<='h')
				castRights.rookSq[ColourBlack][nextCastSide[ColourBlack]++]=sqMake(fileFromChar(*c), Rank8);
			else
				return castRights;
			++c;
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
			posPieceRemove(pos, sqMirror(sq));
	}

	// Add pieces from mirrored board.
	for(sq=0;sq<SqNB;++sq)
		if (board[sq]!=PieceNone)
			posPieceAdd(pos, board[sq], sq);

	// Mirror other fields.
	if (pos->data->epSq!=SqInvalid)
		pos->data->epSq=sqMirror(pos->data->epSq);
	if (pos->data->capSq!=SqInvalid)
		pos->data->capSq=sqMirror(pos->data->capSq);
	CastRights cast=CastRightsNone;
	if (pos->data->cast & CastRightsK)
		cast|=CastRightsQ;
	if (pos->data->cast & CastRightsQ)
		cast|=CastRightsK;
	if (pos->data->cast & CastRightsk)
		cast|=CastRightsq;
	if (pos->data->cast & CastRightsq)
		cast|=CastRightsk;
	pos->data->cast=cast; // Not techinically correct under standard chess rules, but ensures king mobility evaluation is consistent.

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
			posPieceRemove(pos, sqFlip(sq));
		}
	}

	// Add pieces from flipped board.
	for(sq=0;sq<SqNB;++sq)
		if (board[sq]!=PieceNone)
			posPieceAdd(pos, board[sq], sq);

	// Flip other fields.
	pos->stm=colourSwap(pos->stm);
	if (pos->data->epSq!=SqInvalid)
		pos->data->epSq=sqMirror(pos->data->epSq);
	if (pos->data->capSq!=SqInvalid)
		pos->data->capSq=sqMirror(pos->data->capSq);
	CastRights cast=CastRightsNone;
	if (pos->data->cast & CastRightsK)
		cast|=CastRightsk;
	if (pos->data->cast & CastRightsQ)
		cast|=CastRightsq;
	if (pos->data->cast & CastRightsk)
		cast|=CastRightsK;
	if (pos->data->cast & CastRightsq)
		cast|=CastRightsQ;
	pos->data->cast=cast;

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
	memset(pos->array64, 0, SqNB*sizeof(uint8_t));
	unsigned int i;
	for(i=0;i<PieceNB;++i)
		pos->pieceListNext[i]=16*i;
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

void posPieceAdd(Pos *pos, Piece piece, Sq sq) {
	// Sanity checks.
	assert(pieceIsValid(piece));
	assert(sqIsValid(sq));
	assert(posGetPieceOnSq(pos, sq)==PieceNone);

	// Update position.
	pos->bbPiece[piece]^=bbSq(sq);
	pos->bbColour[pieceGetColour(piece)]^=bbSq(sq);
	pos->bbAll^=bbSq(sq);
	uint8_t index=(pos->pieceListNext[piece]++);
	pos->array64[sq]=index;
	pos->pieceList[index]=sq;

	// Update hash keys.
	pos->data->key^=posKeyPiece[piece][sq];
	pos->pawnKey^=posPawnKeyPiece[piece][sq];
	pos->matKey^=posMatKey[index];

	// Update PST score.
	evalVPairAddTo(&pos->pstScore, &evalPST[piece][sq]);
}

void posPieceRemove(Pos *pos, Sq sq) {
	// Sanity checks.
	assert(sqIsValid(sq));
	assert(posGetPieceOnSq(pos, sq)!=PieceNone);

	// Update position.
	uint8_t index=pos->array64[sq];
	Piece piece=(index>>4);
	pos->bbPiece[piece]^=bbSq(sq);
	pos->bbColour[pieceGetColour(piece)]^=bbSq(sq);
	pos->bbAll^=bbSq(sq);
	uint8_t lastIndex=(--pos->pieceListNext[piece]);
	pos->pieceList[index]=pos->pieceList[lastIndex];
	pos->array64[pos->pieceList[index]]=index;
	pos->array64[sq]=(PieceNone<<4);

	// Update hash keys.
	pos->data->key^=posKeyPiece[piece][sq];
	pos->pawnKey^=posPawnKeyPiece[piece][sq];
	pos->matKey^=posMatKey[lastIndex];

	// Update PST score.
	evalVPairSubFrom(&pos->pstScore, &evalPST[piece][sq]);
}

void posPieceMove(Pos *pos, Sq fromSq, Sq toSq) {
	// Sanity checks.
	assert(sqIsValid(fromSq) && sqIsValid(toSq));
	assert(posGetPieceOnSq(pos, fromSq)!=PieceNone);
	assert(posGetPieceOnSq(pos, toSq)==PieceNone);

	// Update position.
	uint8_t index=pos->array64[fromSq];
	Piece piece=(index>>4);
	pos->bbPiece[piece]^=bbSq(fromSq)^bbSq(toSq);
	pos->bbColour[pieceGetColour(piece)]^=bbSq(fromSq)^bbSq(toSq);
	pos->bbAll^=bbSq(fromSq)^bbSq(toSq);
	pos->array64[toSq]=index;
	pos->array64[fromSq]=(PieceNone<<4);
	pos->pieceList[index]=toSq;

	// Update hash keys.
	pos->data->key^=posKeyPiece[piece][fromSq]^posKeyPiece[piece][toSq];
	pos->pawnKey^=posPawnKeyPiece[piece][fromSq]^posPawnKeyPiece[piece][toSq];

	// Update PST score.
	evalVPairSubFrom(&pos->pstScore, &evalPST[piece][fromSq]);
	evalVPairAddTo(&pos->pstScore, &evalPST[piece][toSq]);
}

void posPieceMoveChange(Pos *pos, Sq fromSq, Sq toSq, Piece toPiece) {
	// Sanity checks.
	assert(sqIsValid(fromSq) && sqIsValid(toSq));
	assert(posGetPieceOnSq(pos, fromSq)!=PieceNone);
	assert(posGetPieceOnSq(pos, toSq)==PieceNone);
	assert(pieceIsValid(toPiece));
	assert(pieceGetColour(toPiece)==pieceGetColour(posGetPieceOnSq(pos, fromSq)));

	// Update position.
	posPieceRemove(pos, fromSq);
	posPieceAdd(pos, toPiece, toSq);
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
		const Sq *sq=posGetPieceListStart(pos, piece);
		const Sq *endSq=posGetPieceListEnd(pos, piece);
		for(;sq<endSq;++sq) {
			// Calculate attack set.
			BB set=(attacksPiece(piece, *sq, occ) & allowed);

			// Loop over destination squares and add as individual moves.
			while(set)
				PUSH(moveMake(*sq, bbScanReset(&set), piece));
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
	BB occ=posGetBBAll(pos);
	if (posGetSTM(pos)==ColourWhite) {
		if ((pos->data->cast & CastRightsK) && !(occ & (bbSq(SqF1) | bbSq(SqG1))))
			PUSH(moveMake(SqE1, SqG1, PieceWKing));
		if ((pos->data->cast & CastRightsQ) && !(occ & (bbSq(SqB1) | bbSq(SqC1) | bbSq(SqD1))))
			PUSH(moveMake(SqE1, SqC1, PieceWKing));
	} else {
		if ((pos->data->cast & CastRightsk) && !(occ & (bbSq(SqF8) | bbSq(SqG8))))
			PUSH(moveMake(SqE8, SqG8, PieceBKing));
		if ((pos->data->cast & CastRightsq) && !(occ & (bbSq(SqB8) | bbSq(SqC8) | bbSq(SqD8))))
			PUSH(moveMake(SqE8, SqC8, PieceBKing));
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
	Sq sq;
	for(sq=0;sq<SqNB;++sq)
		key^=posMatKey[pos->array64[sq]];

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

	// Test Array64 'pointers' are correct (consistent and agree with bitboards).
	Sq sq;
	uint8_t index;
	for(sq=0;sq<SqNB;++sq) {
		index=pos->array64[sq];
		Piece piece=(index>>4);
		if (piece!=PieceNone && !pieceIsValid(piece)) {
			sprintf(error, "Invalid piece '%i' derived from array64 index '%i' (%c%c)"
										 ".\n", piece, index, fileToChar(sqFile(sq)), rankToChar(sqRank(sq)));
			goto Error;
		}
		if (piece==PieceNone) {
			assert(index==0);
			if (pos->bbAll & bbSq(sq)) {
				sprintf(error, "Piece exists in bitboards but not in array (%c%c).\n",
								fileToChar(sqFile(sq)), rankToChar(sqRank(sq)));
				goto Error;
			}
			continue; // Special case (the 'null index').
		}
		if ((pos->bbPiece[piece] & bbSq(sq))==BBNone) {
			sprintf(error, "Piece exists in array, but not in bitboards (%c%c).\n",
							fileToChar(sqFile(sq)), rankToChar(sqRank(sq)));
			goto Error;
		}
		if (index>=pos->pieceListNext[piece]) {
			sprintf(error, "Array64 points outside of list for piece '%i' (%c%c).\n",
							piece, fileToChar(sqFile(sq)), rankToChar(sqRank(sq)));
			goto Error;
		}
		if (pos->pieceList[index]!=sq) {
			sprintf(error, "Array64 sq %c%c disagrees with piece list sq %c%c at "
										 "index %i.\n", fileToChar(sqFile(sq)), rankToChar(sqRank(sq)),
										 fileToChar(sqFile(pos->pieceList[index])), rankToChar(sqRank(pos->pieceList[index])),
										 index);
			goto Error;
		}
	}

	// Test piece lists are correct.
	for(type1=PieceTypePawn;type1<=PieceTypeKing;++type1) {
		Piece piece=pieceMake(type1, ColourWhite);
		for(index=(piece<<4);index<pos->pieceListNext[piece];++index)
			if ((pos->bbPiece[piece] & bbSq(pos->pieceList[index]))==BBNone) {
				sprintf(error, "Piece list thinks piece %i exists on %c%c but bitboards"
											 " do not.\n", piece, fileToChar(sqFile(pos->pieceList[index])),
											 rankToChar(sqRank(pos->pieceList[index])));
				goto Error;
			}
		piece=pieceMake(type1, ColourBlack);
		for(index=(piece<<4);index<pos->pieceListNext[piece];++index)
			if ((pos->bbPiece[piece] & bbSq(pos->pieceList[index]))==BBNone) {
				sprintf(error, "Piece list thinks piece %i exists on %c%c but bitboards"
											 " do not.\n", piece, fileToChar(sqFile(pos->pieceList[index])),
											 rankToChar(sqRank(pos->pieceList[index])));
				goto Error;
			}
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
	if (move==MoveNone)
		return !posIsSTMInCheck(pos);
	else if (!moveIsValid(move))
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

	// Friendly capture?
	Sq toSq=moveGetToSq(move);
	Piece capPiece=posGetPieceOnSq(pos, toSq);
	if (capPiece!=PieceNone && pieceGetColour(capPiece)==stm)
		return false;

	// Piece-specific logic.
	Sq fromSq=moveGetFromSq(move);
	Piece fromPiece=posGetPieceOnSq(pos, fromSq);
	BB occ=posGetBBAll(pos);
	unsigned int dX=abs(((int)sqFile(fromSq))-((int)sqFile(toSq)));
	int dY=abs(((int)sqRank(fromSq))-((int)sqRank(toSq)));
	switch(pieceGetType(fromPiece)) {
		case PieceTypePawn: {
			// Moving pawn of correct colour?
			if (pieceGetColour(fromPiece)!=stm)
				return false;

			// Valid rank movement?
			int colourDelta=(stm==ColourWhite ? 1 : -1);
			int rankDelta=sqRank(toSq)-sqRank(fromSq);
			if (rankDelta!=colourDelta &&
			    (rankDelta!=2*colourDelta || (sqRank(fromSq)!=Rank2 && sqRank(fromSq)!=Rank7) ||
			     posGetPieceOnSq(pos, (fromSq+toSq)/2)!=PieceNone))
				return false;

			// Valid file movement?
			// The next line was derived from the following equivalent expression:
			// (!(dX==0 && capPiece==PieceNone) && !(dX==1 && (capPiece!=PieceNone || toSq==pos->data->epSq)));
			if ((dX!=0 || capPiece!=PieceNone) && (dX!=1 || (capPiece==PieceNone && toSq!=pos->data->epSq)))
				return false;

			// Valid to-piece?
			PieceType toPieceType=pieceGetType(toPiece);
			if (sqRank(toSq)==Rank1 || sqRank(toSq)==Rank8)
				return (toPieceType>=PieceTypeKnight && toPieceType<=PieceTypeQueen);
			else
				return (toPieceType==PieceTypePawn);
		} break;
		case PieceTypeKnight:
			return (((dX==2 && dY==1) || (dX==1 && dY==2)) && fromPiece==toPiece && (bbBetween(fromSq, toSq) & occ)==BBNone);
		break;
		case PieceTypeBishopL:
		case PieceTypeBishopD:
			return (dX==dY && fromPiece==toPiece && (bbBetween(fromSq, toSq) & occ)==BBNone);
		break;
		case PieceTypeRook:
			return ((dX==0 || dY==0) && fromPiece==toPiece && (bbBetween(fromSq, toSq) & occ)==BBNone);
		break;
		case PieceTypeQueen:
			return (fromPiece==toPiece && (bbBetween(fromSq, toSq) & occ)==BBNone);
		break;
		case PieceTypeKing:
			// King cannot change.
			if (fromPiece!=toPiece)
				return false;

			// Castling requires extra tests.
			if (toSq==fromSq+2)
				return (stm==ColourWhite ? ((pos->data->cast & CastRightsK) && !(occ & (bbSq(SqF1) | bbSq(SqG1))))
				                         : ((pos->data->cast & CastRightsk) && !(occ & (bbSq(SqF8) | bbSq(SqG8)))));
			else if (toSq==fromSq-2)
				return (stm==ColourWhite ? ((pos->data->cast & CastRightsQ) && !(occ & (bbSq(SqB1) | bbSq(SqC1) | bbSq(SqD1))))
				                         : ((pos->data->cast & CastRightsq) && !(occ & (bbSq(SqB8) | bbSq(SqC8) | bbSq(SqD8)))));
			else
				return true; // Standard move.
		break;
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
