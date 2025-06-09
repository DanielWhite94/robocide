/*

.....

## TODO ##########################################################################################################################################

* in eval add more/all params obviously
	thinking about it, if it works well and can handle a lot of parameters
	we could even add e.g. 64 values for each pst e.g. pawn mg, knight eg etc
	(start them at current values produced by best pst params in current implementation)
	(or could do e.g. 32 values if symmetrical etc)

	also, do we really want to add them manually like we are now?
	or can we take advantage of the uci option stuff?
	or even just add code to do both uci+texel in common functions like evalOptionNewVPairF?

* we need to think a bit about MG/EG interpolated crack
	imagine if we now just simply added e.g. KnightEG
	it would have the exact same coefficient as KnightMG and thus would get essentially the exact same weight assigned
	in reality need to either scale the MG/EG coefficients we return from here
	OR do something in the ttune module which scales coeff*weight or w/e (so when adding each parameter pass a flag indiciating if mg or eg)

	ok so how will this work?
	we keep two separate parameters - one for mg and one for eg
	so two different weights and two different coefficients
	naively the coefficients will be equal in any given position
	e.g. if you have +2 knights then both MG and EG is still just counting +2
	so what if we adjust this +2 count by the current MG/EG 'fraction'?
	doesn't actually matter that the counts are integers
	then can just as easily be fractional so doubles

	if we do all of this in evaluateCoefficients function,
	then dont need to worry about it anywhere else

* do we still want a separate evaluateCoefficients function?
	but this duplicates logic we already have in main eval function
	but main eval function is complex and has special cases and hash tables etc etc

## FUTURE ########################################################################################################################################
* threading to speed up
	should be as simple as dividing up the Q/subtraction/squaring bits during E calculation
	each thread keeps its own running total, then combine them at the end
	no locking needed etc and should scale well to any number of threads
	number of threads could be specified by the uci option we already have,
	or by an argument to 'ttune' uci command (after the path, if not provided use 1 core)

	this might still be a good idea although now it seems computing E is very fast to the point of not being worth it
	although with more parameters added and more positions in the EPD file this could become slower again

* daniel shawul has good idea with 'jacobian'/coefficients idea
	so basically as a first initial step, loop over each position and call eval
	but instead of caring about returned value, collect a list of 'counts' of each (linear) factor
	e.g. if white up 2 pawns, then pawn coefficient is +2, knight/bishop etc coefficients might all then be 0
	means when we are computing E, each position eval doesnt actually have to call eval
	just has to multiply the precomputed coefficients with the new parameter values

	can probably also use this information for better gradient descent optimising too

	so how will we extract these 'counts'/coefficients?
	perhaps we can compile a different version with a flag/macro (like we do already with EVALINFO)
	this version will allow a struct ptr to be passed to eval which will be filled with all of the 'coefficients'
	in reality, we can slowly add coefficients to this and even skip some initially which are non-linear or just complex
	can then just tune what we get back

	the eval for a particular position would return the coefficients
	which will be an array of ints (counts) basically
	use an enum to list the coefficients/parameters we have added to TTune interface
* eventually want to replace weight tweaking (to minimise E) with a better gradient descent method or similar
	perhaps taking advantage of the eval coefficients for each position as a sort of gradient idk

*/

#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "eval.h"
#include "pos.h"
#include "ttune.h"

typedef double TTuneCoefficient; // count of a particular feature in the eval of a position (fractional to allow MG/EG phasing)

typedef struct {
	TTuneCoefficient *coefficients; // coefficients for position n begin at n*ttuneParametersCount
	double *results; // game results for each position (0.0 for black win, 0.5 for draw, 1.0 for white win)
	unsigned count; // actual number of positions added
	unsigned size; // max number of positions
} TTunePositions;

typedef struct {
	char name[128];
	int minValue;
	int maxValue;
	int initialValue;
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

double ttuneComputeE(const TTunePositions *positions, const int *weights);
double ttuneComputeQ(const TTuneCoefficient *coefficients, const int *weights); // white-relative score
double ttuneComputeSigmoid(double s);

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
		ttuneParameters[i].name[0]='\0';
}

void ttuneQuit(void) {
	// Free memory and reset variables
	free(ttuneParameters);
	ttuneParameters=NULL;
	ttuneParametersCount=0;
}

void ttuneRun(const char *positionFile) {
	// Verify parameters have been setup correctly
	if (ttuneParametersCount<1) {
		printf("Error: no parameters added\n");
		return;
	}

	for(unsigned i=0; i<ttuneParametersCount; ++i) {
		if (ttuneParameters[i].name[0]=='\0') {
			printf("Error: parameter %u not initialized\n", i);
			return;
		}

		if ((ttuneParameters[i].initialValue>ttuneParameters[i].maxValue) ||
		    (ttuneParameters[i].initialValue<ttuneParameters[i].minValue)) {
			printf("Error: parameter %u inconsistent\n", i);
			return;
		}
	}

	// Read (quiet) positions from file (and compute evaluation coefficients for each one)
	printf("Loading quiet positions from EPD file at '%s'...\n", positionFile);
	TTunePositions *positions=ttuneReadEpd(positionFile);
	if (positions==NULL) {
		printf("Error: could not load positions\n");
		return;
	}

	printf("Loaded %u positions\n", positions->count);

	// Allocate weights array and initially set to current engine values
	int *weights=malloc(sizeof(int)*ttuneParametersCount);
	for(unsigned i=0; i<ttuneParametersCount; ++i)
		weights[i]=ttuneParameters[i].initialValue;

	// Tuning loop (attempting to minimise E by varying evaluation weights)
	double currentE=ttuneComputeE(positions, weights);
	bool improvement;
	do {
		// Loop over parameters one by one
		improvement=false;
		for(unsigned i=0; i<ttuneParametersCount; ++i) {
			// Consider incrementing and decrementing the weight of the parameter in question
			double incE=currentE;
			if (weights[i]<ttuneParameters[i].maxValue) {
				++weights[i];
				incE=ttuneComputeE(positions, weights);
				--weights[i];
			}

			double decE=currentE;
			if (weights[i]>ttuneParameters[i].minValue) {
				--weights[i];
				decE=ttuneComputeE(positions, weights);
				++weights[i];
			}

			// No improvement moving in either direction?
			if (incE>=currentE && decE>=currentE)
				continue;

			// Adjust weight for this parameter and update our current E value
			improvement=true;

			if (incE<decE) {
				++weights[i];
				currentE=incE;
			} else {
				--weights[i];
				currentE=decE;
			}
		}

		// Output
		printf("Iteration complete (E=%f):\n", currentE);
		for(unsigned i=0; i<ttuneParametersCount; ++i)
			printf("    %s %i -> %i\n", ttuneParameters[i].name, ttuneParameters[i].initialValue, weights[i]);
	} while(improvement);

	printf("Tuning complete\n");

	// Tidy up
	free(weights);
	ttunePositionsFree(positions);
}

void ttuneAddParameter(unsigned id, const char *name, int minValue, int maxValue, int initialValue) {
	assert(id<ttuneParametersCount);

	// Copy fields into our array entry
	strcpy(ttuneParameters[id].name, name); // TODO: fix buffer overflow
	ttuneParameters[id].minValue=minValue;
	ttuneParameters[id].maxValue=maxValue;
	ttuneParameters[id].initialValue=initialValue;
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

		evaluateCoefficients(pos, coefficients);

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
	if (positions==NULL || coefficients==NULL || results==NULL) {
		free(positions);
		free(coefficients);
		free(results);
		return NULL;
	}

	// Set fields
	positions->coefficients=coefficients;
	positions->results=results;
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
	free(positions);
}

void ttunePositionsAdd(TTunePositions *positions, const TTuneCoefficient *coefficients, double result) {
	assert(positions!=NULL);
	assert(positions->count+1<=positions->size);

	// TODO: can probably avoid this memcpy by passing the adjusted positions->coefficients pointer directly into the evaluation function
	memcpy(positions->coefficients+ttuneParametersCount*positions->count, coefficients, sizeof(TTuneCoefficient)*ttuneParametersCount);
	positions->results[positions->count]=result;
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

double ttuneComputeE(const TTunePositions *positions, const int *weights) {
	assert(positions!=NULL);

	// Loop over all positions
	double total=0.0;
	double correction=0.0; // ensure accuracy by using Kahan summation
	for(unsigned i=0; i<positions->count; ++i) {
		// Grab eval coefficients and the actual game result for this position
		const TTuneCoefficient *coefficients=ttunePositionsGetCoefficients(positions, i);
		const double result=ttunePositionsGetResult(positions, i);

		// Compute score and squared difference
		double q=ttuneComputeQ(coefficients, weights);
		double s=ttuneComputeSigmoid(q);
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

double ttuneComputeQ(const TTuneCoefficient *coefficients, const int *weights) {
	// Simply loop over both arrays multiplying pairs and summing them
	double total=0.0;
	for(unsigned i=0; i<ttuneParametersCount; ++i)
		total+=coefficients[i]*((double)weights[i]);

	return total;
}

double ttuneComputeSigmoid(double s) {
	// Convert evaluation/search score s into a logistic win/draw/loss value in the range [0,1]
	// Roughly equivalent to K=0.12041 in original Texel Tuning description
	return 1.0/(1.0+pow(2.0, -0.001*s));
}
