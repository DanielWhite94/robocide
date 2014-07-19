#ifndef SEE_H
#define SEE_H
/*
const bool SEEincludecheks?
but checks are just special case of a pinned piece moving (or say a capture directly giving check?)
leads to a sort of simplified qsearch, only searching from lowest-highest, not every combination, and only materialistic

actually, what to do about positions like recently on CCC where order of say capturing bishops matters, but SEE only considers one
*/

#include "pos.h"
#include "types.h"

int SEE(const pos_t *Pos, sq_t FromSq, sq_t ToSq);
int SEESign(const pos_t *Pos, sq_t FromSq, sq_t ToSq);

#endif
