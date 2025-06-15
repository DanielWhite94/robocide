#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "eval.h"
#include "pos.h"
#include "time.h"
#include "ttune.h"

typedef float TTuneCoefficient; // count of a particular feature in the eval of a position (fractional to allow MG/EG phasing)

typedef struct {
	TTuneCoefficient *coefficients; // coefficients for position n begin at n*ttuneParametersCount
	double *results; // game results for each position (0.0 for black win, 0.5 for draw, 1.0 for white win)
	double *cachedScores; // used to avoid calling ttuneComputeQ when we only make individual weight changes (calling ttunePositionsUpdateCache as needed)
	unsigned count; // actual number of positions added
	unsigned size; // max number of positions
} TTunePositions;

typedef struct {
	int initialValue;
	bool init;
	bool tune;
} TTuneParameter;

TTuneParameter *ttuneParameters=NULL;
unsigned ttuneParametersCount=0;

////////////////////////////////////////////////////////////////////////////////
// Private prototypes.
////////////////////////////////////////////////////////////////////////////////

TTunePositions *ttuneReadEpd(const char *path);

TTunePositions *ttunePositionsNew(unsigned size);
void ttunePositionsFree(TTunePositions *positions);
void ttunePositionsAdd(TTunePositions *positions, const TTuneCoefficient *coefficients, double result);
const TTuneCoefficient *ttunePositionsGetCoefficients(const TTunePositions *positions, unsigned n);
double ttunePositionsGetResult(const TTunePositions *positions, unsigned n);
double ttunePositionsGetCachedScore(const TTunePositions *positions, unsigned n);
void ttunePositionsUpdateCache(TTunePositions *positions, unsigned paramIndex, double delta);

double ttuneComputeE(TTunePositions *positions, const float *weights, double k, bool updateCache);
double ttuneComputeQ(const TTuneCoefficient *coefficients, const float *weights); // white-relative score
double ttuneComputeSigmoid(double s, double k);
double ttuneComputeOptimalK(TTunePositions *positions, const float *weights);

////////////////////////////////////////////////////////////////////////////////
// Public functions.
////////////////////////////////////////////////////////////////////////////////

void ttuneInit(unsigned parameterCount) {
	assert(ttuneParameters==NULL);
	assert(ttuneParametersCount==0);

	// Allocate memory
	ttuneParameters=malloc(sizeof(TTuneParameter)*parameterCount);
	ttuneParametersCount=parameterCount;

	// Indicate all parameters are uninitialized
	for(unsigned i=0; i<ttuneParametersCount; ++i)
		ttuneParameters[i].init=false;
}

void ttuneQuit(void) {
	// Free memory and reset variables
	free(ttuneParameters);
	ttuneParameters=NULL;
	ttuneParametersCount=0;
}

void ttuneRun(const char *positionInputFile, const char *codeOutputFile) {
	// Verify parameters have been setup correctly
	if (ttuneParametersCount<1) {
		printf("Error: no parameters added\n");
		return;
	}

	for(unsigned i=0; i<ttuneParametersCount; ++i) {
		if (!ttuneParameters[i].init) {
			printf("Error: parameter %u not initialized\n", i);
			return;
		}
	}

	// Read (quiet) positions from file (and compute evaluation coefficients for each one)
	printf("Loading quiet positions from EPD file at '%s'...\n", positionInputFile);
	TTunePositions *positions=ttuneReadEpd(positionInputFile);
	if (positions==NULL) {
		printf("Error: could not load positions\n");
		return;
	}

	printf("Loaded %u positions\n", positions->count);

	// Allocate weights array and initially set to current engine values
	float *weights=malloc(sizeof(float)*ttuneParametersCount);
	for(unsigned i=0; i<ttuneParametersCount; ++i)
		weights[i]=ttuneParameters[i].initialValue;

	// Find k (which minimises E)
	printf("Computing optimal K value...\n");

	double k=ttuneComputeOptimalK(positions, weights);

	printf("Found K=%f\n", k);

	// Tuning loop (attempting to minimise E by varying evaluation weights)
	double currentE=ttuneComputeE(positions, weights, k, true);
	printf("Preparing to run iteration loop with %u parameters (initial E=%f)\n", ttuneParametersCount, currentE);

	bool improvement;
	unsigned currentIteration=0;
	do {
		// Iteration start
		TimeMs startTime=timeGet();
		++currentIteration;

		// Loop over parameters one by one
		improvement=false;
		for(unsigned i=0; i<ttuneParametersCount; ++i) {
			// Not tuning this parameter?
			if (!ttuneParameters[i].tune)
				continue;

			// Look for improvement by adjusting weight (first try down then up)
			--weights[i];
			ttunePositionsUpdateCache(positions, i, -1.0);
			double decE=ttuneComputeE(positions, weights, k, false);
			if (decE<currentE) {
				improvement=true;
				currentE=decE;
				continue;
			}

			weights[i]+=2; // undo decrement and apply increment instead
			ttunePositionsUpdateCache(positions, i, +2.0);
			double incE=ttuneComputeE(positions, weights, k, false);
			if (incE<currentE) {
				improvement=true;
				currentE=incE;
				continue;
			}

			--weights[i];
			ttunePositionsUpdateCache(positions, i, -1.0);
		}

		// Simplify weights (e.g. averaging PSTs to zero)
		evaluateTTuneSimplifyWeights(weights);

		currentE=ttuneComputeE(positions, weights, k, true); // need to recompute cached values due to above call

		// Terminal output
		TimeMs deltaTime=timeGet()-startTime;
		printf("Iteration %u complete (E=%f, took %llu.%03llus)\n", currentIteration, currentE, deltaTime/1000, deltaTime%1000);

		// Code output
		evaluateTTuneOutputCode(codeOutputFile, weights);
	} while(improvement);

	printf("Tuning complete\n");

	// Tidy up
	free(weights);
	ttunePositionsFree(positions);
}

void ttuneAddParameter(unsigned id, int initialValue, bool tune) {
	assert(id<ttuneParametersCount);
	assert(!ttuneParameters[id].init);

	// Copy fields into our array entry
	ttuneParameters[id].initialValue=initialValue;
	ttuneParameters[id].init=true;
	ttuneParameters[id].tune=tune;
}

////////////////////////////////////////////////////////////////////////////////
// Private functions.
////////////////////////////////////////////////////////////////////////////////

TTunePositions *ttuneReadEpd(const char *path) {
	// Open EPD file for reading
	FILE *file=fopen(path, "r");
	if (file==NULL)
		return NULL;

	// Do an initial pass over the file to count the number of lines so we can allocate our positions object
	unsigned lineCount=utilFileCountLines(file);
	TTunePositions *positions=ttunePositionsNew(lineCount);

	// Create scratch position and coefficients to use to find evaluation coefficients
	Pos *pos=posNew(NULL);
	TTuneCoefficient *coefficients=malloc(sizeof(TTuneCoefficient)*ttuneParametersCount);

	// Read file one line at a time
	char line[1024];
	while(fgets(line, 1024, file)!=NULL) {
		// Try to parse FEN string and game result
		char *c9pos=strstr(line, " c9 \"");
		if (c9pos==NULL)
			continue;
		*c9pos='\0';

		char *fenStr=line;
		char *resultStr=c9pos+5;
		char *resultStrEndPos=strstr(resultStr, "\"");
		if (resultStrEndPos==NULL)
			continue;
		*resultStrEndPos='\0';

		double result;
		if (strcmp(resultStr, "1-0")==0)
			result=1.0;
		else if (strcmp(resultStr, "1/2-1/2")==0)
			result=0.5;
		else if (strcmp(resultStr, "0-1")==0)
			result=0.0;
		else
			continue;

		// Setup position and compute evaluation coefficients
		if (!posSetToFEN(pos, fenStr))
			continue;

		evaluateTTuneCoefficients(pos, coefficients);

		// Add position to list
		ttunePositionsAdd(positions, coefficients, result);
	}

	// Tidy up
	posFree(pos);
	free(coefficients);
	fclose(file);

	return positions;
}

TTunePositions *ttunePositionsNew(unsigned size) {
	// Allocate memory
	TTunePositions *positions=malloc(sizeof(TTunePositions));
	TTuneCoefficient *coefficients=malloc(sizeof(TTuneCoefficient)*ttuneParametersCount*size);
	double *results=malloc(sizeof(double)*size);
	double *cachedScores=malloc(sizeof(double)*size);
	if (positions==NULL || coefficients==NULL || results==NULL || cachedScores==NULL) {
		free(positions);
		free(coefficients);
		free(results);
		return NULL;
	}

	// Set fields
	positions->coefficients=coefficients;
	positions->results=results;
	positions->cachedScores=cachedScores;
	positions->count=0;
	positions->size=size;

	return positions;
}

void ttunePositionsFree(TTunePositions *positions) {
	// NULL check
	if (positions==NULL)
		return;

	// Free memory
	free(positions->coefficients);
	free(positions->results);
	free(positions->cachedScores);
	free(positions);
}

void ttunePositionsAdd(TTunePositions *positions, const TTuneCoefficient *coefficients, double result) {
	assert(positions!=NULL);
	assert(positions->count+1<=positions->size);

	// TODO: can probably avoid this memcpy by passing the adjusted positions->coefficients pointer directly into the evaluation function
	memcpy(positions->coefficients+ttuneParametersCount*positions->count, coefficients, sizeof(TTuneCoefficient)*ttuneParametersCount);
	positions->results[positions->count]=result;
	positions->cachedScores[positions->count]=0.0;
	++positions->count;
}

const TTuneCoefficient *ttunePositionsGetCoefficients(const TTunePositions *positions, unsigned n) {
	assert(positions!=NULL);
	assert(n<positions->count);

	return positions->coefficients+ttuneParametersCount*n;
}

double ttunePositionsGetResult(const TTunePositions *positions, unsigned n) {
	assert(positions!=NULL);
	assert(n<positions->count);

	return positions->results[n];
}

double ttunePositionsGetCachedScore(const TTunePositions *positions, unsigned n) {
	assert(positions!=NULL);
	assert(n<positions->count);

	return positions->cachedScores[n];
}

void ttunePositionsUpdateCache(TTunePositions *positions, unsigned paramIndex, double delta) {
	assert(positions!=NULL);
	assert(i<ttuneParametersCount);

	for(unsigned posIndex=0; posIndex<positions->count; ++posIndex) {
		const TTuneCoefficient *coefficients=ttunePositionsGetCoefficients(positions, posIndex);
		positions->cachedScores[posIndex]+=delta*coefficients[paramIndex];
	}
}

double ttuneComputeE(TTunePositions *positions, const float *weights, double k, bool updateCache) {
	assert(positions!=NULL);

	// Loop over all positions
	double total=0.0;
	double correction=0.0; // ensure accuracy by using Kahan summation
	for(unsigned posIndex=0; posIndex<positions->count; ++posIndex) {
		// Grab eval coefficients and the actual game result for this position
		const TTuneCoefficient *coefficients=ttunePositionsGetCoefficients(positions, posIndex);
		const double result=ttunePositionsGetResult(positions, posIndex);

		// Compute score and squared difference
		if (updateCache)
			positions->cachedScores[posIndex]=ttuneComputeQ(coefficients, weights);
		else
			assert(abs(positions->cachedScores[posIndex]-ttuneComputeQ(coefficients, weights))<0.0001);
		double q=ttunePositionsGetCachedScore(positions, posIndex);
		double s=ttuneComputeSigmoid(q, k);
		double delta=result-s;
		double delta2=delta*delta;

		// Add to total (preserving accuracy)
		double t=total+delta2;
		if (total>=delta2)
			correction+=(total-t)+delta2;
		else
			correction+=(delta2-t)+total;
		total=t;
	}

	// Return average error
	return (total+correction)/((double)positions->count);
}

double ttuneComputeQ(const TTuneCoefficient *coefficients, const float *weights) {
	// Loop over both arrays multiplying pairs and summing them
	// Split over four accumulators for speed
	double total0=0.0, total1=0.0, total2=0.0, total3=0.0;
	unsigned i;
	for(i=0; i+3<ttuneParametersCount; i+=4) {
		total0+=coefficients[i+0]*((float)weights[i+0]);
		total1+=coefficients[i+1]*((float)weights[i+1]);
		total2+=coefficients[i+2]*((float)weights[i+2]);
		total3+=coefficients[i+3]*((float)weights[i+3]);
	}
	for(; i<ttuneParametersCount; ++i)
		total0+=coefficients[i]*((float)weights[i]);

	return (total0+total1)+(total2+total3);
}

double ttuneComputeSigmoid(double s, double k) {
	// Convert evaluation/search score s into a logistic win/draw/loss value in the range [0,1]
	// Roughly equivalent to K=0.12041 in original Texel Tuning description
	return 1.0/(1.0+pow(2.0, -k*s));
}

double ttuneComputeOptimalK(TTunePositions *positions, const float *weights) {
	// Find k such that E is minimised
	double start=0.0, end=10.0, step=1.0;
	double curr=start;
	double best=ttuneComputeE(positions, weights, start, true);
	for (int i=0; i<10; ++i) {
		// Find the minimum within [start, end] using the current step
		curr=start-step;
		while (curr<end) {
			curr=curr+step;
			double error=ttuneComputeE(positions, weights, curr, true);
			if (error<best) {
				best=error;
				start=curr;
			}
		}
		// Adjust the search space
		end=start+step;
		start=start-step;
		step=step/10.0;
	}

	return start;
}
