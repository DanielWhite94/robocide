#include <assert.h>

#include "main.h"
#include "nnue.h"

NnueNet *nnueNet=NULL; // holds the currently loaded network

////////////////////////////////////////////////////////////////////////////////
// Private prototypes
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
// Public functions
////////////////////////////////////////////////////////////////////////////////

void nnueInit(void) {
	assert(nnueNet==NULL);

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
