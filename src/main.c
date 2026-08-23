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
#include "nnue.h"
#include "pos.h"
#include "search.h"
#include "syzygy.h"
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
	nnueInit();
	ttInit();
	searchInit();
	syzygyInit();

	// UCI input loop
	uciLoop();

	// Quit modules
	syzygyQuit();
	searchQuit();
	ttQuit();
	nnueQuit();
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
	fprintf(file, "ucinewgame\n");
	fprintf(file, "hash %u\n", ttGetSizeMb());
	fprintf(file, "\n");

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
	fprintf(file, "info depth %u score %s nodes %llu time %llu", (unsigned int)depth, SCORETOSTR(score, bound), nodeCount, (unsigned long long int)time);
	if (time>0)
		fprintf(file, " nps %llu", (nodeCount*1000llu)/time);
	fprintf(file, " pv%s\n", pvStr);

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
	// Open log file
	FILE *file=fopen(path, "r");
	if (file==NULL) {
		printf("Error: could not read log data at '%s'\n", path);
		return false;
	}

	// Read log file looking for the given date string (and also for preceeding ucinewgame location)
	long ucinewgamePos=-1;
	long datePos=-1;

	char line[4096]; // TODO: better
	while(1) {
		// Read line (taking note of current position before we do so)
		long linePos=ftell(file);
		if (fgets(line, 4096, file)==NULL)
			break;

		// Strip newline to make parsing simpler
		if (line[0]!='\0' && line[strlen(line)-1]=='\n')
			line[strlen(line)-1]='\0';

		// Parse line
		if (strcmp(line, "ucinewgame")==0) {
			ucinewgamePos=linePos;
		} else if (strncmp(line, "date ", 5)==0 && strcmp(line+5, date)==0) {
			datePos=linePos;
			break;
		}
	}

	if (ucinewgamePos==-1 || datePos==-1) {
		printf("Error: could not find given date string/preceeding ucinewgame\n");
		fclose(file);
		return false;
	}

	// Start replaying game from first move (or first move we had to search for after any book opening)
	// First clear internal search state and make note of TT size (we may have to resize it to match the state of the game in the log file).
	searchClear();
	unsigned long originalTTSize=ttGetSizeMb();

	// Now start replaying game, move by move, until desired position reached.
	if (fseek(file, ucinewgamePos, SEEK_SET)!=0) {
		printf("Error: seek error (ucinewgame)\n");
		fclose(file);
		return false;
	}

	bool inEntry=false;
	char posStr[4096]; // TODO: better
	unsigned long long int nodes;
	while(1) {
		// Read line (taking note of current position before we do so)
		long linePos=ftell(file);
		if (fgets(line, 4096, file)==NULL)
			break;

		// Strip newline to make parsing simpler
		if (line[0]!='\0' && line[strlen(line)-1]=='\n')
			line[strlen(line)-1]='\0';

		// Parse line
		if (inEntry) {
			if (strncmp(line, "date ", 5)==0) {
				// Should have encountered 'nodes' before this
				printf("Error: file consistency error (subsequent 'date' before terminating 'nodes')\n");
				fclose(file);
				return false;
			} else if (strncmp(line, "position ", 9)==0) {
				// Grab position and also output it
				strcpy(posStr, line);
				printf("\n%s\n", posStr);
				printf("Log data:\n");
			} else if (strncmp(line, "info ", 5)==0) {
				// Output info lines from log file
				printf("info %s\n", line+5);
			} else if (strncmp(line, "nodes ", 6)==0) {
				// Grab nodes and also output it
				nodes=atoll(line+6);
				printf("info nodes %llu\n", nodes);

				// Check we have all the info we need from the log file
				inEntry=false;
				if (posStr[0]=='\0') {
					printf("Error: could not read position string for log file entry\n");
					fclose(file);
					return false;
				}

				if (nodes==0) {
					printf("Error: could not read node count for log file entry\n");
					fclose(file);
					return false;
				}

				// Set position instance
				Pos *pos=posNew(NULL);
				if (!uciPosFromStr(pos, posStr)) {
					printf("Error: could set position from string '%s'\n", posStr);
					posFree(pos);
					fclose(file);
					return false;
				}

				// Search
				SearchLimit limit;
				searchLimitInit(&limit, timeGet());
				searchLimitSetNodes(&limit, nodes);

				printf("Replay data:\n");
				searchThink(pos, &limit, SearchOutputPartial);
				searchWait();

				// Tidy up
				posFree(pos);
			}
		} else {
			if (strncmp(line, "date ", 5)==0) {
				// Have we reached the end of the entry the caller specified?
				if (linePos>datePos)
					break;

				// Reset for new position/search instance/entry
				posStr[0]='\0';
				nodes=0;
				inEntry=true;
			} else if (strncmp(line, "hash ", 5)==0) {
				// Grab TT size and also output it
				unsigned long newTTSize=atol(line+5);
				printf("Hash size: %lumb\n", newTTSize);

				// Resize the table
				ttSetSizeMb(newTTSize);
			}
		}
	}

	printf("\nReplay complete\n");

	// Restore TT size
	ttSetSizeMb(originalTTSize);

	// Close log file
	fclose(file);

	return true;
}
