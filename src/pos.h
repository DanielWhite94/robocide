#ifndef POS_H
#define POS_H

#include <stdbool.h>
#include "bb.h"
#include "types.h"

// Macros for use with PosGetMat()
#define POSMAT_SHIFT(P) (8*PIECE_TYPE(P)+4*PIECE_COLOUR(P))
#define POSMAT_MASK(P) (15llu<<POSMAT_SHIFT(P))
#define POSMAT_GET(MAT, P) ((int)(((MAT)>>POSMAT_SHIFT(P)) & 15))

typedef struct pos_t pos_t;

void PosInit();
pos_t *PosNew(const char *FEN); // If FEN==NULL uses standard initial position
void PosFree(pos_t *Pos);
pos_t *PosCopy(const pos_t *Src);
bool PosSetToFEN(pos_t *Pos, const char *String); // If fails Pos is unchanged
void PosDraw(const pos_t *Pos);
inline col_t PosGetSTM(const pos_t *Pos);
inline piece_t PosGetPieceOnSq(const pos_t *Pos, sq_t Sq);
inline bb_t PosGetBBAll(const pos_t *Pos);
inline bb_t PosGetBBColour(const pos_t *Pos, col_t Colour);
inline bb_t PosGetBBPiece(const pos_t *Pos, piece_t Piece);
inline char PosPieceToChar(piece_t Piece);
inline int PosPieceCount(const pos_t *Pos, piece_t Piece);
bool PosMakeMove(pos_t *Pos, move_t Move);
void PosUndoMove(pos_t *Pos);
bool PosIsSqAttackedByColour(const pos_t *Pos, sq_t Sq, col_t Colour);
inline sq_t PosGetKingSq(const pos_t *Pos, col_t Colour);
inline bool PosIsSTMInCheck(const pos_t *Pos);
inline bool PosIsXSTMInCheck(const pos_t *Pos);
move_t *PosGenPseudoMoves(const pos_t *Pos, move_t *Moves);
move_t *PosGenPseudoCaptures(const pos_t *Pos, move_t *Moves);
move_t *PosGenPseudoQuiets(const pos_t *Pos, move_t *Moves);
inline move_t PosGenLegalMove(pos_t *Pos);
inline const sq_t *PosGetPieceListStart(const pos_t *Pos, piece_t Piece);
inline const sq_t *PosGetPieceListEnd(const pos_t *Pos, piece_t Piece);
void PosMoveToStr(move_t Move, char Str[static 6]);
move_t PosStrToMove(const pos_t *Pos, const char Str[static 6]);
bool PosIsDraw(const pos_t *Pos, int Ply);
inline unsigned int PosGetHalfMoveClock(const pos_t *Pos);
bool PosLegalMoveExist(pos_t *Pos);
inline hkey_t PosGetKey(const pos_t *Pos);
inline hkey_t PosGetPawnKey(const pos_t *Pos);
bool PosIsMovePseudoLegal(const pos_t *Pos, move_t Move);
static inline bool PosIsMoveCapture(const pos_t *Pos, move_t Move);
inline uint64_t PosGetMat(const pos_t *Pos); // Use macros above to access

static inline bool PosIsMoveCapture(const pos_t *Pos, move_t Move)
{
  return (PosGetPieceOnSq(Pos, MOVE_GETTOSQ(Move))!=empty ||
          MOVE_ISPROMO(Move) || MOVE_ISEP(Move));
}

#endif
