#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "attacks.h"
#include "eval.h"
#include "util.h"
#ifdef TUNE
# include "uci.h"
#endif

////////////////////////////////////////////////////////////////////////////////
// 
////////////////////////////////////////////////////////////////////////////////

typedef int32_t value_t;

typedef struct
{
  value_t MG, EG;
}vpair_t;

typedef struct
{
  bb_t Pawns[2], Passed[2], SemiOpenFiles[2], OpenFiles;
  vpair_t Score;
}evalpawndata_t;
evalpawndata_t *EvalPawnTable=NULL;
size_t EvalPawnTableSize=0;

typedef struct
{
  hkey_t Mat;
  vpair_t (*Function)(const pos_t *Pos);
  vpair_t Offset, Tempo;
  uint8_t WeightMG, WeightEG;
}evalmatdata_t;
evalmatdata_t *EvalMatTable=NULL;
size_t EvalMatTableSize=0;

////////////////////////////////////////////////////////////////////////////////
// Tunable values
////////////////////////////////////////////////////////////////////////////////
TUNECONST vpair_t EvalMaterial[8]={{0,0},{900,1300},{3250,3250},{3250,3250},{3250,3250},{5000,5000},{10000,10000},{0,0}};
TUNECONST vpair_t EvalPawnDoubled={-100,-200};
TUNECONST vpair_t EvalPawnIsolated={-300,-200};
TUNECONST vpair_t EvalPawnBlocked={-100,-100};
TUNECONST vpair_t EvalPawnPassedQuadA={56,50}; // coefficients used in quadratic formula for passed pawn score (with rank as the input)
TUNECONST vpair_t EvalPawnPassedQuadB={-109,50};
TUNECONST vpair_t EvalPawnPassedQuadC={155,50};
TUNECONST vpair_t EvalKnightPawnAffinity={30,30}; // bonus each knight receives for each friendly pawn on the board
TUNECONST vpair_t EvalBishopPair={500,500};
TUNECONST vpair_t EvalBishopMob={40,30};
TUNECONST vpair_t EvalRookPawnAffinity={-70,-70}; // bonus each rook receives for each friendly pawn on the board
TUNECONST vpair_t EvalRookMobFile={20,30};
TUNECONST vpair_t EvalRookMobRank={10,20};
TUNECONST vpair_t EvalRookOpenFile={100,50};
TUNECONST vpair_t EvalRookSemiOpenFile={50,20};
TUNECONST vpair_t EvalRookOn7th={50,100};
TUNECONST vpair_t EvalRookTrapped={-400,0};
TUNECONST vpair_t EvalKingShieldClose={150,0};
TUNECONST vpair_t EvalKingShieldFar={50,0};
TUNECONST vpair_t EvalTempoDefault={0,0};
TUNECONST value_t EvalWeightFactor=144;

TUNECONST vpair_t EvalPawnPST[64]={
{  -30, -410},{ -150, -400},{ -230, -380},{ -270, -370},{ -270, -370},{ -230, -380},{ -150, -400},{  -30, -410},
{ -150, -380},{    0, -350},{  -60, -340},{  -90, -320},{  -90, -320},{  -60, -340},{    0, -350},{ -150, -380},
{ -210, -300},{  -40, -270},{   70, -250},{   40, -220},{   40, -220},{   70, -250},{  -40, -270},{ -210, -300},
{ -220, -190},{  -50, -160},{   70, -120},{  210,  -30},{  210,  -30},{   70, -120},{  -50, -160},{ -220, -190},
{ -190,  -50},{  -20,  -20},{  110,   10},{  240,  100},{  240,  100},{  110,   10},{  -20,  -20},{ -190,  -50},
{ -100,  120},{   50,  140},{  170,  170},{  150,  200},{  150,  200},{  170,  170},{   50,  140},{ -100,  120},
{   20,  330},{  180,  350},{  110,  370},{   80,  380},{   80,  380},{  110,  370},{  180,  350},{   20,  330},
{  210,  580},{   90,  590},{   10,  610},{  -20,  620},{  -20,  620},{   10,  610},{   90,  590},{  210,  58}};
TUNECONST vpair_t EvalKnightPST[64]={
{ -170, -120},{ -120,  -60},{  -80,  -30},{  -60,  -10},{  -60,  -10},{  -80,  -30},{ -120,  -60},{ -170, -120},
{ -110,  -60},{  -60,  -10},{  -30,   20},{  -10,   30},{  -10,   30},{  -30,   20},{  -60,  -10},{ -110,  -60},
{  -70,  -30},{  -20,   20},{   10,   50},{   20,   60},{   20,   60},{   10,   50},{  -20,   20},{  -70,  -30},
{  -40,  -10},{   10,   30},{   30,   60},{   40,   70},{   40,   70},{   30,   60},{   10,   30},{  -40,  -10},
{  -10,  -10},{   30,   30},{   60,   60},{   60,   70},{   60,   70},{   60,   60},{   30,   30},{  -10,  -10},
{    0,  -30},{   40,   20},{   70,   50},{   80,   60},{   80,   60},{   70,   50},{   40,   20},{    0,  -30},
{  -10,  -60},{   40,  -10},{   70,   20},{   90,   30},{   90,   30},{   70,   20},{   40,  -10},{  -10,  -60},
{  -20, -120},{   20,  -60},{   60,  -30},{   80,  -10},{   80,  -10},{   60,  -30},{   20,  -60},{  -20, -12}};
TUNECONST vpair_t EvalBishopPST[64]={
{  -55,  -75},{  -30,  -40},{  -15,  -20},{  -10,   -5},{  -10,   -5},{  -15,  -20},{  -30,  -40},{  -55,  -75},
{  -30,  -40},{  -10,   -5},{    0,   10},{   10,   20},{   10,   20},{    0,   10},{  -10,   -5},{  -30,  -40},
{  -15,  -20},{    0,   10},{   20,   30},{   30,   40},{   30,   40},{   20,   30},{    0,   10},{  -15,  -20},
{  -10,   -5},{   10,   20},{   30,   40},{   60,   45},{   60,   45},{   30,   40},{   10,   20},{  -10,   -5},
{  -10,   -5},{   10,   20},{   30,   40},{   60,   45},{   60,   45},{   30,   40},{   10,   20},{  -10,   -5},
{  -15,  -20},{    0,   10},{   20,   30},{   30,   40},{   30,   40},{   20,   30},{    0,   10},{  -15,  -20},
{  -30,  -40},{  -10,   -5},{    0,   10},{   10,   20},{   10,   20},{    0,   10},{  -10,   -5},{  -30,  -40},
{  -55,  -75},{  -30,  -40},{  -15,  -20},{  -10,   -5},{  -10,   -5},{  -15,  -20},{  -30,  -40},{  -55,   -7}};
TUNECONST vpair_t EvalKingPST[64]={
{  570, -460},{  570, -240},{  410, -120},{  330,  -40},{  330,  -40},{  410, -120},{  570, -240},{  570, -460},
{  560, -240},{  320,  -40},{  140,   60},{   30,  120},{   30,  120},{  140,   60},{  320,  -40},{  560, -240},
{  370, -120},{  110,   60},{ -110,  180},{ -260,  240},{ -260,  240},{ -110,  180},{  110,   60},{  370, -120},
{  240,  -40},{  -40,  120},{ -320,  240},{ -790,  260},{ -790,  260},{ -320,  240},{  -40,  120},{  240,  -40},
{  170,  -40},{ -110,  120},{ -390,  240},{ -860,  260},{ -860,  260},{ -390,  240},{ -110,  120},{  170,  -40},
{  160, -120},{ -100,   60},{ -320,  180},{ -480,  240},{ -480,  240},{ -320,  180},{ -100,   60},{  160, -120},
{  200, -240},{  -30,  -40},{ -210,   60},{ -310,  120},{ -310,  120},{ -210,   60},{  -30,  -40},{  200, -240},
{  290, -460},{   70, -240},{  -80, -120},{ -160,  -40},{ -160,  -40},{  -80, -120},{   70, -240},{  290, -46}};

////////////////////////////////////////////////////////////////////////////////
// Derived values
////////////////////////////////////////////////////////////////////////////////

vpair_t EvalPawnPassed[8];
uint8_t EvalWeightEGFactor[128];

////////////////////////////////////////////////////////////////////////////////
// Private prototypes
////////////////////////////////////////////////////////////////////////////////

vpair_t EvaluateDefault(const pos_t *Pos);
void EvalMat(const pos_t *Pos, evalmatdata_t *MatData);
void EvalComputeMat(const pos_t *Pos, evalmatdata_t *MatData);
evalpawndata_t EvalPawns(const pos_t *Pos);
static inline void EvalComputePawns(const pos_t *Pos, evalpawndata_t *Data);
static inline vpair_t EvalKnight(const pos_t *Pos, sq_t Sq, col_t Colour, const evalpawndata_t *PawnData);
static inline vpair_t EvalBishop(const pos_t *Pos, sq_t Sq, col_t Colour, const evalpawndata_t *PawnData);
static inline vpair_t EvalRook(const pos_t *Pos, sq_t Sq, col_t Colour, const evalpawndata_t *PawnData);
static inline vpair_t EvalQueen(const pos_t *Pos, sq_t Sq, col_t Colour, const evalpawndata_t *PawnData);
static inline vpair_t EvalKing(const pos_t *Pos, sq_t Sq, col_t Colour, const evalpawndata_t *PawnData);
static inline score_t EvalInterpolate(const pos_t *Pos, const vpair_t *Score, const evalmatdata_t *Data);
void EvalPawnResize(size_t SizeMB);
void EvalPawnFree();
void EvalPawnReset();
static inline bool EvalPawnRead(const pos_t *Pos, evalpawndata_t *Data);
static inline void EvalPawnWrite(const pos_t *Pos, evalpawndata_t *Data);
void EvalMatResize(size_t SizeKB);
void EvalMatFree();
void EvalMatReset();
static inline bool EvalMatRead(const pos_t *Pos, evalmatdata_t *Data);
static inline void EvalMatWrite(const pos_t *Pos, evalmatdata_t *Data);
static inline void EvalVPairAdd(vpair_t *A, vpair_t B);
static inline void EvalVPairSub(vpair_t *A, vpair_t B);
static inline void EvalVPairAddMul(vpair_t *A, vpair_t B, int C);
static inline void EvalVPairSubMul(vpair_t *A, vpair_t B, int C);
#ifdef TUNE
void EvalSetValue(int Value, void *UserData);
#endif
void EvalRecalc();

////////////////////////////////////////////////////////////////////////////////
// Public functions
////////////////////////////////////////////////////////////////////////////////

void EvalInit()
{
  // Init pawn and mat hash tables
  EvalPawnResize(1); // 1mb
  EvalMatResize(16); // 16kb
  
  // Calculate dervied values (such as passed pawn table)
  EvalRecalc();
  
  // Setup callbacks for tuning values
# ifdef TUNE
  value_t Min=-32767, Max=32767;
  UCIOptionNewSpin("PawnMG", &EvalSetValue, &EvalMaterial[pawn].MG, Min, Max, EvalMaterial[pawn].MG);
  UCIOptionNewSpin("PawnEG", &EvalSetValue, &EvalMaterial[pawn].EG, Min, Max, EvalMaterial[pawn].EG);
  UCIOptionNewSpin("KnightMG", &EvalSetValue, &EvalMaterial[knight].MG, Min, Max, EvalMaterial[knight].MG);
  UCIOptionNewSpin("KnightEG", &EvalSetValue, &EvalMaterial[knight].EG, Min, Max, EvalMaterial[knight].EG);
  UCIOptionNewSpin("BishopMG", &EvalSetValue, &EvalMaterial[bishopl].MG, Min, Max, EvalMaterial[bishopl].MG);
  UCIOptionNewSpin("BishopEG", &EvalSetValue, &EvalMaterial[bishopl].EG, Min, Max, EvalMaterial[bishopl].EG);
  UCIOptionNewSpin("RookMG", &EvalSetValue, &EvalMaterial[rook].MG, Min, Max, EvalMaterial[rook].MG);
  UCIOptionNewSpin("RookEG", &EvalSetValue, &EvalMaterial[rook].EG, Min, Max, EvalMaterial[rook].EG);
  UCIOptionNewSpin("QueenMG", &EvalSetValue, &EvalMaterial[queen].MG, Min, Max, EvalMaterial[queen].MG);
  UCIOptionNewSpin("QueenEG", &EvalSetValue, &EvalMaterial[queen].EG, Min, Max, EvalMaterial[queen].EG);
  UCIOptionNewSpin("PawnDoubledMG", &EvalSetValue, &EvalPawnDoubled.MG, Min, Max, EvalPawnDoubled.MG);
  UCIOptionNewSpin("PawnDoubledEG", &EvalSetValue, &EvalPawnDoubled.EG, Min, Max, EvalPawnDoubled.EG);
  UCIOptionNewSpin("PawnIsolatedMG", &EvalSetValue, &EvalPawnIsolated.MG, Min, Max, EvalPawnIsolated.MG);
  UCIOptionNewSpin("PawnIsolatedEG", &EvalSetValue, &EvalPawnIsolated.EG, Min, Max, EvalPawnIsolated.EG);
  UCIOptionNewSpin("PawnBlockedMG", &EvalSetValue, &EvalPawnBlocked.MG, Min, Max, EvalPawnBlocked.MG);
  UCIOptionNewSpin("PawnBlockedEG", &EvalSetValue, &EvalPawnBlocked.EG, Min, Max, EvalPawnBlocked.EG);
  UCIOptionNewSpin("PawnPassedQuadAMG", &EvalSetValue, &EvalPawnPassedQuadA.MG, Min, Max, EvalPawnPassedQuadA.MG);
  UCIOptionNewSpin("PawnPassedQuadAEG", &EvalSetValue, &EvalPawnPassedQuadA.EG, Min, Max, EvalPawnPassedQuadA.EG);
  UCIOptionNewSpin("PawnPassedQuadBMG", &EvalSetValue, &EvalPawnPassedQuadB.MG, Min, Max, EvalPawnPassedQuadB.MG);
  UCIOptionNewSpin("PawnPassedQuadBEG", &EvalSetValue, &EvalPawnPassedQuadB.EG, Min, Max, EvalPawnPassedQuadB.EG);
  UCIOptionNewSpin("PawnPassedQuadCMG", &EvalSetValue, &EvalPawnPassedQuadC.MG, Min, Max, EvalPawnPassedQuadC.MG);
  UCIOptionNewSpin("PawnPassedQuadCEG", &EvalSetValue, &EvalPawnPassedQuadC.EG, Min, Max, EvalPawnPassedQuadC.EG);
  UCIOptionNewSpin("KnightPawnAffinityMG", &EvalSetValue, &EvalKnightPawnAffinity.MG, Min, Max, EvalKnightPawnAffinity.MG);
  UCIOptionNewSpin("KnightPawnAffinityEG", &EvalSetValue, &EvalKnightPawnAffinity.EG, Min, Max, EvalKnightPawnAffinity.EG);
  UCIOptionNewSpin("BishopPairMG", &EvalSetValue, &EvalBishopPair.MG, Min, Max, EvalBishopPair.MG);
  UCIOptionNewSpin("BishopPairEG", &EvalSetValue, &EvalBishopPair.EG, Min, Max, EvalBishopPair.EG);
  UCIOptionNewSpin("BishopMobilityMG", &EvalSetValue, &EvalBishopMob.MG, Min, Max, EvalBishopMob.MG);
  UCIOptionNewSpin("BishopMobilityEG", &EvalSetValue, &EvalBishopMob.EG, Min, Max, EvalBishopMob.EG);
  UCIOptionNewSpin("RookPawnAffinityMG", &EvalSetValue, &EvalRookPawnAffinity.MG, Min, Max, EvalRookPawnAffinity.MG);
  UCIOptionNewSpin("RookPawnAffinityEG", &EvalSetValue, &EvalRookPawnAffinity.EG, Min, Max, EvalRookPawnAffinity.EG);
  UCIOptionNewSpin("RookMobilityFileMG", &EvalSetValue, &EvalRookMobFile.MG, Min, Max, EvalRookMobFile.MG);
  UCIOptionNewSpin("RookMobilityFileEG", &EvalSetValue, &EvalRookMobFile.EG, Min, Max, EvalRookMobFile.EG);
  UCIOptionNewSpin("RookMobilityRankMG", &EvalSetValue, &EvalRookMobRank.MG, Min, Max, EvalRookMobRank.MG);
  UCIOptionNewSpin("RookMobilityRankEG", &EvalSetValue, &EvalRookMobRank.EG, Min, Max, EvalRookMobRank.EG);
  UCIOptionNewSpin("RookOpenFileMG", &EvalSetValue, &EvalRookOpenFile.MG, Min, Max, EvalRookOpenFile.MG);
  UCIOptionNewSpin("RookOpenFileEG", &EvalSetValue, &EvalRookOpenFile.EG, Min, Max, EvalRookOpenFile.EG);
  UCIOptionNewSpin("RookSemiOpenFileMG", &EvalSetValue, &EvalRookSemiOpenFile.MG, Min, Max, EvalRookSemiOpenFile.MG);
  UCIOptionNewSpin("RookSemiOpenFileEG", &EvalSetValue, &EvalRookSemiOpenFile.EG, Min, Max, EvalRookSemiOpenFile.EG);
  UCIOptionNewSpin("RookOn7thMG", &EvalSetValue, &EvalRookOn7th.MG, Min, Max, EvalRookOn7th.MG);
  UCIOptionNewSpin("RookOn7thEG", &EvalSetValue, &EvalRookOn7th.EG, Min, Max, EvalRookOn7th.EG);
  UCIOptionNewSpin("RookTrappedMG", &EvalSetValue, &EvalRookTrapped.MG, Min, Max, EvalRookTrapped.MG);
  UCIOptionNewSpin("RookTrappedEG", &EvalSetValue, &EvalRookTrapped.EG, Min, Max, EvalRookTrapped.EG);
  UCIOptionNewSpin("KingShieldCloseMG", &EvalSetValue, &EvalKingShieldClose.MG, Min, Max, EvalKingShieldClose.MG);
  UCIOptionNewSpin("KingShieldCloseEG", &EvalSetValue, &EvalKingShieldClose.EG, Min, Max, EvalKingShieldClose.EG);
  UCIOptionNewSpin("KingShieldFarMG", &EvalSetValue, &EvalKingShieldFar.MG, Min, Max, EvalKingShieldFar.MG);
  UCIOptionNewSpin("KingShieldFarEG", &EvalSetValue, &EvalKingShieldFar.EG, Min, Max, EvalKingShieldFar.EG);
  UCIOptionNewSpin("TempoMG", &EvalSetValue, &EvalTempoDefault.MG, Min, Max, EvalTempoDefault.MG);
  UCIOptionNewSpin("TempoEG", &EvalSetValue, &EvalTempoDefault.EG, Min, Max, EvalTempoDefault.EG);
  UCIOptionNewSpin("WeightFactor", &EvalSetValue, &EvalWeightFactor, 1, 1024, EvalWeightFactor);
# endif
}

void EvalQuit()
{
  EvalPawnFree();
  EvalMatFree();
}

score_t Evaluate(const pos_t *Pos)
{
  // Evaluation function depends on material combination
  evalmatdata_t MatData;
  EvalMat(Pos, &MatData);
  
  // Evaluate
  vpair_t Score=(*MatData.Function)(Pos);
  
  // Material combination offset
  EvalVPairAdd(&Score, MatData.Offset);
  
  // Tempo bonus
  if (PosGetSTM(Pos)==white)
    EvalVPairAdd(&Score, MatData.Tempo);
  else
    EvalVPairSub(&Score, MatData.Tempo);
  
  // Interpolate score based on phase of the game and special material combination considerations
  score_t ScalarScore=EvalInterpolate(Pos, &Score, &MatData);
  
  // Drag score towards 0 as we approach 50-move rule
  int HMoves=PosGetHalfMoveClock(Pos);
  ScalarScore*=exp2(-(HMoves*HMoves)/(32*32));
  
  // Adjust for side to move
  if (PosGetSTM(Pos)==black)
    ScalarScore=-ScalarScore;
  
  return ScalarScore;
}

void EvalReset()
{
  EvalPawnReset();
  EvalMatReset();
}

////////////////////////////////////////////////////////////////////////////////
// Private functions
////////////////////////////////////////////////////////////////////////////////

vpair_t EvaluateDefault(const pos_t *Pos)
{
  vpair_t Score={0,0};
  const sq_t *Sq, *SqEnd;
  
  // Pawns
  evalpawndata_t PawnData=EvalPawns(Pos);
  EvalVPairAdd(&Score, PawnData.Score);
  
  // Knights
  Sq=PosGetPieceListStart(Pos, wknight);
  SqEnd=PosGetPieceListEnd(Pos, wknight);
  for(;Sq<SqEnd;++Sq)
    EvalVPairAdd(&Score, EvalKnight(Pos, *Sq, white, &PawnData));
  Sq=PosGetPieceListStart(Pos, bknight);
  SqEnd=PosGetPieceListEnd(Pos, bknight);
  for(;Sq<SqEnd;++Sq)
    EvalVPairSub(&Score, EvalKnight(Pos, *Sq, black, &PawnData));
  
  // Bishops
  Sq=PosGetPieceListStart(Pos, wbishopl);
  SqEnd=PosGetPieceListEnd(Pos, wbishopl);
  for(;Sq<SqEnd;++Sq)
    EvalVPairAdd(&Score, EvalBishop(Pos, *Sq, white, &PawnData));
  Sq=PosGetPieceListStart(Pos, wbishopd);
  SqEnd=PosGetPieceListEnd(Pos, wbishopd);
  for(;Sq<SqEnd;++Sq)
    EvalVPairAdd(&Score, EvalBishop(Pos, *Sq, white, &PawnData));
  Sq=PosGetPieceListStart(Pos, bbishopl);
  SqEnd=PosGetPieceListEnd(Pos, bbishopl);
  for(;Sq<SqEnd;++Sq)
    EvalVPairSub(&Score, EvalBishop(Pos, *Sq, black, &PawnData));
  Sq=PosGetPieceListStart(Pos, bbishopd);
  SqEnd=PosGetPieceListEnd(Pos, bbishopd);
  for(;Sq<SqEnd;++Sq)
    EvalVPairSub(&Score, EvalBishop(Pos, *Sq, black, &PawnData));
  
  // Rooks
  Sq=PosGetPieceListStart(Pos, wrook);
  SqEnd=PosGetPieceListEnd(Pos, wrook);
  for(;Sq<SqEnd;++Sq)
    EvalVPairAdd(&Score, EvalRook(Pos, *Sq, white, &PawnData));
  Sq=PosGetPieceListStart(Pos, brook);
  SqEnd=PosGetPieceListEnd(Pos, brook);
  for(;Sq<SqEnd;++Sq)
    EvalVPairSub(&Score, EvalRook(Pos, *Sq, black, &PawnData));
  
  // Queens
  Sq=PosGetPieceListStart(Pos, wqueen);
  SqEnd=PosGetPieceListEnd(Pos, wqueen);
  for(;Sq<SqEnd;++Sq)
    EvalVPairAdd(&Score, EvalQueen(Pos, *Sq, white, &PawnData));
  Sq=PosGetPieceListStart(Pos, bqueen);
  SqEnd=PosGetPieceListEnd(Pos, bqueen);
  for(;Sq<SqEnd;++Sq)
    EvalVPairSub(&Score, EvalQueen(Pos, *Sq, black, &PawnData));
  
  // Kings
  EvalVPairAdd(&Score, EvalKing(Pos, PosGetKingSq(Pos, white), white, &PawnData));
  EvalVPairSub(&Score, EvalKing(Pos, PosGetKingSq(Pos, black), black, &PawnData));
  
  return Score;
}

void EvalMat(const pos_t *Pos, evalmatdata_t *MatData)
{
  // Check if we already have data in hash table
  if (!EvalMatRead(Pos, MatData))
  {
    // No match found, compute and store
    EvalComputeMat(Pos, MatData);
    EvalMatWrite(Pos, MatData);
  }
}

void EvalComputeMat(const pos_t *Pos, evalmatdata_t *MatData)
{
  #define M(P,N) (POSMAT_MAKE((P),(N)))
  
  // Init data
  MatData->Mat=PosGetMat(Pos);
  MatData->Function=&EvaluateDefault;
  MatData->Offset.MG=MatData->Offset.EG=0;
  MatData->Tempo=EvalTempoDefault;
  uint64_t Mat=(MatData->Mat & ~(POSMAT_MASK(wking) | POSMAT_MASK(bking)));
  bool WBishopL=((Mat & POSMAT_MASK(wbishopl))!=0);
  bool WBishopD=((Mat & POSMAT_MASK(wbishopd))!=0);
  bool BBishopL=((Mat & POSMAT_MASK(bbishopl))!=0);
  bool BBishopD=((Mat & POSMAT_MASK(bbishopd))!=0);
  
  // Find weights for middlegame and endgame
  unsigned int MinCount=POSMAT_GET(Mat, wknight)+POSMAT_GET(Mat, wbishopl)+POSMAT_GET(Mat, wbishopd)+
               POSMAT_GET(Mat, bknight)+POSMAT_GET(Mat, bbishopl)+POSMAT_GET(Mat, bbishopd);
  unsigned int RCount=POSMAT_GET(Mat, wrook)+POSMAT_GET(Mat, brook);
  unsigned int QCount=POSMAT_GET(Mat, wqueen)+POSMAT_GET(Mat, bqueen);
  unsigned int Weight=MinCount+2*RCount+4*QCount;
  assert(Weight<128);
  MatData->WeightEG=EvalWeightEGFactor[Weight];
  MatData->WeightMG=256-MatData->WeightEG;
  
  // Specific material combinations
  /* (currently unused)
  unsigned int Factor=1024;
  [combination logic here]
  MatData->WeightMG=(MatData->WeightMG*Factor)/1024;
  MatData->WeightEG=(MatData->WeightEG*Factor)/1024;
  */
  
  // Material
  EvalVPairAddMul(&MatData->Offset, EvalMaterial[pawn], POSMAT_GET(Mat, wpawn)-POSMAT_GET(Mat, bpawn));
  EvalVPairAddMul(&MatData->Offset, EvalMaterial[knight], POSMAT_GET(Mat, wknight)-POSMAT_GET(Mat, bknight));
  EvalVPairAddMul(&MatData->Offset, EvalMaterial[bishopl], (POSMAT_GET(Mat, wbishopl)+POSMAT_GET(Mat, wbishopd))-(POSMAT_GET(Mat, bbishopl)+POSMAT_GET(Mat, bbishopd)));
  EvalVPairAddMul(&MatData->Offset, EvalMaterial[rook], POSMAT_GET(Mat, wrook)-POSMAT_GET(Mat, brook));
  EvalVPairAddMul(&MatData->Offset, EvalMaterial[queen], POSMAT_GET(Mat, wqueen)-POSMAT_GET(Mat, bqueen));
  
  // Knight pawn affinity
  int KnightAffW=POSMAT_GET(Mat, wknight)*(POSMAT_GET(Mat, wpawn)-5);
  int KnightAffB=POSMAT_GET(Mat, bknight)*(POSMAT_GET(Mat, bpawn)-5);
  EvalVPairAddMul(&MatData->Offset, EvalKnightPawnAffinity, KnightAffW-KnightAffB);
  
  // Rook pawn affinity
  int RookAffW=POSMAT_GET(Mat, wrook)*(POSMAT_GET(Mat, wpawn)-5);
  int RookAffB=POSMAT_GET(Mat, brook)*(POSMAT_GET(Mat, bpawn)-5);
  EvalVPairAddMul(&MatData->Offset, EvalRookPawnAffinity, RookAffW-RookAffB);
  
  // Bishop pair bonus
  if (WBishopL && WBishopD)
    EvalVPairAdd(&MatData->Offset, EvalBishopPair);
  if (BBishopL && BBishopD)
    EvalVPairSub(&MatData->Offset, EvalBishopPair);
  
  #undef M
}

evalpawndata_t EvalPawns(const pos_t *Pos)
{
  evalpawndata_t PawnData;
  if (!EvalPawnRead(Pos, &PawnData))
  {
    EvalComputePawns(Pos, &PawnData);
    EvalPawnWrite(Pos, &PawnData);
  }
  
  return PawnData;
}

static inline void EvalComputePawns(const pos_t *Pos, evalpawndata_t *Data)
{
  // Init
  bb_t WP=Data->Pawns[white]=PosGetBBPiece(Pos, wpawn);
  bb_t BP=Data->Pawns[black]=PosGetBBPiece(Pos, bpawn);
  bb_t Occ=PosGetBBAll(Pos);
  Data->Score.MG=Data->Score.EG=0;
  const sq_t *Sq, *SqEnd;
  bb_t FrontSpanW=BBNorthOne(BBNorthFill(WP));
  bb_t FrontSpanB=BBSouthOne(BBSouthFill(BP));
  bb_t RearSpanW=BBSouthOne(BBSouthFill(WP));
  bb_t RearSpanB=BBNorthOne(BBNorthFill(BP));
  bb_t AttacksW=BBNorthOne(BBWingify(WP));
  bb_t AttacksB=BBSouthOne(BBWingify(BP));
  bb_t AttacksWFill=BBFileFill(AttacksW);
  bb_t AttacksBFill=BBFileFill(AttacksB);
  bb_t PotPassedW=~(BBWingify(FrontSpanB) | FrontSpanB);
  bb_t PotPassedB=~(BBWingify(FrontSpanW) | FrontSpanW);
  bb_t FillW=BBFileFill(WP), FillB=BBFileFill(BP);
  
  // Calculate open files (used in other parts of the evaluation)
  Data->SemiOpenFiles[white]=(FillB & ~FillW);
  Data->SemiOpenFiles[black]=(FillW & ~FillB);
  Data->OpenFiles=~(FillW | FillB);
  Data->Passed[white]=Data->Passed[black]=0;
  
  // Loop over every pawn
  Sq=PosGetPieceListStart(Pos, wpawn);
  SqEnd=PosGetPieceListEnd(Pos, wpawn);
  for(;Sq<SqEnd;++Sq)
  {
    // Calculate properties
    bb_t BB=SQTOBB(*Sq);
    bool Doubled=((BB & RearSpanW)!=0);
    bool Isolated=((BB & AttacksWFill)==0);
    bool Blocked=((BB & BBSouthOne(Occ))!=0);
    bool Passed=((BB & PotPassedW)!=0);
    
    // Calculate score
    EvalVPairAdd(&Data->Score, EvalPawnPST[*Sq]);
    if (Doubled)
      EvalVPairAdd(&Data->Score, EvalPawnDoubled);
    else if (Passed)
    {
      EvalVPairAdd(&Data->Score, EvalPawnPassed[SQ_Y(*Sq)]);
      Data->Passed[white]|=BB;
    }
    if (Isolated)
      EvalVPairAdd(&Data->Score, EvalPawnIsolated);
    if (Blocked)
      EvalVPairAdd(&Data->Score, EvalPawnBlocked);
  }
  Sq=PosGetPieceListStart(Pos, bpawn);
  SqEnd=PosGetPieceListEnd(Pos, bpawn);
  for(;Sq<SqEnd;++Sq)
  {
    // Calculate properties
    bb_t BB=SQTOBB(*Sq);
    bool Doubled=((BB & RearSpanB)!=0);
    bool Isolated=((BB & AttacksBFill)==0);
    bool Blocked=((BB & BBNorthOne(Occ))!=0);
    bool Passed=((BB & PotPassedB)!=0);
    
    // Calculate score
    EvalVPairSub(&Data->Score, EvalPawnPST[SQ_FLIP(*Sq)]);
    if (Doubled)
      EvalVPairSub(&Data->Score, EvalPawnDoubled);
    else if (Passed)
    {
      EvalVPairSub(&Data->Score, EvalPawnPassed[SQ_Y(SQ_FLIP(*Sq))]);
      Data->Passed[black]|=BB;
    }
    if (Isolated)
      EvalVPairSub(&Data->Score, EvalPawnIsolated);
    if (Blocked)
      EvalVPairSub(&Data->Score, EvalPawnBlocked);
  }
}

static inline vpair_t EvalKnight(const pos_t *Pos, sq_t Sq, col_t Colour, const evalpawndata_t *PawnData)
{
  vpair_t Score={0,0};
  sq_t AdjSq=(Colour==white ? Sq : SQ_FLIP(Sq));
  
  // PST
  EvalVPairAdd(&Score, EvalKnightPST[AdjSq]);
  
  return Score;
}

static inline vpair_t EvalBishop(const pos_t *Pos, sq_t Sq, col_t Colour, const evalpawndata_t *PawnData)
{
  vpair_t Score={0,0};
  sq_t AdjSq=(Colour==white ? Sq : SQ_FLIP(Sq));
  
  // PST
  EvalVPairAdd(&Score, EvalBishopPST[AdjSq]);
  
  // Mobility
  bb_t Attacks=AttacksBishop(Sq, PosGetBBAll(Pos));
  EvalVPairAddMul(&Score, EvalBishopMob, BBPopCount(Attacks)-6);
  
  return Score;
}

static inline vpair_t EvalRook(const pos_t *Pos, sq_t Sq, col_t Colour, const evalpawndata_t *PawnData)
{
  vpair_t Score={0,0};
  bb_t BB=SQTOBB(Sq);
  sq_t AdjSq=(Colour==white ? Sq : SQ_FLIP(Sq));
  bb_t Rank=BBSqToRank(Sq);
  
  // Mobility
  bb_t Attacks=AttacksRook(Sq, PosGetBBAll(Pos));
  EvalVPairAddMul(&Score, EvalRookMobFile, BBPopCount(Attacks & BBFileFill(BB)));
  EvalVPairAddMul(&Score, EvalRookMobRank, BBPopCount(Attacks & Rank));
  
  // Open and semi-open files
  if (BB & PawnData->OpenFiles)
    EvalVPairAdd(&Score, EvalRookOpenFile);
  else if (BB & PawnData->SemiOpenFiles[Colour])
    EvalVPairAdd(&Score, EvalRookSemiOpenFile);
  
  // Rook on 7th
  bb_t OppPawns=PosGetBBPiece(Pos, PIECE_MAKE(pawn, COL_SWAP(Colour)));
  sq_t AdjOppKingSq=(Colour==white ? PosGetKingSq(Pos, black) :
                                     SQ_FLIP(PosGetKingSq(Pos, white)));
  if (SQ_Y(AdjSq)==6 && ((Rank & OppPawns) || SQ_Y(AdjOppKingSq)==7))
    EvalVPairAdd(&Score, EvalRookOn7th);
  
  // Trapped
  bb_t KingBB=PosGetBBPiece(Pos, PIECE_MAKE(king, Colour));
  if (Colour==white)
  {
    if (((BB & (BBG1 | BBH1)) && (KingBB & (BBF1 | BBG1))) ||
        ((BB & (BBA1 | BBB1)) && (KingBB & (BBB1 | BBC1))))
      EvalVPairAdd(&Score, EvalRookTrapped);
  }
  else
  {
    if (((BB & (BBG8 | BBH8)) && (KingBB & (BBF8 | BBG8))) ||
        ((BB & (BBA8 | BBB8)) && (KingBB & (BBB8 | BBC8))))
      EvalVPairAdd(&Score, EvalRookTrapped);
  }
  
  return Score;
}

static inline vpair_t EvalQueen(const pos_t *Pos, sq_t Sq, col_t Colour, const evalpawndata_t *PawnData)
{
  vpair_t Score={0,0};
  
  return Score;
}

static inline vpair_t EvalKing(const pos_t *Pos, sq_t Sq, col_t Colour, const evalpawndata_t *PawnData)
{
  vpair_t Score={0,0};
  bb_t BB=SQTOBB(Sq), Set;
  bb_t Pawns=PosGetBBPiece(Pos, PIECE_MAKE(pawn, Colour));
  sq_t AdjSq=(Colour==white ? Sq : SQ_FLIP(Sq));
  
  // PST
  EvalVPairAdd(&Score, EvalKingPST[AdjSq]);
  
  // Pawn shield
  Set=BBForwardOne(BBWestOne(BB) | BB | BBEastOne(BB), Colour);
  bb_t ShieldClose=(Pawns & Set);
  bb_t ShieldFar=(Pawns & BBForwardOne(Set, Colour));
  EvalVPairAddMul(&Score, EvalKingShieldClose, BBPopCount(ShieldClose));
  EvalVPairAddMul(&Score, EvalKingShieldFar, BBPopCount(ShieldFar));
  
  return Score;
}

static inline score_t EvalInterpolate(const pos_t *Pos, const vpair_t *Score, const evalmatdata_t *Data)
{
  // Interpolate and also scale to centi-pawns
  return ((Data->WeightMG*Score->MG+Data->WeightEG*Score->EG)*100)/
          (Data->WeightMG*EvalMaterial[pawn].MG+Data->WeightEG*EvalMaterial[pawn].EG);
}

void EvalPawnResize(size_t SizeMB)
{
  // Calculate greatest power of two number of entries we can fit in SizeMB
  uint64_t Entries=(((uint64_t)SizeMB)*1024llu*1024llu)/sizeof(evalpawndata_t);
  Entries=NextPowTwo64(Entries+1)/2;
  
  // Attempt to allocate table
  while(Entries>0)
  {
    evalpawndata_t *Ptr=realloc(EvalPawnTable, Entries*sizeof(evalpawndata_t));
    if (Ptr!=NULL)
    {
      // Update table
      EvalPawnTable=Ptr;
      EvalPawnTableSize=Entries;
      
      // Clear entries
      EvalPawnReset();
      
      return;
    }
    Entries/=2;
  }
  
  // Could not allocate
  EvalPawnFree();
}

void EvalPawnFree()
{
  free(EvalPawnTable);
  EvalPawnTable=NULL;
  EvalPawnTableSize=0;
}

void EvalPawnReset()
{
  memset(EvalPawnTable, 0, EvalPawnTableSize*sizeof(evalpawndata_t)); // HACK
}

static inline bool EvalPawnRead(const pos_t *Pos, evalpawndata_t *Data)
{
  if (EvalPawnTable==NULL)
    return false;
  
  int Index=(PosGetPawnKey(Pos) & (EvalPawnTableSize-1));
  evalpawndata_t *Entry=&EvalPawnTable[Index];
  if (Entry->Pawns[white]!=PosGetBBPiece(Pos, wpawn) ||
      Entry->Pawns[black]!=PosGetBBPiece(Pos, bpawn))
    return false;
  
  *Data=*Entry;
  return true;
}

static inline void EvalPawnWrite(const pos_t *Pos, evalpawndata_t *Data)
{
  if (EvalPawnTable==NULL)
    return;
  int Index=(PosGetPawnKey(Pos) & (EvalPawnTableSize-1));
  EvalPawnTable[Index]=*Data;
}

void EvalMatResize(size_t SizeKB)
{
  // Calculate greatest power of two number of entries we can fit in SizeKB
  uint64_t Entries=(((uint64_t)SizeKB)*1024llu)/sizeof(evalmatdata_t);
  Entries=NextPowTwo64(Entries+1)/2;
  
  // Attempt to allocate table
  while(Entries>0)
  {
    evalmatdata_t *Ptr=realloc(EvalMatTable, Entries*sizeof(evalmatdata_t));
    if (Ptr!=NULL)
    {
      // Update table
      EvalMatTable=Ptr;
      EvalMatTableSize=Entries;
      
      // Clear entries
      EvalMatReset();
      
      return;
    }
    Entries/=2;
  }
  
  // Could not allocate 
  EvalMatFree();
}

void EvalMatFree()
{
  free(EvalMatTable);
  EvalMatTable=NULL;
  EvalMatTableSize=0;
}
void EvalMatReset()
{
  memset(EvalMatTable, 0, EvalMatTableSize*sizeof(evalmatdata_t)); // HACK
}

static inline bool EvalMatRead(const pos_t *Pos, evalmatdata_t *Data)
{
  if (EvalMatTable==NULL)
    return false;
  
  int Index=(PosGetMatKey(Pos) & (EvalMatTableSize-1));
  evalmatdata_t *Entry=&EvalMatTable[Index];
  if (Entry->Mat!=PosGetMat(Pos))
    return false;
  
  *Data=*Entry;
  return true;
}

static inline void EvalMatWrite(const pos_t *Pos, evalmatdata_t *Data)
{
  if (EvalMatTable==NULL)
    return;
  int Index=(PosGetMatKey(Pos) & (EvalMatTableSize-1));
  EvalMatTable[Index]=*Data;
}

static inline void EvalVPairAdd(vpair_t *A, vpair_t B)
{
  A->MG+=B.MG;
  A->EG+=B.EG;
}

static inline void EvalVPairSub(vpair_t *A, vpair_t B)
{
  A->MG-=B.MG;
  A->EG-=B.EG;
}

static inline void EvalVPairAddMul(vpair_t *A, vpair_t B, int C)
{
  A->MG+=B.MG*C;
  A->EG+=B.EG*C;
}

static inline void EvalVPairSubMul(vpair_t *A, vpair_t B, int C)
{
  A->MG-=B.MG*C;
  A->EG-=B.EG*C;
}

#ifdef TUNE
void EvalSetValue(int Value, void *UserData)
{
  // Set value
  *((value_t *)UserData)=Value;
  
  // Hack for bishops
  if (((value_t *)UserData)==&EvalMaterial[bishopl].MG)
    EvalMaterial[bishopd].MG=Value;
  else if (((value_t *)UserData)==&EvalMaterial[bishopl].EG)
    EvalMaterial[bishopd].EG=Value;
  
  // Recalculate dervied values (such as passed pawn table)
  EvalRecalc();
  
  // Clear now-invalid material and pawn tables etc.
  EvalReset();
}
#endif

void EvalRecalc()
{
  // Generate passed pawn array from quadratic coefficients
  int Rank;
  for(Rank=0;Rank<8;++Rank)
  {
    EvalPawnPassed[Rank].MG=EvalPawnPassedQuadA.MG*Rank*Rank+EvalPawnPassedQuadB.MG*Rank+EvalPawnPassedQuadC.MG;
    EvalPawnPassed[Rank].EG=EvalPawnPassedQuadA.EG*Rank*Rank+EvalPawnPassedQuadB.EG*Rank+EvalPawnPassedQuadC.EG;
  }
  
  // Calculate factor for each material weight
  int Weight;
  for(Weight=0;Weight<128;++Weight)
  {
    int Factor=(255.0*exp2f(-(Weight*Weight)/((float)EvalWeightFactor)));
    assert(Factor>=0 && Factor<256);
    EvalWeightEGFactor[Weight]=Factor;
  }
}
