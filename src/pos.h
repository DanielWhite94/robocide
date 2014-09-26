#ifndef POS_H
#define POS_H

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

typedef struct Pos Pos; // Defined here due to circular reference with Moves in moves.h.

typedef enum
{
  MoveTypeNone=0,
  MoveTypeQuiet=1, // Includes castling.
  MoveTypeCapture=2, // Includes promotions.
  MoveTypeAny=(MoveTypeQuiet|MoveTypeCapture),
}MoveType;

#include "bb.h"
#include "colour.h"
#include "piece.h"
#include "move.h"
#include "moves.h"
#include "square.h"

typedef enum
{
  CastRightsNone=0,
  CastRightsq=1,
  CastRightsk=2,
  CastRightsQ=4,
  CastRightsK=8,
  CastRightsKQ=CastRightsK | CastRightsQ,
  CastRightsKk=CastRightsK | CastRightsk,
  CastRightsKq=CastRightsK | CastRightsq,
  CastRightsQk=CastRightsQ | CastRightsk,
  CastRightsQq=CastRightsQ | CastRightsq,
  CastRightskq=CastRightsk | CastRightsq,
  CastRightsKQk=CastRightsKQ | CastRightsk,
  CastRightsKQq=CastRightsKQ | CastRightsq,
  CastRightsKkq=CastRightsKk | CastRightsq,
  CastRightsQkq=CastRightsQk | CastRightsq,
  CastRightsKQkq=CastRightsKQ | CastRightskq,
  CastRightsNB=16
}CastRights;

typedef uint64_t MatInfo; // Holds info on number of pieces.

typedef uint64_t Key;
#define PRIxKey PRIx64

void posInit(void);
Pos *posNew(const char *fen); // If fen is NULL uses standard initial position.
void posFree(Pos *pos);
Pos *posCopy(const Pos *src);
bool posSetToFEN(Pos *pos, const char *string); // If fails pos is unchanged.
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
Key posGetKey(const Pos *pos);
Key posGetPawnKey(const Pos *pos);
Key posGetMatKey(const Pos *pos);
MatInfo posGetMatInfo(const Pos *pos);
bool posMakeMove(Pos *pos, Move move);
bool posCanMakeMove(const Pos *pos, Move move); // Returns the same result as posMakeMove() but does not actually make the move on the board.
void posUndoMove(Pos *pos);
void posGenPseudoMoves(Moves *moves, MoveType type);
Move posGenLegalMove(const Pos *pos, MoveType type);
bool posIsSqAttackedByColour(const Pos *pos, Sq sq, Colour colour);
bool posIsSTMInCheck(const Pos *pos);
bool posIsXSTMInCheck(const Pos *pos);
bool posIsDraw(const Pos *pos, unsigned int ply);
bool posIsMate(const Pos *pos);
bool posIsStalemate(const Pos *pos);
bool posLegalMoveExists(const Pos *pos, MoveType type);
bool posHasPieces(const Pos *pos, Colour colour); // Non-pawn material?
bool posMoveIsPseudoLegal(const Pos *pos, Move move); // If side-to-move is not in check will also permit MoveNone.
MoveType posMoveGetType(const Pos *pos, Move move); // Assumes move is pseudo-legal in the current position.
Move posMoveFromStr(const Pos *pos, const char str[static 6]);
void posMoveToStr(const Pos *pos, Move move, char str[static 6]);
unsigned int matInfoGetPieceCount(MatInfo info, Piece piece);
MatInfo matInfoMake(Piece piece, unsigned int count); // Can be OR'd together to make full MatInfo 'object'.
MatInfo matInfoMakeMaskPiece(Piece piece); // A mask which one can AND with to test if any pieces of a given kind are present.
MatInfo matInfoMakeMaskPieceType(PieceType type);
MatInfo matInfoMakeMaskColour(Colour colour);

#endif
