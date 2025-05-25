#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "attacks.h"
#include "bb.h"
#include "bitbase.h"
#include "eval.h"
#include "history.h"
#include "main.h"
#include "pos.h"
#include "search.h"
#include "tt.h"
#include "uci.h"

char *mainLogFilePath=NULL;

int main(int argc, char **argv) {
	// Parse arguments
	for(unsigned i=1; i<argc; ++i) {
		if (strncmp(argv[i], "--logfile=", 10)==0)
			mainLogFilePath=argv[i]+10;
		else
			mainFatalError("Error: unknown argument '%s'\n", argv[i]);
	}

	// Init modules
	uciInit();
	bbInit();
	attacksInit();
	bitbaseInit();
	posInit();
	evalInit();
	ttInit();
	searchInit();

	// UCI input loop
	uciLoop();

	// Quit modules
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

bool mainIsLogging(void) {
	return (mainLogFilePath!=NULL);
}

void mainLogF(const char *format, ...) {
	// No logging required?
	if (mainLogFilePath==NULL)
		return;

	// Open log file
	FILE *file=fopen(mainLogFilePath, "a");
	if (file==NULL)
		return;

	// Write formatted string
	va_list ap;
	va_start(ap, format);
	vfprintf(file, format, ap);
	va_end(ap);

	// Close log file
	fclose(file);
}

void mainLogSearchStart(Pos *pos, TimeMs searchTime) {
	// No logging required?
	if (mainLogFilePath==NULL)
		return;

	// Open log file
	FILE *file=fopen(mainLogFilePath, "a");
	if (file==NULL)
		return;

	// Output date/time string
	time_t t=time(NULL);
	struct tm *tmp=localtime(&t);
	if (tmp==NULL) {
		fclose(file);
		return;
	}
	char dateStr[128];
	strftime(dateStr, 128, "%F.%H:%M:%S", tmp);
	fprintf(file, "date %s\n", dateStr);

	// Output position string
	char *posStr=uciPosToStr(pos);
	fprintf(file, "%s\n", posStr);
	free(posStr);

	// Output fen string
	char fenStr[128];
	posGetFEN(pos, fenStr);
	fprintf(file, "fen %s\n", fenStr);

	// Output search time
	fprintf(file, "searchtime %llu\n", searchTime);

	// Output TT and history data
	char ttPath[4096]; // TODO: better
	sprintf(ttPath, "%s.TT.%s", mainLogFilePath, dateStr);
	ttExport(ttPath);

	char historyPath[4096]; // TODO: better
	sprintf(historyPath, "%s.Hist.%s", mainLogFilePath, dateStr);
	historyExport(historyPath);

	// Close log file
	fclose(file);
}

void mainLogSearchDepth(Depth depth, Score score, Bound bound, unsigned long long nodeCount, TimeMs startTime, const char *pvStr) {
	// No logging required?
	if (mainLogFilePath==NULL)
		return;

	// Open log file
	FILE *file=fopen(mainLogFilePath, "a");
	if (file==NULL)
		return;

	// Write data
	TimeMs time=timeGet()-startTime;
	fprintf(file, "info depth %u score %s nodes %llu time %llu pv%s\n", (unsigned int)depth, SCORETOSTR(score, bound), nodeCount, (unsigned long long int)time, pvStr);

	// Close log file
	fclose(file);
}

void mainLogSearchEnd(unsigned long long int nodeCount) {
	// No logging required?
	if (mainLogFilePath==NULL)
		return;

	// Open log file
	FILE *file=fopen(mainLogFilePath, "a");
	if (file==NULL)
		return;

	// Write data
	fprintf(file, "nodes %llu\n", nodeCount);
	fprintf(file, "\n");

	// Close log file
	fclose(file);
}
