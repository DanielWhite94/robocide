#ifndef COLOUR_H
#define COLOUR_H

#include <stdbool.h>

typedef enum { ColourWhite, ColourBlack, ColourNB} Colour;

Colour colourSwap(Colour colour);
bool colourIsValid(Colour colour);

#endif
