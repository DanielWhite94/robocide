#ifndef SYZYGY_H
#define SYZYGY_H

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

const char *syzygyWdlToStr(SyzygyWdl wdl);

#endif
