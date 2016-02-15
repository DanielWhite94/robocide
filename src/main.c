#include <stdio.h>
#include <stdlib.h>

#include "attacks.h"
#include "bb.h"
#include "bitbase.h"
#include "eval.h"
#include "main.h"
#include "pos.h"
#include "search.h"
#include "tt.h"
#include "uci.h"

int main(int argc, char **argv) {
	bbInit();
	attacksInit();
	bitbaseInit();
	posInit();
	evalInit();
	ttInit();
	searchInit();

	uciLoop();

	searchQuit();
	ttQuit();
	evalQuit();
	bitbaseQuit();

	return EXIT_SUCCESS;
}

void mainFatalError(const char *format, ...) {
	va_list ap;
	va_start(ap, format);
	vfprintf(stderr, format, ap);
	va_end(ap);
	exit(EXIT_FAILURE);
}
