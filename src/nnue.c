#include <assert.h>

#include "eval.h"
#include "main.h"
#include "nnue.h"
#include "nnue/tune.h"
#include "search.h"
#include "uci.h"

bool nnueUseNnue=true;
const char *nnueNetPath="nnue/nets/minifishport.nnue";

NnueNet *nnueNet=NULL; // holds the currently loaded network

////////////////////////////////////////////////////////////////////////////////
// Private prototypes
////////////////////////////////////////////////////////////////////////////////

void nnueInterfaceUseNnue(void *userData, bool value);
void nnueInterfaceSetEvalFile(void *userData, const char *value);

////////////////////////////////////////////////////////////////////////////////
// Public functions
////////////////////////////////////////////////////////////////////////////////

void nnueInit(void) {
	assert(nnueNet==NULL);

	// Add UCI option
	if (!uciOptionNewCheck("NNUE", &nnueInterfaceUseNnue, NULL, nnueUseNnue))
		mainFatalError("Error: Could not add NNUE option.\n");
	if (!uciOptionNewString("EvalFile", &nnueInterfaceSetEvalFile, NULL, nnueNetPath))
		mainFatalError("Error: Could not add EvalFile option.\n");

	// Load network
	nnueNet=nnueNetLoad(nnueNetPath);
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

void nnueInterfaceSetEvalFile(void *userData, const char *value) {
	assert(userData==NULL);
	assert(value!=NULL);

	// Attempt to load given file
	NnueNet *newNet=nnueNetLoad(value);
	if (nnueNet==NULL)
		uciWrite("info string Failed to load NNUE network at '%s'.\n", value);

	// If successful, replace existing
	if (newNet!=NULL) {
		nnueNetFree(nnueNet);
		nnueNet=newNet;
	}
}
