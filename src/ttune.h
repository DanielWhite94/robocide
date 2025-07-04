#ifndef TTUNE_H
#define TTUNE_H

#include <stdbool.h>
#include <stdlib.h>

#include "eval.h"

void ttuneInit(unsigned parameterCount);
void ttuneQuit(void);

void ttuneRun(const char *positionInputFile, const char *codeOutputFile); // positionInputFile should point to an EPD file of 'quiet' positions, with game result as a string after the 'c9' tag (after then FEN)

void ttuneAddParameter(unsigned id, int initialValue, bool tune);
void ttuneAddParameterVPair(unsigned baseId, const VPair *initialValue, bool tune);
void ttuneAddParameterArray(unsigned parameterCount, unsigned baseId, const int *initialValues, bool tune);
void ttuneAddParameterVPairArray(unsigned parameterCount, unsigned mgBaseId, unsigned egBaseId, const VPair *initialValues, bool tune);

#endif
