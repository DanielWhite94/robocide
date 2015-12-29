#ifndef COLOUR_H
#define COLOUR_H

#include <stdbool.h>

typedef enum { ColourWhite, ColourBlack, ColourNB} Colour;

bool colourIsValid(Colour colour);

Colour colourSwap(Colour colour);

const char *colourToStr(Colour colour); // NOT thread-safe.

#endif
