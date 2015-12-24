#ifdef STATS

#ifndef STATS_H
#define STATS_H

#include <stdbool.h>

#include "pos.h"

bool statsInit(void);
void statsQuit(void);

void statsClear(void);

bool statsAdd(const Pos *pos);

bool statsWrite(const char *path);

bool statsRead(const char *path);

#endif

#endif

