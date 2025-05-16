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

#define POSMOVETOSTRMAXLEN 8
#define POSMOVETOSTR(pos, move) ({char *str=alloca(POSMOVETOSTRMAXLEN); posMoveToStr((pos), (move), str); (const char *)str;})

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
Move posGetLastMove(const Pos *pos);

BB posGetBBAll(const Pos *pos);
BB posGetBBColour(const Pos *pos, Colour colour);
BB posGetBBPiece(const Pos *pos, Piece piece);

unsigned int posGetPieceCount(const Pos *pos, Piece piece);

Sq posGetKingSq(const Pos *pos, Colour colour);

unsigned int posGetHalfMoveNumber(const Pos *pos);
unsigned int posGetFullMoveNumber(const Pos *pos);
Key posGetKey(const Pos *pos);
Key posGetPawnKey(const Pos *pos);
Key posGetMatKey(const Pos *pos);
CastRights posGetCastRights(const Pos *pos);
Sq posGetEPSq(const Pos *pos);
VPair posGetPstScore(const Pos *pos);

bool posMakeMove(Pos *pos, Move move);
bool posCanMakeMove(const Pos *pos, Move move); // Returns the same result as posMakeMove() but does not actually make the move on the board.
void posUndoMove(Pos *pos);
bool posMakeNullMove(Pos *pos);
void posUndoNullMove(Pos *pos);

void posGenPseudoMoves(Moves *moves, MoveType type);
Move posGenLegalMove(const Pos *pos, MoveType type);

bool posIsSqAttackedByColour(const Pos *pos, Sq sq, Colour colour);

bool posIsSTMInCheck(const Pos *pos);

bool posIsDraw(const Pos *pos);
bool posIsMate(const Pos *pos);
bool posIsStalemate(const Pos *pos);

bool posLegalMoveExists(const Pos *pos, MoveType type);

bool posHasPieces(const Pos *pos, Colour colour); // Non-pawn material?

bool posMoveIsPseudoLegal(const Pos *pos, Move move);

MoveType posMoveGetType(const Pos *pos, Move move); // Assumes move is pseudo-legal in the current position.
bool posMoveIsPromotion(const Pos *pos, Move move);
bool posMoveIsCastling(const Pos *pos, Move move);
bool posMoveIsCastlingA(const Pos *pos, Move move);
bool posMoveIsCastlingH(const Pos *pos, Move move);
Move posMoveFromStr(const Pos *pos, const char str[static 6]);
void posMoveToStr(const Pos *pos, Move move, char str[static 6]);
Sq posMoveGetToSqTrue(const Pos *pos, Move move); // Adjusted if castling

void posCastRightsToStr(CastRights castRights, char str[static 8]);
CastRights posCastRightsFromStr(const char *str, const Piece pieceArray[SqNB]);

void posMirror(Pos *pos);
void posFlip(Pos *pos);

#endif
