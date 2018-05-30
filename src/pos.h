#ifndef POS_H
#define POS_H

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

typedef struct Pos Pos; // Defined here due to circular reference with Moves in moves.h.

typedef enum {
	MoveTypeNone=0,
	MoveTypeQuiet=1, // Includes castling.
	MoveTypeCapture=2, // Includes promotions.
	MoveTypeAny=(MoveTypeQuiet|MoveTypeCapture),
} MoveType;

#include "bb.h"
#include "colour.h"
#include "piece.h"
#include "move.h"
#include "moves.h"
#include "square.h"

typedef enum {
	CastSideA, // queen side in standard games
	CastSideH, // king side in standard games
	CastSideNB,
} CastSide;

typedef struct {
	uint8_t rookSq[ColourNB][CastSideNB]; // Set to SqInvalid if not allowed
} CastRights;
extern const CastRights CastRightsNone;

typedef uint64_t MatInfo; // Holds info on numbers of pieces.

#define MAKE(p,n) matInfoMake((p),(n))
#define MASK(t) matInfoMakeMaskPieceType(t)
#define MatInfoMaskKings (MAKE(PieceWKing,1)|MAKE(PieceBKing,1)) // Kings mask or equivalently KvK position.
#define MatInfoMaskKNvK (MAKE(PieceWKnight,1)|MatInfoMaskKings) // King and white knight vs lone king.
#define MatInfoMaskKvKN (MAKE(PieceBKnight,1)|MatInfoMaskKings) // King and black knight vs lone king.
#define MatInfoMaskBishopsL (MASK(PieceTypeBishopL)|MatInfoMaskKings) // Light coloured bishop mask (for both colours).
#define MatInfoMaskBishopsD (MASK(PieceTypeBishopD)|MatInfoMaskKings) // Dark coloured bishop mask (for both colours).
#define MatInfoMaskMinors (matInfoMakeMaskPieceType(PieceTypeKnight) | \
                           matInfoMakeMaskPieceType(PieceTypeBishopL) | \
                           matInfoMakeMaskPieceType(PieceTypeBishopD))
#define MatInfoMaskMajors (matInfoMakeMaskPieceType(PieceTypeRook)| \
                           matInfoMakeMaskPieceType(PieceTypeQueen))
#define MatInfoMaskKNvKN (M(PieceWKnight,1)|M(PieceBKnight,1)) // KNvKN.
#define MatInfoMaskKQvKQ (M(PieceWQueen,1)|M(PieceBQueen,1)) // KQvKQ.
#define MatInfoMaskKQQvKQQ (M(PieceWQueen,2)|M(PieceBQueen,2)) // KQQvKQQ.
#define MatInfoMaskNRQ (matInfoMakeMaskPieceType(PieceTypeKnight) | \
                        matInfoMakeMaskPieceType(PieceTypeRook) | \
                        matInfoMakeMaskPieceType(PieceTypeQueen)) // Knight, rooks and queens of any colour.

#undef MAKE
#undef MASK

typedef uint64_t Key;
#define PRIxKey PRIx64

#include "eval.h"

void posInit(void);

Pos *posNew(const char *fen); // If fen is NULL uses standard initial position.
Pos *posNewFromPos(const Pos *src);
void posFree(Pos *pos);

bool posCopy(Pos *dest, const Pos *src);

bool posSetToFEN(Pos *pos, const char *string); // If fails pos is unchanged.
void posGetFEN(const Pos *pos, char string[static 128]);

void posDraw(const Pos *pos);

Colour posGetSTM(const Pos *pos);
Piece posGetPieceOnSq(const Pos *pos, Sq sq);

BB posGetBBAll(const Pos *pos);
BB posGetBBColour(const Pos *pos, Colour colour);
BB posGetBBPiece(const Pos *pos, Piece piece);

unsigned int posGetPieceCount(const Pos *pos, Piece piece);

Sq posGetKingSq(const Pos *pos, Colour colour);

const Sq *posGetPieceListStart(const Pos *pos, Piece piece); // Used to loop over each piece of a given kind.
const Sq *posGetPieceListEnd(const Pos *pos, Piece piece);

unsigned int posGetHalfMoveNumber(const Pos *pos);
unsigned int posGetFullMoveNumber(const Pos *pos);
Key posGetKey(const Pos *pos);
Key posGetPawnKey(const Pos *pos);
Key posGetMatKey(const Pos *pos);
MatInfo posGetMatInfo(const Pos *pos);
CastRights posGetCastRights(const Pos *pos);
Sq posGetEPSq(const Pos *pos);
VPair posGetPstScore(const Pos *pos);

bool posMakeMove(Pos *pos, Move move);
bool posCanMakeMove(const Pos *pos, Move move); // Returns the same result as posMakeMove() but does not actually make the move on the board.
void posUndoMove(Pos *pos);

void posGenPseudoMoves(Moves *moves, MoveType type);
Move posGenLegalMove(const Pos *pos, MoveType type);

bool posIsSqAttackedByColour(const Pos *pos, Sq sq, Colour colour);

bool posIsSTMInCheck(const Pos *pos);

bool posIsDraw(const Pos *pos, unsigned int ply);
bool posIsMate(const Pos *pos);
bool posIsStalemate(const Pos *pos);

bool posLegalMoveExists(const Pos *pos, MoveType type);

bool posHasPieces(const Pos *pos, Colour colour); // Non-pawn material?

bool posMoveIsPseudoLegal(const Pos *pos, Move move); // If side-to-move is not in check will also permit MoveNone.

MoveType posMoveGetType(const Pos *pos, Move move); // Assumes move is pseudo-legal in the current position.
bool posMoveIsPromotion(const Pos *pos, Move move);
bool posMoveIsCastling(const Pos *pos, Move move);
Move posMoveFromStr(const Pos *pos, const char str[static 6]);
void posMoveToStr(const Pos *pos, Move move, char str[static 6]);

unsigned int matInfoGetPieceCount(MatInfo info, Piece piece);

MatInfo matInfoMake(Piece piece, unsigned int count); // Can be OR'd together to make full MatInfo 'object'.
MatInfo matInfoMakeMaskPiece(Piece piece); // A mask which one can AND with to test if any pieces of a given kind are present.
MatInfo matInfoMakeMaskPieceType(PieceType type);
MatInfo matInfoMakeMaskColour(Colour colour);

void posCastRightsToStr(CastRights castRights, char str[static 8]);
CastRights posCastRightsFromStr(const char *str);

void posMirror(Pos *pos);
void posFlip(Pos *pos);

#endif
