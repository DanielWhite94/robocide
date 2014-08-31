#include "colour.h"
#include "util.h"

Colour colourSwap(Colour colour)
{
  STATICASSERT(ColourWhite==(ColourBlack^1));
  return (colour^1);
}

bool colourIsValid(Colour colour)
{
  return (colour==ColourWhite || colour==ColourBlack);
}
