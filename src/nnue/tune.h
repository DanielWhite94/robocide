#ifndef NNUE_TUNE_H
#define NNUE_TUNE_H

#include "../nnue.h"

typedef struct NnueTunePositions NnueTunePositions; // a collection of positions each with the final game result

// Positions functions
NnueTunePositions *nnueTunePositionsNew(size_t count); // count can be 0 if not known in advance
void nnueTunePositionsFree(NnueTunePositions *positions);

NnueTunePositions *nnueTunePositionsLoadEpd(const char *path);

#endif
