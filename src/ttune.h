#ifndef TTUNE_H
#define TTUNE_H

#include <stdlib.h>

void ttuneInit(unsigned parameterCount);
void ttuneQuit(void);

void ttuneRun(const char *positionInputFile, const char *codeOutputFile); // positionInputFile should point to an EPD file of 'quiet' positions, with game result as a string after the 'c9' tag (after then FEN)

void ttuneAddParameter(unsigned id, const char *name, int initialValue, bool tune);

#endif
