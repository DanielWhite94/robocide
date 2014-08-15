#ifndef MOVES_H
#define MOVES_H

#include <stdbool.h>
#include "types.h"
#include "pos.h"

typedef enum
{
  movesstage_tt,
  movesstage_captures,
  movesstage_quiets,
}movesstage_t;

typedef uint64_t scoredmove_t;
#define SCOREDMOVE_MOVE(SM) ((move_t)((SM)&0xFFFF))
#define SCOREDMOVE_MAKE(S,M) ((((scoredmove_t)(S))<<16)|((scoredmove_t)(M)))

#define MOVES_MAX 256
struct moves_t
{
  // All entries should be considered private - only here to allow easy allocation on the stack
  scoredmove_t List[MOVES_MAX], *Next, *End;
  movesstage_t Stage;
  move_t TTMove;
  const pos_t *Pos;
  bool GenCaptures, GenQuiets; // true => still need to generate
};

void MovesInit(moves_t *Moves, const pos_t *Pos, bool Quiets); // Quiets - should quiet moves be generated
void MovesRewind(moves_t *Moves, move_t TTMove); // should be called before MovesNext() to rewind to first move (and potentially set/update TT move)
move_t MovesNext(moves_t *Moves); // returns distinct moves until none remain (returning MOVE_INVALID)
const pos_t *MovesPos(moves_t *Moves);
void MovesPush(moves_t *Moves, move_t Move); // used by generators to add moves

#endif
