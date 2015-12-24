#include <stdio.h>
#include <stdlib.h>

#include "attacks.h"
#include "bb.h"
#include "robocide.h"
#include "pos.h"
#include "uci.h"

int main() {
	bbInit();
	attacksInit();
	posInit();

	uciLoop();


	return EXIT_SUCCESS;
}

void mainFatalError(const char *format, ...) {
	va_list ap;
	va_start(ap, format);
	vfprintf(stderr, format, ap);
	va_end(ap);
	exit(EXIT_FAILURE);
}
