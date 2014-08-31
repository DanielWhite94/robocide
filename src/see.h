#ifndef SEE_H
#define SEE_H

#include "pos.h"
#include "square.h"

int see(const Pos *pos, Sq fromSq, Sq toSq);
int seeSign(const Pos *pos, Sq fromSq, Sq toSq);

#endif
