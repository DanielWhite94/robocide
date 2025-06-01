#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

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

void mainLogNewGame(void) {
	// No logging required?
	if (mainLogFilePath==NULL)
		return;

	// Open log file
	FILE *file=fopen(mainLogFilePath, "a");
	if (file==NULL)
		return;

	// Write data
	fprintf(file, "ucinewgame\n\n");

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

bool mainReplay(const char *path, const char *date) {
	// Reset state
	searchClear();
	evalClear();

	// Import TT and history data
	char ttPath[4096]; // TODO: better
	sprintf(ttPath, "%s.TT.%s", path, date);
	if (!ttImport(ttPath)) {
		printf("Error: could not import TT data at '%s'\n", ttPath);
		return false;
	}

	char historyPath[4096]; // TODO: better
	sprintf(historyPath, "%s.Hist.%s", path, date);
	if (!historyImport(historyPath)) {
		printf("Error: could not import history data at '%s'\n", historyPath);
		return false;
	}

	// Read position and nodes searched
	FILE *file=fopen(path, "r");
	if (file==NULL) {
		printf("Error: could not read log data at '%s'\n", path);
		return false;
	}

	char posStr[4096]={0}; // TODO: better
	unsigned long long int nodes=0;
	TimeMs searchTimeMs=0;

	printf("Log file data:\n");
	bool inMatchingEntry=false;
	char line[4096]; // TODO: better
	while(fgets(line, 4096, file)!=NULL) {
		if (line[0]!='\0' && line[strlen(line)-1]=='\n')
			line[strlen(line)-1]='\0';

		if (strncmp(line, "date ", 5)==0) {
			if (strcmp(line+5, date)==0)
				inMatchingEntry=true;
			else if (inMatchingEntry)
				break; // end of entry - no need to read further into the file
		} else if (inMatchingEntry && strncmp(line, "position ", 9)==0) {
			strcpy(posStr, line);
		} else if (inMatchingEntry && strncmp(line, "nodes ", 6)==0) {
			nodes=atoll(line+6);
		} else if (inMatchingEntry && strncmp(line, "searchtime ", 11)==0) {
			searchTimeMs=atoll(line+11);
		} else if (inMatchingEntry && strncmp(line, "info ", 5)==0) {
			printf("%s\n", line+5);
		}
	}

	fclose(file);

	if (posStr[0]=='\0') {
		printf("Error: could not read position string from log file\n");
		return false;
	}

	if (nodes==0) {
		printf("Error: could not read node count from log file\n");
		return false;
	}

	if (searchTimeMs==0) {
		printf("Error: could not read search time from log file\n");
		return false;
	}

	// Set position instance
	Pos *pos=posNew(NULL);
	if (!uciPosFromStr(pos, posStr)) {
		posFree(pos);
		printf("Error: could set position from string '%s'\n", posStr);
		return false;
	}

	// Search
	SearchLimit limit;
	searchLimitInit(&limit, timeGet());
	searchLimitSetNodes(&limit, nodes);

	printf("Replay data:\n");
	searchThink(pos, &limit, SearchOutputPartial);

	// Tidy up
	posFree(pos);

	return true;
}
