#ifndef WRAPPER_H
#define WRAPPER_H

#include <stdint.h>

unsigned wrapperBbPopCount(uint64_t x);
unsigned wrapperBbScanForward(uint64_t x);

uint64_t wrapperAttacksKing(unsigned square);
uint64_t wrapperAttacksKnight(unsigned square);
uint64_t wrapperAttacksRook(unsigned square, uint64_t occ);
uint64_t wrapperAttacksBishop(unsigned square, uint64_t occ);
uint64_t wrapperAttacksQueen(unsigned square, uint64_t occ);
uint64_t wrapperAttacksPawn(unsigned square, unsigned colour);

#endif
