#ifndef BB_H
#define BB_H

#include <stdint.h>

#include "square.h"

typedef uint64_t BB;

extern const BB BBNone, BBAll;
extern const BB BBLight, BBDark; // Bitboards with all light/dark squares set.

void bbInit(void); // Must be called before any other bb functions.
void bbDraw(BB bb); // Prints bitboard as 8x8 table with a1 at the bottom left.
BB bbSq(Sq sq); // Returns bitboard with single square 'sq' set.
BB bbFile(File file); // Returns bitboard with all squares on the given file set.
BB bbRank(Rank rank); // Returns bitboard with all squares on the given rank set.
unsigned int bbPopCount(BB bb); // Returns number of 1 bits in given bitboard.
Sq bbScanReset(BB *bb); // Finds a set bit, clears it, and then returns the square it represents. bb should not be BBNone (i.e. at least one bit should be set).
Sq bbScanForward(BB bb); // Scans for least-significant set bit and returns the square it represents. bb should not be BBNone (i.e. at least one bit should be set).
BB bbNorth(BB bb, unsigned int n); // Shifts all set bits north n times.
BB bbSouth(BB bb, unsigned int n); // Shift all set bits south n times.
BB bbNorthOne(BB bb); // Shifts all set bits north one.
BB bbSouthOne(BB bb); // Shifts all set bits south one.
BB bbWestOne(BB bb); // Shift all set bits west one.
BB bbEastOne(BB bb); // Shift all set bits east one.
BB bbForwardOne(BB bb, Colour colour); // (colour==ColourWhite ? bbNorthOne(bb) : bbSouthOne(bb))
BB bbNorthFill(BB bb); // If a bit is set, all of those directly north of it on the same file will also be set.
BB bbSouthFill(BB bb); // If a bit is set, all of those directly south of it on the same file will also be set.
BB bbFileFill(BB bb); // (bbNorthFill(bb) | bbSouthFill(bb))
BB bbWingify(BB bb); // (bbWestOne(bb) | bbEastOne(bb))
BB bbBetween(BB sq1, BB sq2); // If sq1 and sq2 lie on the same file, rank or diagonal then returns a bitboard of all squares between sq1 and sq2 (otherwise BBNone).
BB bbBeyond(BB sq1, BB sq2); // If sq1 and sq2 lie on the same file, rank or diagonal then returns a bitboard of squares behind sq2 from sq1's point of view,
  // otherwise BBNone is returned. e.g. bbBeyond(SqA2, SqE5)==(bbSq(SqF6)|bbSq(SqG7)|bbSq(SqH8)).

#endif
