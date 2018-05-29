#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "attacks.h"
#include "bb.h"
#include "bitbase.h"
#include "colour.h"
#include "eval.h"
#include "htable.h"
#include "main.h"
#include "tune.h"
#include "uci.h"

const VPair VPairZero={0,0};

typedef int16_t Value16;
typedef struct { Value16 mg, eg; } VPair16;
const VPair16 VPair16Zero={0,0};

typedef struct EvalData EvalData;

typedef struct {
	BB pawns[ColourNB], passed[ColourNB], semiOpenFiles[ColourNB], openFiles;
	VPair score;
} EvalPawnData;
HTable *evalPawnTable=NULL;
const size_t evalPawnTableDefaultSizeMb=1;
#define evalPawnTableMaxSizeMb ((HTableMaxEntryCount*sizeof(EvalPawnData))/(1024*1024)) // 256gb

STATICASSERT(ScoreBit<=16);
STATICASSERT(EvalMatTypeBit<=8);
typedef struct {
	MatInfo mat;
	VPair offset;
	int16_t scoreOffset;
	uint8_t weightMG, weightEG;
	uint8_t type; // If this is EvalMatTypeInvalid implies all fields not yet computed. Otherwise mat must also be set.
	uint8_t computed; // True if all fields are set, not just mat and type.
	uint8_t padding[2];
} EvalMatData;

HTable *evalMatTable=NULL;
const size_t evalMatTableDefaultSizeMb=1;
#define evalMatTableMaxSizeMb ((HTableMaxEntryCount*sizeof(EvalMatData))/(1024*1024)) // 96gb

struct EvalData {
	const Pos *pos;
	EvalPawnData pawnData;
	EvalMatData matData;
};

////////////////////////////////////////////////////////////////////////////////
// Tunable values.
////////////////////////////////////////////////////////////////////////////////

TUNECONST VPair evalMaterial[PieceTypeNB]={
	[PieceTypeNone]={0,0},
	[PieceTypePawn]={900,1300},
	[PieceTypeKnight]={3100,3100},
	[PieceTypeBishopL]={3010,3070},
	[PieceTypeBishopD]={3010,3070},
	[PieceTypeRook]={5350,5350},
	[PieceTypeQueen]={10000,10000},
	[PieceTypeKing]={0,0}
};
TUNECONST VPair evalPawnFiles[FileNB]={{-100,-50},{-50,0},{0,0},{50,50},{50,50},{0,0},{-50,0},{-100,-50}};
TUNECONST VPair evalPawnRanks[RankNB]={{0,0},{0,0},{10,10},{20,35},{30,80},{40,110},{50,150},{0,0}};
TUNECONST VPair evalPawnCentre={200,0};
TUNECONST VPair evalPawnOuterCentre={50,0};
TUNECONST VPair evalPawnDoubled={-100,-200};
TUNECONST VPair evalPawnIsolated={-300,-200};
TUNECONST VPair evalPawnBlocked={-100,-100};
TUNECONST VPair evalPawnPassedQuadA={56,50}; // Coefficients used in quadratic formula for passed pawn score (with rank as the input).
TUNECONST VPair evalPawnPassedQuadB={-109,50};
TUNECONST VPair evalPawnPassedQuadC={155,50};
TUNECONST VPair evalKnightPawnAffinity={30,30}; // Bonus each knight receives for each friendly pawn on the board.
TUNECONST VPair evalBishopPair={500,500};
TUNECONST VPair evalBishopMob={40,30};
TUNECONST VPair evalOppositeBishopFactor={256,192}; // /256.
TUNECONST VPair evalRookPawnAffinity={-70,-70}; // Bonus each rook receives for each friendly pawn on the board.
TUNECONST VPair evalRookMobFile={20,30};
TUNECONST VPair evalRookMobRank={10,20};
TUNECONST VPair evalRookOpenFile={100,50};
TUNECONST VPair evalRookSemiOpenFile={50,20};
TUNECONST VPair evalRookOn7th={50,100};
TUNECONST VPair evalRookTrapped={-400,0};
TUNECONST VPair evalKingShieldClose={150,0};
TUNECONST VPair evalKingShieldFar={50,0};
TUNECONST VPair evalKingNearPasserFactor={0,200};
TUNECONST VPair evalKingCastlingMobilityNone={0,0};
TUNECONST VPair evalKingCastlingMobilityK={100,0};
TUNECONST VPair evalKingCastlingMobilityQ={100,0};
TUNECONST VPair evalKingCastlingMobilityKQ={200,0};
TUNECONST VPair evalTempoDefault={35,0};
TUNECONST Value evalHalfMoveFactor=2048;
TUNECONST Value evalWeightFactor=144;

VPair evalPST[PieceNB][SqNB]={
	[PieceNone]={
		{    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0},
		{    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0},
		{    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0},
		{    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0},
		{    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0},
		{    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0},
		{    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0},
		{    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0}
	},
	[PieceWPawn]={
		{    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0},
		{    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0},
		{    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0},
		{    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0},
		{    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0},
		{    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0},
		{    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0},
		{    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0}
	},
	[PieceWKnight]={
		{ -170, -120},{ -120,  -60},{  -80,  -30},{  -60,  -10},{  -60,  -10},{  -80,  -30},{ -120,  -60},{ -170, -120},
		{ -110,  -60},{  -60,  -10},{  -30,   20},{  -10,   30},{  -10,   30},{  -30,   20},{  -60,  -10},{ -110,  -60},
		{  -70,  -30},{  -20,   20},{   10,   50},{   20,   60},{   20,   60},{   10,   50},{  -20,   20},{  -70,  -30},
		{  -40,  -10},{   10,   30},{   30,   60},{   40,   70},{   40,   70},{   30,   60},{   10,   30},{  -40,  -10},
		{  -10,  -10},{   30,   30},{   60,   60},{   60,   70},{   60,   70},{   60,   60},{   30,   30},{  -10,  -10},
		{    0,  -30},{   40,   20},{   70,   50},{   80,   60},{   80,   60},{   70,   50},{   40,   20},{    0,  -30},
		{  -10,  -60},{   40,  -10},{   70,   20},{   90,   30},{   90,   30},{   70,   20},{   40,  -10},{  -10,  -60},
		{  -20, -120},{   20,  -60},{   60,  -30},{   80,  -10},{   80,  -10},{   60,  -30},{   20,  -60},{  -20, -120}
	},
	[PieceWBishopL]={
		{   80,   60},{   80,   60},{   80,   60},{   80,   60},{   80,   60},{   80,   60},{   80,   60},{   80,   60},
		{   80,   60},{  120,   90},{  120,   90},{  120,   90},{  120,   90},{  120,   90},{  120,   90},{   80,   60},
		{   80,   60},{  120,   90},{  120,   90},{  120,   90},{  120,   90},{  120,   90},{  120,   90},{   80,   60},
		{   80,   60},{  120,   90},{  120,   90},{  160,  120},{  160,  120},{  120,   90},{  120,   90},{   80,   60},
		{   80,   60},{  120,   90},{  120,   90},{  160,  120},{  160,  120},{  120,   90},{  120,   90},{   80,   60},
		{   80,   60},{  120,   90},{  120,   90},{  120,   90},{  120,   90},{  120,   90},{  120,   90},{   80,   60},
		{   80,   60},{  120,   90},{  120,   90},{  120,   90},{  120,   90},{  120,   90},{  120,   90},{   80,   60},
		{   80,   60},{   80,   60},{   80,   60},{   80,   60},{   80,   60},{   80,   60},{   80,   60},{   80,   60}
	},
	[PieceWBishopD]={
		{   80,   60},{   80,   60},{   80,   60},{   80,   60},{   80,   60},{   80,   60},{   80,   60},{   80,   60},
		{   80,   60},{  120,   90},{  120,   90},{  120,   90},{  120,   90},{  120,   90},{  120,   90},{   80,   60},
		{   80,   60},{  120,   90},{  120,   90},{  120,   90},{  120,   90},{  120,   90},{  120,   90},{   80,   60},
		{   80,   60},{  120,   90},{  120,   90},{  160,  120},{  160,  120},{  120,   90},{  120,   90},{   80,   60},
		{   80,   60},{  120,   90},{  120,   90},{  160,  120},{  160,  120},{  120,   90},{  120,   90},{   80,   60},
		{   80,   60},{  120,   90},{  120,   90},{  120,   90},{  120,   90},{  120,   90},{  120,   90},{   80,   60},
		{   80,   60},{  120,   90},{  120,   90},{  120,   90},{  120,   90},{  120,   90},{  120,   90},{   80,   60},
		{   80,   60},{   80,   60},{   80,   60},{   80,   60},{   80,   60},{   80,   60},{   80,   60},{   80,   60}
	},
	[PieceWRook]={
		{  -15,    0},{    0,    0},{   15,    0},{   30,    0},{   30,    0},{   15,    0},{    0,    0},{  -15,    0},
		{  -15,    0},{    0,    0},{   15,    0},{   30,    0},{   30,    0},{   15,    0},{    0,    0},{  -15,    0},
		{  -15,    0},{    0,    0},{   15,    0},{   30,    0},{   30,    0},{   15,    0},{    0,    0},{  -15,    0},
		{  -15,    0},{    0,    0},{   15,    0},{   30,    0},{   30,    0},{   15,    0},{    0,    0},{  -15,    0},
		{  -15,    0},{    0,    0},{   15,    0},{   30,    0},{   30,    0},{   15,    0},{    0,    0},{  -15,    0},
		{  -15,    0},{    0,    0},{   15,    0},{   30,    0},{   30,    0},{   15,    0},{    0,    0},{  -15,    0},
		{  -15,    0},{    0,    0},{   15,    0},{   30,    0},{   30,    0},{   15,    0},{    0,    0},{  -15,    0},
		{  -15,    0},{    0,    0},{   15,    0},{   30,    0},{   30,    0},{   15,    0},{    0,    0},{  -15,    0}
	},
	[PieceWQueen]={
		{  -50,    0},{  -50,    0},{  -50,    0},{  -50,    0},{  -50,    0},{  -50,    0},{  -50,    0},{  -50,    0},
		{  -50,    0},{  -50,    0},{  -50,    0},{  -50,    0},{  -50,    0},{  -50,    0},{  -50,    0},{  -50,    0},
		{  -50,    0},{  -50,    0},{  -50,    0},{  -50,    0},{  -50,    0},{  -50,    0},{  -50,    0},{  -50,    0},
		{  -50,    0},{  -50,    0},{  -50,    0},{  -50,    0},{  -50,    0},{  -50,    0},{  -50,    0},{  -50,    0},
		{  -25,    0},{  -25,    0},{  -25,    0},{  -25,    0},{  -25,    0},{  -25,    0},{  -25,    0},{  -25,    0},
		{    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0},
		{   25,    0},{   25,    0},{   25,    0},{   25,    0},{   25,    0},{   25,    0},{   25,    0},{   25,    0},
		{   50,    0},{   50,    0},{   50,    0},{   50,    0},{   50,    0},{   50,    0},{   50,    0},{   50,    0}
	},
	[PieceWKing]={
		{  570, -460},{  570, -240},{  350, -120},{  200,  -40},{  200,  -40},{  350, -120},{  570, -240},{  570, -460},
		{  350, -240},{  320,  -40},{  140,   60},{   30,  120},{   30,  120},{  140,   60},{  320,  -40},{  350, -240},
		{  100, -120},{   50,   60},{ -110,  180},{ -260,  240},{ -260,  240},{ -110,  180},{   50,   60},{  100, -120},
		{    0,  -40},{  -40,  120},{ -320,  240},{ -790,  260},{ -790,  260},{ -320,  240},{  -40,  120},{    0,  -40},
		{  -50,  -40},{ -110,  120},{ -390,  240},{ -860,  260},{ -860,  260},{ -390,  240},{ -110,  120},{  -50,  -40},
		{  160, -120},{ -100,   60},{ -320,  180},{ -480,  240},{ -480,  240},{ -320,  180},{ -100,   60},{  160, -120},
		{  200, -240},{  -30,  -40},{ -210,   60},{ -310,  120},{ -310,  120},{ -210,   60},{  -30,  -40},{  200, -240},
		{  290, -460},{   70, -240},{  -80, -120},{ -160,  -40},{ -160,  -40},{  -80, -120},{   70, -240},{  290, -460}
	}
};

////////////////////////////////////////////////////////////////////////////////
// Derived values
////////////////////////////////////////////////////////////////////////////////

typedef enum {
	PawnTypeStandard=0,
	PawnTypeShiftDoubled=0,
	PawnTypeShiftIsolated=1,
	PawnTypeShiftPassed=2,
	PawnTypeDoubled=(1u<<PawnTypeShiftDoubled),
	PawnTypeIsolated=(1u<<PawnTypeShiftIsolated),
	PawnTypePassed=(1u<<PawnTypeShiftPassed),
	PawnTypeNB=16,
} PawnType;
VPair evalPawnValue[ColourNB][PawnTypeNB][SqNB];
VPair evalKingCastlingMobility[CastRightsNB];
int evalHalfMoveFactors[128];
uint8_t evalWeightEGFactors[128];

VPair evalKingNearPasser[8];

////////////////////////////////////////////////////////////////////////////////
// Private prototypes.
////////////////////////////////////////////////////////////////////////////////

Score evaluateInternal(const Pos *pos);

VPair evaluateDefault(EvalData *data);
VPair evaluateKPvK(EvalData *data);

void evalGetMatData(const Pos *pos, EvalMatData *matData);
void evalComputeMatData(const Pos *pos, EvalMatData *matData);
HTableKey evalGetMatDataHTableKeyFromPos(const Pos *pos);

void evalGetPawnData(const Pos *pos, EvalPawnData *pawnData);
void evalComputePawnData(const Pos *pos, EvalPawnData *pawnData);
HTableKey evalGetPawnDataHTableKeyFromPos(const Pos *pos);

VPair evaluateDefaultGlobal(EvalData *data);
VPair evaluateDefaultKing(EvalData *data, Colour colour);

Score evalInterpolate(const EvalData *data, const VPair *score);

void evalVPairAddVPair16To(VPair *a, const VPair16 *b);
void evalVPairSubVPair16From(VPair *a, const VPair16 *b);

#ifdef TUNE
void evalSetValue(void *varPtr, int value);
bool evalOptionNewVPair(const char *name, VPair *score);
void evalSetValue16(void *varPtr, int value);
bool evalOptionNewVPair16(const char *name, VPair16 *score);
#endif

void evalRecalc(void);

void evalVerify(void);

EvalMatType evalComputeMatType(const Pos *pos);

////////////////////////////////////////////////////////////////////////////////
// Public functions.
////////////////////////////////////////////////////////////////////////////////

void evalInit(void) {
	// Setup pawn hash table.
	evalPawnTable=htableNew(sizeof(EvalPawnData), evalPawnTableDefaultSizeMb);
	if (evalPawnTable==NULL)
		mainFatalError("Error: Could not allocate pawn hash table.\n");
	uciOptionNewSpin("PawnHash", &htableResizeInterface, evalPawnTable, 1, evalPawnTableMaxSizeMb, evalPawnTableDefaultSizeMb);
	uciOptionNewButton("Clear PawnHash", &htableClearInterface, evalPawnTable);

	// Setup mat hash table.
	evalMatTable=htableNew(sizeof(EvalMatData), evalMatTableDefaultSizeMb);
	if (evalMatTable==NULL)
		mainFatalError("Error: Could not allocate mat hash table.\n");
	uciOptionNewSpin("MatHash", &htableResizeInterface, evalMatTable, 1, evalMatTableMaxSizeMb, evalMatTableDefaultSizeMb);
	uciOptionNewButton("Clear MatHash", &htableClearInterface, evalMatTable);

	// Calculate dervied values (such as passed pawn table).
	evalRecalc();

	// Setup callbacks for tuning values.
# ifdef TUNE
	evalOptionNewVPair("Pawn", &evalMaterial[PieceTypePawn]);
	evalOptionNewVPair("Knight", &evalMaterial[PieceTypeKnight]);
	evalOptionNewVPair("Bishop", &evalMaterial[PieceTypeBishopL]);
	evalOptionNewVPair("Rook", &evalMaterial[PieceTypeRook]);
	evalOptionNewVPair("Queen", &evalMaterial[PieceTypeQueen]);
	evalOptionNewVPair("PawnCentre", &evalPawnCentre);
	evalOptionNewVPair("PawnOuterCentre", &evalPawnOuterCentre);
	evalOptionNewVPair("PawnFilesAH", &evalPawnFiles[FileA]);
	evalOptionNewVPair("PawnFilesBG", &evalPawnFiles[FileB]);
	evalOptionNewVPair("PawnFilesCF", &evalPawnFiles[FileC]);
	evalOptionNewVPair("PawnFilesDE", &evalPawnFiles[FileD]);
	evalOptionNewVPair("PawnRank2", &evalPawnRanks[Rank2]);
	evalOptionNewVPair("PawnRank3", &evalPawnRanks[Rank3]);
	evalOptionNewVPair("PawnRank4", &evalPawnRanks[Rank4]);
	evalOptionNewVPair("PawnRank5", &evalPawnRanks[Rank5]);
	evalOptionNewVPair("PawnRank6", &evalPawnRanks[Rank6]);
	evalOptionNewVPair("PawnRank7", &evalPawnRanks[Rank7]);
	evalOptionNewVPair("PawnDoubled", &evalPawnDoubled);
	evalOptionNewVPair("PawnIsolated", &evalPawnIsolated);
	evalOptionNewVPair("PawnBlocked", &evalPawnBlocked);
	evalOptionNewVPair("PawnPassedQuadA", &evalPawnPassedQuadA);
	evalOptionNewVPair("PawnPassedQuadB", &evalPawnPassedQuadB);
	evalOptionNewVPair("PawnPassedQuadC", &evalPawnPassedQuadC);
	evalOptionNewVPair("KnightPawnAffinity", &evalKnightPawnAffinity);
	evalOptionNewVPair("BishopPair", &evalBishopPair);
	evalOptionNewVPair("BishopMobility", &evalBishopMob);
	uciOptionNewSpin("OppositeBishopFactorMG", &evalSetValue, &evalOppositeBishopFactor.mg, 0, 512, evalOppositeBishopFactor.mg);
	uciOptionNewSpin("OppositeBishopFactorEG", &evalSetValue, &evalOppositeBishopFactor.eg, 0, 512, evalOppositeBishopFactor.eg);
	evalOptionNewVPair("RookPawnAffinity", &evalRookPawnAffinity);
	evalOptionNewVPair("RookMobilityFile", &evalRookMobFile);
	evalOptionNewVPair("RookMobilityRank", &evalRookMobRank);
	evalOptionNewVPair("RookOpenFile", &evalRookOpenFile);
	evalOptionNewVPair("RookSemiOpenFile", &evalRookSemiOpenFile);
	evalOptionNewVPair("RookOn7th", &evalRookOn7th);
	evalOptionNewVPair("RookTrapped", &evalRookTrapped);
	evalOptionNewVPair("KingShieldClose", &evalKingShieldClose);
	evalOptionNewVPair("KingShieldFar", &evalKingShieldFar);
	evalOptionNewVPair("KingNearPasser", &evalKingNearPasserFactor);
	evalOptionNewVPair("KingCastlingMobilityNone", &evalKingCastlingMobilityNone);
	evalOptionNewVPair("KingCastlingMobilityK", &evalKingCastlingMobilityK);
	evalOptionNewVPair("KingCastlingMobilityQ", &evalKingCastlingMobilityQ);
	evalOptionNewVPair("KingCastlingMobilityKQ", &evalKingCastlingMobilityKQ);
	evalOptionNewVPair("Tempo", &evalTempoDefault);
	evalOptionNewVPair("TempoKQKQ", &evalTempoKQKQ);
	evalOptionNewVPair("TempoKQQKQQ", &evalTempoKQQKQQ);
	uciOptionNewSpin("HalfMoveFactor", &evalSetValue, &evalHalfMoveFactor, 1, 32768, evalHalfMoveFactor);
	uciOptionNewSpin("WeightFactor", &evalSetValue, &evalWeightFactor, 1, 1024, evalWeightFactor);
# endif
}

void evalQuit(void) {
	htableFree(evalPawnTable);
	evalPawnTable=NULL;
	htableFree(evalMatTable);
	evalMatTable=NULL;
}

Score evaluate(const Pos *pos) {
	Score score=evaluateInternal(pos);
#	ifndef NDEBUG
	Pos *scratchPos=posNewFromPos(pos);
	posMirror(scratchPos);
	Score scoreM=evaluateInternal(scratchPos);
	posFlip(scratchPos);
	Score scoreFM=evaluateInternal(scratchPos);
	posMirror(scratchPos);
	Score scoreF=evaluateInternal(scratchPos);
	posFree(scratchPos);
	assert(scoreM==score && scoreFM==score && scoreF==score);
#	endif
	return score;
}

void evalClear(void) {
	// Clear hash tables.
	htableClear(evalPawnTable);
	htableClear(evalMatTable);

	// The material table uses mat!=entry->mat to indicate an unused/uncalculated entry.
	// The only entry where this is not the case after clearing above, is the entry with key 0,
	// so fix this now as a special case.
	EvalMatData *entry=htableGrab(evalMatTable, 0);
	entry->mat=1;
	htableRelease(evalMatTable, 0);
}

EvalMatType evalGetMatType(const Pos *pos) {
	// Grab hash entry for this position key
	HTableKey hTableKey=evalGetMatDataHTableKeyFromPos(pos);
	EvalMatData *entry=htableGrab(evalMatTable, hTableKey);

	// If not a match clear entry
	MatInfo mat=posGetMatInfo(pos);
	if (entry->mat!=mat)
		entry->type=EvalMatTypeInvalid;

	// If no data already, compute
	if (entry->type==EvalMatTypeInvalid) {
		entry->mat=mat;
		entry->type=evalComputeMatType(pos);
		entry->computed=false;
	}

	// Copy data to return it
	EvalMatType type=entry->type;

	// We are finished with Entry, release lock
	htableRelease(evalMatTable, hTableKey);

	return type;
}

const char *evalMatTypeStrs[EvalMatTypeNB]={[EvalMatTypeInvalid]="invalid", [EvalMatTypeOther]="other ", [EvalMatTypeDraw]="draw", [EvalMatTypeKNNvK]="KNNvK", [EvalMatTypeKPvK]="KPvK", [EvalMatTypeKBPvK]="KBPvK"};
const char *evalMatTypeToStr(EvalMatType matType) {
	assert(matType<EvalMatTypeNB);
	return evalMatTypeStrs[matType];
}

VPair evalComputePstScore(const Pos *pos) {
	VPair score=VPairZero;

	Colour colour;
	for(colour=0; colour<ColourNB; ++colour) {
		// Pawns are not included
		PieceType type;
		for(type=PieceTypeKnight;type<=PieceTypeKing;++type) {
			Piece piece=pieceMake(type, colour);
			const Sq *sq=posGetPieceListStart(pos, piece);
			const Sq *sqEnd=posGetPieceListEnd(pos, piece);
			for(;sq<sqEnd;++sq)
				evalVPairAddTo(&score, &evalPST[piece][*sq]);
		}
	}

	return score;
}

void evalVPairAddTo(VPair *a, const VPair *b) {
	a->mg+=b->mg;
	a->eg+=b->eg;
}

void evalVPairSubFrom(VPair *a, const VPair *b) {
	a->mg-=b->mg;
	a->eg-=b->eg;
}

void evalVPairAddMulTo(VPair *a, const VPair *b, int c) {
	a->mg+=b->mg*c;
	a->eg+=b->eg*c;
}

void evalVPairSubMulFrom(VPair *a, const VPair *b, int c) {
	a->mg-=b->mg*c;
	a->eg-=b->eg*c;
}

void evalVPairNegate(VPair *a) {
	a->mg=-a->mg;
	a->eg=-a->eg;
}

VPair evalVPairAdd(const VPair *a, const VPair *b) {
	VPair result=*a;
	evalVPairAddTo(&result, b);
	return result;
}

VPair evalVPairSub(const VPair *a, const VPair *b) {
	VPair result=*a;
	evalVPairSubFrom(&result, b);
	return result;
}

VPair evalVPairNegation(const VPair *a) {
	VPair result=*a;
	evalVPairNegate(&result);
	return result;
}

////////////////////////////////////////////////////////////////////////////////
// Private functions
////////////////////////////////////////////////////////////////////////////////

Score evaluateInternal(const Pos *pos) {
	// Init data struct.
	EvalData data={.pos=pos};

	// Evaluation function depends on material combination.
	evalGetMatData(pos, &data.matData);

	// Evaluate.
	VPair score;
	switch(data.matData.type) {
		case EvalMatTypeKPvK:
			score=evaluateKPvK(&data);
		break;
		default:
			score=evaluateDefault(&data);
		break;
	}

	// Material combination offset.
	evalVPairAddTo(&score, &data.matData.offset);

	// Tempo bonus.
	if (posGetSTM(pos)==ColourWhite)
		evalVPairAddTo(&score, &evalTempoDefault);
	else
		evalVPairSubFrom(&score, &evalTempoDefault);

	// Interpolate score based on phase of the game and special material combination considerations.
	Score scalarScore=evalInterpolate(&data, &score);

	// Drag score towards 0 as we approach 50-move rule
	unsigned int halfMoves=posGetHalfMoveNumber(data.pos);
	assert(halfMoves<128);
	scalarScore=(scalarScore*evalHalfMoveFactors[halfMoves])/256;

	// Add score offset
	scalarScore+=data.matData.scoreOffset;

	// Adjust for side to move
	if (posGetSTM(data.pos)==ColourBlack)
		scalarScore=-scalarScore;

	return scalarScore;
}

VPair evaluateDefault(EvalData *data) {
	const Pos *pos=data->pos;

	const Sq *sq, *sqEnd;

	// 'Global' calculations (includes pawns)
	VPair score=evaluateDefaultGlobal(data);

	// Bishop mobility
	sq=posGetPieceListStart(pos, PieceWBishopL);
	sqEnd=posGetPieceListEnd(pos, PieceWBishopL);
	for(;sq<sqEnd;++sq) {
		BB attacks=attacksBishop(*sq, posGetBBAll(pos));
		evalVPairAddMulTo(&score, &evalBishopMob, bbPopCount(attacks));
	}
	sq=posGetPieceListStart(pos, PieceWBishopD);
	sqEnd=posGetPieceListEnd(pos, PieceWBishopD);
	for(;sq<sqEnd;++sq) {
		BB attacks=attacksBishop(*sq, posGetBBAll(pos));
		evalVPairAddMulTo(&score, &evalBishopMob, bbPopCount(attacks));
	}
	sq=posGetPieceListStart(pos, PieceBBishopL);
	sqEnd=posGetPieceListEnd(pos, PieceBBishopL);
	for(;sq<sqEnd;++sq) {
		BB attacks=attacksBishop(*sq, posGetBBAll(pos));
		evalVPairSubMulFrom(&score, &evalBishopMob, bbPopCount(attacks));
	}
	sq=posGetPieceListStart(pos, PieceBBishopD);
	sqEnd=posGetPieceListEnd(pos, PieceBBishopD);
	for(;sq<sqEnd;++sq) {
		BB attacks=attacksBishop(*sq, posGetBBAll(pos));
		evalVPairSubMulFrom(&score, &evalBishopMob, bbPopCount(attacks));
	}

	// Rook mobilty
	sq=posGetPieceListStart(pos, PieceWRook);
	sqEnd=posGetPieceListEnd(pos, PieceWRook);
	for(;sq<sqEnd;++sq) {
		BB attacks=attacksRook(*sq, posGetBBAll(pos));
		evalVPairAddMulTo(&score, &evalRookMobFile, bbPopCount(attacks & bbFile(sqFile(*sq))));
		evalVPairAddMulTo(&score, &evalRookMobRank, bbPopCount(attacks & bbRank(sqRank(*sq))));
	}
	sq=posGetPieceListStart(pos, PieceBRook);
	sqEnd=posGetPieceListEnd(pos, PieceBRook);
	for(;sq<sqEnd;++sq) {
		BB attacks=attacksRook(*sq, posGetBBAll(pos));
		evalVPairSubMulFrom(&score, &evalRookMobFile, bbPopCount(attacks & bbFile(sqFile(*sq))));
		evalVPairSubMulFrom(&score, &evalRookMobRank, bbPopCount(attacks & bbRank(sqRank(*sq))));
	}

	// Kings
	VPair kingScoreWhite=evaluateDefaultKing(data, ColourWhite);
	evalVPairAddTo(&score, &kingScoreWhite);

	VPair kingScoreBlack=evaluateDefaultKing(data, ColourBlack);
	evalVPairSubFrom(&score, &kingScoreBlack);

	return score;
}

VPair evaluateKPvK(EvalData *data) {
	// Use tablebase to find exact result.
	BitBaseResult result=bitbaseProbe(data->pos);
	switch(result) {
		case BitBaseResultDraw:
			data->matData.offset=VPairZero;
			data->matData.scoreOffset=0;
			return VPairZero;
		break;
		case BitBaseResultWin: {
			Colour attacker=(posGetBBPiece(data->pos, PieceWPawn)!=BBNone ? ColourWhite : ColourBlack);
			data->matData.scoreOffset+=(attacker==ColourWhite ? ScoreHardWin : -ScoreHardWin);
			return evaluateDefault(data);
		} break;
	}

	assert(false);
	return VPairZero;
}

void evalGetMatData(const Pos *pos, EvalMatData *matData) {
	// Grab hash entry for this position key.
	HTableKey hTableKey=evalGetMatDataHTableKeyFromPos(pos);
	EvalMatData *entry=htableGrab(evalMatTable, hTableKey);

	// If not a match clear entry
	MatInfo mat=posGetMatInfo(pos);
	if (entry->mat!=mat)
		entry->type=EvalMatTypeInvalid;

	// If no type info, compute first.
	if (entry->type==EvalMatTypeInvalid) {
		entry->mat=mat;
		entry->type=evalComputeMatType(pos);
		entry->computed=false;
	}

	// If no data already, compute.
	if (!entry->computed)
		evalComputeMatData(pos, entry);

	// Copy data to return it.
	*matData=*entry;

	// We are finished with entry, release lock.
	htableRelease(evalMatTable, hTableKey);
}

void evalComputeMatData(const Pos *pos, EvalMatData *matData) {
#	define M(P,N) (matInfoMake((P),(N)))
#	define G(P) (matInfoGetPieceCount(mat,(P))) // Hard-coded 'mat'.

	// Init data.
	assert(matData->mat==posGetMatInfo(pos));
	assert(matData->type!=EvalMatTypeInvalid);
	matData->computed=true;
	matData->offset=VPairZero;
	matData->scoreOffset=0;
	MatInfo mat=(matData->mat & ~matInfoMakeMaskPieceType(PieceTypeKing)); // Remove kings as these as always present.
	bool wBishopL=((mat & matInfoMakeMaskPiece(PieceWBishopL))!=0);
	bool bBishopL=((mat & matInfoMakeMaskPiece(PieceBBishopL))!=0);
	bool wBishopD=((mat & matInfoMakeMaskPiece(PieceWBishopD))!=0);
	bool bBishopD=((mat & matInfoMakeMaskPiece(PieceBBishopD))!=0);

	// Find weights for middlegame and endgame.
	unsigned int whiteBishopCount=G(PieceWBishopL)+G(PieceWBishopD);
	unsigned int blackBishopCount=G(PieceBBishopL)+G(PieceBBishopD);
	unsigned int minorCount=G(PieceWKnight)+G(PieceBKnight)+whiteBishopCount+blackBishopCount;
	unsigned int rookCount=G(PieceWRook)+G(PieceBRook);
	unsigned int queenCount=G(PieceWQueen)+G(PieceBQueen);
	unsigned int pieceWeight=minorCount+2*rookCount+4*queenCount;
	assert(pieceWeight<128);
	matData->weightEG=evalWeightEGFactors[pieceWeight];
	matData->weightMG=256-matData->weightEG;

	// Specific material combinations.
	unsigned int factor=1024;
	switch(matData->type) {
		case EvalMatTypeInvalid:
			assert(false);
		break;
		case EvalMatTypeOther:
			assert(mat); // KvK should already be handled.
			const MatInfo matPawns=matInfoMakeMaskPieceType(PieceTypePawn);
			const MatInfo matWhite=matInfoMakeMaskColour(ColourWhite);
			const MatInfo matBlack=matInfoMakeMaskColour(ColourBlack);

			if (!(mat & matPawns)) {
				// Pawnless.
				if ((mat & MatInfoMaskMinors)==mat) {
					// Minors only.
					switch(minorCount) {
						case 0: case 1:
							assert(false); // Should have already been handled.
						break;
						case 2:
							// Don't need to consider bishops of a single colour as these are
							// evaluated as insufficient material draws.
							assert(mat!=(M(PieceWBishopL,1)|M(PieceBBishopL,1)) && // KBvKB (same coloured bishops)
							       mat!=(M(PieceWBishopD,1)|M(PieceBBishopD,1)) &&
							       mat!=M(PieceWBishopL,2) && mat!=M(PieceWBishopD,2) && // KBBvK (same coloured bishops).
							       mat!=M(PieceBBishopL,2) && mat!=M(PieceBBishopD,2));
							// Nor do we need to consider KNNvK as this is handled in other case statement.
							assert(mat!=M(PieceWKnight,2) && mat!=M(PieceBKnight,2)); // KNNvK.

							// Win for bishop pair and bishop + knight, draw for everything else.
							if (mat==(M(PieceWBishopL,1)|M(PieceWBishopD,1)) || // KBBvK.
							    mat==(M(PieceBBishopL,1)|M(PieceBBishopD,1)) ||
							    mat==(M(PieceWBishopL,1)|M(PieceWKnight,1)) || // KBNvK.
							    mat==(M(PieceWBishopD,1)|M(PieceWKnight,1)) ||
							    mat==(M(PieceBBishopL,1)|M(PieceBKnight,1)) ||
							    mat==(M(PieceBBishopD,1)|M(PieceBKnight,1)))
								factor/=2; // More difficult than material advantage suggests.
							else {
								assert(mat==(M(PieceWBishopL,1)|M(PieceBKnight,1)) || // KBvKN.
								       mat==(M(PieceWBishopD,1)|M(PieceBKnight,1)) ||
								       mat==(M(PieceBBishopL,1)|M(PieceWKnight,1)) ||
								       mat==(M(PieceBBishopD,1)|M(PieceWKnight,1)) ||
								       mat==(M(PieceWBishopL,1)|M(PieceBBishopD,1)) || // KBvKB (opposite bishops)
								       mat==(M(PieceWBishopD,1)|M(PieceBBishopL,1)) ||
								       mat==MatInfoMaskKNvKN); // KNvKN.
								factor/=128; // All others are trivial draws.
							}
						break;
						case 3:
							if (mat==(M(PieceWBishopL,1)|M(PieceWBishopD,1)|M(PieceWKnight,1)) || // KBBvKN (bishop pair).
							    mat==(M(PieceBBishopL,1)|M(PieceBBishopD,1)|M(PieceBKnight,1)))
								factor/=2;
							else if (mat==(M(PieceWKnight,1)|M(PieceWBishopL,1)|M(PieceBBishopL,1)) || // KBNvKB (same coloured bishops).
							         mat==(M(PieceWKnight,1)|M(PieceWBishopD,1)|M(PieceBBishopD,1)) ||
							         mat==(M(PieceBKnight,1)|M(PieceBBishopL,1)|M(PieceWBishopL,1)) ||
							         mat==(M(PieceBKnight,1)|M(PieceBBishopD,1)|M(PieceWBishopD,1)) ||
							         mat==(M(PieceWKnight,1)|M(PieceWBishopD,1)|M(PieceBBishopL,1)) || // KBNvKB (opposite coloured bishops).
							         mat==(M(PieceWKnight,1)|M(PieceWBishopL,1)|M(PieceBBishopD,1)) ||
							         mat==(M(PieceBKnight,1)|M(PieceBBishopD,1)|M(PieceWBishopL,1)) ||
							         mat==(M(PieceBKnight,1)|M(PieceBBishopL,1)|M(PieceWBishopD,1)) ||
							         mat==(M(PieceWKnight,1)|M(PieceWBishopL,1)|M(PieceBKnight,1)) || // KBNvN.
							         mat==(M(PieceWKnight,1)|M(PieceWBishopD,1)|M(PieceBKnight,1)) ||
							         mat==(M(PieceBKnight,1)|M(PieceBBishopL,1)|M(PieceWKnight,1)) ||
							         mat==(M(PieceBKnight,1)|M(PieceBBishopD,1)|M(PieceWKnight,1)))
								factor/=16;
							else if (mat==(M(PieceWKnight,2)|M(PieceBKnight,1)) || // KNNvKN.
							         mat==(M(PieceBKnight,2)|M(PieceWKnight,1)) ||
							         mat==(M(PieceWKnight,2)|M(PieceBBishopL,1)) || // KNNvKB.
							         mat==(M(PieceWKnight,2)|M(PieceBBishopD,1)) ||
							         mat==(M(PieceBKnight,2)|M(PieceWBishopL,1)) ||
							         mat==(M(PieceBKnight,2)|M(PieceWBishopD,1)) ||
							         mat==(M(PieceWBishopL,1)|M(PieceWBishopD,1)|M(PieceBBishopL,1)) || // KBBvKB (bishop pair).
							         mat==(M(PieceWBishopL,1)|M(PieceWBishopD,1)|M(PieceBBishopD,1)) ||
							         mat==(M(PieceBBishopL,1)|M(PieceBBishopD,1)|M(PieceWBishopL,1)) ||
							         mat==(M(PieceBBishopL,1)|M(PieceBBishopD,1)|M(PieceWBishopD,1)) ||
							         mat==(M(PieceWBishopL,2)|M(PieceBBishopD,1)) ||  // KBBvKB (no bishop pair).
							         mat==(M(PieceWBishopD,2)|M(PieceBBishopL,1)) ||
							         mat==(M(PieceBBishopL,2)|M(PieceWBishopD,1)) ||
							         mat==(M(PieceBBishopD,2)|M(PieceWBishopL,1)) ||
							         mat==(M(PieceWBishopL,2)|M(PieceBKnight,1)) || // KBBvN (no bishop pair).
							         mat==(M(PieceWBishopD,2)|M(PieceBKnight,1)) ||
							         mat==(M(PieceBBishopL,2)|M(PieceWKnight,1)) ||
							         mat==(M(PieceBBishopD,2)|M(PieceWKnight,1)))
								factor/=32;
						break;
					}
				} else if ((mat & MatInfoMaskMajors)==mat) {
					// Majors only.

					// Single side with material should be easy win (at least a rook ahead).
					if ((mat & matWhite)==mat)
						matData->scoreOffset+=ScoreEasyWin;
					else if ((mat & matBlack)==mat)
						matData->scoreOffset-=ScoreEasyWin;
					else if (mat==(M(PieceWQueen,1)|M(PieceBRook,1))|| // KQvKR.
					         mat==(M(PieceBQueen,1)|M(PieceWRook,1)))
						factor/=2;
				} else {
					// Mix of major and minor pieces.
					switch(minorCount+rookCount+queenCount) {
						case 0: case 1:
							assert(false); // KvK already handled and single piece cannot be both minor and major.
						break;
						case 2:
							if (mat==(M(PieceWRook,1)|M(PieceBBishopL,1)) || // KRvKB.
							    mat==(M(PieceBRook,1)|M(PieceWBishopL,1)) ||
							    mat==(M(PieceWRook,1)|M(PieceBBishopD,1)) ||
							    mat==(M(PieceBRook,1)|M(PieceWBishopD,1)) ||
							    mat==(M(PieceWRook,1)|M(PieceBKnight,1)) || // KRvKN.
							    mat==(M(PieceBRook,1)|M(PieceWKnight,1)))
								factor/=4;
						break;
						case 3:
							if (mat==(M(PieceWQueen,1)|M(PieceWBishopL,1)|M(PieceBQueen,1)) || // KQBvKQ.
							    mat==(M(PieceWQueen,1)|M(PieceWBishopD,1)|M(PieceBQueen,1)) ||
							    mat==(M(PieceBQueen,1)|M(PieceBBishopL,1)|M(PieceWQueen,1)) ||
							    mat==(M(PieceBQueen,1)|M(PieceBBishopD,1)|M(PieceWQueen,1)) ||
							    mat==(M(PieceWQueen,1)|M(PieceBKnight,2)) || // KQvKNN.
							    mat==(M(PieceBQueen,1)|M(PieceWKnight,2)))
								factor/=8;
							else if (mat==(M(PieceWQueen,1)|M(PieceWKnight,1)|M(PieceBQueen,1)) || // KQNvKQ.
							         mat==(M(PieceBQueen,1)|M(PieceBKnight,1)|M(PieceWQueen,1)) ||
							         mat==(M(PieceWRook,1)|M(PieceWKnight,1)|M(PieceBRook,1)) || // KRNvKR.
							         mat==(M(PieceBRook,1)|M(PieceBKnight,1)|M(PieceWRook,1)) ||
							         mat==(M(PieceWBishopL,1)|M(PieceWBishopD,1)|M(PieceBQueen,1)) || // KQvKBB. (bishop pair)
							         mat==(M(PieceBBishopL,1)|M(PieceBBishopD,1)|M(PieceWQueen,1)) ||
							         mat==(M(PieceWQueen,1)|M(PieceBRook,1)|M(PieceBBishopL,1)) || // KQvKRB.
							         mat==(M(PieceWQueen,1)|M(PieceBRook,1)|M(PieceBBishopD,1)) ||
							         mat==(M(PieceBQueen,1)|M(PieceWRook,1)|M(PieceWBishopL,1)) ||
							         mat==(M(PieceBQueen,1)|M(PieceWRook,1)|M(PieceWBishopD,1)) ||
							         mat==(M(PieceWQueen,1)|M(PieceBRook,1)|M(PieceBKnight,1)) || // KQvKRN.
							         mat==(M(PieceBQueen,1)|M(PieceWRook,1)|M(PieceWKnight,1)) ||
							         mat==(M(PieceWBishopL,1)|M(PieceWBishopD,1)|M(PieceBRook,1)) || // KRvKBB (bishop pair).
							         mat==(M(PieceBBishopL,1)|M(PieceBBishopD,1)|M(PieceWRook,1)) ||
							         mat==(M(PieceWRook,1)|M(PieceWBishopL,1)|M(PieceBRook,1)) || // KRBvKR.
							         mat==(M(PieceWRook,1)|M(PieceWBishopD,1)|M(PieceBRook,1)) ||
							         mat==(M(PieceBRook,1)|M(PieceBBishopL,1)|M(PieceWRook,1)) ||
							         mat==(M(PieceBRook,1)|M(PieceBBishopD,1)|M(PieceWRook,1)))
								factor/=4;
						break;
					}
				}
			}
		break;
		case EvalMatTypeDraw:
			factor=0;
		break;
		case EvalMatTypeKNNvK:
			factor/=128;
		break;
		case EvalMatTypeKPvK:
		break;
		case EvalMatTypeKBPvK:
		break;
		case EvalMatTypeNB: // To appease the compiler.
			assert(false);
		break;
	}
	matData->weightMG=(matData->weightMG*factor)/1024;
	matData->weightEG=(matData->weightEG*factor)/1024;

	// Opposite coloured bishop endgames are drawish.
	if ((wBishopL^wBishopD) && (bBishopL^bBishopD) && (wBishopL^bBishopL)) {
		matData->weightMG=(matData->weightMG*evalOppositeBishopFactor.mg)/256;
		matData->weightEG=(matData->weightEG*evalOppositeBishopFactor.eg)/256;
	}

	// Knight pawn affinity.
	unsigned int knightAffW=G(PieceWKnight)*G(PieceWPawn);
	unsigned int knightAffB=G(PieceBKnight)*G(PieceBPawn);
	evalVPairAddMulTo(&matData->offset, &evalKnightPawnAffinity, knightAffW-knightAffB);

	// Rook pawn affinity.
	unsigned int rookAffW=G(PieceWRook)*G(PieceWPawn);
	unsigned int rookAffB=G(PieceBRook)*G(PieceBPawn);
	evalVPairAddMulTo(&matData->offset, &evalRookPawnAffinity, rookAffW-rookAffB);

	// Bishop pair bonus
	if (wBishopL && wBishopD)
		evalVPairAddTo(&matData->offset, &evalBishopPair);
	if (bBishopL && bBishopD)
		evalVPairSubFrom(&matData->offset, &evalBishopPair);

#	undef G
#	undef M
}

HTableKey evalGetMatDataHTableKeyFromPos(const Pos *pos) {
	assert(pos!=NULL);

	STATICASSERT(HTableKeySize==32);
	return posGetMatKey(pos)&0xFFFFFFFFu; // Use lower 32 bits
}

void evalGetPawnData(const Pos *pos, EvalPawnData *pawnData) {
	// Grab hash entry for this position key.
	HTableKey hTableKey=evalGetPawnDataHTableKeyFromPos(pos);
	EvalPawnData *entry=htableGrab(evalPawnTable, hTableKey);

	// If not a match recompute data.
	if (entry->pawns[ColourWhite]!=posGetBBPiece(pos, PieceWPawn) ||
	    entry->pawns[ColourBlack]!=posGetBBPiece(pos, PieceBPawn))
		evalComputePawnData(pos, entry);

	// Copy data to return it.
	*pawnData=*entry;

	// We are finished with Entry, release lock.
	htableRelease(evalPawnTable, hTableKey);

	// Compute terms which depend on other (non-pawn) aspects of the position, hence cannot be hashed.
	BB occ=posGetBBAll(pos);
	BB blocked[ColourNB];
	blocked[ColourWhite]=(pawnData->pawns[ColourWhite] & bbSouthOne(occ));
	blocked[ColourBlack]=(pawnData->pawns[ColourBlack] & bbNorthOne(occ));
	evalVPairAddMulTo(&pawnData->score, &evalPawnBlocked, bbPopCount(blocked[ColourWhite])-bbPopCount(blocked[ColourBlack]));
}

void evalComputePawnData(const Pos *pos, EvalPawnData *pawnData) {
	// Init.
	pawnData->score=VPairZero;
	BB pawns[ColourNB], frontSpan[ColourNB], rearSpan[ColourNB], attacks[ColourNB];
	BB attacksFill[ColourNB], doubled[ColourNB], isolated[ColourNB];
	BB influence[ColourNB], fill[ColourNB];
	pawns[ColourWhite]=pawnData->pawns[ColourWhite]=posGetBBPiece(pos, PieceWPawn);
	pawns[ColourBlack]=pawnData->pawns[ColourBlack]=posGetBBPiece(pos, PieceBPawn);
	frontSpan[ColourWhite]=bbNorthOne(bbNorthFill(pawns[ColourWhite])); // All squares infront of pawns of given colour.
	frontSpan[ColourBlack]=bbSouthOne(bbSouthFill(pawns[ColourBlack]));
	rearSpan[ColourWhite]=bbSouthOne(bbSouthFill(pawns[ColourWhite])); // All squares behind pawns of given colour.
	rearSpan[ColourBlack]=bbNorthOne(bbNorthFill(pawns[ColourBlack]));
	attacks[ColourWhite]=bbNorthOne(bbWingify(pawns[ColourWhite]));
	attacks[ColourBlack]=bbSouthOne(bbWingify(pawns[ColourBlack]));
	attacksFill[ColourWhite]=bbFileFill(attacks[ColourWhite]);
	attacksFill[ColourBlack]=bbFileFill(attacks[ColourBlack]);
	doubled[ColourWhite]=(pawns[ColourWhite] & rearSpan[ColourWhite]);
	doubled[ColourBlack]=(pawns[ColourBlack] & rearSpan[ColourBlack]);
	isolated[ColourWhite]=(pawns[ColourWhite] & ~attacksFill[ColourWhite]);
	isolated[ColourBlack]=(pawns[ColourBlack] & ~attacksFill[ColourBlack]);
	influence[ColourWhite]=(frontSpan[ColourWhite] | bbWingify(frontSpan[ColourWhite])); // Squares which colour in question may attack or move to, now or in the future.
	influence[ColourBlack]=(frontSpan[ColourBlack] | bbWingify(frontSpan[ColourBlack]));
	pawnData->passed[ColourWhite]=(pawns[ColourWhite] & ~(doubled[ColourWhite] | influence[ColourBlack]));
	pawnData->passed[ColourBlack]=(pawns[ColourBlack] & ~(doubled[ColourBlack] | influence[ColourWhite]));
	fill[ColourWhite]=bbFileFill(pawns[ColourWhite]);
	fill[ColourBlack]=bbFileFill(pawns[ColourBlack]);
	pawnData->semiOpenFiles[ColourWhite]=(fill[ColourBlack] & ~fill[ColourWhite]);
	pawnData->semiOpenFiles[ColourBlack]=(fill[ColourWhite] & ~fill[ColourBlack]);
	pawnData->openFiles=~(fill[ColourWhite] | fill[ColourBlack]);

	// Loop over each pawn.
	Colour colour;
	for(colour=ColourWhite;colour<=ColourBlack;++colour) {
		const Sq *sq=posGetPieceListStart(pos, pieceMake(PieceTypePawn, colour));
		const Sq *sqEnd=posGetPieceListEnd(pos, pieceMake(PieceTypePawn, colour));
		for(;sq<sqEnd;++sq) {
			PawnType type=((((doubled[colour]>>*sq)&1)<<PawnTypeShiftDoubled) |
			               (((isolated[colour]>>*sq)&1)<<PawnTypeShiftIsolated) |
			               (((pawnData->passed[colour]>>*sq)&1)<<PawnTypeShiftPassed));
			assert(type>=0 && type<PawnTypeNB);
			evalVPairAddTo(&pawnData->score, &evalPawnValue[colour][type][*sq]);
		}
	}
}

HTableKey evalGetPawnDataHTableKeyFromPos(const Pos *pos) {
	assert(pos!=NULL);

	STATICASSERT(HTableKeySize==32);
	return posGetPawnKey(pos)&0xFFFFFFFFu;
}

VPair evaluateDefaultGlobal(EvalData *data) {
	assert(data!=NULL);

	const Pos *pos=data->pos;

	// Start with incrementally updated PST score.
	VPair score=posGetPstScore(pos);

	// Pawns
	evalGetPawnData(pos, &data->pawnData);
	evalVPairAddTo(&score, &data->pawnData.score);

	// Rook stuff
	for(Colour colour=ColourWhite; colour<=ColourBlack; ++colour,evalVPairNegate(&score)) {
		BB rooks=posGetBBPiece(pos, pieceMake(PieceTypeRook, colour));
		if (rooks==BBNone)
			continue;

		// Rooks on open and semi-open files.
		evalVPairAddMulTo(&score, &evalRookOpenFile, bbPopCount(rooks & data->pawnData.openFiles));
		evalVPairAddMulTo(&score, &evalRookSemiOpenFile, bbPopCount(rooks & data->pawnData.semiOpenFiles[colour]));

		// Any rooks on 7th rank?
		BB rank7=bbRank(colour==ColourWhite ? Rank7 : Rank2);
		BB oppPawns=posGetBBPiece(pos, pieceMake(PieceTypePawn, colourSwap(colour)));
		if ((oppPawns & rank7) || sqRank(sqNormalise(posGetKingSq(pos, colourSwap(colour)), colour))==Rank8)
			evalVPairAddMulTo(&score, &evalRookOn7th, bbPopCount(rooks & rank7));

		// Any rooks trapped on edge of back rank by own king?
		BB kingBB=posGetBBPiece(pos, pieceMake(PieceTypeKing, colour));
		if (colour==ColourWhite) {
			if (((rooks & (bbSq(SqG1) | bbSq(SqH1))) && (kingBB & (bbSq(SqF1) | bbSq(SqG1)))) ||
			    ((rooks & (bbSq(SqA1) | bbSq(SqB1))) && (kingBB & (bbSq(SqB1) | bbSq(SqC1)))))
				evalVPairAddTo(&score, &evalRookTrapped);
		} else {
			if (((rooks & (bbSq(SqG8) | bbSq(SqH8))) && (kingBB & (bbSq(SqF8) | bbSq(SqG8)))) ||
			    ((rooks & (bbSq(SqA8) | bbSq(SqB8))) && (kingBB & (bbSq(SqB8) | bbSq(SqC8)))))
				evalVPairAddTo(&score, &evalRookTrapped);
		}
	}

	// King castling 'mobility'.
	CastRights castRights=posGetCastRights(pos);
	evalVPairAddTo(&score, &evalKingCastlingMobility[castRights]);

	return score;
}

VPair evaluateDefaultKing(EvalData *data, Colour colour) {
	assert(data!=NULL);

	Sq kingSq=posGetKingSq(data->pos, colour);
	BB kingBB=bbSq(kingSq);

	VPair score=VPairZero;

	// Pawn shield.
	BB pawns=posGetBBPiece(data->pos, pieceMake(PieceTypePawn, colour));
	BB kingSpan=bbForwardOne((bbWestOne(kingBB) | kingBB | bbEastOne(kingBB)), colour);

	BB shieldClose=(pawns & kingSpan);
	evalVPairAddMulTo(&score, &evalKingShieldClose, bbPopCount(shieldClose));

	BB shieldFar=(pawns & bbForwardOne(kingSpan, colour));
	evalVPairAddMulTo(&score, &evalKingShieldFar, bbPopCount(shieldFar));

	// Distance to enemy passed pawns
	BB oppPassers=data->pawnData.passed[colourSwap(colour)];
	while(oppPassers) {
		Sq passerSq=bbScanReset(&oppPassers);
		unsigned distance=sqDist(kingSq, passerSq);
		assert(distance>=1 && distance<=7);
		evalVPairAdd(&score, &evalKingNearPasser[distance]);
	}

	return score;
}

Score evalInterpolate(const EvalData *data, const VPair *score) {
	// Interpolate and also scale to centi-pawns
	return ((data->matData.weightMG*score->mg+data->matData.weightEG*score->eg)*100)/(evalMaterial[PieceTypePawn].mg*256);
}

void evalVPairAddVPair16To(VPair *a, const VPair16 *b) {
	a->mg+=b->mg;
	a->eg+=b->eg;
}

void evalVPairSubVPair16From(VPair *a, const VPair16 *b) {
	a->mg-=b->mg;
	a->eg-=b->eg;
}

#ifdef TUNE
void evalSetValue(void *varPtr, int value) {
	// Set value.
	Value *var=(Value *)varPtr;
	*var=value;

	// Hack for bishops.
	if (var==&evalMaterial[PieceTypeBishopL].mg)
		evalMaterial[PieceTypeBishopD].mg=value;
	else if (var==&evalMaterial[PieceTypeBishopL].eg)
		evalMaterial[PieceTypeBishopD].eg=value;

	// Hack to reflect pawn files.
	File file;
	for(file=FileA;file<=FileD;++file) {
		if (var==&evalPawnFiles[file].mg)
			evalPawnFiles[fileMirror(file)].mg=value;
		else if (var==&evalPawnFiles[file].eg)
			evalPawnFiles[fileMirror(file)].eg=value;
	}

	// Recalculate dervied values (such as passed pawn table).
	evalRecalc();
}

bool evalOptionNewVPair(const char *name, VPair *score) {
	// Allocate string to hold name with 'MG'/'EG' appended
	size_t nameLen=strlen(name);
	char *fullName=malloc(nameLen+2+1);
	if (fullName==NULL)
		return false;

	// Add option for each of mg/eg
	bool success=true;
	const Value min=-32767, max=32767;
	sprintf(fullName, "%sMG", name);
	success&=uciOptionNewSpin(fullName, &evalSetValue, &score->mg, min, max, score->mg);
	sprintf(fullName, "%sEG", name);
	success&=uciOptionNewSpin(fullName, &evalSetValue, &score->eg, min, max, score->eg);

	free(fullName);
	return success;
}

void evalSetValue16(void *varPtr, int value) {
	// Set value.
	Value16 *var=(Value16 *)varPtr;
	*var=value;

	// Recalculate dervied values (such as passed pawn table).
	evalRecalc();
}

bool evalOptionNewVPair16(const char *name, VPair16 *score) {
	// Allocate string to hold name with 'MG'/'EG' appended
	size_t nameLen=strlen(name);
	char *fullName=malloc(nameLen+2+1);
	if (fullName==NULL)
		return false;

	// Add option for each of mg/eg
	bool success=true;
	const Value16 min=INT16_MIN, max=INT16_MAX;
	sprintf(fullName, "%sMG", name);
	success&=uciOptionNewSpin(fullName, &evalSetValue16, &score->mg, min, max, score->mg);
	sprintf(fullName, "%sEG", name);
	success&=uciOptionNewSpin(fullName, &evalSetValue16, &score->eg, min, max, score->eg);

	free(fullName);
	return success;
}
#endif

void evalRecalc(void) {
	PieceType pieceType;

	// White pawn PST.
	Sq sq;
	const BB centre=(bbSq(SqD4)|bbSq(SqE4)|
	                 bbSq(SqD5)|bbSq(SqE5));
	const BB outerCentre=(bbSq(SqC3)|bbSq(SqD3)|bbSq(SqE3)|bbSq(SqF3)|
	                      bbSq(SqC4)|                      bbSq(SqF4)|
	                      bbSq(SqC5)|                      bbSq(SqF5)|
	                      bbSq(SqC6)|bbSq(SqD6)|bbSq(SqE6)|bbSq(SqF6));
	VPair whitePawnPst[SqNB];
	for(sq=0;sq<SqNB;++sq) {
		whitePawnPst[sq]=VPairZero;
		if (sqRank(sq)==Rank1 || sqRank(sq)==Rank8)
			continue;
		BB bb=bbSq(sq);
		evalVPairAddTo(&whitePawnPst[sq], &evalMaterial[PieceTypePawn]);
		evalVPairAddTo(&whitePawnPst[sq], &evalPawnFiles[sqFile(sq)]);
		evalVPairAddTo(&whitePawnPst[sq], &evalPawnRanks[sqRank(sq)]);
		if (bb & centre)
			evalVPairAddTo(&whitePawnPst[sq], &evalPawnCentre);
		else if (bb & outerCentre)
			evalVPairAddTo(&whitePawnPst[sq], &evalPawnOuterCentre);
	}

	// Pawn table.
	PawnType type;
	for(type=0;type<PawnTypeNB;++type) {
		bool isDoubled=((type & PawnTypeDoubled)!=0);
		bool isIsolated=((type & PawnTypeIsolated)!=0);
		bool isPassed=((type & PawnTypePassed)!=0);
		for(sq=0;sq<SqNB;++sq) {
			// Calculate score for white.
			VPair *score=&evalPawnValue[ColourWhite][type][sq];
			*score=VPairZero;
			evalVPairAddTo(score, &whitePawnPst[sq]);
			if (isDoubled)
				evalVPairAddTo(score, &evalPawnDoubled);
			if (isIsolated)
				evalVPairAddTo(score, &evalPawnIsolated);
			if (isPassed) {
				// Generate passed pawn score from quadratic coefficients.
				Rank rank=sqRank(sq);
				evalVPairAddMulTo(score, &evalPawnPassedQuadA, rank*rank);
				evalVPairAddMulTo(score, &evalPawnPassedQuadB, rank);
				evalVPairAddTo(score, &evalPawnPassedQuadC);
			}

			// Flip square and negate score for black.
			evalPawnValue[ColourBlack][type][sqFlip(sq)]=VPairZero;
			evalVPairSubFrom(&evalPawnValue[ColourBlack][type][sqFlip(sq)], score);
		}
	}

	// Add material to white PSTs
	for(pieceType=PieceTypeKnight; pieceType<=PieceTypeQueen; ++pieceType) {
		Piece piece=pieceMake(pieceType, ColourWhite);
		Sq sq;
		for(sq=0; sq<SqNB; ++sq)
			evalVPairAddTo(&evalPST[piece][sq], &evalMaterial[pieceType]);
	}

	// Copy white PSTs into black.
	for(pieceType=PieceTypePawn; pieceType<=PieceTypeKing; ++pieceType) {
		Piece whitePiece=pieceMake(pieceType, ColourWhite);
		Piece blackPiece=pieceMake(pieceType, ColourBlack);
		Sq blackSq;
		for(blackSq=0; blackSq<SqNB; ++blackSq) {
			evalPST[blackPiece][blackSq]=evalPST[whitePiece][sqFlip(blackSq)];
			evalVPairNegate(&evalPST[blackPiece][blackSq]);
		}
	}

	// King near passer table
	int dist;
	for(dist=1; dist<=7; ++dist) {
		double normDist=(7-dist)/6.0;
		assert(normDist>=0.0 && normDist<=1.0);
		double normDist2=pow(normDist, 2.0);

		double scoreMg=normDist2*evalKingNearPasserFactor.mg;
		double scoreEg=normDist2*evalKingNearPasserFactor.eg;
		evalKingNearPasser[dist].mg=floor(scoreMg);
		evalKingNearPasser[dist].eg=floor(scoreEg);
	}

	// Calculate factor for number of half moves since capture/pawn move.
	unsigned int i;
	for(i=0;i<128;++i) {
		float factor=exp2f(-((float)(i*i)/((float)evalHalfMoveFactor)));
		assert(factor>=0.0 && factor<=1.0);
		evalHalfMoveFactors[i]=floorf(255.0*factor);
	}

	// Calculate factor for each material weight.
	for(i=0;i<128;++i) {
		float factor=exp2f(-(float)(i*i)/((float)evalWeightFactor));
		assert(factor>=0.0 && factor<=1.0);
		evalWeightEGFactors[i]=floorf(255.0*factor);
	}

	// Castling mobility.
	evalKingCastlingMobility[CastRightsNone]=VPairZero; // (balanced)
	evalKingCastlingMobility[CastRightsq]=evalVPairNegation(&evalKingCastlingMobilityQ);
	evalKingCastlingMobility[CastRightsk]=evalVPairNegation(&evalKingCastlingMobilityK);
	evalKingCastlingMobility[CastRightsQ]=evalKingCastlingMobilityQ;
	evalKingCastlingMobility[CastRightsK]=evalKingCastlingMobilityK;
	evalKingCastlingMobility[CastRightsKQ]=evalKingCastlingMobilityKQ;
	evalKingCastlingMobility[CastRightsKk]=VPairZero; // (balanced)
	evalKingCastlingMobility[CastRightsKq]=evalVPairSub(&evalKingCastlingMobilityK, &evalKingCastlingMobilityQ);
	evalKingCastlingMobility[CastRightsQk]=evalVPairSub(&evalKingCastlingMobilityQ, &evalKingCastlingMobilityK);
	evalKingCastlingMobility[CastRightsQq]=VPairZero; // (balanced)
	evalKingCastlingMobility[CastRightskq]=evalVPairNegation(&evalKingCastlingMobilityKQ);
	evalKingCastlingMobility[CastRightsKQk]=evalVPairSub(&evalKingCastlingMobilityKQ, &evalKingCastlingMobilityK);
	evalKingCastlingMobility[CastRightsKQq]=evalVPairSub(&evalKingCastlingMobilityKQ, &evalKingCastlingMobilityQ);
	evalKingCastlingMobility[CastRightsKkq]=evalVPairSub(&evalKingCastlingMobilityK, &evalKingCastlingMobilityKQ);
	evalKingCastlingMobility[CastRightsQkq]=evalVPairSub(&evalKingCastlingMobilityQ, &evalKingCastlingMobilityKQ);
	evalKingCastlingMobility[CastRightsKQkq]=VPairZero; // (balanced)

	// Clear now-invalid material and pawn tables etc.
	evalClear();

	// Verify eval weights are all sensible and consistent.
	evalVerify();
}

void evalVerify(void) {
	// Check evalPawnFiles is symmetrical.
	File file;
	for(file=0; file<FileNB; ++file) {
		assert(evalPawnFiles[file].mg==evalPawnFiles[fileMirror(file)].mg);
		assert(evalPawnFiles[file].eg==evalPawnFiles[fileMirror(file)].eg);
	}

	// Check PSTs are symmetrical.
	PieceType pieceType;
	Sq sq;
	for(pieceType=PieceTypePawn; pieceType<=PieceTypeKing; ++pieceType)
		for(sq=0; sq<SqNB; ++sq) {
			assert(evalPST[pieceMake(pieceType, ColourWhite)][sq].mg==evalPST[pieceMake(pieceType, ColourWhite)][sqMirror(sq)].mg);
			assert(evalPST[pieceMake(pieceType, ColourWhite)][sq].eg==evalPST[pieceMake(pieceType, ColourWhite)][sqMirror(sq)].eg);

			assert(evalPST[pieceMake(pieceType, ColourBlack)][sq].mg==evalPST[pieceMake(pieceType, ColourBlack)][sqMirror(sq)].mg);
			assert(evalPST[pieceMake(pieceType, ColourBlack)][sq].eg==evalPST[pieceMake(pieceType, ColourBlack)][sqMirror(sq)].eg);

			assert(evalPST[pieceMake(pieceType, ColourBlack)][sq].mg==-evalPST[pieceMake(pieceType, ColourWhite)][sqFlip(sq)].mg);
			assert(evalPST[pieceMake(pieceType, ColourBlack)][sq].eg==-evalPST[pieceMake(pieceType, ColourWhite)][sqFlip(sq)].eg);
		}
}

EvalMatType evalComputeMatType(const Pos *pos) {
#	define MAKE(p,n) matInfoMake((p),(n))
#	define MASK(t) matInfoMakeMaskPieceType(t)

	// Grab material info.
	MatInfo mat=posGetMatInfo(pos);

	// If only pieces are bishops and all share same colour squares, draw.
	if ((mat & ~MatInfoMaskBishopsL)==0 || (mat & ~MatInfoMaskBishopsD)==0)
		return EvalMatTypeDraw;

	// Check for known combinations.
	unsigned int pieceCount=bbPopCount(posGetBBAll(pos));
	assert(pieceCount>=2 && pieceCount<=32);
	switch(pieceCount) {
		case 2:
			// This should be handled by same-bishop code above.
			assert(false);
		break;
		case 3:
			if (mat==MatInfoMaskKNvK || mat==MatInfoMaskKvKN)
				return EvalMatTypeDraw;
			else if (mat==(MAKE(PieceWPawn,1)|MatInfoMaskKings) || mat==(MAKE(PieceBPawn,1)|MatInfoMaskKings))
				return EvalMatTypeKPvK;
		break;
		case 4:
			if (mat==(MAKE(PieceWKnight,2)|MatInfoMaskKings) || mat==(MAKE(PieceBKnight,2)|MatInfoMaskKings))
				return EvalMatTypeKNNvK;
		break;
	}

	// KBPvK (any positive number of pawns and any positive number of same coloured bishops).
	const MatInfo matW=(mat & matInfoMakeMaskColour(ColourWhite) & ~MatInfoMaskKings);
	const MatInfo matB=(mat & matInfoMakeMaskColour(ColourBlack) & ~MatInfoMaskKings);
	const MatInfo matPawns=matInfoMakeMaskPieceType(PieceTypePawn);
	if (!matB && (matW & matPawns) && !(matW & MatInfoMaskNRQ)) { // No black pieces, white has pawns and white does not have any knights, rooks or queens.
		if (((matW & MatInfoMaskBishopsL)!=0)^((matW & MatInfoMaskBishopsD)!=0)) // Check white has exactly one type (colour) of bishop.
			return EvalMatTypeKBPvK;
	} else if (!matW && (matB & matPawns) && !(matB & MatInfoMaskNRQ)) {
		if (((matB & MatInfoMaskBishopsL)!=0)^((matB & MatInfoMaskBishopsD)!=0))
			return EvalMatTypeKBPvK;
	}

	// Other combination.
	return EvalMatTypeOther;

#	undef MASK
#	undef MAKE
}

