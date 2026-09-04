#ifndef MAIN_H
#define MAIN_H

#include <stdarg.h>

#include "bound.h"
#include "depth.h"
#include "pos.h"
#include "score.h"
#include "time.h"

void mainFatalError(const char *format, ...) __attribute__ ((noreturn));

bool mainIsLogging(void);
void mainLogF(const char *format, ...);
void mainLogNewGame(void);
void mainLogSearchStart(Pos *pos, TimeMs searchTime);
void mainLogSearchDepth(Depth depth, Score score, Bound bound, unsigned long long nodeCount, TimeMs startTime, const char *pvStr);
void mainLogSearchEnd(unsigned long long int nodeCount);
void mainLogVerifyFirstCurrmove(const  char *path);
bool mainReplay(const char *path, const char *date);

#endif
