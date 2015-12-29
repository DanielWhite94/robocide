#include <assert.h>

#include "colour.h"
#include "util.h"

bool colourIsValid(Colour colour) {
	return (colour==ColourWhite || colour==ColourBlack);
}

Colour colourSwap(Colour colour) {
	STATICASSERT(ColourWhite==(ColourBlack^1));
	assert(colourIsValid(colour));
	return (colour^1);
}

const char *colourStrs[ColourNB]={[ColourWhite]="white", [ColourBlack]="black"};
const char *colourToStr(Colour colour) {
	assert(colour<ColourNB);
	return colourStrs[colour];
}
