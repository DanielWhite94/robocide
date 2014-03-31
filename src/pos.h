#ifndef POS_H
#define POS_H

#include <stdbool.h>
#include "bb.h"
#include "types.h"

typedef struct pos_t pos_t;

pos_t *PosNew(const char *FEN); // If FEN==NULL uses standard initial position
                                // Note: This is NOT thread safe
void PosFree(pos_t *Pos);
bool PosSetToFEN(pos_t *Pos, const char *String); // If fails Pos is unchanged
void PosDraw(const pos_t *Pos);
inline col_t PosGetSTM(const pos_t *Pos);
inline piece_t PosGetPieceOnSq(const pos_t *Pos, sq_t Sq);
inline bb_t PosGetBBPiece(const pos_t *Pos, piece_t Piece);
inline char PosPieceToChar(piece_t Piece);
inline unsigned int PosPieceCount(const pos_t *Pos, piece_t Piece);

#endif
