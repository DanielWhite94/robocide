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
	EvalTTuneParamKnightPawnAffinityMG,
	EvalTTuneParamKnightPawnAffinityEG,
	EvalTTuneParamRookPawnAffinityMG,
	EvalTTuneParamRookPawnAffinityEG,
	EvalTTuneParamRookOpenFileMG,
	EvalTTuneParamRookOpenFileEG,
	EvalTTuneParamRookSemiOpenFileMG,
	EvalTTuneParamRookSemiOpenFileEG,
	EvalTTuneParamRookOn7thMG,
	EvalTTuneParamRookOn7thEG,
	EvalTTuneParamRookTrappedMG,
	EvalTTuneParamRookTrappedEG,
	EvalTTuneParamKingShieldCloseMG,
	EvalTTuneParamKingShieldCloseEG,
	EvalTTuneParamKingShieldFarMG,
	EvalTTuneParamKingShieldFarEG,
	EvalTTuneParamKingNearPasser1MG,
	EvalTTuneParamKingNearPasser1EG,
	EvalTTuneParamKingNearPasser2MG,
	EvalTTuneParamKingNearPasser2EG,
	EvalTTuneParamKingNearPasser3MG,
	EvalTTuneParamKingNearPasser3EG,
	EvalTTuneParamKingNearPasser4MG,
	EvalTTuneParamKingNearPasser4EG,
	EvalTTuneParamKingNearPasser5MG,
	EvalTTuneParamKingNearPasser5EG,
	EvalTTuneParamKingNearPasser6MG,
	EvalTTuneParamKingNearPasser6EG,
	EvalTTuneParamKingNearPasser7MG,
	EvalTTuneParamKingNearPasser7EG,
	EvalTTuneParamKingCastlingMobilityMG,
	EvalTTuneParamKingCastlingMobilityEG,
	EvalTTuneParamOutpostSqMG,
	EvalTTuneParamOutpostSqEG,
	EvalTTuneParamOutpostKnightMG,
	EvalTTuneParamOutpostKnightEG,
	EvalTTuneParamTempoDefaultMG,
	EvalTTuneParamTempoDefaultEG,
	// PSTS use two sets (MG/EG) of 32 parameters from A1 to D8 (first four squares of each rank, sq is mirrored if not on left side)
	EvalTTuneParamPstPawnMGBase,
	EvalTTuneParamPstPawnMGEnd=EvalTTuneParamPstPawnMGBase+23, // don't need ranks 1 & 8 for pawns
	EvalTTuneParamPstPawnEGBase,
	EvalTTuneParamPstPawnEGEnd=EvalTTuneParamPstPawnEGBase+23, // don't need ranks 1 & 8 for pawns
	EvalTTuneParamPstKnightMGBase,
	EvalTTuneParamPstKnightMGEnd=EvalTTuneParamPstKnightMGBase+31,
	EvalTTuneParamPstKnightEGBase,
	EvalTTuneParamPstKnightEGEnd=EvalTTuneParamPstKnightEGBase+31,
	EvalTTuneParamPstBishopMGBase,
	EvalTTuneParamPstBishopMGEnd=EvalTTuneParamPstBishopMGBase+31,
	EvalTTuneParamPstBishopEGBase,
	EvalTTuneParamPstBishopEGEnd=EvalTTuneParamPstBishopEGBase+31,
	EvalTTuneParamPstRookMGBase,
	EvalTTuneParamPstRookMGEnd=EvalTTuneParamPstRookMGBase+31,
	EvalTTuneParamPstRookEGBase,
	EvalTTuneParamPstRookEGEnd=EvalTTuneParamPstRookEGBase+31,
	EvalTTuneParamPstQueenMGBase,
	EvalTTuneParamPstQueenMGEnd=EvalTTuneParamPstQueenMGBase+31,
	EvalTTuneParamPstQueenEGBase,
	EvalTTuneParamPstQueenEGEnd=EvalTTuneParamPstQueenEGBase+31,
	EvalTTuneParamPstKingMGBase,
	EvalTTuneParamPstKingMGEnd=EvalTTuneParamPstKingMGBase+31,
	EvalTTuneParamPstKingEGBase,
	EvalTTuneParamPstKingEGEnd=EvalTTuneParamPstKingEGBase+31,
	EvalTTuneParamNB,
} EvalTTuneParam;

const unsigned evalTTuneParamPstMGBase[PieceTypeNB]={
	[PieceTypePawn]=EvalTTuneParamPstPawnMGBase,
	[PieceTypeKnight]=EvalTTuneParamPstKnightMGBase,
	[PieceTypeBishopL]=EvalTTuneParamPstBishopMGBase,
	[PieceTypeBishopD]=EvalTTuneParamPstBishopMGBase,
	[PieceTypeRook]=EvalTTuneParamPstRookMGBase,
	[PieceTypeQueen]=EvalTTuneParamPstQueenMGBase,
	[PieceTypeKing]=EvalTTuneParamPstKingMGBase,
};

const unsigned evalTTuneParamPstEGBase[PieceTypeNB]={
	[PieceTypePawn]=EvalTTuneParamPstPawnEGBase,
	[PieceTypeKnight]=EvalTTuneParamPstKnightEGBase,
	[PieceTypeBishopL]=EvalTTuneParamPstBishopEGBase,
	[PieceTypeBishopD]=EvalTTuneParamPstBishopEGBase,
	[PieceTypeRook]=EvalTTuneParamPstRookEGBase,
	[PieceTypeQueen]=EvalTTuneParamPstQueenEGBase,
	[PieceTypeKing]=EvalTTuneParamPstKingEGBase,
};

////////////////////////////////////////////////////////////////////////////////
// Tunable values.
////////////////////////////////////////////////////////////////////////////////

// Values which support Texel Tuning are defined in generated code.
#include "evaltunable.h"

// The following values can be tuned through UCI options but not the Texel Tuning interface
TUNECONST VPair evalOppositeBishopFactor={256,192}; // /256.
TUNECONST Value evalHalfMoveFactor=2048;
TUNECONST Value evalWeightFactor=151;

////////////////////////////////////////////////////////////////////////////////
// Derived values
////////////////////////////////////////////////////////////////////////////////

int evalHalfMoveFactors[128];
uint8_t evalWeightEGFactors[128];

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

Sq evalTTunePstIndexToSq(unsigned index);
unsigned evalTTunePstSqToIndex(Sq sq);
Sq evalTTunePawnPstIndexToSq(unsigned index);
unsigned evalTTunePawnPstSqToIndex(Sq sq);

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
	evalOptionNewVPair("KingCastlingMobility", &evalKingCastlingMobility, 0, 200);
	evalOptionNewVPair("OutpostSq", &evalOutpostSq, 0, 500);
	evalOptionNewVPair("OutpostKnight", &evalOutpostKnight, 0, 1000);
	evalOptionNewVPair("Tempo", &evalTempoDefault, 0, 100);
	uciOptionNewSpin("HalfMoveFactor", &evalSetValue, &evalHalfMoveFactor, 1, 4096, evalHalfMoveFactor);
	uciOptionNewSpin("WeightFactor", &evalSetValue, &evalWeightFactor, 1, 512, evalWeightFactor);
#	endif

	// Setup Texel Tuning parameters
	ttuneAddParameter(EvalTTuneParamPawnMG, "PawnMG", evalMaterial[PieceTypePawn].mg, false); // this is the one fixed value everything else is relative
	ttuneAddParameter(EvalTTuneParamPawnEG, "PawnEG", evalMaterial[PieceTypePawn].eg, true);
	ttuneAddParameter(EvalTTuneParamKnightMG, "KnightMG", evalMaterial[PieceTypeKnight].mg, true);
	ttuneAddParameter(EvalTTuneParamKnightEG, "KnightEG", evalMaterial[PieceTypeKnight].eg, true);
	ttuneAddParameter(EvalTTuneParamBishopMG, "BishopMG", evalMaterial[PieceTypeBishopL].mg, true);
	ttuneAddParameter(EvalTTuneParamBishopEG, "BishopEG", evalMaterial[PieceTypeBishopL].eg, true);
	ttuneAddParameter(EvalTTuneParamRookMG, "RookMG", evalMaterial[PieceTypeRook].mg, true);
	ttuneAddParameter(EvalTTuneParamRookEG, "RookEG", evalMaterial[PieceTypeRook].eg, true);
	ttuneAddParameter(EvalTTuneParamQueenMG, "QueenMG", evalMaterial[PieceTypeQueen].mg, true);
	ttuneAddParameter(EvalTTuneParamQueenEG, "QueenEG", evalMaterial[PieceTypeQueen].eg, true);
	ttuneAddParameter(EvalTTuneParamKnightMobMG, "KnightMobMG", evalKnightMob.mg, true);
	ttuneAddParameter(EvalTTuneParamKnightMobEG, "KnightMobEG", evalKnightMob.eg, true);
	ttuneAddParameter(EvalTTuneParamBishopMobMG, "BishopMobMG", evalBishopMob.mg, true);
	ttuneAddParameter(EvalTTuneParamBishopMobEG, "BishopMobEG", evalBishopMob.eg, true);
	ttuneAddParameter(EvalTTuneParamRookMobFileMG, "RookMobFileMG", evalRookMobFile.mg, true);
	ttuneAddParameter(EvalTTuneParamRookMobFileEG, "RookMobFileEG", evalRookMobFile.eg, true);
	ttuneAddParameter(EvalTTuneParamRookMobRankMG, "RookMobRankMG", evalRookMobRank.mg, true);
	ttuneAddParameter(EvalTTuneParamRookMobRankEG, "RookMobRankEG", evalRookMobRank.eg, true);

	char str[32];
	for(unsigned i=0; i<24; ++i) {
		Sq sq=evalTTunePawnPstIndexToSq(i);
		sprintf(str, "PawnPst%c%cMG", fileToChar(sqFile(sq))-'a'+'A', rankToChar(sqRank(sq)));
		ttuneAddParameter(EvalTTuneParamPstPawnMGBase+i, str, evalPST[PieceTypePawn][sq].mg-evalMaterial[PieceTypePawn].mg, true);
		sprintf(str, "PawnPst%c%cEG", fileToChar(sqFile(sq))-'a'+'A', rankToChar(sqRank(sq)));
		ttuneAddParameter(EvalTTuneParamPstPawnEGBase+i, str, evalPST[PieceTypePawn][sq].eg-evalMaterial[PieceTypePawn].eg, true);
	}
	for(unsigned t=PieceTypeKnight; t<=PieceTypeKing; ++t) {
		for(unsigned i=0; i<32; ++i) {
			Sq sq=evalTTunePstIndexToSq(i);
			sprintf(str, "%sPst%c%cMG", pieceTypeToStr(t), fileToChar(sqFile(sq))-'a'+'A', rankToChar(sqRank(sq)));
			ttuneAddParameter(evalTTuneParamPstMGBase[t]+i, str, evalPST[PieceTypeKnight][sq].mg-evalMaterial[PieceTypeKnight].mg, true);
			sprintf(str, "%sPst%c%cEG", pieceTypeToStr(t), fileToChar(sqFile(sq))-'a'+'A', rankToChar(sqRank(sq)));
			ttuneAddParameter(evalTTuneParamPstEGBase[t]+i, str, evalPST[PieceTypeKnight][sq].eg-evalMaterial[PieceTypeKnight].eg, true);
		}
	}

	ttuneAddParameter(EvalTTuneParamPawnDoubledMG, "PawnDoubledMG", evalPawnDoubled.mg, true);
	ttuneAddParameter(EvalTTuneParamPawnDoubledEG, "PawnDoubledEG", evalPawnDoubled.eg, true);
	ttuneAddParameter(EvalTTuneParamPawnIsolatedMG, "PawnIsolatedMG", evalPawnIsolated.mg, true);
	ttuneAddParameter(EvalTTuneParamPawnIsolatedEG, "PawnIsolatedEG", evalPawnIsolated.eg, true);
	ttuneAddParameter(EvalTTuneParamPawnBlockedMG, "PawnBlockedMG", evalPawnBlocked.mg, true);
	ttuneAddParameter(EvalTTuneParamPawnBlockedEG, "PawnBlockedEG", evalPawnBlocked.mg, true);
	ttuneAddParameter(EvalTTuneParamPawnPassedR2MG, "PawnPassedR2MG", evalPawnPassed[Rank2].mg, true);
	ttuneAddParameter(EvalTTuneParamPawnPassedR2EG, "PawnPassedR2EG", evalPawnPassed[Rank2].eg, true);
	ttuneAddParameter(EvalTTuneParamPawnPassedR3MG, "PawnPassedR3MG", evalPawnPassed[Rank3].mg, true);
	ttuneAddParameter(EvalTTuneParamPawnPassedR3EG, "PawnPassedR3EG", evalPawnPassed[Rank3].eg, true);
	ttuneAddParameter(EvalTTuneParamPawnPassedR4MG, "PawnPassedR4MG", evalPawnPassed[Rank4].mg, true);
	ttuneAddParameter(EvalTTuneParamPawnPassedR4EG, "PawnPassedR4EG", evalPawnPassed[Rank4].eg, true);
	ttuneAddParameter(EvalTTuneParamPawnPassedR5MG, "PawnPassedR5MG", evalPawnPassed[Rank5].mg, true);
	ttuneAddParameter(EvalTTuneParamPawnPassedR5EG, "PawnPassedR5EG", evalPawnPassed[Rank5].eg, true);
	ttuneAddParameter(EvalTTuneParamPawnPassedR6MG, "PawnPassedR6MG", evalPawnPassed[Rank6].mg, true);
	ttuneAddParameter(EvalTTuneParamPawnPassedR6EG, "PawnPassedR6EG", evalPawnPassed[Rank6].eg, true);
	ttuneAddParameter(EvalTTuneParamPawnPassedR7MG, "PawnPassedR7MG", evalPawnPassed[Rank7].mg, true);
	ttuneAddParameter(EvalTTuneParamPawnPassedR7EG, "PawnPassedR7EG", evalPawnPassed[Rank7].eg, true);
	ttuneAddParameter(EvalTTuneParamBishopPairMG, "BishopPairMG", evalBishopPair.mg, true);
	ttuneAddParameter(EvalTTuneParamBishopPairEG, "BishopPairEG", evalBishopPair.eg, true);
	ttuneAddParameter(EvalTTuneParamKnightPawnAffinityMG, "KnightPawnAffinityMG", evalKnightPawnAffinity.mg, true);
	ttuneAddParameter(EvalTTuneParamKnightPawnAffinityEG, "KnightPawnAffinityEG", evalKnightPawnAffinity.eg, true);
	ttuneAddParameter(EvalTTuneParamRookPawnAffinityMG, "RookPawnAffinityMG", evalRookPawnAffinity.mg, true);
	ttuneAddParameter(EvalTTuneParamRookPawnAffinityEG, "RookPawnAffinityEG", evalRookPawnAffinity.eg, true);
	ttuneAddParameter(EvalTTuneParamRookOpenFileMG, "RookOpenFileMG", evalRookOpenFile.mg, true);
	ttuneAddParameter(EvalTTuneParamRookOpenFileEG, "RookOpenFileEG", evalRookOpenFile.eg, true);
	ttuneAddParameter(EvalTTuneParamRookSemiOpenFileMG, "RookSemiOpenFileMG", evalRookSemiOpenFile.mg, true);
	ttuneAddParameter(EvalTTuneParamRookSemiOpenFileEG, "RookSemiOpenFileEG", evalRookSemiOpenFile.eg, true);
	ttuneAddParameter(EvalTTuneParamRookOn7thMG, "RookOn7thMG", evalRookOn7th.mg, true);
	ttuneAddParameter(EvalTTuneParamRookOn7thEG, "RookOn7thEG", evalRookOn7th.eg, true);
	ttuneAddParameter(EvalTTuneParamRookTrappedMG, "RookTrappedMG", evalRookTrapped.mg, true);
	ttuneAddParameter(EvalTTuneParamRookTrappedEG, "RookTrappedEG", evalRookTrapped.eg, true);
	ttuneAddParameter(EvalTTuneParamKingShieldCloseMG, "KingShieldCloseMG", evalKingShieldClose.mg, true);
	ttuneAddParameter(EvalTTuneParamKingShieldCloseEG, "KingShieldCloseEG", evalKingShieldClose.eg, true);
	ttuneAddParameter(EvalTTuneParamKingShieldFarMG, "KingShieldFarMG", evalKingShieldFar.mg, true);
	ttuneAddParameter(EvalTTuneParamKingShieldFarEG, "KingShieldFarEG", evalKingShieldFar.eg, true);
	ttuneAddParameter(EvalTTuneParamKingNearPasser1MG, "KingNearPasser1MG", evalKingNearPasser[1].mg, true);
	ttuneAddParameter(EvalTTuneParamKingNearPasser1EG, "KingNearPasser1EG", evalKingNearPasser[1].eg, true);
	ttuneAddParameter(EvalTTuneParamKingNearPasser2MG, "KingNearPasser2MG", evalKingNearPasser[2].mg, true);
	ttuneAddParameter(EvalTTuneParamKingNearPasser2EG, "KingNearPasser2EG", evalKingNearPasser[2].eg, true);
	ttuneAddParameter(EvalTTuneParamKingNearPasser3MG, "KingNearPasser3MG", evalKingNearPasser[3].mg, true);
	ttuneAddParameter(EvalTTuneParamKingNearPasser3EG, "KingNearPasser3EG", evalKingNearPasser[3].eg, true);
	ttuneAddParameter(EvalTTuneParamKingNearPasser4MG, "KingNearPasser4MG", evalKingNearPasser[4].mg, true);
	ttuneAddParameter(EvalTTuneParamKingNearPasser4EG, "KingNearPasser4EG", evalKingNearPasser[4].eg, true);
	ttuneAddParameter(EvalTTuneParamKingNearPasser5MG, "KingNearPasser5MG", evalKingNearPasser[5].mg, true);
	ttuneAddParameter(EvalTTuneParamKingNearPasser5EG, "KingNearPasser5EG", evalKingNearPasser[5].eg, true);
	ttuneAddParameter(EvalTTuneParamKingNearPasser6MG, "KingNearPasser6MG", evalKingNearPasser[6].mg, true);
	ttuneAddParameter(EvalTTuneParamKingNearPasser6EG, "KingNearPasser6EG", evalKingNearPasser[6].eg, true);
	ttuneAddParameter(EvalTTuneParamKingNearPasser7MG, "KingNearPasser7MG", evalKingNearPasser[7].mg, true);
	ttuneAddParameter(EvalTTuneParamKingNearPasser7EG, "KingNearPasser7EG", evalKingNearPasser[7].eg, true);
	ttuneAddParameter(EvalTTuneParamKingCastlingMobilityMG, "KingCastlingMobilityMG", evalKingCastlingMobility.mg, true);
	ttuneAddParameter(EvalTTuneParamKingCastlingMobilityEG, "KingCastlingMobilityEG", evalKingCastlingMobility.eg, true);
	ttuneAddParameter(EvalTTuneParamOutpostSqMG, "OutpostSqMG", evalOutpostSq.mg, true);
	ttuneAddParameter(EvalTTuneParamOutpostSqEG, "OutpostSqEG", evalOutpostSq.eg, true);
	ttuneAddParameter(EvalTTuneParamOutpostKnightMG, "OutpostKnightMG", evalOutpostKnight.mg, true);
	ttuneAddParameter(EvalTTuneParamOutpostKnightEG, "OutpostKnightEG", evalOutpostKnight.eg, true);
	ttuneAddParameter(EvalTTuneParamTempoDefaultMG, "TempoDefaultMG", evalTempoDefault.mg, true);
	ttuneAddParameter(EvalTTuneParamTempoDefaultEG, "TempoDefaultEG", evalTempoDefault.eg, true);
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

void evaluateCoefficients(const Pos *pos, float *coefficients) {
	// Set all coefficents to 0 initially
	for(unsigned i=0; i<EvalTTuneParamNB; ++i)
		coefficients[i]=0.0;

	// Precomputed info
	BB wp=posGetBBPiece(pos, PieceWPawn);
	BB bp=posGetBBPiece(pos, PieceBPawn);
	BB wn=posGetBBPiece(pos, PieceWKnight);
	BB bn=posGetBBPiece(pos, PieceBKnight);
	BB wb=posGetBBPiece(pos, PieceWBishopL) | posGetBBPiece(pos, PieceWBishopD);
	BB bb=posGetBBPiece(pos, PieceBBishopL) | posGetBBPiece(pos, PieceBBishopD);
	BB wr=posGetBBPiece(pos, PieceWRook);
	BB br=posGetBBPiece(pos, PieceBRook);
	BB wq=posGetBBPiece(pos, PieceWQueen);
	BB bq=posGetBBPiece(pos, PieceBQueen);
	BB wk=posGetBBPiece(pos, PieceWKing);
	BB bk=posGetBBPiece(pos, PieceBKing);
	BB occ=posGetBBAll(pos);

	int wPawnCount=bbPopCount(wp);
	int bPawnCount=bbPopCount(bp);
	int wKnightCount=bbPopCount(wn);
	int bKnightCount=bbPopCount(bn);
	int wBishopCount=bbPopCount(wb);
	int bBishopCount=bbPopCount(bb);
	int wRookCount=bbPopCount(wr);
	int bRookCount=bbPopCount(br);
	int wQueenCount=bbPopCount(wq);
	int bQueenCount=bbPopCount(bq);

	int minorCount=wKnightCount+wBishopCount+bKnightCount+bBishopCount;
	int rookCount=wRookCount+bRookCount;
	int queenCount=wQueenCount+bQueenCount;

	int pieceWeight=minorCount+2*rookCount+4*queenCount;
	assert(pieceWeight>=0 && pieceWeight<128);

	int weightEG=evalWeightEGFactors[pieceWeight];
	int weightMG=256-weightEG;

	float factorMG=weightMG/256.0;
	float factorEG=weightEG/256.0;

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
	BB pieceSet;
	float count;

	BB wpAttacks=bbForwardOne(bbWingify(wp), ColourWhite);
	BB bpAttacks=bbForwardOne(bbWingify(bp), ColourBlack);

	BB mobilityAllowed[ColourNB];
	mobilityAllowed[ColourWhite]=~(wp | wk | bpAttacks);
	mobilityAllowed[ColourBlack]=~(bp | bk | wpAttacks);

	pieceSet=wn;
	while(pieceSet) {
		Sq sq=bbScanReset(&pieceSet);
		BB attacks=attacksKnight(sq);
		count=bbPopCount(attacks & mobilityAllowed[ColourWhite]);
		coefficients[EvalTTuneParamKnightMobMG]+=factorMG*count;
		coefficients[EvalTTuneParamKnightMobEG]+=factorEG*count;
	}
	pieceSet=bn;
	while(pieceSet) {
		Sq sq=bbScanReset(&pieceSet);
		BB attacks=attacksKnight(sq);
		count=bbPopCount(attacks & mobilityAllowed[ColourBlack]);
		coefficients[EvalTTuneParamKnightMobMG]-=factorMG*count;
		coefficients[EvalTTuneParamKnightMobEG]-=factorEG*count;
	}

	BB bishopMobOcc[ColourNB];
	bishopMobOcc[ColourWhite]=(occ^(wb|wq));
	bishopMobOcc[ColourBlack]=(occ^(bb|bq));

	pieceSet=wb;
	while(pieceSet) {
		Sq sq=bbScanReset(&pieceSet);
		BB attacks=attacksBishop(sq, bishopMobOcc[ColourWhite]);
		count=bbPopCount(attacks & mobilityAllowed[ColourWhite]);
		coefficients[EvalTTuneParamBishopMobMG]+=factorMG*count;
		coefficients[EvalTTuneParamBishopMobEG]+=factorEG*count;
	}
	pieceSet=bb;
	while(pieceSet) {
		Sq sq=bbScanReset(&pieceSet);
		BB attacks=attacksBishop(sq, bishopMobOcc[ColourBlack]);
		count=bbPopCount(attacks & mobilityAllowed[ColourBlack]);
		coefficients[EvalTTuneParamBishopMobMG]-=factorMG*count;
		coefficients[EvalTTuneParamBishopMobEG]-=factorEG*count;
	}

	BB rookMobOcc[ColourNB];
	rookMobOcc[ColourWhite]=(occ^(wr|wq));
	rookMobOcc[ColourBlack]=(occ^(br|bq));

	pieceSet=wr;
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
	pieceSet=br;
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

	// PSTs
	pieceSet=wp;
	while(pieceSet) {
		Sq sq=bbScanReset(&pieceSet);
		unsigned pstIndex=evalTTunePawnPstSqToIndex(sq);
		coefficients[EvalTTuneParamPstPawnMGBase+pstIndex]+=factorMG;
		coefficients[EvalTTuneParamPstPawnEGBase+pstIndex]+=factorEG;
	}
	pieceSet=bp;
	while(pieceSet) {
		Sq sq=bbScanReset(&pieceSet);
		unsigned pstIndex=evalTTunePawnPstSqToIndex(sq);
		coefficients[EvalTTuneParamPstPawnMGBase+pstIndex]-=factorMG;
		coefficients[EvalTTuneParamPstPawnEGBase+pstIndex]-=factorEG;
	}

	for(unsigned type=PieceTypeKnight; type<=PieceTypeKing; ++type) {
		pieceSet=posGetBBPiece(pos, pieceMake(type, ColourWhite));
		while(pieceSet) {
			Sq sq=bbScanReset(&pieceSet);
			unsigned pstIndex=evalTTunePstSqToIndex(sq);
			coefficients[evalTTuneParamPstMGBase[type]+pstIndex]+=factorMG;
			coefficients[evalTTuneParamPstEGBase[type]+pstIndex]+=factorEG;
		}
		pieceSet=posGetBBPiece(pos, pieceMake(type, ColourBlack));
		while(pieceSet) {
			Sq sq=bbScanReset(&pieceSet);
			unsigned pstIndex=evalTTunePstSqToIndex(sq);
			coefficients[evalTTuneParamPstMGBase[type]+pstIndex]-=factorMG;
			coefficients[evalTTuneParamPstEGBase[type]+pstIndex]-=factorEG;
		}
	}

	// Pawns
	BB frontSpan[ColourNB], rearSpan[ColourNB], pawnAttacks[ColourNB];
	BB doubled[ColourNB], isolated[ColourNB];
	BB influence[ColourNB], fill[ColourNB];
	BB blocked[ColourNB], passed[ColourNB];
	BB semiOpenFiles[ColourNB], outposts[ColourNB];
	fill[ColourWhite]=bbFileFill(wp);
	fill[ColourBlack]=bbFileFill(bp);
	frontSpan[ColourWhite]=bbNorthOne(bbNorthFill(wp)); // All squares infront of pawns of given colour
	frontSpan[ColourBlack]=bbSouthOne(bbSouthFill(bp));
	rearSpan[ColourWhite]=bbSouthOne(bbSouthFill(wp)); // All squares behind pawns of given colour
	rearSpan[ColourBlack]=bbNorthOne(bbNorthFill(bp));
	pawnAttacks[ColourWhite]=bbNorthOne(bbWingify(wp)); // All squares attacked by pawns of given colour
	pawnAttacks[ColourBlack]=bbSouthOne(bbWingify(bp));
	influence[ColourWhite]=(frontSpan[ColourWhite] | bbWingify(frontSpan[ColourWhite])); // Squares which colour in question may attack or move to, now or in the future.
	influence[ColourBlack]=(frontSpan[ColourBlack] | bbWingify(frontSpan[ColourBlack]));

	doubled[ColourWhite]=(wp & rearSpan[ColourWhite]);
	doubled[ColourBlack]=(bp & rearSpan[ColourBlack]);
	isolated[ColourWhite]=(wp & ~bbFileFill(pawnAttacks[ColourWhite]));
	isolated[ColourBlack]=(bp & ~bbFileFill(pawnAttacks[ColourBlack]));
	blocked[ColourWhite]=(wp & bbSouthOne(occ));
	blocked[ColourBlack]=(bp & bbNorthOne(occ));
	passed[ColourWhite]=(wp & ~(doubled[ColourWhite] | influence[ColourBlack]));
	passed[ColourBlack]=(bp & ~(doubled[ColourBlack] | influence[ColourWhite]));

	semiOpenFiles[ColourWhite]=(fill[ColourBlack] & ~fill[ColourWhite]);
	semiOpenFiles[ColourBlack]=(fill[ColourWhite] & ~fill[ColourBlack]);
	outposts[ColourWhite]=(pawnAttacks[ColourWhite] & (bbRank(Rank4)|bbRank(Rank5)|bbRank(Rank6)|bbRank(Rank7)) & ~bbWingify(frontSpan[ColourBlack]));
	outposts[ColourBlack]=(pawnAttacks[ColourBlack] & (bbRank(Rank5)|bbRank(Rank4)|bbRank(Rank3)|bbRank(Rank2)) & ~bbWingify(frontSpan[ColourWhite]));
	BB openFiles=~(fill[ColourWhite] | fill[ColourBlack]);

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

	int knightAffCount=wKnightCount*wPawnCount-bKnightCount*bPawnCount;
	coefficients[EvalTTuneParamKnightPawnAffinityMG]=factorMG*knightAffCount;
	coefficients[EvalTTuneParamKnightPawnAffinityEG]=factorEG*knightAffCount;

	int rookAffCount=wRookCount*wPawnCount-bRookCount*bPawnCount;
	coefficients[EvalTTuneParamRookPawnAffinityMG]=factorMG*rookAffCount;
	coefficients[EvalTTuneParamRookPawnAffinityEG]=factorEG*rookAffCount;

	int rookOpenFileCount=((int)bbPopCount(wr & openFiles))-((int)bbPopCount(br & openFiles));
	coefficients[EvalTTuneParamRookOpenFileMG]=factorMG*rookOpenFileCount;
	coefficients[EvalTTuneParamRookOpenFileEG]=factorEG*rookOpenFileCount;

	int rookSemiOpenFileCount=((int)bbPopCount(wr & semiOpenFiles[ColourWhite]))-((int)bbPopCount(br & semiOpenFiles[ColourBlack]));
	coefficients[EvalTTuneParamRookSemiOpenFileMG]=factorMG*rookSemiOpenFileCount;
	coefficients[EvalTTuneParamRookSemiOpenFileEG]=factorEG*rookSemiOpenFileCount;

	int rookOn7thCount=0;
	if ((bp&Rank7)!=0 || sqRank(posGetKingSq(pos, ColourBlack))==Rank8)
		rookOn7thCount+=bbPopCount(wr&Rank7);
	if ((wp&Rank2)!=0 || sqRank(posGetKingSq(pos, ColourWhite))==Rank1)
		rookOn7thCount-=bbPopCount(br&Rank2);
	coefficients[EvalTTuneParamRookOn7thMG]=factorMG*rookOn7thCount;
	coefficients[EvalTTuneParamRookOn7thEG]=factorEG*rookOn7thCount;

	int rookTrappedCount=0;
	if (((wr & (bbSq(SqG1) | bbSq(SqH1))) && (wk & (bbSq(SqF1) | bbSq(SqG1)))) ||
	    ((wr & (bbSq(SqA1) | bbSq(SqB1))) && (wk & (bbSq(SqB1) | bbSq(SqC1)))))
		++rookTrappedCount;
	if (((br & (bbSq(SqG8) | bbSq(SqH8))) && (bk & (bbSq(SqF8) | bbSq(SqG8)))) ||
	    ((br & (bbSq(SqA8) | bbSq(SqB8))) && (bk & (bbSq(SqB8) | bbSq(SqC8)))))
		--rookTrappedCount;
	coefficients[EvalTTuneParamRookTrappedMG]=factorMG*rookTrappedCount;
	coefficients[EvalTTuneParamRookTrappedEG]=factorEG*rookTrappedCount;

	BB wKingSpan=bbForwardOne((bbWestOne(wk) | wk | bbEastOne(wk)), ColourWhite);
	BB bKingSpan=bbForwardOne((bbWestOne(bk) | bk | bbEastOne(bk)), ColourBlack);
	BB wKingShieldClose=(wp & wKingSpan);
	BB bKingShieldClose=(bp & bKingSpan);
	BB wKingShieldFar=(wp & bbForwardOne(wKingSpan, ColourWhite));
	BB bKingShieldFar=(bp & bbForwardOne(bKingSpan, ColourBlack));
	int kingShieldCloseCount=((int)bbPopCount(wKingShieldClose))-((int)bbPopCount(bKingShieldClose));
	int kingShieldFarCount=((int)bbPopCount(wKingShieldFar))-((int)bbPopCount(bKingShieldFar));
	coefficients[EvalTTuneParamKingShieldCloseMG]=factorMG*kingShieldCloseCount;
	coefficients[EvalTTuneParamKingShieldCloseEG]=factorEG*kingShieldCloseCount;
	coefficients[EvalTTuneParamKingShieldFarMG]=factorMG*kingShieldFarCount;
	coefficients[EvalTTuneParamKingShieldFarEG]=factorEG*kingShieldFarCount;

	int kingNearPasserCount[8]={0,0,0,0,0,0,0,0};
	pieceSet=passed[ColourBlack];
	while(pieceSet) {
		Sq passerSq=bbScanReset(&pieceSet);
		unsigned distance=sqDist(posGetKingSq(pos, ColourWhite), passerSq);
		assert(distance>=1 && distance<=7);
		++kingNearPasserCount[distance];
	}
	pieceSet=passed[ColourWhite];
	while(pieceSet) {
		Sq passerSq=bbScanReset(&pieceSet);
		unsigned distance=sqDist(posGetKingSq(pos, ColourBlack), passerSq);
		assert(distance>=1 && distance<=7);
		--kingNearPasserCount[distance];
	}
	coefficients[EvalTTuneParamKingNearPasser1MG]=factorMG*kingNearPasserCount[1];
	coefficients[EvalTTuneParamKingNearPasser1EG]=factorEG*kingNearPasserCount[1];
	coefficients[EvalTTuneParamKingNearPasser2MG]=factorMG*kingNearPasserCount[2];
	coefficients[EvalTTuneParamKingNearPasser2EG]=factorEG*kingNearPasserCount[2];
	coefficients[EvalTTuneParamKingNearPasser3MG]=factorMG*kingNearPasserCount[3];
	coefficients[EvalTTuneParamKingNearPasser3EG]=factorEG*kingNearPasserCount[3];
	coefficients[EvalTTuneParamKingNearPasser4MG]=factorMG*kingNearPasserCount[4];
	coefficients[EvalTTuneParamKingNearPasser4EG]=factorEG*kingNearPasserCount[4];
	coefficients[EvalTTuneParamKingNearPasser5MG]=factorMG*kingNearPasserCount[5];
	coefficients[EvalTTuneParamKingNearPasser5EG]=factorEG*kingNearPasserCount[5];
	coefficients[EvalTTuneParamKingNearPasser6MG]=factorMG*kingNearPasserCount[6];
	coefficients[EvalTTuneParamKingNearPasser6EG]=factorEG*kingNearPasserCount[6];
	coefficients[EvalTTuneParamKingNearPasser7MG]=factorMG*kingNearPasserCount[7];
	coefficients[EvalTTuneParamKingNearPasser7EG]=factorEG*kingNearPasserCount[7];

	CastRights castRights=posGetCastRights(pos);
	int kingCastlingMobilityCount=(((int)(castRights.rookSq[ColourWhite][CastSideA]!=SqInvalid))+((int)(castRights.rookSq[ColourWhite][CastSideH]!=SqInvalid)))-
	                              (((int)(castRights.rookSq[ColourBlack][CastSideA]!=SqInvalid))-((int)(castRights.rookSq[ColourBlack][CastSideH]!=SqInvalid)));
	coefficients[EvalTTuneParamKingCastlingMobilityMG]=factorMG*kingCastlingMobilityCount;
	coefficients[EvalTTuneParamKingCastlingMobilityEG]=factorEG*kingCastlingMobilityCount;

	int outpostSqCount=((int)bbPopCount(outposts[ColourWhite]))-((int)bbPopCount(outposts[ColourBlack]));
	coefficients[EvalTTuneParamOutpostSqMG]=factorMG*outpostSqCount;
	coefficients[EvalTTuneParamOutpostSqEG]=factorEG*outpostSqCount;

	int outpostKnightCount=((int)bbPopCount(outposts[ColourWhite] & wn))-
	                       ((int)bbPopCount(outposts[ColourBlack] & bn));
	coefficients[EvalTTuneParamOutpostKnightMG]=factorMG*outpostKnightCount;
	coefficients[EvalTTuneParamOutpostKnightEG]=factorEG*outpostKnightCount;

	int tempoCount=(posGetSTM(pos)==ColourWhite ? 1 : -1);
	coefficients[EvalTTuneParamTempoDefaultMG]=factorMG*tempoCount;
	coefficients[EvalTTuneParamTempoDefaultEG]=factorEG*tempoCount;
}

void evaluateOutputCode(const char *path, const float *weights) {
	// Open file
	FILE *file=fopen(path, "w");
	if (file==NULL)
		return;

	// Generate code for all weights (parameters)
	fprintf(file, "TUNECONST VPair evalMaterial[PieceTypeNB]={\n");
	fprintf(file, "	[PieceTypeNone]={0,0},\n");
	fprintf(file, "	[PieceTypePawn]={%.0f,%.0f},\n", weights[EvalTTuneParamPawnMG], weights[EvalTTuneParamPawnEG]);
	fprintf(file, "	[PieceTypeKnight]={%.0f,%.0f},\n", weights[EvalTTuneParamKnightMG], weights[EvalTTuneParamKnightEG]);
	fprintf(file, "	[PieceTypeBishopL]={%.0f,%.0f},\n", weights[EvalTTuneParamBishopMG], weights[EvalTTuneParamBishopEG]);
	fprintf(file, "	[PieceTypeBishopD]={%.0f,%.0f},\n", weights[EvalTTuneParamBishopMG], weights[EvalTTuneParamBishopEG]);
	fprintf(file, "	[PieceTypeRook]={%.0f,%.0f},\n", weights[EvalTTuneParamRookMG], weights[EvalTTuneParamRookEG]);
	fprintf(file, "	[PieceTypeQueen]={%.0f,%.0f},\n", weights[EvalTTuneParamQueenMG], weights[EvalTTuneParamQueenEG]);
	fprintf(file, "	[PieceTypeKing]={0,0},\n");
	fprintf(file, "};\n");

	fprintf(file, "TUNECONST VPair evalPawnDoubled={%.0f,%.0f};\n", weights[EvalTTuneParamPawnDoubledMG], weights[EvalTTuneParamPawnDoubledEG]);
	fprintf(file, "TUNECONST VPair evalPawnIsolated={%.0f,%.0f};\n", weights[EvalTTuneParamPawnIsolatedMG], weights[EvalTTuneParamPawnIsolatedEG]);
	fprintf(file, "TUNECONST VPair evalPawnBlocked={%.0f,%.0f};\n", weights[EvalTTuneParamPawnBlockedMG], weights[EvalTTuneParamPawnBlockedEG]);
	fprintf(file, "TUNECONST VPair evalPawnPassed[RankNB]={\n");
	fprintf(file, "	{%.0f,%.0f},\n", 0.0, 0.0);
	fprintf(file, "	{%.0f,%.0f},\n", weights[EvalTTuneParamPawnPassedR2MG], weights[EvalTTuneParamPawnPassedR2EG]);
	fprintf(file, "	{%.0f,%.0f},\n", weights[EvalTTuneParamPawnPassedR3MG], weights[EvalTTuneParamPawnPassedR3EG]);
	fprintf(file, "	{%.0f,%.0f},\n", weights[EvalTTuneParamPawnPassedR4MG], weights[EvalTTuneParamPawnPassedR4EG]);
	fprintf(file, "	{%.0f,%.0f},\n", weights[EvalTTuneParamPawnPassedR5MG], weights[EvalTTuneParamPawnPassedR5EG]);
	fprintf(file, "	{%.0f,%.0f},\n", weights[EvalTTuneParamPawnPassedR6MG], weights[EvalTTuneParamPawnPassedR6EG]);
	fprintf(file, "	{%.0f,%.0f},\n", weights[EvalTTuneParamPawnPassedR7MG], weights[EvalTTuneParamPawnPassedR7EG]);
	fprintf(file, "	{%.0f,%.0f},\n", 0.0, 0.0);
	fprintf(file, "};\n");

	fprintf(file, "TUNECONST VPair evalKnightMob={%.0f,%.0f};\n", weights[EvalTTuneParamKnightMobMG], weights[EvalTTuneParamKnightMobEG]);
	fprintf(file, "TUNECONST VPair evalBishopPair={%.0f,%.0f};\n", weights[EvalTTuneParamBishopPairMG], weights[EvalTTuneParamBishopPairEG]);
	fprintf(file, "TUNECONST VPair evalBishopMob={%.0f,%.0f};\n", weights[EvalTTuneParamBishopMobMG], weights[EvalTTuneParamBishopMobEG]);
	fprintf(file, "TUNECONST VPair evalRookMobFile={%.0f,%.0f};\n", weights[EvalTTuneParamRookMobFileMG], weights[EvalTTuneParamRookMobFileEG]);
	fprintf(file, "TUNECONST VPair evalRookMobRank={%.0f,%.0f};\n", weights[EvalTTuneParamRookMobRankMG], weights[EvalTTuneParamRookMobRankEG]);

	fprintf(file, "TUNECONST VPair evalKnightPawnAffinity={%.0f,%.0f};\n", weights[EvalTTuneParamKnightPawnAffinityMG], weights[EvalTTuneParamKnightPawnAffinityEG]);
	fprintf(file, "TUNECONST VPair evalRookPawnAffinity={%.0f,%.0f};\n", weights[EvalTTuneParamRookPawnAffinityMG], weights[EvalTTuneParamRookPawnAffinityEG]);
	fprintf(file, "TUNECONST VPair evalRookOpenFile={%.0f,%.0f};\n", weights[EvalTTuneParamRookOpenFileMG], weights[EvalTTuneParamRookOpenFileEG]);
	fprintf(file, "TUNECONST VPair evalRookSemiOpenFile={%.0f,%.0f};\n", weights[EvalTTuneParamRookSemiOpenFileMG], weights[EvalTTuneParamRookSemiOpenFileEG]);
	fprintf(file, "TUNECONST VPair evalRookOn7th={%.0f,%.0f};\n", weights[EvalTTuneParamRookOn7thMG], weights[EvalTTuneParamRookOn7thEG]);
	fprintf(file, "TUNECONST VPair evalRookTrapped={%.0f,%.0f};\n", weights[EvalTTuneParamRookTrappedMG], weights[EvalTTuneParamRookTrappedEG]);
	fprintf(file, "TUNECONST VPair evalKingShieldClose={%.0f,%.0f};\n", weights[EvalTTuneParamKingShieldCloseMG], weights[EvalTTuneParamKingShieldCloseEG]);
	fprintf(file, "TUNECONST VPair evalKingShieldFar={%.0f,%.0f};\n", weights[EvalTTuneParamKingShieldFarMG], weights[EvalTTuneParamKingShieldFarEG]);
	fprintf(file, "TUNECONST VPair evalKingNearPasser[8]={ // indexed by distance in interval [1, 7]\n");
	fprintf(file, "	{%.0f,%.0f}, // unused\n", 0.0, 0.0);
	fprintf(file, "	{%.0f,%.0f},\n", weights[EvalTTuneParamKingNearPasser1MG], weights[EvalTTuneParamKingNearPasser1EG]);
	fprintf(file, "	{%.0f,%.0f},\n", weights[EvalTTuneParamKingNearPasser2MG], weights[EvalTTuneParamKingNearPasser2EG]);
	fprintf(file, "	{%.0f,%.0f},\n", weights[EvalTTuneParamKingNearPasser3MG], weights[EvalTTuneParamKingNearPasser3EG]);
	fprintf(file, "	{%.0f,%.0f},\n", weights[EvalTTuneParamKingNearPasser4MG], weights[EvalTTuneParamKingNearPasser4EG]);
	fprintf(file, "	{%.0f,%.0f},\n", weights[EvalTTuneParamKingNearPasser5MG], weights[EvalTTuneParamKingNearPasser5EG]);
	fprintf(file, "	{%.0f,%.0f},\n", weights[EvalTTuneParamKingNearPasser6MG], weights[EvalTTuneParamKingNearPasser6EG]);
	fprintf(file, "	{%.0f,%.0f},\n", weights[EvalTTuneParamKingNearPasser7MG], weights[EvalTTuneParamKingNearPasser7EG]);
	fprintf(file, "};\n");
	fprintf(file, "TUNECONST VPair evalKingCastlingMobility={%.0f,%.0f};\n", weights[EvalTTuneParamKingCastlingMobilityMG], weights[EvalTTuneParamKingCastlingMobilityEG]);
	fprintf(file, "TUNECONST VPair evalOutpostSq={%.0f,%.0f};\n", weights[EvalTTuneParamOutpostSqMG], weights[EvalTTuneParamOutpostSqEG]);
	fprintf(file, "TUNECONST VPair evalOutpostKnight={%.0f,%.0f};\n", weights[EvalTTuneParamOutpostKnightMG], weights[EvalTTuneParamOutpostKnightEG]);
	fprintf(file, "TUNECONST VPair evalTempoDefault={%.0f,%.0f};\n", weights[EvalTTuneParamTempoDefaultMG], weights[EvalTTuneParamTempoDefaultEG]);

	// PSTs
	fprintf(file, "VPair evalPST[PieceNB][SqNB]={\n");

	fprintf(file, "	[PieceTypePawn]={\n");
	for(unsigned y=0; y<8; ++y) {
		fprintf(file, "		");
		for(unsigned x=0; x<8; ++x) {
			if (y==0 || y==7)
				fprintf(file, "{%5.0f,%5.0f},", 0.0, 0.0);
			else {
				unsigned index=evalTTunePawnPstSqToIndex(sqMake(x,y));
				fprintf(file, "{%5.0f,%5.0f},", weights[EvalTTuneParamPstPawnMGBase+index], weights[EvalTTuneParamPstPawnEGBase+index]);
			}
		}
		fprintf(file, "\n");
	}
	fprintf(file, "	},\n");

	for(unsigned type=PieceTypeKnight; type<=PieceTypeKing; ++type) {
		if (type==PieceTypeBishopD)
			continue;

		if (type==PieceTypeBishopL)
			fprintf(file, "	[PieceTypeBishopL]={\n");
		else
			fprintf(file, "	[PieceType%s]={\n", pieceTypeToStr(type));
		for(unsigned y=0; y<8; ++y) {
			fprintf(file, "		");
			for(unsigned x=0; x<8; ++x) {
				unsigned index=evalTTunePstSqToIndex(sqMake(x,y));
				fprintf(file, "{%5.0f,%5.0f},", weights[evalTTuneParamPstMGBase[type]+index], weights[evalTTuneParamPstMGBase[type]+index]);
			}
			fprintf(file, "\n");
		}
		fprintf(file, "	},\n");
	}

	fprintf(file, "};\n");

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
	int outpostRelativeCount=((int)bbPopCount(pawnData->outposts[ColourWhite]))-((int)bbPopCount(pawnData->outposts[ColourBlack]));
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
		evalVPairAddTo(&pawnData->score, &evalPawnPassed[sqRank(sq)]);
	}
	pieceSet=pawnData->passed[ColourBlack];
	while(pieceSet) {
		Sq sq=bbScanReset(&pieceSet);
		evalVPairSubFrom(&pawnData->score, &evalPawnPassed[sqRank(sqFlip(sq))]);
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

Sq evalTTunePstIndexToSq(unsigned index) {
	assert(index<32);

	return sqMake(index%4, index/4);
}

unsigned evalTTunePstSqToIndex(Sq sq) {
	if (sqFile(sq)>=FileE)
		sq=sqMirror(sq);
	return 4*sqRank(sq)+sqFile(sq);
}

Sq evalTTunePawnPstIndexToSq(unsigned index) {
	assert(index<24);

	return sqMake(index%4, index/4+1);
}

unsigned evalTTunePawnPstSqToIndex(Sq sq) {
	if (sqFile(sq)>=FileE)
		sq=sqMirror(sq);
	return 4*(sqRank(sq)-1)+sqFile(sq);
}
