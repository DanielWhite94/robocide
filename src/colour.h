#ifndef COLOUR_H
#define COLOUR_H

#include <stdbool.h>

typedef enum { ColourWhite, ColourBlack, ColourNB} Colour;
#define ColourBit 1

bool colourIsValid(Colour colour);

Colour colourSwap(Colour colour);

const char *colourToStr(Colour colour);

#endif
