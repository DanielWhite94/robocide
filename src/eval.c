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
#include "ttune.h"
#include "tune.h"
#include "uci.h"

const VPair VPairZero={0,0};

typedef struct EvalData EvalData;

typedef struct {
	BB pawns[ColourNB], passed[ColourNB], semiOpenFiles[ColourNB], outposts[ColourNB], openFiles;
	VPair score;
} EvalPawnData;
HTable *evalPawnTable=NULL;
const size_t evalPawnTableDefaultSizeMb=1;
#define evalPawnTableMaxSizeMb ((HTableMaxEntryCount*sizeof(EvalPawnData))/(1024*1024)) // 256gb

STATICASSERT(ScoreBit<=16);
STATICASSERT(EvalMatTypeBit<=8);
typedef struct {
	Key key;
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

typedef enum {
	EvalTTuneParamPawnMG,
	EvalTTuneParamPawnEG,
	EvalTTuneParamKnightMG,
	EvalTTuneParamKnightEG,
	EvalTTuneParamBishopMG,
	EvalTTuneParamBishopEG,
	EvalTTuneParamRookMG,
	EvalTTuneParamRookEG,
	EvalTTuneParamQueenMG,
	EvalTTuneParamQueenEG,
	EvalTTuneParamKnightMobMG,
	EvalTTuneParamKnightMobEG,
	EvalTTuneParamBishopMobMG,
	EvalTTuneParamBishopMobEG,
	EvalTTuneParamRookMobFileMG,
	EvalTTuneParamRookMobFileEG,
	EvalTTuneParamRookMobRankMG,
	EvalTTuneParamRookMobRankEG,
	EvalTTuneParamPawnDoubledMG,
	EvalTTuneParamPawnDoubledEG,
	EvalTTuneParamPawnIsolatedMG,
	EvalTTuneParamPawnIsolatedEG,
	EvalTTuneParamPawnBlockedMG,
	EvalTTuneParamPawnBlockedEG,
	EvalTTuneParamPawnPassedR2MG,
	EvalTTuneParamPawnPassedR2EG,
	EvalTTuneParamPawnPassedR3MG,
	EvalTTuneParamPawnPassedR3EG,
	EvalTTuneParamPawnPassedR4MG,
	EvalTTuneParamPawnPassedR4EG,
	EvalTTuneParamPawnPassedR5MG,
	EvalTTuneParamPawnPassedR5EG,
	EvalTTuneParamPawnPassedR6MG,
	EvalTTuneParamPawnPassedR6EG,
	EvalTTuneParamPawnPassedR7MG,
	EvalTTuneParamPawnPassedR7EG,
	EvalTTuneParamBishopPairMG,
	EvalTTuneParamBishopPairEG,
	EvalTTuneParamNB,
} EvalTTuneParam;

////////////////////////////////////////////////////////////////////////////////
// Tunable values.
////////////////////////////////////////////////////////////////////////////////

TUNECONST VPair evalMaterial[PieceTypeNB]={
	[PieceTypeNone]={0,0},
	[PieceTypePawn]={705,967},
	[PieceTypeKnight]={2807,2370},
	[PieceTypeBishopL]={3108,2659},
	[PieceTypeBishopD]={3108,2659},
	[PieceTypeRook]={5491,4985},
	[PieceTypeQueen]={8975,10143},
	[PieceTypeKing]={600,-460} // these values exist soley to make PSTs look nicer (both sides always have exactly one king of course)
};
TUNECONST VPair evalPawnDoubled={-30,-167};
TUNECONST VPair evalPawnIsolated={-207,-96};
TUNECONST VPair evalPawnBlocked={-22,-100};
TUNECONST VPair evalPassedPawn[8]={
	{   0,   0},
	{  11,  96},
	{  24, 134},
	{ 129, 264},
	{ 326, 486},
	{ 615, 800},
	{ 996,1206},
	{   0,   0},
};
TUNECONST VPair evalKnightMob={30,18};
TUNECONST VPair evalKnightPawnAffinity={30,32}; // Bonus each knight receives for each friendly pawn on the board.
TUNECONST VPair evalBishopPair={500,489};
TUNECONST VPair evalBishopMob={38,32};
TUNECONST VPair evalOppositeBishopFactor={256,192}; // /256.
TUNECONST VPair evalRookPawnAffinity={-70,-70}; // Bonus each rook receives for each friendly pawn on the board.
TUNECONST VPair evalRookMobFile={21,30};
TUNECONST VPair evalRookMobRank={11,20};
TUNECONST VPair evalRookOpenFile={100,50};
TUNECONST VPair evalRookSemiOpenFile={50,20};
TUNECONST VPair evalRookOn7th={50,100};
TUNECONST VPair evalRookTrapped={-400,0};
TUNECONST VPair evalKingShieldClose={150,0};
TUNECONST VPair evalKingShieldFar={73,0};
TUNECONST VPair evalKingNearPasserFactor={10,200};
TUNECONST VPair evalKingCastlingMobility={100,0};
TUNECONST VPair evalOutpostSq={40,20};
TUNECONST VPair evalOutpostKnight={132,100};
TUNECONST VPair evalTempoDefault={35,0};
TUNECONST Value evalHalfMoveFactor=2048;
TUNECONST Value evalWeightFactor=151;

VPair evalPST[PieceNB][SqNB]={
	[PieceTypePawn]={
		{   0,   0},{   0,   0},{    0,   0},{    0,   0},{    0,   0},{    0,   0},{    0,   0},{    0,   0},
		{  37, 135},{  52, 135},{   67, 135},{   82, 135},{   82, 135},{   67, 135},{   52, 135},{   37, 135},
		{   4,  95},{  19,  95},{   84,  95},{   99,  95},{   99,  95},{   84,  95},{   19,  95},{    4,  95},
		{ -29,  55},{ -14,  55},{   51,  55},{  179,  55},{  179,  55},{   51,  55},{  -14,  55},{  -29,  55},
		{ -39,  30},{ -24,  30},{   41,  30},{  169,  30},{  169,  30},{   41,  30},{  -24,  30},{  -39,  30},
		{ -26,  20},{ -11,  20},{   54,  20},{   69,  20},{   69,  20},{   54,  20},{  -11,  20},{  -26,  20},
		{ -13,  10},{   2,  10},{   17,  10},{   32,  10},{   32,  10},{   17,  10},{    2,  10},{  -13,  10},
		{   0,   0},{   0,   0},{    0,   0},{    0,   0},{    0,   0},{    0,   0},{    0,   0},{    0,   0},
	},
	[PieceTypeKnight]={
		{ 252,   0},{ 307,  26},{  362,  52},{  417,  78},{  417,  78},{  362,  52},{  307,  26},{  252,   0},
		{ 255,  49},{ 310,  75},{  365, 101},{  420, 127},{  420, 127},{  365, 101},{  310,  75},{  255,  49},
		{ 258,  98},{ 313, 124},{  368, 150},{  423, 176},{  423, 176},{  368, 150},{  313, 124},{  258,  98},
		{ 261, 147},{ 316, 173},{  371, 199},{  426, 225},{  426, 225},{  371, 199},{  316, 173},{  261, 147},
		{ 225, 147},{ 280, 173},{  335, 199},{  390, 225},{  390, 225},{  335, 199},{  280, 173},{  225, 147},
		{ 150,  98},{ 205, 124},{  260, 150},{  315, 176},{  315, 176},{  260, 150},{  205, 124},{  150,  98},
		{  75,  49},{ 130,  75},{  185, 101},{  240, 127},{  240, 127},{  185, 101},{  130,  75},{   75,  49},
		{   0,   0},{  55,  26},{  110,  52},{  165,  78},{  165,  78},{  110,  52},{   55,  26},{    0,   0},
	},
	[PieceTypeBishopL]={
		{   0,-105},{  14, -96},{   28, -87},{   42, -78},{   42, -78},{   28, -87},{   14, -96},{    0,-105},
		{  14, -81},{  28, -72},{   42, -63},{   56, -54},{   56, -54},{   42, -63},{   28, -72},{   14, -81},
		{  28, -57},{  42, -48},{   56, -39},{   70, -30},{   70, -30},{   56, -39},{   42, -48},{   28, -57},
		{  42, -33},{  56, -24},{   70, -15},{   84,  -6},{   84,  -6},{   70, -15},{   56, -24},{   42, -33},
		{  42, -18},{  56,  -9},{   70,   0},{   84,   9},{   84,   9},{   70,   0},{   56,  -9},{   42, -18},
		{  28, -12},{  42,  -3},{   56,   6},{   70,  15},{   70,  15},{   56,   6},{   42,  -3},{   28, -12},
		{  14,  -6},{  28,   3},{   42,  12},{   56,  21},{   56,  21},{   42,  12},{   28,   3},{   14,  -6},
		{   0,   0},{  14,   9},{   28,  18},{   42,  27},{   42,  27},{   28,  18},{   14,   9},{    0,   0},
	},
	[PieceTypeRook]={
		{   0, 112},{  26, 112},{   52, 112},{   78, 112},{   78, 112},{   52, 112},{   26, 112},{    0, 112},
		{   0,  96},{  26,  96},{   52,  96},{   78,  96},{   78,  96},{   52,  96},{   26,  96},{    0,  96},
		{   0,  80},{  26,  80},{   52,  80},{   78,  80},{   78,  80},{   52,  80},{   26,  80},{    0,  80},
		{   0,  64},{  26,  64},{   52,  64},{   78,  64},{   78,  64},{   52,  64},{   26,  64},{    0,  64},
		{   0,  48},{  26,  48},{   52,  48},{   78,  48},{   78,  48},{   52,  48},{   26,  48},{    0,  48},
		{   0,  32},{  26,  32},{   52,  32},{   78,  32},{   78,  32},{   52,  32},{   26,  32},{    0,  32},
		{   0,  16},{  26,  16},{   52,  16},{   78,  16},{   78,  16},{   52,  16},{   26,  16},{    0,  16},
		{   0,   0},{  26,   0},{   52,   0},{   78,   0},{   78,   0},{   52,   0},{   26,   0},{    0,   0},
	},
	[PieceTypeQueen]={
		{ 217,   0},{ 261,  24},{  305,  48},{  349,  72},{  349,  72},{  305,  48},{  261,  24},{  217,   0},
		{ 176,  10},{ 220,  34},{  264,  58},{  308,  82},{  308,  82},{  264,  58},{  220,  34},{  176,  10},
		{ 135,  20},{ 179,  44},{  223,  68},{  267,  92},{  267,  92},{  223,  68},{  179,  44},{  135,  20},
		{  94,  30},{ 138,  54},{  182,  78},{  226, 102},{  226, 102},{  182,  78},{  138,  54},{   94,  30},
		{  63,  30},{ 107,  54},{  151,  78},{  195, 102},{  195, 102},{  151,  78},{  107,  54},{   63,  30},
		{  42,  20},{  86,  44},{  130,  68},{  174,  92},{  174,  92},{  130,  68},{   86,  44},{   42,  20},
		{  21,  10},{  65,  34},{  109,  58},{  153,  82},{  153,  82},{  109,  58},{   65,  34},{   21,  10},
		{   0,   0},{  44,  24},{   88,  48},{  132,  72},{  132,  72},{   88,  48},{   44,  24},{    0,   0},
	},
	[PieceTypeKing]={
		{-350, 413},{-593, 533},{ -836, 653},{-1079, 773},{-1079, 773},{ -836, 653},{ -593, 533},{ -350, 413},
		{-441, 474},{-684, 594},{ -927, 714},{-1170, 834},{-1170, 834},{ -927, 714},{ -684, 594},{ -441, 474},
		{-532, 535},{-775, 655},{-1018, 775},{-1261, 895},{-1261, 895},{-1018, 775},{ -775, 655},{ -532, 535},
		{-623, 596},{-866, 716},{-1109, 836},{-1352, 956},{-1352, 956},{-1109, 836},{ -866, 716},{ -623, 596},
		{-573, 537},{-816, 657},{-1059, 777},{-1302, 897},{-1302, 897},{-1059, 777},{ -816, 657},{ -573, 537},
		{-382, 358},{-625, 478},{ -868, 598},{-1111, 718},{-1111, 718},{ -868, 598},{ -625, 478},{ -382, 358},
		{-191, 179},{-434, 299},{ -677, 419},{ -920, 539},{ -920, 539},{ -677, 419},{ -434, 299},{ -191, 179},
		{-243,   0},{-243, 120},{ -486, 240},{ -729, 360},{ -729, 360},{ -486, 240},{ -243, 120},{ -243,   0},
	},
};

////////////////////////////////////////////////////////////////////////////////
// Derived values
////////////////////////////////////////////////////////////////////////////////

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

#ifdef TUNE
void evalSetValue(void *varPtr, long long value);
bool evalOptionNewVPair(const char *name, VPair *score, Value min, Value max);
bool evalOptionNewVPairF(const char *nameFormat, VPair *score, Value min, Value max, ...);
#endif

void evalRecalc(void);

void evalVerify(void);

EvalMatType evalComputeMatType(const Pos *pos);

void evalPstDraw(PieceType type);

////////////////////////////////////////////////////////////////////////////////
// Public functions.
////////////////////////////////////////////////////////////////////////////////

void evalInit(void) {
	// Init Texel Tuning module
	ttuneInit(EvalTTuneParamNB);

	// Setup pawn hash table.
	evalPawnTable=htableNew(sizeof(EvalPawnData), evalPawnTableDefaultSizeMb);
	if (evalPawnTable==NULL)
		mainFatalError("Error: Could not allocate pawn hash table.\n");
	uciOptionNewSpin("PawnHash", &htableResizeInterface, evalPawnTable, 1, evalPawnTableMaxSizeMb, evalPawnTableDefaultSizeMb);
	uciOptionNewButton("ClearPawnHash", &htableClearInterface, evalPawnTable);

	// Setup mat hash table.
	evalMatTable=htableNew(sizeof(EvalMatData), evalMatTableDefaultSizeMb);
	if (evalMatTable==NULL)
		mainFatalError("Error: Could not allocate mat hash table.\n");
	uciOptionNewSpin("MatHash", &htableResizeInterface, evalMatTable, 1, evalMatTableMaxSizeMb, evalMatTableDefaultSizeMb);
	uciOptionNewButton("ClearMatHash", &htableClearInterface, evalMatTable);

	// Calculate dervied values (such as passed pawn table).
	evalRecalc();

	// Setup callbacks for tuning values.
#	ifdef TUNE
	evalOptionNewVPair("Pawn", &evalMaterial[PieceTypePawn], 0, 2000);
	evalOptionNewVPair("Knight", &evalMaterial[PieceTypeKnight], 0, 6000);
	evalOptionNewVPair("Bishop", &evalMaterial[PieceTypeBishopL], 0, 6000);
	evalOptionNewVPair("Rook", &evalMaterial[PieceTypeRook], 0, 10000);
	evalOptionNewVPair("Queen", &evalMaterial[PieceTypeQueen], 0, 18000);
	evalOptionNewVPair("PawnDoubled", &evalPawnDoubled, -1000, 0);
	evalOptionNewVPair("PawnIsolated", &evalPawnIsolated, -1000, 0);
	evalOptionNewVPair("PawnBlocked", &evalPawnBlocked, -1000, 0);
	evalOptionNewVPair("KnightMob", &evalKnightMob, 0, 100);
	evalOptionNewVPair("KnightPawnAffinity", &evalKnightPawnAffinity, -100, 100);
	evalOptionNewVPair("BishopPair", &evalBishopPair, 0, 1000);
	evalOptionNewVPair("BishopMobility", &evalBishopMob, 0, 100);
	uciOptionNewSpin("OppositeBishopFactorMG", &evalSetValue, &evalOppositeBishopFactor.mg, 0, 512, evalOppositeBishopFactor.mg);
	uciOptionNewSpin("OppositeBishopFactorEG", &evalSetValue, &evalOppositeBishopFactor.eg, 0, 512, evalOppositeBishopFactor.eg);
	evalOptionNewVPair("RookPawnAffinity", &evalRookPawnAffinity, -200, 200);
	evalOptionNewVPair("RookMobilityFile", &evalRookMobFile, 0, 50);
	evalOptionNewVPair("RookMobilityRank", &evalRookMobRank, 0, 50);
	evalOptionNewVPair("RookOpenFile", &evalRookOpenFile, -200, 200);
	evalOptionNewVPair("RookSemiOpenFile", &evalRookSemiOpenFile, 0, 150);
	evalOptionNewVPair("RookOn7th", &evalRookOn7th, -200, 200);
	evalOptionNewVPair("RookTrapped", &evalRookTrapped, -3000, 0);
	evalOptionNewVPair("KingShieldClose", &evalKingShieldClose, 0, 500);
	evalOptionNewVPair("KingShieldFar", &evalKingShieldFar, 0, 300);
	evalOptionNewVPair("KingNearPasser", &evalKingNearPasserFactor, 0, 500);
	evalOptionNewVPair("KingCastlingMobility", &evalKingCastlingMobility, 0, 200);
	evalOptionNewVPair("OutpostSq", &evalOutpostSq, 0, 500);
	evalOptionNewVPair("OutpostKnight", &evalOutpostKnight, 0, 1000);
	evalOptionNewVPair("Tempo", &evalTempoDefault, 0, 100);
	uciOptionNewSpin("HalfMoveFactor", &evalSetValue, &evalHalfMoveFactor, 1, 4096, evalHalfMoveFactor);
	uciOptionNewSpin("WeightFactor", &evalSetValue, &evalWeightFactor, 1, 512, evalWeightFactor);
#	endif

	// Setup Texel Tuning parameters
	ttuneAddParameter(EvalTTuneParamPawnMG, "PawnMG", evalMaterial[PieceTypePawn].mg, evalMaterial[PieceTypePawn].mg, evalMaterial[PieceTypePawn].mg); // this is the one fixed value everything else is relative to
	ttuneAddParameter(EvalTTuneParamPawnEG, "PawnEG", 0, 2000, evalMaterial[PieceTypePawn].eg);
	ttuneAddParameter(EvalTTuneParamKnightMG, "KnightMG", 0, 6000, evalMaterial[PieceTypeKnight].mg);
	ttuneAddParameter(EvalTTuneParamKnightEG, "KnightEG", 0, 6000, evalMaterial[PieceTypeKnight].eg);
	ttuneAddParameter(EvalTTuneParamBishopMG, "BishopMG", 0, 6000, evalMaterial[PieceTypeBishopL].mg);
	ttuneAddParameter(EvalTTuneParamBishopEG, "BishopEG", 0, 6000, evalMaterial[PieceTypeBishopL].eg);
	ttuneAddParameter(EvalTTuneParamRookMG, "RookMG", 0, 10000, evalMaterial[PieceTypeRook].mg);
	ttuneAddParameter(EvalTTuneParamRookEG, "RookEG", 0, 10000, evalMaterial[PieceTypeRook].eg);
	ttuneAddParameter(EvalTTuneParamQueenMG, "QueenMG", 0, 18000, evalMaterial[PieceTypeQueen].mg);
	ttuneAddParameter(EvalTTuneParamQueenEG, "QueenEG", 0, 18000, evalMaterial[PieceTypeQueen].eg);
	ttuneAddParameter(EvalTTuneParamKnightMobMG, "KnightMobMG", 0, 300, evalKnightMob.mg);
	ttuneAddParameter(EvalTTuneParamKnightMobEG, "KnightMobEG", 0, 300, evalKnightMob.eg);
	ttuneAddParameter(EvalTTuneParamBishopMobMG, "BishopMobMG", 0, 300, evalBishopMob.mg);
	ttuneAddParameter(EvalTTuneParamBishopMobEG, "BishopMobEG", 0, 300, evalBishopMob.eg);
	ttuneAddParameter(EvalTTuneParamRookMobFileMG, "RookMobFileMG", 0, 300, evalRookMobFile.mg);
	ttuneAddParameter(EvalTTuneParamRookMobFileEG, "RookMobFileEG", 0, 300, evalRookMobFile.eg);
	ttuneAddParameter(EvalTTuneParamRookMobRankMG, "RookMobRankMG", 0, 300, evalRookMobRank.mg);
	ttuneAddParameter(EvalTTuneParamRookMobRankEG, "RookMobRankEG", 0, 300, evalRookMobRank.eg);
	ttuneAddParameter(EvalTTuneParamPawnDoubledMG, "PawnDoubledMG", -1000, 0, evalPawnDoubled.mg);
	ttuneAddParameter(EvalTTuneParamPawnDoubledEG, "PawnDoubledEG", -1000, 0, evalPawnDoubled.eg);
	ttuneAddParameter(EvalTTuneParamPawnIsolatedMG, "PawnIsolatedMG", -1000, 0, evalPawnIsolated.mg);
	ttuneAddParameter(EvalTTuneParamPawnIsolatedEG, "PawnIsolatedEG", -1000, 0, evalPawnIsolated.eg);
	ttuneAddParameter(EvalTTuneParamPawnBlockedMG, "PawnBlockedMG", -1000, 0, evalPawnBlocked.mg);
	ttuneAddParameter(EvalTTuneParamPawnBlockedEG, "PawnBlockedEG", -1000, 0, evalPawnBlocked.mg);
	ttuneAddParameter(EvalTTuneParamPawnPassedR2MG, "PawnPassedR2MG", 0, 20000, evalPassedPawn[Rank2].mg);
	ttuneAddParameter(EvalTTuneParamPawnPassedR2EG, "PawnPassedR2EG", 0, 20000, evalPassedPawn[Rank2].eg);
	ttuneAddParameter(EvalTTuneParamPawnPassedR3MG, "PawnPassedR3MG", 0, 20000, evalPassedPawn[Rank3].mg);
	ttuneAddParameter(EvalTTuneParamPawnPassedR3EG, "PawnPassedR3EG", 0, 20000, evalPassedPawn[Rank3].eg);
	ttuneAddParameter(EvalTTuneParamPawnPassedR4MG, "PawnPassedR4MG", 0, 20000, evalPassedPawn[Rank4].mg);
	ttuneAddParameter(EvalTTuneParamPawnPassedR4EG, "PawnPassedR4EG", 0, 20000, evalPassedPawn[Rank4].eg);
	ttuneAddParameter(EvalTTuneParamPawnPassedR5MG, "PawnPassedR5MG", 0, 20000, evalPassedPawn[Rank5].mg);
	ttuneAddParameter(EvalTTuneParamPawnPassedR5EG, "PawnPassedR5EG", 0, 20000, evalPassedPawn[Rank5].eg);
	ttuneAddParameter(EvalTTuneParamPawnPassedR6MG, "PawnPassedR6MG", 0, 20000, evalPassedPawn[Rank6].mg);
	ttuneAddParameter(EvalTTuneParamPawnPassedR6EG, "PawnPassedR6EG", 0, 20000, evalPassedPawn[Rank6].eg);
	ttuneAddParameter(EvalTTuneParamPawnPassedR7MG, "PawnPassedR7MG", 0, 20000, evalPassedPawn[Rank7].mg);
	ttuneAddParameter(EvalTTuneParamPawnPassedR7EG, "PawnPassedR7EG", 0, 20000, evalPassedPawn[Rank7].eg);
	ttuneAddParameter(EvalTTuneParamBishopPairMG, "BishopPairMG", 0, 2000, evalBishopPair.mg);
	ttuneAddParameter(EvalTTuneParamBishopPairEG, "BishopPairEG", 0, 2000, evalBishopPair.eg);
}

void evalQuit(void) {
	// Free hash tables
	htableFree(evalPawnTable);
	evalPawnTable=NULL;
	htableFree(evalMatTable);
	evalMatTable=NULL;

	// Quit Texel Tuning module
	ttuneQuit();
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

void evaluateCoefficients(const Pos *pos, double *coefficients) {
	// Precomputed info
	int wPawnCount=bbPopCount(posGetBBPiece(pos, PieceWPawn));
	int bPawnCount=bbPopCount(posGetBBPiece(pos, PieceBPawn));
	int wKnightCount=bbPopCount(posGetBBPiece(pos, PieceWKnight));
	int bKnightCount=bbPopCount(posGetBBPiece(pos, PieceBKnight));
	int wBishopCount=bbPopCount(posGetBBPiece(pos, PieceWBishopL) | posGetBBPiece(pos, PieceWBishopD));
	int bBishopCount=bbPopCount(posGetBBPiece(pos, PieceBBishopL) | posGetBBPiece(pos, PieceBBishopD));
	int wRookCount=bbPopCount(posGetBBPiece(pos, PieceWRook));
	int bRookCount=bbPopCount(posGetBBPiece(pos, PieceBRook));
	int wQueenCount=bbPopCount(posGetBBPiece(pos, PieceWQueen));
	int bQueenCount=bbPopCount(posGetBBPiece(pos, PieceBQueen));

	int minorCount=wKnightCount+wBishopCount+bKnightCount+bBishopCount;
	int rookCount=wRookCount+bRookCount;
	int queenCount=wQueenCount+bQueenCount;

	int pieceWeight=minorCount+2*rookCount+4*queenCount;
	assert(pieceWeight>=0 && pieceWeight<128);

	int weightEG=evalWeightEGFactors[pieceWeight];
	int weightMG=256-weightEG;

	double factorMG=weightMG/256.0;
	double factorEG=weightEG/256.0;

	// Piece counts (material)
	coefficients[EvalTTuneParamPawnMG]=factorMG*(wPawnCount-bPawnCount);
	coefficients[EvalTTuneParamPawnEG]=factorEG*(wPawnCount-bPawnCount);
	coefficients[EvalTTuneParamKnightMG]=factorMG*(wKnightCount-bKnightCount);
	coefficients[EvalTTuneParamKnightEG]=factorEG*(wKnightCount-bKnightCount);
	coefficients[EvalTTuneParamBishopMG]=factorMG*(wBishopCount-bBishopCount);
	coefficients[EvalTTuneParamBishopEG]=factorEG*(wBishopCount-bBishopCount);
	coefficients[EvalTTuneParamRookMG]=factorMG*(wRookCount-bRookCount);
	coefficients[EvalTTuneParamRookEG]=factorEG*(wRookCount-bRookCount);
	coefficients[EvalTTuneParamQueenMG]=factorMG*(wQueenCount-bQueenCount);
	coefficients[EvalTTuneParamQueenEG]=factorEG*(wQueenCount-bQueenCount);

	// Mobility
	coefficients[EvalTTuneParamKnightMobMG]=0.0;
	coefficients[EvalTTuneParamKnightMobEG]=0.0;
	coefficients[EvalTTuneParamBishopMobMG]=0.0;
	coefficients[EvalTTuneParamBishopMobEG]=0.0;
	coefficients[EvalTTuneParamRookMobFileMG]=0.0;
	coefficients[EvalTTuneParamRookMobFileEG]=0.0;
	coefficients[EvalTTuneParamRookMobRankMG]=0.0;
	coefficients[EvalTTuneParamRookMobRankEG]=0.0;

	BB pieceSet;
	double count;

	BB wp=posGetBBPiece(pos, PieceWPawn);
	BB bp=posGetBBPiece(pos, PieceBPawn);
	BB wpAttacks=bbForwardOne(bbWingify(wp), ColourWhite);
	BB bpAttacks=bbForwardOne(bbWingify(bp), ColourBlack);

	BB mobilityAllowed[ColourNB];
	mobilityAllowed[ColourWhite]=~(wp | posGetBBPiece(pos, PieceWKing) | bpAttacks);
	mobilityAllowed[ColourBlack]=~(bp | posGetBBPiece(pos, PieceBKing) | wpAttacks);

	pieceSet=posGetBBPiece(pos, PieceWKnight);
	while(pieceSet) {
		Sq sq=bbScanReset(&pieceSet);
		BB attacks=attacksKnight(sq);
		count=bbPopCount(attacks & mobilityAllowed[ColourWhite]);
		coefficients[EvalTTuneParamKnightMobMG]+=factorMG*count;
		coefficients[EvalTTuneParamKnightMobEG]+=factorEG*count;
	}
	pieceSet=posGetBBPiece(pos, PieceBKnight);
	while(pieceSet) {
		Sq sq=bbScanReset(&pieceSet);
		BB attacks=attacksKnight(sq);
		count=bbPopCount(attacks & mobilityAllowed[ColourBlack]);
		coefficients[EvalTTuneParamKnightMobMG]-=factorMG*count;
		coefficients[EvalTTuneParamKnightMobEG]-=factorEG*count;
	}

	BB bishopMobOcc[ColourNB];
	bishopMobOcc[ColourWhite]=(posGetBBAll(pos)^(posGetBBPiece(pos, PieceWBishopL)|posGetBBPiece(pos, PieceWBishopD)|posGetBBPiece(pos, PieceWQueen)));
	bishopMobOcc[ColourBlack]=(posGetBBAll(pos)^(posGetBBPiece(pos, PieceBBishopL)|posGetBBPiece(pos, PieceBBishopD)|posGetBBPiece(pos, PieceBQueen)));

	pieceSet=(posGetBBPiece(pos, PieceWBishopL)|posGetBBPiece(pos, PieceWBishopD));
	while(pieceSet) {
		Sq sq=bbScanReset(&pieceSet);
		BB attacks=attacksBishop(sq, bishopMobOcc[ColourWhite]);
		count=bbPopCount(attacks & mobilityAllowed[ColourWhite]);
		coefficients[EvalTTuneParamBishopMobMG]+=factorMG*count;
		coefficients[EvalTTuneParamBishopMobEG]+=factorEG*count;
	}
	pieceSet=(posGetBBPiece(pos, PieceBBishopL)|posGetBBPiece(pos, PieceBBishopD));
	while(pieceSet) {
		Sq sq=bbScanReset(&pieceSet);
		BB attacks=attacksBishop(sq, bishopMobOcc[ColourBlack]);
		count=bbPopCount(attacks & mobilityAllowed[ColourBlack]);
		coefficients[EvalTTuneParamBishopMobMG]-=factorMG*count;
		coefficients[EvalTTuneParamBishopMobEG]-=factorEG*count;
	}

	BB rookMobOcc[ColourNB];
	rookMobOcc[ColourWhite]=(posGetBBAll(pos)^(posGetBBPiece(pos, PieceWRook)|posGetBBPiece(pos, PieceWQueen)));
	rookMobOcc[ColourBlack]=(posGetBBAll(pos)^(posGetBBPiece(pos, PieceBRook)|posGetBBPiece(pos, PieceBQueen)));

	pieceSet=posGetBBPiece(pos, PieceWRook);
	while(pieceSet) {
		Sq sq=bbScanReset(&pieceSet);
		BB attacks=attacksRook(sq, rookMobOcc[ColourWhite]);
		count=bbPopCount(attacks & mobilityAllowed[ColourWhite] & bbFile(sqFile(sq)));
		coefficients[EvalTTuneParamRookMobFileMG]+=factorMG*count;
		coefficients[EvalTTuneParamRookMobFileEG]+=factorEG*count;
		count=bbPopCount(attacks & mobilityAllowed[ColourWhite] & bbRank(sqRank(sq)));
		coefficients[EvalTTuneParamRookMobRankMG]+=factorMG*count;
		coefficients[EvalTTuneParamRookMobRankEG]+=factorEG*count;
	}
	pieceSet=posGetBBPiece(pos, PieceBRook);
	while(pieceSet) {
		Sq sq=bbScanReset(&pieceSet);
		BB attacks=attacksRook(sq, rookMobOcc[ColourBlack]);
		count=bbPopCount(attacks & mobilityAllowed[ColourBlack] & bbFile(sqFile(sq)));
		coefficients[EvalTTuneParamRookMobFileMG]-=factorMG*count;
		coefficients[EvalTTuneParamRookMobFileEG]-=factorEG*count;
		count=bbPopCount(attacks & mobilityAllowed[ColourBlack] & bbRank(sqRank(sq)));
		coefficients[EvalTTuneParamRookMobRankMG]-=factorMG*count;
		coefficients[EvalTTuneParamRookMobRankEG]-=factorEG*count;
	}

	// Pawns
	BB pawns[ColourNB], frontSpan[ColourNB], rearSpan[ColourNB], attacks[ColourNB];
	BB doubled[ColourNB], isolated[ColourNB];
	BB influence[ColourNB], fill[ColourNB];
	BB blocked[ColourNB], passed[ColourNB];
	BB occ=posGetBBAll(pos);
	pawns[ColourWhite]=posGetBBPiece(pos, PieceWPawn); // All pawns of given colour
	pawns[ColourBlack]=posGetBBPiece(pos, PieceBPawn);
	fill[ColourWhite]=bbFileFill(pawns[ColourWhite]);
	fill[ColourBlack]=bbFileFill(pawns[ColourBlack]);
	frontSpan[ColourWhite]=bbNorthOne(bbNorthFill(pawns[ColourWhite])); // All squares infront of pawns of given colour
	frontSpan[ColourBlack]=bbSouthOne(bbSouthFill(pawns[ColourBlack]));
	rearSpan[ColourWhite]=bbSouthOne(bbSouthFill(pawns[ColourWhite])); // All squares behind pawns of given colour
	rearSpan[ColourBlack]=bbNorthOne(bbNorthFill(pawns[ColourBlack]));
	attacks[ColourWhite]=bbNorthOne(bbWingify(pawns[ColourWhite])); // All squares attacked by pawns of given colour
	attacks[ColourBlack]=bbSouthOne(bbWingify(pawns[ColourBlack]));
	influence[ColourWhite]=(frontSpan[ColourWhite] | bbWingify(frontSpan[ColourWhite])); // Squares which colour in question may attack or move to, now or in the future.
	influence[ColourBlack]=(frontSpan[ColourBlack] | bbWingify(frontSpan[ColourBlack]));

	doubled[ColourWhite]=(pawns[ColourWhite] & rearSpan[ColourWhite]);
	doubled[ColourBlack]=(pawns[ColourBlack] & rearSpan[ColourBlack]);
	isolated[ColourWhite]=(pawns[ColourWhite] & ~bbFileFill(attacks[ColourWhite]));
	isolated[ColourBlack]=(pawns[ColourBlack] & ~bbFileFill(attacks[ColourBlack]));
	blocked[ColourWhite]=(pawns[ColourWhite] & bbSouthOne(occ));
	blocked[ColourBlack]=(pawns[ColourBlack] & bbNorthOne(occ));
	passed[ColourWhite]=(pawns[ColourWhite] & ~(doubled[ColourWhite] | influence[ColourBlack]));
	passed[ColourBlack]=(pawns[ColourBlack] & ~(doubled[ColourBlack] | influence[ColourWhite]));

	int doubledCount=((int)bbPopCount(doubled[ColourWhite]))-((int)bbPopCount(doubled[ColourBlack]));
	int isolatedCount=((int)bbPopCount(isolated[ColourWhite]))-((int)bbPopCount(isolated[ColourBlack]));
	int blockedCount=((int)bbPopCount(blocked[ColourWhite]))-((int)bbPopCount(blocked[ColourBlack]));

	coefficients[EvalTTuneParamPawnDoubledMG]=factorMG*doubledCount;
	coefficients[EvalTTuneParamPawnDoubledEG]=factorEG*doubledCount;
	coefficients[EvalTTuneParamPawnIsolatedMG]=factorMG*isolatedCount;
	coefficients[EvalTTuneParamPawnIsolatedEG]=factorEG*isolatedCount;
	coefficients[EvalTTuneParamPawnBlockedMG]=factorMG*blockedCount;
	coefficients[EvalTTuneParamPawnBlockedEG]=factorEG*blockedCount;

	int passedCount[RankNB];
	passedCount[Rank2]=((int)bbPopCount(passed[ColourWhite] & bbRank(Rank2)))-((int)bbPopCount(passed[ColourBlack] & bbRank(Rank7)));
	passedCount[Rank3]=((int)bbPopCount(passed[ColourWhite] & bbRank(Rank3)))-((int)bbPopCount(passed[ColourBlack] & bbRank(Rank6)));
	passedCount[Rank4]=((int)bbPopCount(passed[ColourWhite] & bbRank(Rank4)))-((int)bbPopCount(passed[ColourBlack] & bbRank(Rank5)));
	passedCount[Rank5]=((int)bbPopCount(passed[ColourWhite] & bbRank(Rank5)))-((int)bbPopCount(passed[ColourBlack] & bbRank(Rank4)));
	passedCount[Rank6]=((int)bbPopCount(passed[ColourWhite] & bbRank(Rank6)))-((int)bbPopCount(passed[ColourBlack] & bbRank(Rank3)));
	passedCount[Rank7]=((int)bbPopCount(passed[ColourWhite] & bbRank(Rank7)))-((int)bbPopCount(passed[ColourBlack] & bbRank(Rank2)));

	coefficients[EvalTTuneParamPawnPassedR2MG]=factorMG*passedCount[Rank2];
	coefficients[EvalTTuneParamPawnPassedR2EG]=factorEG*passedCount[Rank2];
	coefficients[EvalTTuneParamPawnPassedR3MG]=factorMG*passedCount[Rank3];
	coefficients[EvalTTuneParamPawnPassedR3EG]=factorEG*passedCount[Rank3];
	coefficients[EvalTTuneParamPawnPassedR4MG]=factorMG*passedCount[Rank4];
	coefficients[EvalTTuneParamPawnPassedR4EG]=factorEG*passedCount[Rank4];
	coefficients[EvalTTuneParamPawnPassedR5MG]=factorMG*passedCount[Rank5];
	coefficients[EvalTTuneParamPawnPassedR5EG]=factorEG*passedCount[Rank5];
	coefficients[EvalTTuneParamPawnPassedR6MG]=factorMG*passedCount[Rank6];
	coefficients[EvalTTuneParamPawnPassedR6EG]=factorEG*passedCount[Rank6];
	coefficients[EvalTTuneParamPawnPassedR7MG]=factorMG*passedCount[Rank7];
	coefficients[EvalTTuneParamPawnPassedR7EG]=factorEG*passedCount[Rank7];

	// Misc
	int bishopPairCount=((int)(posGetBBPiece(pos, PieceWBishopL)!=0 && posGetBBPiece(pos, PieceWBishopD)!=0))-
	                    ((int)(posGetBBPiece(pos, PieceBBishopL)!=0 && posGetBBPiece(pos, PieceBBishopD)!=0));
	coefficients[EvalTTuneParamBishopPairMG]=factorMG*bishopPairCount;
	coefficients[EvalTTuneParamBishopPairEG]=factorEG*bishopPairCount;
}

void evaluateOutputCode(const char *path, const int *weights) {
	// Open file
	FILE *file=fopen(path, "w");
	if (file==NULL)
		return;

	// Generate code for all weights (parameters)
	fprintf(file, "TUNECONST VPair evalMaterial[PieceTypeNB]={\n");
	fprintf(file, "	[PieceTypeNone]={0,0},\n");
	fprintf(file, "	[PieceTypePawn]={%i,%i},\n", weights[EvalTTuneParamPawnMG], weights[EvalTTuneParamPawnEG]);
	fprintf(file, "	[PieceTypeKnight]={%i,%i},\n", weights[EvalTTuneParamKnightMG], weights[EvalTTuneParamKnightEG]);
	fprintf(file, "	[PieceTypeBishopL]={%i,%i},\n", weights[EvalTTuneParamBishopMG], weights[EvalTTuneParamBishopEG]);
	fprintf(file, "	[PieceTypeBishopD]={%i,%i},\n", weights[EvalTTuneParamBishopMG], weights[EvalTTuneParamBishopEG]);
	fprintf(file, "	[PieceTypeRook]={%i,%i},\n", weights[EvalTTuneParamRookMG], weights[EvalTTuneParamRookEG]);
	fprintf(file, "	[PieceTypeQueen]={%i,%i},\n", weights[EvalTTuneParamQueenMG], weights[EvalTTuneParamQueenEG]);
	fprintf(file, "	[PieceTypeKing]={600,-460} // these values exist soley to make PSTs look nicer (both sides always have exactly one king of course)\n");
	fprintf(file, "};\n");

	fprintf(file, "TUNECONST VPair evalPawnDoubled={%i,%i};\n", weights[EvalTTuneParamPawnDoubledMG], weights[EvalTTuneParamPawnDoubledEG]);
	fprintf(file, "TUNECONST VPair evalPawnIsolated={%i,%i};\n", weights[EvalTTuneParamPawnIsolatedMG], weights[EvalTTuneParamPawnIsolatedEG]);
	fprintf(file, "TUNECONST VPair evalPawnBlocked={%i,%i};\n", weights[EvalTTuneParamPawnBlockedMG], weights[EvalTTuneParamPawnBlockedEG]);
	fprintf(file, "TUNECONST VPair evalPawnPassed[RankNB]={\n");
	fprintf(file, "	{%i,%i},\n", 0, 0);
	fprintf(file, "	{%i,%i},\n", weights[EvalTTuneParamPawnPassedR2MG], weights[EvalTTuneParamPawnPassedR2EG]);
	fprintf(file, "	{%i,%i},\n", weights[EvalTTuneParamPawnPassedR3MG], weights[EvalTTuneParamPawnPassedR3EG]);
	fprintf(file, "	{%i,%i},\n", weights[EvalTTuneParamPawnPassedR4MG], weights[EvalTTuneParamPawnPassedR4EG]);
	fprintf(file, "	{%i,%i},\n", weights[EvalTTuneParamPawnPassedR5MG], weights[EvalTTuneParamPawnPassedR5EG]);
	fprintf(file, "	{%i,%i},\n", weights[EvalTTuneParamPawnPassedR6MG], weights[EvalTTuneParamPawnPassedR6EG]);
	fprintf(file, "	{%i,%i},\n", weights[EvalTTuneParamPawnPassedR7MG], weights[EvalTTuneParamPawnPassedR7EG]);
	fprintf(file, "	{%i,%i},\n", 0, 0);
	fprintf(file, "};\n");

	fprintf(file, "TUNECONST VPair evalKnightMob={%i,%i};\n", weights[EvalTTuneParamKnightMobMG], weights[EvalTTuneParamKnightMobEG]);
	fprintf(file, "TUNECONST VPair evalBishopPair={%i,%i};\n", weights[EvalTTuneParamBishopPairMG], weights[EvalTTuneParamBishopPairMG]);
	fprintf(file, "TUNECONST VPair evalBishopMob={%i,%i};\n", weights[EvalTTuneParamBishopMobMG], weights[EvalTTuneParamBishopMobMG]);
	fprintf(file, "TUNECONST VPair evalRookMobFile={%i,%i};\n", weights[EvalTTuneParamRookMobFileMG], weights[EvalTTuneParamRookMobFileMG]);
	fprintf(file, "TUNECONST VPair evalRookMobRank={%i,%i};\n", weights[EvalTTuneParamRookMobRankMG], weights[EvalTTuneParamRookMobRankMG]);

	// Close file
	fclose(file);
}

void evalClear(void) {
	// Clear hash tables.
	htableClear(evalPawnTable);
	htableClear(evalMatTable);
}

EvalMatType evalGetMatType(const Pos *pos) {
	// Grab hash entry for this position key
	HTableKey hTableKey=evalGetMatDataHTableKeyFromPos(pos);
	EvalMatData *entry=htableGrab(evalMatTable, hTableKey);

	// If not a match clear entry
	Key key=posGetMatKey(pos);
	if (entry->key!=key)
		entry->type=EvalMatTypeInvalid;

	// If no data already, compute
	if (entry->type==EvalMatTypeInvalid) {
		entry->key=key;
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
		PieceType type;
		for(type=PieceTypePawn;type<=PieceTypeKing;++type) {
			Piece piece=pieceMake(type, colour);
			BB pieceSet=posGetBBPiece(pos, piece);
			while(pieceSet) {
				Sq sq=bbScanReset(&pieceSet);
				evalVPairAddTo(&score, &evalPST[piece][sq]);
			}
		}
	}

	return score;
}

void evalPstDebug(void) {
	for(PieceType piece=PieceTypePawn; piece<=PieceTypeKing; ++piece) {
		if (piece==PieceTypeBishopD)
			continue;
		printf("%s:\n", pieceTypeToStr(piece));
		evalPstDraw(piece);
	}
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

VPair evalVPairMul(const VPair *a, int c) {
	VPair result={.mg=a->mg*c, .eg=a->eg*c};
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

	// Extra info
#ifdef EVALINFO
	printf("Evalution info:\n");
#endif

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

	// Extra info
#ifdef EVALINFO
	printf("    default eval (%i,%i)\n", score.mg, score.eg);
#endif

	// Material combination offset.
	evalVPairAddTo(&score, &data.matData.offset);

	// Extra info
#ifdef EVALINFO
	printf("    mat combo offset (%i,%i)\n", data.matData.offset.mg, data.matData.offset.eg);
#endif

	// Tempo bonus.
	if (posGetSTM(pos)==ColourWhite)
		evalVPairAddTo(&score, &evalTempoDefault);
	else
		evalVPairSubFrom(&score, &evalTempoDefault);

	// Extra info
#ifdef EVALINFO
	printf("    post adding tempo bonus (%i,%i) (bonus is (%i,%i))\n", score.mg, score.eg, evalTempoDefault.mg, evalTempoDefault.eg);
#endif

	// Interpolate score based on phase of the game and special material combination considerations.
	Score scalarScore=evalInterpolate(&data, &score);

	// Extra info
#ifdef EVALINFO
	printf("    interpolated scalar score %i (mg weight %i, eg weight %i)\n", scalarScore, data.matData.weightMG, data.matData.weightEG);
#endif

	// Add score offset
	scalarScore+=data.matData.scoreOffset;

	// Extra info
#ifdef EVALINFO
	printf("    post adding matdata score offset %i (offset value %i)\n", scalarScore, data.matData.scoreOffset);
#endif

	// Drag score towards 0 as we approach 50-move rule
	unsigned int halfMoves=posGetHalfMoveNumber(data.pos);
	assert(halfMoves<128);
	scalarScore=(((int)scalarScore)*evalHalfMoveFactors[halfMoves])/256;

	// Extra info
#ifdef EVALINFO
	printf("    post scaling for 50 move rule %i (half moves %u)\n", scalarScore, halfMoves);
#endif

	// Adjust for side to move
	if (posGetSTM(data.pos)==ColourBlack)
		scalarScore=-scalarScore;

	// Extra info
#ifdef EVALINFO
	printf("Final evaluation result: %i\n\n", scalarScore);
#endif

	return scalarScore;
}

VPair evaluateDefault(EvalData *data) {
	// Init
#ifdef EVALINFO
	VPair tempScore;
#endif
	const Pos *pos=data->pos;

	BB pieceSet;

	BB wp=posGetBBPiece(pos, PieceWPawn);
	BB bp=posGetBBPiece(pos, PieceBPawn);
	BB wpAttacks=bbForwardOne(bbWingify(wp), ColourWhite);
	BB bpAttacks=bbForwardOne(bbWingify(bp), ColourBlack);

	BB mobilityAllowed[ColourNB];
	mobilityAllowed[ColourWhite]=~(wp | posGetBBPiece(pos, PieceWKing) | bpAttacks);
	mobilityAllowed[ColourBlack]=~(bp | posGetBBPiece(pos, PieceBKing) | wpAttacks);

	// 'Global' calculations (includes pawns)
	VPair score=evaluateDefaultGlobal(data);

	// Extra info
#ifdef EVALINFO
	printf("        default global score (%i,%i)\n", score.mg, score.eg);
	tempScore=score;
#endif

	// Knight mobility
	pieceSet=posGetBBPiece(pos, PieceWKnight);
	while(pieceSet) {
		Sq sq=bbScanReset(&pieceSet);
		BB attacks=attacksKnight(sq);
		evalVPairAddMulTo(&score, &evalKnightMob, bbPopCount(attacks & mobilityAllowed[ColourWhite]));
	}
	pieceSet=posGetBBPiece(pos, PieceBKnight);
	while(pieceSet) {
		Sq sq=bbScanReset(&pieceSet);
		BB attacks=attacksKnight(sq);
		evalVPairSubMulFrom(&score, &evalKnightMob, bbPopCount(attacks & mobilityAllowed[ColourBlack]));
	}

	// Extra info
#ifdef EVALINFO
	printf("        knight mobility (%i,%i)\n", score.mg-tempScore.mg, score.eg-tempScore.eg);
	tempScore=score;
#endif

	// Bishop mobility
	BB bishopMobOcc[ColourNB];
	bishopMobOcc[ColourWhite]=(posGetBBAll(pos)^(posGetBBPiece(pos, PieceWBishopL)|posGetBBPiece(pos, PieceWBishopD)|posGetBBPiece(pos, PieceWQueen)));
	bishopMobOcc[ColourBlack]=(posGetBBAll(pos)^(posGetBBPiece(pos, PieceBBishopL)|posGetBBPiece(pos, PieceBBishopD)|posGetBBPiece(pos, PieceBQueen)));

	pieceSet=(posGetBBPiece(pos, PieceWBishopL)|posGetBBPiece(pos, PieceWBishopD));
	while(pieceSet) {
		Sq sq=bbScanReset(&pieceSet);
		BB attacks=attacksBishop(sq, bishopMobOcc[ColourWhite]);
		evalVPairAddMulTo(&score, &evalBishopMob, bbPopCount(attacks & mobilityAllowed[ColourWhite]));
	}
	pieceSet=(posGetBBPiece(pos, PieceBBishopL)|posGetBBPiece(pos, PieceBBishopD));
	while(pieceSet) {
		Sq sq=bbScanReset(&pieceSet);
		BB attacks=attacksBishop(sq, bishopMobOcc[ColourBlack]);
		evalVPairSubMulFrom(&score, &evalBishopMob, bbPopCount(attacks & mobilityAllowed[ColourBlack]));
	}

	// Extra info
#ifdef EVALINFO
	printf("        bishop mobility (%i,%i)\n", score.mg-tempScore.mg, score.eg-tempScore.eg);
	tempScore=score;
#endif

	// Rook mobilty
	BB rookMobOcc[ColourNB];
	rookMobOcc[ColourWhite]=(posGetBBAll(pos)^(posGetBBPiece(pos, PieceWRook)|posGetBBPiece(pos, PieceWQueen)));
	rookMobOcc[ColourBlack]=(posGetBBAll(pos)^(posGetBBPiece(pos, PieceBRook)|posGetBBPiece(pos, PieceBQueen)));

	pieceSet=posGetBBPiece(pos, PieceWRook);
	while(pieceSet) {
		Sq sq=bbScanReset(&pieceSet);
		BB attacks=attacksRook(sq, rookMobOcc[ColourWhite]);
		evalVPairAddMulTo(&score, &evalRookMobFile, bbPopCount(attacks & mobilityAllowed[ColourWhite] & bbFile(sqFile(sq))));
		evalVPairAddMulTo(&score, &evalRookMobRank, bbPopCount(attacks & mobilityAllowed[ColourWhite] & bbRank(sqRank(sq))));
	}
	pieceSet=posGetBBPiece(pos, PieceBRook);
	while(pieceSet) {
		Sq sq=bbScanReset(&pieceSet);
		BB attacks=attacksRook(sq, rookMobOcc[ColourBlack]);
		evalVPairSubMulFrom(&score, &evalRookMobFile, bbPopCount(attacks & mobilityAllowed[ColourBlack] & bbFile(sqFile(sq))));
		evalVPairSubMulFrom(&score, &evalRookMobRank, bbPopCount(attacks & mobilityAllowed[ColourBlack] & bbRank(sqRank(sq))));
	}

	// Extra info
#ifdef EVALINFO
	printf("        rook mobility (%i,%i)\n", score.mg-tempScore.mg, score.eg-tempScore.eg);
	tempScore=score;
#endif

	// Kings
	VPair kingScoreWhite=evaluateDefaultKing(data, ColourWhite);
	evalVPairAddTo(&score, &kingScoreWhite);

	VPair kingScoreBlack=evaluateDefaultKing(data, ColourBlack);
	evalVPairSubFrom(&score, &kingScoreBlack);

	// Extra info
#ifdef EVALINFO
	printf("        king scores: white (%i,%i), black (%i,%i)\n", kingScoreWhite.mg, kingScoreWhite.eg, kingScoreBlack.mg, kingScoreBlack.eg);
	tempScore=score;
#endif

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
			const Value bonus=1000; // makes displayed score more sensible
			Colour attacker=(posGetBBPiece(data->pos, PieceWPawn)!=BBNone ? ColourWhite : ColourBlack);
			data->matData.scoreOffset+=(attacker==ColourWhite ? ScoreHardWin+bonus : -(ScoreHardWin+bonus));
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
	Key key=posGetMatKey(pos);
	if (entry->key!=key)
		entry->type=EvalMatTypeInvalid;

	// If no type info, compute first.
	if (entry->type==EvalMatTypeInvalid) {
		entry->key=key;
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
	// Init data.
	assert(matData->key==posGetMatKey(pos));
	assert(matData->type!=EvalMatTypeInvalid);
	matData->computed=true;
	matData->offset=VPairZero;
	matData->scoreOffset=0;

	// Find weights for middlegame and endgame.
	unsigned wPawnCount=bbPopCount(posGetBBPiece(pos, PieceWPawn));
	unsigned bPawnCount=bbPopCount(posGetBBPiece(pos, PieceBPawn));
	unsigned wKnightCount=bbPopCount(posGetBBPiece(pos, PieceWKnight));
	unsigned bKnightCount=bbPopCount(posGetBBPiece(pos, PieceBKnight));
	unsigned wBishopLCount=bbPopCount(posGetBBPiece(pos, PieceWBishopL));
	unsigned bBishopLCount=bbPopCount(posGetBBPiece(pos, PieceBBishopL));
	unsigned wBishopDCount=bbPopCount(posGetBBPiece(pos, PieceWBishopD));
	unsigned bBishopDCount=bbPopCount(posGetBBPiece(pos, PieceBBishopD));
	unsigned wRookCount=bbPopCount(posGetBBPiece(pos, PieceWRook));
	unsigned bRookCount=bbPopCount(posGetBBPiece(pos, PieceBRook));
	unsigned wQueenCount=bbPopCount(posGetBBPiece(pos, PieceWQueen));
	unsigned bQueenCount=bbPopCount(posGetBBPiece(pos, PieceBQueen));

	unsigned pawnCount=wPawnCount+bPawnCount;
	unsigned minorCount=wKnightCount+wBishopLCount+wBishopDCount+bKnightCount+bBishopLCount+bBishopDCount;
	unsigned rookCount=wRookCount+bRookCount;
	unsigned queenCount=wQueenCount+bQueenCount;
	unsigned majorCount=rookCount+queenCount;

	unsigned pieceWeight=minorCount+2*rookCount+4*queenCount;
	assert(pieceWeight<128);

	unsigned wXKingsCount=bbPopCount(posGetBBColour(pos, ColourWhite))-1;
	unsigned bXKingsCount=bbPopCount(posGetBBColour(pos, ColourBlack))-1;
	unsigned totalXKingsCount=bbPopCount(posGetBBAll(pos))-2;

	matData->weightEG=evalWeightEGFactors[pieceWeight];
	matData->weightMG=256-matData->weightEG;

	// Specific material combinations.
	unsigned int factor=1024;
	switch(matData->type) {
		case EvalMatTypeInvalid:
			assert(false);
		break;
		case EvalMatTypeOther:
			assert(totalXKingsCount>0); // KvK has already been handled

			if (pawnCount==0) {
				// Pawnless.
				if (minorCount==totalXKingsCount) {
					// Minors only.
					switch(minorCount) {
						case 0: case 1:
							assert(false); // Should have already been handled.
						break;
						case 2:
							// Don't need to consider bishops of a single colour as these are
							// evaluated as insufficient material draws.
							assert(!(wBishopLCount==1 && bBishopLCount==1)); // KBvKB (same (light) coloured bishops)
							assert(!(wBishopDCount==1 && bBishopDCount==1)); // KBvKB (same (dark) coloured bishops)
							assert(wBishopLCount!=2 && wBishopDCount!=2); // KBBvK (same coloured (white) bishops)
							assert(bBishopLCount!=2 && bBishopDCount!=2); // KBBvK (same coloured (black) bishops)

							// Nor do we need to consider KNNvK as this is handled in other case statement.
							assert(wKnightCount!=2 && bKnightCount!=2); // KNNvK

							// Win for bishop pair and bishop + knight, draw for everything else.
							if ((wBishopLCount==1 && wBishopDCount==1) || // KBBvK
							    (bBishopLCount==1 && bBishopDCount==1) ||
							    ((wBishopLCount==1 || wBishopDCount==1) && wKnightCount==1) || // KBNvK
							    ((bBishopLCount==1 || bBishopDCount==1) && bKnightCount==1))
								factor/=2; // More difficult than material advantage suggests.
							else {
								assert((wBishopLCount==1 && bKnightCount==1) || // KBvKN
									   (wBishopDCount==1 && bKnightCount==1) ||
									   (bBishopLCount==1 && wKnightCount==1) ||
									   (bBishopDCount==1 && wKnightCount==1) ||
									   (wBishopLCount==1 && bBishopDCount==1) || // KBvKB (opposite bishops)
									   (wBishopDCount==1 && bBishopLCount==1) ||
									   (wKnightCount==1 && bKnightCount==1)); // KNvKN
								factor/=128; // All others are trivial draws.
							}
						break;
						case 3:
							if ((wBishopLCount==1 && wBishopDCount==1 && bKnightCount==1) || // KBBvKN (bishop pair)
							    (bBishopLCount==1 && bBishopDCount==1 && wKnightCount==1))
								factor/=2;
							else if ((wKnightCount==1 && wBishopLCount==1 && bBishopLCount==1) || // KBNvKB (same coloured bishops).
							         (wKnightCount==1 && wBishopDCount==1 && bBishopDCount==1) ||
							         (bKnightCount==1 && bBishopLCount==1 && wBishopLCount==1) ||
							         (bKnightCount==1 && bBishopDCount==1 && wBishopDCount==1) ||
							         (wKnightCount==1 && wBishopDCount==1 && bBishopLCount==1) || // KBNvKB (opposite coloured bishops).
							         (wKnightCount==1 && wBishopLCount==1 && bBishopDCount==1) ||
							         (bKnightCount==1 && bBishopDCount==1 && wBishopLCount==1) ||
							         (bKnightCount==1 && bBishopLCount==1 && wBishopDCount==1) ||
							         (wKnightCount==1 && wBishopLCount==1 && bKnightCount==1) || // KBNvN.
							         (wKnightCount==1 && wBishopDCount==1 && bKnightCount==1) ||
							         (bKnightCount==1 && bBishopLCount==1 && wKnightCount==1) ||
							         (bKnightCount==1 && bBishopDCount==1 && wKnightCount==1))
								factor/=16;
							else if ((wKnightCount==2 && bKnightCount==1) || // KNNvKN.
							         (bKnightCount==2 && wKnightCount==1) ||
							         (wKnightCount==2 && bBishopLCount==1) || // KNNvKB.
							         (wKnightCount==2 && bBishopDCount==1) ||
							         (bKnightCount==2 && wBishopLCount==1) ||
							         (bKnightCount==2 && wBishopDCount==1) ||
							         (wBishopLCount==1 && wBishopDCount==1 && bBishopLCount==1) || // KBBvKB (bishop pair).
							         (wBishopLCount==1 && wBishopDCount==1 && bBishopDCount==1) ||
							         (bBishopLCount==1 && bBishopDCount==1 && wBishopLCount==1) ||
							         (bBishopLCount==1 && bBishopDCount==1 && wBishopDCount==1) ||
							         (wBishopLCount==2 && bBishopDCount==1) ||  // KBBvKB (no bishop pair).
							         (wBishopDCount==2 && bBishopLCount==1) ||
							         (bBishopLCount==2 && wBishopDCount==1) ||
							         (bBishopDCount==2 && wBishopLCount==1) ||
							         (wBishopLCount==2 && bKnightCount==1) || // KBBvN (no bishop pair).
							         (wBishopDCount==2 && bKnightCount==1) ||
							         (bBishopLCount==2 && wKnightCount==1) ||
							         (bBishopDCount==2 && wKnightCount==1))
								factor/=32;
						break;
					}
				} else if (majorCount==totalXKingsCount) {
					// Majors only.

					// Single side with material should be easy win (at least a rook ahead).
					if (wXKingsCount==totalXKingsCount)
						matData->scoreOffset+=ScoreEasyWin;
					else if (bXKingsCount==totalXKingsCount)
						matData->scoreOffset-=ScoreEasyWin;
					else if (wXKingsCount==1 && bXKingsCount==1) {
						if ((wQueenCount==1 && bRookCount==1) ||
						    (bQueenCount==1 && wRookCount==1))
						    factor/=2;
					}
				} else {
					// Mix of major and minor pieces.
					switch(minorCount+rookCount+queenCount) {
						case 0: case 1:
							assert(false); // KvK already handled and single piece cannot be both minor and major.
						break;
						case 2:
							if ((wRookCount==1 && bBishopLCount==1) || // KRvKB
							    (wRookCount==1 && bBishopDCount==1) ||
							    (bRookCount==1 && wBishopLCount==1) ||
							    (bRookCount==1 && wBishopDCount==1) ||
							    (wRookCount==1 && bKnightCount==1) || // KRvKN
							    (bRookCount==1 && wKnightCount==1))
								factor/=4;
						break;
						case 3:
							if ((wQueenCount==1 && wBishopLCount==1 && bQueenCount==1) || // KQBvKQ
							    (wQueenCount==1 && wBishopDCount==1 && bQueenCount==1) ||
							    (bQueenCount==1 && bBishopLCount==1 && wQueenCount==1) ||
							    (bQueenCount==1 && bBishopDCount==1 && wQueenCount==1) ||
							    (wQueenCount==1 && bKnightCount==2) || // KQvKNN
							    (bQueenCount==1 && wKnightCount==2))
								factor/=8;
							else if ((wQueenCount==1 && wKnightCount==1 && bQueenCount==1) || // KQNvKQ
							         (bQueenCount==1 && bKnightCount==1 && wQueenCount==1) ||
							         (wRookCount==1 && wKnightCount==1 && bRookCount==1) || // KRNvKR
							         (bRookCount==1 && bKnightCount==1 && wRookCount==1) ||
							         (wQueenCount==1 && bBishopLCount==1 && bBishopDCount==1) || // KQvKBB (bishop pair)
							         (bQueenCount==1 && wBishopLCount==1 && wBishopDCount==1) ||
							         (wQueenCount==1 && bRookCount==1 && bBishopLCount==1) || // KQvKRB
							         (wQueenCount==1 && bRookCount==1 && bBishopDCount==1) ||
							         (bQueenCount==1 && wRookCount==1 && wBishopLCount==1) ||
							         (bQueenCount==1 && wRookCount==1 && wBishopDCount==1) ||
							         (wQueenCount==1 && bRookCount==1 && bKnightCount==1) || // KQvKRN
							         (bQueenCount==1 && wRookCount==1 && wKnightCount==1) ||
							         (wRookCount==1 && bBishopLCount==1 && bBishopDCount==1) || // KRvKBB (bishop pair)
							         (bRookCount==1 && wBishopLCount==1 && wBishopDCount==1) ||
							         (wRookCount==1 && wBishopLCount==1 && bRookCount==1) || // KRBvKR
							         (wRookCount==1 && wBishopDCount==1 && bRookCount==1) ||
							         (bRookCount==1 && bBishopLCount==1 && wRookCount==1) ||
							         (bRookCount==1 && bBishopDCount==1 && wRookCount==1))
								factor/=4;
						break;
					}
				}
			} else {
				if (totalXKingsCount==2) {
					if (wPawnCount==1 && (bBishopLCount==1 || bBishopDCount==1)) { // KBvKP
						matData->scoreOffset+=220; // side with bishop can at most draw so adjust score to try and reflect this situation better
						factor/=32; // almost always a draw unless pawn can promote immediately without capture - let search deal with it
					}
					if (bPawnCount==1 && (wBishopLCount==1 || wBishopDCount==1)) { // KBvKP
						matData->scoreOffset-=220; // side with bishop can at most draw so adjust score to try and reflect this situation better
						factor/=32; // almost always a draw unless pawn can promote immediately without capture - let search deal with it
					}
					if (wPawnCount==1 && bKnightCount==1) // KNvKP
						matData->scoreOffset+=250; // side with knight can at most draw so adjust score to try and reflect this situation better
					if (bPawnCount==1 && wKnightCount==1) // KNvKP
						matData->scoreOffset-=250; // side with knight can at most draw so adjust score to try and reflect this situation better
				} else if (totalXKingsCount==3) {
					if (wPawnCount==1 && bKnightCount==2) // KNNvKP
						matData->scoreOffset+=500; // side with knights can at most draw so adjust score to try and reflect this situation better
					if (bPawnCount==1 && wKnightCount==2) // KNNvKP
						matData->scoreOffset-=500; // side with knights can at most draw so adjust score to try and reflect this situation better
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
	if ((wBishopLCount>0 && wBishopDCount==0 && bBishopDCount>0 && bBishopLCount==0) ||
	    (wBishopDCount>0 && wBishopLCount==0 && bBishopLCount>0 && bBishopDCount==0)) {
		matData->weightMG=(matData->weightMG*evalOppositeBishopFactor.mg)/256;
		matData->weightEG=(matData->weightEG*evalOppositeBishopFactor.eg)/256;
	}

	// Knight pawn affinity.
	int knightAffW=wKnightCount*wPawnCount;
	int knightAffB=bKnightCount*bPawnCount;
	evalVPairAddMulTo(&matData->offset, &evalKnightPawnAffinity, knightAffW-knightAffB);

	// Rook pawn affinity.
	int rookAffW=wRookCount*wPawnCount;
	int rookAffB=bRookCount*bPawnCount;
	evalVPairAddMulTo(&matData->offset, &evalRookPawnAffinity, rookAffW-rookAffB);

	// Bishop pair bonus
	if (wBishopLCount>0 && wBishopDCount>0)
		evalVPairAddTo(&matData->offset, &evalBishopPair);
	if (bBishopLCount>0 && bBishopDCount>0)
		evalVPairSubFrom(&matData->offset, &evalBishopPair);
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
	evalVPairAddMulTo(&pawnData->score, &evalPawnBlocked, ((int)bbPopCount(blocked[ColourWhite]))-((int)bbPopCount(blocked[ColourBlack])));
}

void evalComputePawnData(const Pos *pos, EvalPawnData *pawnData) {
	// Init.
	pawnData->score=VPairZero;
	BB pawns[ColourNB], frontSpan[ColourNB], rearSpan[ColourNB], attacks[ColourNB];
	BB doubled[ColourNB], isolated[ColourNB];
	BB influence[ColourNB], fill[ColourNB];
	pawns[ColourWhite]=posGetBBPiece(pos, PieceWPawn); // All pawns of given colour
	pawns[ColourBlack]=posGetBBPiece(pos, PieceBPawn);
	fill[ColourWhite]=bbFileFill(pawns[ColourWhite]);
	fill[ColourBlack]=bbFileFill(pawns[ColourBlack]);
	frontSpan[ColourWhite]=bbNorthOne(bbNorthFill(pawns[ColourWhite])); // All squares infront of pawns of given colour
	frontSpan[ColourBlack]=bbSouthOne(bbSouthFill(pawns[ColourBlack]));
	rearSpan[ColourWhite]=bbSouthOne(bbSouthFill(pawns[ColourWhite])); // All squares behind pawns of given colour
	rearSpan[ColourBlack]=bbNorthOne(bbNorthFill(pawns[ColourBlack]));
	attacks[ColourWhite]=bbNorthOne(bbWingify(pawns[ColourWhite])); // All squares attacked by pawns of given colour
	attacks[ColourBlack]=bbSouthOne(bbWingify(pawns[ColourBlack]));
	influence[ColourWhite]=(frontSpan[ColourWhite] | bbWingify(frontSpan[ColourWhite])); // Squares which colour in question may attack or move to, now or in the future.
	influence[ColourBlack]=(frontSpan[ColourBlack] | bbWingify(frontSpan[ColourBlack]));

	doubled[ColourWhite]=(pawns[ColourWhite] & rearSpan[ColourWhite]);
	doubled[ColourBlack]=(pawns[ColourBlack] & rearSpan[ColourBlack]);
	isolated[ColourWhite]=(pawns[ColourWhite] & ~bbFileFill(attacks[ColourWhite]));
	isolated[ColourBlack]=(pawns[ColourBlack] & ~bbFileFill(attacks[ColourBlack]));

	pawnData->pawns[ColourWhite]=pawns[ColourWhite];
	pawnData->pawns[ColourBlack]=pawns[ColourBlack];
	pawnData->passed[ColourWhite]=(pawns[ColourWhite] & ~(doubled[ColourWhite] | influence[ColourBlack]));
	pawnData->passed[ColourBlack]=(pawns[ColourBlack] & ~(doubled[ColourBlack] | influence[ColourWhite]));
	pawnData->semiOpenFiles[ColourWhite]=(fill[ColourBlack] & ~fill[ColourWhite]);
	pawnData->semiOpenFiles[ColourBlack]=(fill[ColourWhite] & ~fill[ColourBlack]);
	pawnData->outposts[ColourWhite]=(attacks[ColourWhite] & (bbRank(Rank4)|bbRank(Rank5)|bbRank(Rank6)|bbRank(Rank7)) & ~bbWingify(frontSpan[ColourBlack]));
	pawnData->outposts[ColourBlack]=(attacks[ColourBlack] & (bbRank(Rank5)|bbRank(Rank4)|bbRank(Rank3)|bbRank(Rank2)) & ~bbWingify(frontSpan[ColourWhite]));
	pawnData->openFiles=~(fill[ColourWhite] | fill[ColourBlack]);

	// Outposts
	int outpostRelativeCount=bbPopCount(pawnData->outposts[ColourWhite])-bbPopCount(pawnData->outposts[ColourBlack]);
	evalVPairAddMulTo(&pawnData->score, &evalOutpostSq, outpostRelativeCount);

	// Doubled and isolated pawns
	int doubledCount=((int)bbPopCount(doubled[ColourWhite]))-((int)bbPopCount(doubled[ColourBlack]));
	evalVPairAddMulTo(&pawnData->score, &evalPawnDoubled, doubledCount);
	int isolatedCount=((int)bbPopCount(isolated[ColourWhite]))-((int)bbPopCount(isolated[ColourBlack]));
	evalVPairAddMulTo(&pawnData->score, &evalPawnIsolated, isolatedCount);

	// Passed pawns
	BB pieceSet;
	pieceSet=pawnData->passed[ColourWhite];
	while(pieceSet) {
		Sq sq=bbScanReset(&pieceSet);
		evalVPairAddTo(&pawnData->score, &evalPassedPawn[sqRank(sq)]);
	}
	pieceSet=pawnData->passed[ColourBlack];
	while(pieceSet) {
		Sq sq=bbScanReset(&pieceSet);
		evalVPairSubFrom(&pawnData->score, &evalPassedPawn[sqRank(sqFlip(sq))]);
	}
}

HTableKey evalGetPawnDataHTableKeyFromPos(const Pos *pos) {
	assert(pos!=NULL);

	STATICASSERT(HTableKeySize==32);
	return posGetPawnKey(pos)&0xFFFFFFFFu;
}

VPair evaluateDefaultGlobal(EvalData *data) {
	assert(data!=NULL);

#ifdef EVALINFO
	VPair tempScore=VPairZero;
#endif
	const Pos *pos=data->pos;

	// Start with incrementally updated PST score.
	VPair score=posGetPstScore(pos);

	// Extra info
#ifdef EVALINFO
	printf("            pst scores: (%i,%i)\n", score.mg-tempScore.mg, score.eg-tempScore.eg);
	tempScore=score;
#endif

	// Pawns
	evalGetPawnData(pos, &data->pawnData);
	evalVPairAddTo(&score, &data->pawnData.score);

	// Extra info
#ifdef EVALINFO
	printf("            pawns scores: (%i,%i)\n", score.mg-tempScore.mg, score.eg-tempScore.eg);
	tempScore=score;
#endif

	// Outposts
	int knightOutpostCountW=bbPopCount(data->pawnData.outposts[ColourWhite] & posGetBBPiece(pos, PieceWKnight));
	int knightOutpostCountB=bbPopCount(data->pawnData.outposts[ColourBlack] & posGetBBPiece(pos, PieceBKnight));
	evalVPairAddMulTo(&score, &evalOutpostKnight, (knightOutpostCountW-knightOutpostCountB));

	// Extra info
#ifdef EVALINFO
	printf("            knight outposts: (%i,%i)\n", score.mg-tempScore.mg, score.eg-tempScore.eg);
	tempScore=score;
#endif

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

	// Extra info
#ifdef EVALINFO
	printf("            rook stuff: (%i,%i)\n", score.mg-tempScore.mg, score.eg-tempScore.eg);
	tempScore=score;
#endif

	// King castling 'mobility'.
	CastRights castRights=posGetCastRights(pos);
	if (castRights.rookSq[ColourWhite][CastSideA]!=SqInvalid)
		evalVPairAddTo(&score, &evalKingCastlingMobility);
	if (castRights.rookSq[ColourWhite][CastSideH]!=SqInvalid)
		evalVPairAddTo(&score, &evalKingCastlingMobility);
	if (castRights.rookSq[ColourBlack][CastSideA]!=SqInvalid)
		evalVPairSubFrom(&score, &evalKingCastlingMobility);
	if (castRights.rookSq[ColourBlack][CastSideH]!=SqInvalid)
		evalVPairSubFrom(&score, &evalKingCastlingMobility);

	// Extra info
#ifdef EVALINFO
	printf("            king castling mobility: (%i,%i)\n", score.mg-tempScore.mg, score.eg-tempScore.eg);
	tempScore=score;
#endif

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

#ifdef TUNE
void evalSetValue(void *varPtr, long long value) {
	// Set value.
	Value *var=(Value *)varPtr;
	*var=value;

	// Hack for bishops.
	if (var==&evalMaterial[PieceTypeBishopL].mg)
		evalMaterial[PieceTypeBishopD].mg=value;
	else if (var==&evalMaterial[PieceTypeBishopL].eg)
		evalMaterial[PieceTypeBishopD].eg=value;

	// Recalculate dervied values (such as passed pawn table).
	evalRecalc();
}

bool evalOptionNewVPair(const char *name, VPair *score, Value min, Value max) {
	return uciOptionNewSpinF("%sMG", &evalSetValue, &score->mg, min, max, score->mg, name) &&
	       uciOptionNewSpinF("%sEG", &evalSetValue, &score->eg, min, max, score->eg, name);
}

bool evalOptionNewVPairF(const char *nameFormat, VPair *score, Value min, Value max, ...) {
	char nameFormat1[1024]; // TODO: avoid hardcoded size
	char nameFormat2[1024]; // TODO: avoid hardcoded size
	sprintf(nameFormat1, "%sMG", nameFormat);
	sprintf(nameFormat2, "%sEG", nameFormat);

	va_list ap1, ap2;
	va_start(ap1, max);
	va_copy(ap2, ap1);

	bool result=true;
	result&=!uciOptionNewSpinFV(nameFormat1, &evalSetValue, &score->mg, min, max, score->mg, ap1);
	result&=!uciOptionNewSpinFV(nameFormat2, &evalSetValue, &score->eg, min, max, score->eg, ap2);

	va_end(ap2);
	va_end(ap1);

	return result;
}

#endif

void evalRecalc(void) {
	PieceType pieceType;

	// Add material to white PSTs
	for(pieceType=PieceTypePawn; pieceType<=PieceTypeKing; ++pieceType) {
		Piece piece=pieceMake(pieceType, ColourWhite);
		Sq sq;
		for(sq=0; sq<SqNB; ++sq)
			evalVPairAddTo(&evalPST[piece][sq], &evalMaterial[pieceType]);
	}

	// Copy light bishop PSTs into dark bishop PSTs
	for(Sq sq=0; sq<SqNB; ++sq)
		evalPST[PieceTypeBishopD][sq]=evalPST[PieceTypeBishopL][sq];

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

	// Clear now-invalid material and pawn tables etc.
	evalClear();

	// Verify eval weights are all sensible and consistent.
	evalVerify();
}

void evalVerify(void) {
	// Check light/dark bishop entries match
	assert(evalMaterial[PieceTypeBishopL].mg==evalMaterial[PieceTypeBishopD].mg);
	assert(evalMaterial[PieceTypeBishopL].eg==evalMaterial[PieceTypeBishopD].eg);
	for(Sq sq=0; sq<SqNB; ++sq) {
		assert(evalPST[PieceWBishopL][sq].mg==evalPST[PieceWBishopD][sq].mg);
		assert(evalPST[PieceWBishopL][sq].eg==evalPST[PieceWBishopD][sq].eg);
		assert(evalPST[PieceBBishopL][sq].mg==evalPST[PieceBBishopD][sq].mg);
		assert(evalPST[PieceBBishopL][sq].eg==evalPST[PieceBBishopD][sq].eg);
	}

	// Check PSTs are symmetrical.
	PieceType pieceType;
	for(pieceType=PieceTypePawn; pieceType<=PieceTypeKing; ++pieceType)
		for(Sq sq=0; sq<SqNB; ++sq) {
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

	// Collect pos data
	BB bbWhite=posGetBBColour(pos, ColourWhite);
	BB bbWhiteXKings=(bbWhite^posGetBBPiece(pos, PieceWKing));
	BB bbBlack=posGetBBColour(pos, ColourBlack);
	BB bbBlackXKings=(bbBlack^posGetBBPiece(pos, PieceBKing));

	BB occXKings=bbWhiteXKings|bbBlackXKings;

	// If only pieces are bishops and all share same colour squares, draw.
	BB bishopsL=(posGetBBPiece(pos, PieceWBishopL)|posGetBBPiece(pos, PieceBBishopL));
	BB bishopsD=(posGetBBPiece(pos, PieceWBishopD)|posGetBBPiece(pos, PieceBBishopD));
	if (occXKings==bishopsL || occXKings==bishopsD)
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
			if (occXKings==posGetBBPiece(pos, PieceWKnight) || occXKings==posGetBBPiece(pos, PieceBKnight))
				return EvalMatTypeDraw; // KNvK
			else if (occXKings==posGetBBPiece(pos, PieceWPawn) || occXKings==posGetBBPiece(pos, PieceBPawn))
				return EvalMatTypeKPvK;
		break;
		case 4:
			if (occXKings==posGetBBPiece(pos, PieceWKnight) || occXKings==posGetBBPiece(pos, PieceBKnight))
				return EvalMatTypeKNNvK;
		break;
	}

	// KBPvK (any positive number of pawns and any positive number of same coloured bishops).
	if (occXKings==bbWhiteXKings) { // only white material?
		BB pawns=posGetBBPiece(pos, PieceWPawn);
		if ((bbWhiteXKings&pawns)!=BBNone && (bbWhiteXKings^pawns)!=BBNone) { // does white even have any pawns and non-pawns?
			if ((bbWhiteXKings^pawns)==posGetBBPiece(pos, PieceWBishopL))
				return EvalMatTypeKBPvK;
			if ((bbWhiteXKings^pawns)==posGetBBPiece(pos, PieceWBishopD))
				return EvalMatTypeKBPvK;
		}
	} else if (occXKings==bbBlackXKings) { // only black material?
		BB pawns=posGetBBPiece(pos, PieceBPawn);
		if ((bbBlackXKings&pawns)!=BBNone && (bbBlackXKings^pawns)!=BBNone) { // does black even have any pawns and non-pawns?
			if ((bbBlackXKings^pawns)==posGetBBPiece(pos, PieceBBishopL))
				return EvalMatTypeKBPvK;
			if ((bbBlackXKings^pawns)==posGetBBPiece(pos, PieceBBishopD))
				return EvalMatTypeKBPvK;
		}
	}

	// Other combination.
	return EvalMatTypeOther;

#	undef MASK
#	undef MAKE
}

void evalPstDraw(PieceType type) {
	for(int y=7; y>=0; --y) {
		for(int x=0; x<8; ++x) {
			Sq sq=sqMake(x, y);
			printf("%5i ", evalPST[pieceMake(type, ColourWhite)][sq].mg-evalMaterial[type].mg);
		}
		printf("     ");
		for(int x=0; x<8; ++x) {
			Sq sq=sqMake(x, y);
			printf("%5i ", evalPST[pieceMake(type, ColourWhite)][sq].eg-evalMaterial[type].eg);
		}
		printf("\n");
	}
	printf("\n");
}
