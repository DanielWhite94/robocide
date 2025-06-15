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
	EvalTTuneParamBishopPairMG,
	EvalTTuneParamBishopPairEG,
	EvalTTuneParamKnightMobMG,
	EvalTTuneParamKnightMobEG,
	EvalTTuneParamBishopMobMG,
	EvalTTuneParamBishopMobEG,
	EvalTTuneParamRookMobFileMG,
	EvalTTuneParamRookMobFileEG,
	EvalTTuneParamRookMobRankMG,
	EvalTTuneParamRookMobRankEG,
	EvalTTuneParamQueenMobMG,
	EvalTTuneParamQueenMobEG,
	// PSTS use two sets (MG/EG) of 32 parameters from A1 to D8 (first four squares of each rank, sq is mirrored if not on left side)
	EvalTTuneParamPstKingMGBase,
	EvalTTuneParamPstKingMGEnd=EvalTTuneParamPstKingMGBase+31,
	EvalTTuneParamPstKingEGBase,
	EvalTTuneParamPstKingEGEnd=EvalTTuneParamPstKingEGBase+31,
	EvalTTuneParamNB,
} EvalTTuneParam;

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

Sq evalTTunePstIndexToSq(unsigned index);
unsigned evalTTunePstSqToIndex(Sq sq);

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
	evalOptionNewVPair("BishopPair", &evalBishopPair, 0, 1000);;
	evalOptionNewVPair("KnightMob", &evalKnightMob, 0, 100);
	evalOptionNewVPair("BishopMobility", &evalBishopMob, 0, 100);
	evalOptionNewVPair("RookMobilityFile", &evalRookMobFile, 0, 50);
	evalOptionNewVPair("RookMobilityRank", &evalRookMobRank, 0, 50);
	evalOptionNewVPair("QueenMobility", &evalQueenMob, 0, 50);
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
	ttuneAddParameter(EvalTTuneParamBishopPairMG, "BishopPairMG", evalBishopPair.mg, true);
	ttuneAddParameter(EvalTTuneParamBishopPairEG, "BishopPairEG", evalBishopPair.eg, true);
	ttuneAddParameter(EvalTTuneParamKnightMobMG, "KnightMobMG", evalKnightMob.mg, true);
	ttuneAddParameter(EvalTTuneParamKnightMobEG, "KnightMobEG", evalKnightMob.eg, true);
	ttuneAddParameter(EvalTTuneParamBishopMobMG, "BishopMobMG", evalBishopMob.mg, true);
	ttuneAddParameter(EvalTTuneParamBishopMobEG, "BishopMobEG", evalBishopMob.eg, true);
	ttuneAddParameter(EvalTTuneParamRookMobFileMG, "RookMobFileMG", evalRookMobFile.mg, true);
	ttuneAddParameter(EvalTTuneParamRookMobFileEG, "RookMobFileEG", evalRookMobFile.eg, true);
	ttuneAddParameter(EvalTTuneParamRookMobRankMG, "RookMobRankMG", evalRookMobRank.mg, true);
	ttuneAddParameter(EvalTTuneParamRookMobRankEG, "RookMobRankEG", evalRookMobRank.eg, true);
	ttuneAddParameter(EvalTTuneParamQueenMobMG, "QueemMobMG", evalQueenMob.mg, true);
	ttuneAddParameter(EvalTTuneParamQueenMobEG, "QueemMobEG", evalQueenMob.eg, true);
	for(unsigned i=0; i<32; ++i) {
		Sq sq=evalTTunePstIndexToSq(i);
		char str[32];
		sprintf(str, "KingPst%c%cMG", fileToChar(sqFile(sq))-'a'+'A', rankToChar(sqRank(sq)));
		ttuneAddParameter(EvalTTuneParamPstKingMGBase+i, str, evalKingPST[sq].mg, true);
		sprintf(str, "KingPst%c%cEG", fileToChar(sqFile(sq))-'a'+'A', rankToChar(sqRank(sq)));
		ttuneAddParameter(EvalTTuneParamPstKingEGBase+i, str, evalKingPST[sq].eg, true);
	}
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
	if (!(scoreM==score && scoreFM==score && scoreF==score)) {
		posDraw(pos);
		printf("%i %i %i %i\n", score, scoreM, scoreFM, scoreF);
	}
	assert(scoreM==score && scoreFM==score && scoreF==score);
#	endif
	return score;
}

void evaluateCoefficients(const Pos *pos, float *coefficients) {
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

	int bishopPairCount=((int)(posGetBBPiece(pos, PieceWBishopL)!=0 && posGetBBPiece(pos, PieceWBishopD)!=0))-
	                    ((int)(posGetBBPiece(pos, PieceBBishopL)!=0 && posGetBBPiece(pos, PieceBBishopD)!=0));
	coefficients[EvalTTuneParamBishopPairMG]=factorMG*bishopPairCount;
	coefficients[EvalTTuneParamBishopPairEG]=factorEG*bishopPairCount;

	// Mobility
	coefficients[EvalTTuneParamKnightMobMG]=0.0;
	coefficients[EvalTTuneParamKnightMobEG]=0.0;
	coefficients[EvalTTuneParamBishopMobMG]=0.0;
	coefficients[EvalTTuneParamBishopMobEG]=0.0;
	coefficients[EvalTTuneParamRookMobFileMG]=0.0;
	coefficients[EvalTTuneParamRookMobFileEG]=0.0;
	coefficients[EvalTTuneParamRookMobRankMG]=0.0;
	coefficients[EvalTTuneParamRookMobRankEG]=0.0;
	coefficients[EvalTTuneParamQueenMobMG]=0.0;
	coefficients[EvalTTuneParamQueenMobEG]=0.0;

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

	pieceSet=wq;
	while(pieceSet) {
		Sq sq=bbScanReset(&pieceSet);
		BB attacksB=attacksBishop(sq, bishopMobOcc[ColourWhite]);
		BB attacksR=attacksRook(sq, rookMobOcc[ColourWhite]);
		count=bbPopCount((attacksB|attacksR) & mobilityAllowed[ColourWhite]);
		coefficients[EvalTTuneParamQueenMobMG]+=factorMG*count;
		coefficients[EvalTTuneParamQueenMobEG]+=factorEG*count;
	}
	pieceSet=bq;
	while(pieceSet) {
		Sq sq=bbScanReset(&pieceSet);
		BB attacksB=attacksBishop(sq, bishopMobOcc[ColourBlack]);
		BB attacksR=attacksRook(sq, rookMobOcc[ColourBlack]);
		count=bbPopCount((attacksB|attacksR) & mobilityAllowed[ColourBlack]);
		coefficients[EvalTTuneParamQueenMobMG]-=factorMG*count;
		coefficients[EvalTTuneParamQueenMobEG]-=factorEG*count;
	}

	// PSTs
	pieceSet=wk;
	while(pieceSet) {
		Sq sq=bbScanReset(&pieceSet);
		unsigned pstIndex=evalTTunePstSqToIndex(sq);
		coefficients[EvalTTuneParamPstKingMGBase+pstIndex]+=factorMG;
		coefficients[EvalTTuneParamPstKingEGBase+pstIndex]+=factorEG;
	}
	pieceSet=bk;
	while(pieceSet) {
		Sq sq=bbScanReset(&pieceSet);
		unsigned pstIndex=evalTTunePstSqToIndex(sq);
		coefficients[EvalTTuneParamPstKingMGBase+pstIndex]-=factorMG;
		coefficients[EvalTTuneParamPstKingEGBase+pstIndex]-=factorEG;
	}
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

	fprintf(file, "TUNECONST VPair evalBishopPair={%.0f,%.0f};\n", weights[EvalTTuneParamBishopPairMG], weights[EvalTTuneParamBishopPairEG]);
	fprintf(file, "TUNECONST VPair evalKnightMob={%.0f,%.0f};\n", weights[EvalTTuneParamKnightMobMG], weights[EvalTTuneParamKnightMobEG]);
	fprintf(file, "TUNECONST VPair evalBishopMob={%.0f,%.0f};\n", weights[EvalTTuneParamBishopMobMG], weights[EvalTTuneParamBishopMobEG]);
	fprintf(file, "TUNECONST VPair evalRookMobFile={%.0f,%.0f};\n", weights[EvalTTuneParamRookMobFileMG], weights[EvalTTuneParamRookMobFileEG]);
	fprintf(file, "TUNECONST VPair evalRookMobRank={%.0f,%.0f};\n", weights[EvalTTuneParamRookMobRankMG], weights[EvalTTuneParamRookMobRankEG]);
	fprintf(file, "TUNECONST VPair evalQueenMob={%.0f,%.0f};\n", weights[EvalTTuneParamQueenMobMG], weights[EvalTTuneParamQueenMobEG]);

	fprintf(file, "VPair evalKingPST[SqNB]={\n");

	for(unsigned y=0; y<8; ++y) {
		fprintf(file, "	");
		for(unsigned x=0; x<8; ++x) {
			unsigned index=evalTTunePstSqToIndex(sqMake(x,y));
			fprintf(file, "{%5.0f,%5.0f},", weights[EvalTTuneParamPstKingMGBase+index], weights[EvalTTuneParamPstKingEGBase+index]);
		}
		fprintf(file, "\n");
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
}

void evalComputePawnData(const Pos *pos, EvalPawnData *pawnData) {
	// Init.
	pawnData->score=VPairZero;
	BB pawns[ColourNB], frontSpan[ColourNB], rearSpan[ColourNB], attacks[ColourNB];
	BB doubled[ColourNB];
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

	pawnData->pawns[ColourWhite]=pawns[ColourWhite];
	pawnData->pawns[ColourBlack]=pawns[ColourBlack];
	pawnData->passed[ColourWhite]=(pawns[ColourWhite] & ~(doubled[ColourWhite] | influence[ColourBlack]));
	pawnData->passed[ColourBlack]=(pawns[ColourBlack] & ~(doubled[ColourBlack] | influence[ColourWhite]));
	pawnData->semiOpenFiles[ColourWhite]=(fill[ColourBlack] & ~fill[ColourWhite]);
	pawnData->semiOpenFiles[ColourBlack]=(fill[ColourWhite] & ~fill[ColourBlack]);
	pawnData->outposts[ColourWhite]=(attacks[ColourWhite] & (bbRank(Rank4)|bbRank(Rank5)|bbRank(Rank6)|bbRank(Rank7)) & ~bbWingify(frontSpan[ColourBlack]));
	pawnData->outposts[ColourBlack]=(attacks[ColourBlack] & (bbRank(Rank5)|bbRank(Rank4)|bbRank(Rank3)|bbRank(Rank2)) & ~bbWingify(frontSpan[ColourWhite]));
	pawnData->openFiles=~(fill[ColourWhite] | fill[ColourBlack]);
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

	return score;
}

VPair evaluateDefaultKing(EvalData *data, Colour colour) {
	assert(data!=NULL);

	VPair score=VPairZero;
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

Sq evalTTunePstIndexToSq(unsigned index) {
	assert(index<32);

	return sqMake(index%4, index/4);
}

unsigned evalTTunePstSqToIndex(Sq sq) {
	if (sqFile(sq)>=FileE)
		sq=sqMirror(sq);
	return 4*sqRank(sq)+sqFile(sq);
}
