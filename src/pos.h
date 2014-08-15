#ifndef POS_H
#define POS_H

#include <stdbool.h>
#include <stdint.h>
#include "bb.h"
#include "types.h"

// Macros for use with PosGetMat()
#define POSMAT_SHIFT(P) (4*(2*PIECE_TYPE(P)+PIECE_COLOUR(P)))
#define POSMAT_MASK(P) (15llu<<POSMAT_SHIFT(P))
#define POSMAT_MASKCOL(C) (0x0F0F0F0F0F0F0F0Fllu<<(4*C))
#define POSMAT_MAKE(P,Num) (((uint64_t)Num)<<POSMAT_SHIFT(P))
#define POSMAT_GET(MAT, P) ((int)(((MAT)>>POSMAT_SHIFT(P)) & 15))

typedef struct pos_t pos_t;

void PosInit();
pos_t *PosNew(const char *FEN); // If FEN==NULL uses standard initial position
void PosFree(pos_t *Pos);
pos_t *PosCopy(const pos_t *Src);
bool PosSetToFEN(pos_t *Pos, const char *String); // If fails Pos is unchanged
void PosDraw(const pos_t *Pos);
col_t PosGetSTM(const pos_t *Pos);
piece_t PosGetPieceOnSq(const pos_t *Pos, sq_t Sq);
bb_t PosGetBBAll(const pos_t *Pos);
bb_t PosGetBBColour(const pos_t *Pos, col_t Colour);
bb_t PosGetBBPiece(const pos_t *Pos, piece_t Piece);
char PosPieceToChar(piece_t Piece);
int PosPieceCount(const pos_t *Pos, piece_t Piece);
bool PosMakeMove(pos_t *Pos, move_t Move);
void PosUndoMove(pos_t *Pos);
bool PosIsSqAttackedByColour(const pos_t *Pos, sq_t Sq, col_t Colour);
sq_t PosGetKingSq(const pos_t *Pos, col_t Colour);
bool PosIsSTMInCheck(const pos_t *Pos);
bool PosIsXSTMInCheck(const pos_t *Pos);
void PosGenPseudoMoves(moves_t *Moves);
void PosGenPseudoCaptures(moves_t *Moves);
void PosGenPseudoQuiets(moves_t *Moves);
move_t PosGenLegalMove(pos_t *Pos);
const sq_t *PosGetPieceListStart(const pos_t *Pos, piece_t Piece);
const sq_t *PosGetPieceListEnd(const pos_t *Pos, piece_t Piece);
void PosMoveToStr(move_t Move, char Str[static 6]);
move_t PosStrToMove(const pos_t *Pos, const char Str[static 6]);
bool PosIsDraw(const pos_t *Pos, int Ply);
bool PosIsMate(pos_t *Pos);
bool PosIsStalemate(pos_t *Pos);
unsigned int PosGetHalfMoveClock(const pos_t *Pos);
bool PosLegalMoveExist(pos_t *Pos);
hkey_t PosGetKey(const pos_t *Pos);
hkey_t PosGetPawnKey(const pos_t *Pos);
hkey_t PosGetMatKey(const pos_t *Pos);
bool PosIsMovePseudoLegal(const pos_t *Pos, move_t Move);
static inline bool PosIsMoveCapture(const pos_t *Pos, move_t Move);
uint64_t PosGetMat(const pos_t *Pos); // Use macros above to access
bool PosIsConsistent(pos_t *Pos);
static inline bool PosHasPieces(const pos_t *Pos, col_t Col); // Non-pawn material?

static inline bool PosIsMoveCapture(const pos_t *Pos, move_t Move)
{
  return (PosGetPieceOnSq(Pos, MOVE_GETTOSQ(Move))!=empty ||
          MOVE_ISPROMO(Move) || MOVE_ISEP(Move));
}

static inline bool PosHasPieces(const pos_t *Pos, col_t Col)
{
  return (PosGetBBColour(Pos, Col)!=(PosGetBBPiece(Pos, PIECE_MAKE(pawn, Col)) | PosGetBBPiece(Pos, PIECE_MAKE(king, Col))));
}

#endif
