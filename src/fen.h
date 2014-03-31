#ifndef FEN_H
#define FEN_H

#include <stdbool.h>
#include "types.h"

typedef struct
{
  piece_t Array[64];
  col_t STM;
  castrights_t CastRights;
  sq_t EPSq;
  unsigned int HalfMoveClock, FullMoveNumber;
}fen_t;

bool FENRead(fen_t *Data, const char *String);

#endif
