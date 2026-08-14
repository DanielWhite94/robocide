#include <assert.h>

#include "eval.h"
#include "main.h"
#include "nnue.h"
#include "nnue/tune.h"
#include "search.h"
#include "uci.h"

bool nnueUseNnue=true;

NnueNet *nnueNet=NULL; // holds the currently loaded network

////////////////////////////////////////////////////////////////////////////////
// Private prototypes
////////////////////////////////////////////////////////////////////////////////

void nnueInterfaceUseNnue(void *userData, bool value);

////////////////////////////////////////////////////////////////////////////////
// Public functions
////////////////////////////////////////////////////////////////////////////////

void nnueInit(void) {
	assert(nnueNet==NULL);

	// Add UCI option
	if (!uciOptionNewCheck("NNUE", &nnueInterfaceUseNnue, NULL, nnueUseNnue))
		mainFatalError("Error: Could not add NNUE option.\n");

	// Load network
	nnueNet=nnueNetNewPST();
	if (nnueNet==NULL)
		mainFatalError("Error: Could not load NNUE network.\n");
}

void nnueQuit(void) {
	nnueNetFree(nnueNet);
}

const NnueNet *nnueNetGet(void) {
	return nnueNet;
}

Score nnueEvaluate(const Pos *pos) {
	assert(nnueNet!=NULL);
	assert(pos!=NULL);

	return nnueEvaluateNet(pos, nnueNet);
}

Score nnueEvaluateNet(const Pos *pos, const NnueNet *net) {
	assert(pos!=NULL);
	assert(net!=NULL);

	// Pass cached accumulator values into nnueEvaluateNetAccum to do the heavy lifting
	return nnueEvaluateNetAccum(posGetSTM(pos), net, posGetNnueAccum(pos), false);
}

////////////////////////////////////////////////////////////////////////////////
// Private functions
////////////////////////////////////////////////////////////////////////////////

void nnueInterfaceUseNnue(void *userData, bool value) {
	assert(userData==NULL);

	// Update global flag
	nnueUseNnue=value;

	// Clear cached values where possible (ideally restart the engine and set this option before doing any searches)
	searchClear(); // includes TT
	evalClear();
}
