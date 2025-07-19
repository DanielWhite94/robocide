#ifndef SYZYGY_H
#define SYZYGY_H

#include "move.h"
#include "pos.h"

typedef enum {
	SyzygyWdlError,
	SyzygyWdlWin,
	SyzygyWdlLoss,
	SyzygyWdlDraw,
	SyzygyWdlNB,
} SyzygyWdl;

typedef struct {
	Move move;
	SyzygyWdl wdl;
	unsigned dtz;
} SyzygyMove;

#define SyzygyMovesMax 256

void syzygyInit(void);
void syzygyQuit(void);

SyzygyWdl syzygyProbeWdl(const Pos *pos);
Move syzygyProbeRoot(const Pos *pos, SyzygyWdl *wdl, int *dtz, SyzygyMove *moves); // moves should have space for at least SyzygyMovesMax elements. on failure returns MoveInvalid and leaves *wdl and *dtz unchanged. wdl, dtz or moves can be NULL. moves is terminated with an entry with move=MoveInvalid and wdl=SyzygyWdlError

const char *syzygyWdlToStr(SyzygyWdl wdl);

#endif
