#ifndef NET_H
#define NET_H

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "colour.h"
#include "piece.h"
#include "pos.h"
#include "score.h"
#include "square.h"

// General functions
void netInit(void);
void netQuit(void);

// Evaluation functions
Score netEvaluateRaw(const Pos *pos);

#endif
