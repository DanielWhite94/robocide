#ifndef SYZYGY_H
#define SYZYGY_H

#include "move.h"
#include "pos.h"

typedef enum {
	SyzygyWdlError,
	SyzygyWdlWin,
	SyzygyWdlLoss,
	SyzygyWdlDraw,
} SyzygyWdl;

void syzygyInit(void);
void syzygyQuit(void);

SyzygyWdl syzygyProbeWdl(const Pos *pos);
Move syzygyProbeRoot(const Pos *pos, SyzygyWdl *wdl, int *dtz); // on failure returns MoveInvalid and leaves *wdl and *dtz unchanged. wdl and/or dtz can be NULL

const char *syzygyWdlToStr(SyzygyWdl wdl);

#endif
