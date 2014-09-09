#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "attacks.h"
#include "bb.h"
#include "colour.h"
#include "eval.h"
#include "htable.h"
#include "main.h"
#include "piece.h"
#include "square.h"
#include "tune.h"
#include "uci.h"

typedef int32_t Value;
typedef struct { Value mg, eg; } VPair;
const VPair VPairZero={0,0};

typedef struct EvalData EvalData;

typedef struct
{
  BB pawns[ColourNB], passed[ColourNB], semiOpenFiles[ColourNB], openFiles;
  VPair score;
}EvalPawnData;
HTable *evalPawnTable=NULL;
const size_t evalPawnTableDefaultSizeMb=1;
const size_t evalPawnTableMaxSizeMb=1024*1024; // 1tb

typedef struct
{
  MatInfo mat;
  EvalMatType type; // If this is EvalMatTypeInvalid implies not yet computed.
  VPair (*function)(EvalData *data); // If this is NULL implies all entries below have yet to be computed.
  VPair offset, tempo;
  uint8_t weightMG, weightEG;
  Score scoreOffset;
}EvalMatData;
HTable *evalMatTable=NULL;
const size_t evalMatTableDefaultSizeMb=1;
const size_t evalMatTableMaxSizeMb=1024*1024; // 1tb

struct EvalData
{
  const Pos *pos;
  EvalPawnData pawnData;
  EvalMatData matData;
};

////////////////////////////////////////////////////////////////////////////////
// Tunable values
////////////////////////////////////////////////////////////////////////////////

TUNECONST VPair evalMaterial[PieceTypeNB]={
  [PieceTypeNone]={0,0},
  [PieceTypePawn]={900,1300},
  [PieceTypeKnight]={3100,3100},
  [PieceTypeBishopL]={3010,3070},
  [PieceTypeBishopD]={3010,3070},
  [PieceTypeRook]={5350,5350},
  [PieceTypeQueen]={10000,10000},
  [PieceTypeKing]={0,0}};
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
TUNECONST VPair evalRookPawnAffinity={-70,-70}; // Bonus each rook receives for each friendly pawn on the board.
TUNECONST VPair evalRookMobFile={20,30};
TUNECONST VPair evalRookMobRank={10,20};
TUNECONST VPair evalRookOpenFile={100,50};
TUNECONST VPair evalRookSemiOpenFile={50,20};
TUNECONST VPair evalRookOn7th={50,100};
TUNECONST VPair evalRookTrapped={-400,0};
TUNECONST VPair evalKingShieldClose={150,0};
TUNECONST VPair evalKingShieldFar={50,0};
TUNECONST VPair evalTempoDefault={35,0};
TUNECONST Value evalWeightFactor=144;

VPair evalPST[PieceTypeNB][SqNB]={
[PieceTypeNone]={
  {    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0},
  {    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0},
  {    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0},
  {    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0},
  {    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0},
  {    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0},
  {    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0},
  {    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0}},
[PieceTypePawn]={
  {    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0},
  {    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0},
  {    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0},
  {    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0},
  {    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0},
  {    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0},
  {    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0},
  {    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0}},
[PieceTypeKnight]={
  { -170, -120},{ -120,  -60},{  -80,  -30},{  -60,  -10},{  -60,  -10},{  -80,  -30},{ -120,  -60},{ -170, -120},
  { -110,  -60},{  -60,  -10},{  -30,   20},{  -10,   30},{  -10,   30},{  -30,   20},{  -60,  -10},{ -110,  -60},
  {  -70,  -30},{  -20,   20},{   10,   50},{   20,   60},{   20,   60},{   10,   50},{  -20,   20},{  -70,  -30},
  {  -40,  -10},{   10,   30},{   30,   60},{   40,   70},{   40,   70},{   30,   60},{   10,   30},{  -40,  -10},
  {  -10,  -10},{   30,   30},{   60,   60},{   60,   70},{   60,   70},{   60,   60},{   30,   30},{  -10,  -10},
  {    0,  -30},{   40,   20},{   70,   50},{   80,   60},{   80,   60},{   70,   50},{   40,   20},{    0,  -30},
  {  -10,  -60},{   40,  -10},{   70,   20},{   90,   30},{   90,   30},{   70,   20},{   40,  -10},{  -10,  -60},
  {  -20, -120},{   20,  -60},{   60,  -30},{   80,  -10},{   80,  -10},{   60,  -30},{   20,  -60},{  -20,  -12}},
[PieceTypeBishopL]={
  {   80,   60},{   80,   60},{   80,   60},{   80,   60},{   80,   60},{   80,   60},{   80,   60},{   80,   60},
  {   80,   60},{  120,   90},{  120,   90},{  120,   90},{  120,   90},{  120,   90},{  120,   90},{   80,   60},
  {   80,   60},{  120,   90},{  120,   90},{  120,   90},{  120,   90},{  120,   90},{  120,   90},{   80,   60},
  {   80,   60},{  120,   90},{  120,   90},{  160,  120},{  160,  120},{  120,   90},{  120,   90},{   80,   60},
  {   80,   60},{  120,   90},{  120,   90},{  160,  120},{  160,  120},{  120,   90},{  120,   90},{   80,   60},
  {   80,   60},{  120,   90},{  120,   90},{  120,   90},{  120,   90},{  120,   90},{  120,   90},{   80,   60},
  {   80,   60},{  120,   90},{  120,   90},{  120,   90},{  120,   90},{  120,   90},{  120,   90},{   80,   60},
  {   80,   60},{   80,   60},{   80,   60},{   80,   60},{   80,   60},{   80,   60},{   80,   60},{   80,   60}},
[PieceTypeBishopD]={
  {   80,   60},{   80,   60},{   80,   60},{   80,   60},{   80,   60},{   80,   60},{   80,   60},{   80,   60},
  {   80,   60},{  120,   90},{  120,   90},{  120,   90},{  120,   90},{  120,   90},{  120,   90},{   80,   60},
  {   80,   60},{  120,   90},{  120,   90},{  120,   90},{  120,   90},{  120,   90},{  120,   90},{   80,   60},
  {   80,   60},{  120,   90},{  120,   90},{  160,  120},{  160,  120},{  120,   90},{  120,   90},{   80,   60},
  {   80,   60},{  120,   90},{  120,   90},{  160,  120},{  160,  120},{  120,   90},{  120,   90},{   80,   60},
  {   80,   60},{  120,   90},{  120,   90},{  120,   90},{  120,   90},{  120,   90},{  120,   90},{   80,   60},
  {   80,   60},{  120,   90},{  120,   90},{  120,   90},{  120,   90},{  120,   90},{  120,   90},{   80,   60},
  {   80,   60},{   80,   60},{   80,   60},{   80,   60},{   80,   60},{   80,   60},{   80,   60},{   80,   60}},
[PieceTypeRook]={
  {  -15,    0},{    0,    0},{   15,    0},{   30,    0},{   30,    0},{   15,    0},{    0,    0},{  -15,    0},
  {  -15,    0},{    0,    0},{   15,    0},{   30,    0},{   30,    0},{   15,    0},{    0,    0},{  -15,    0},
  {  -15,    0},{    0,    0},{   15,    0},{   30,    0},{   30,    0},{   15,    0},{    0,    0},{  -15,    0},
  {  -15,    0},{    0,    0},{   15,    0},{   30,    0},{   30,    0},{   15,    0},{    0,    0},{  -15,    0},
  {  -15,    0},{    0,    0},{   15,    0},{   30,    0},{   30,    0},{   15,    0},{    0,    0},{  -15,    0},
  {  -15,    0},{    0,    0},{   15,    0},{   30,    0},{   30,    0},{   15,    0},{    0,    0},{  -15,    0},
  {  -15,    0},{    0,    0},{   15,    0},{   30,    0},{   30,    0},{   15,    0},{    0,    0},{  -15,    0},
  {  -15,    0},{    0,    0},{   15,    0},{   30,    0},{   30,    0},{   15,    0},{    0,    0},{  -15,    0}},
[PieceTypeQueen]={
  {  -50,    0},{  -50,    0},{  -50,    0},{  -50,    0},{  -50,    0},{  -50,    0},{  -50,    0},{  -50,    0},
  {  -50,    0},{  -50,    0},{  -50,    0},{  -50,    0},{  -50,    0},{  -50,    0},{  -50,    0},{  -50,    0},
  {  -50,    0},{  -50,    0},{  -50,    0},{  -50,    0},{  -50,    0},{  -50,    0},{  -50,    0},{  -50,    0},
  {  -50,    0},{  -50,    0},{  -50,    0},{  -50,    0},{  -50,    0},{  -50,    0},{  -50,    0},{  -50,    0},
  {  -25,    0},{  -25,    0},{  -25,    0},{  -25,    0},{  -25,    0},{  -25,    0},{  -25,    0},{  -25,    0},
  {    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0},{    0,    0},
  {   25,    0},{   25,    0},{   25,    0},{   25,    0},{   25,    0},{   25,    0},{   25,    0},{   25,    0},
  {   50,    0},{   50,    0},{   50,    0},{   50,    0},{   50,    0},{   50,    0},{   50,    0},{   50,    0}},
[PieceTypeKing]={
  {  570, -460},{  570, -240},{  350, -120},{  200,  -40},{  200,  -40},{  350, -120},{  570, -240},{  570, -460},
  {  350, -240},{  320,  -40},{  140,   60},{   30,  120},{   30,  120},{  140,   60},{  320,  -40},{  350, -240},
  {  100, -120},{   50,   60},{ -110,  180},{ -260,  240},{ -260,  240},{ -110,  180},{   50,   60},{  100, -120},
  {    0,  -40},{  -40,  120},{ -320,  240},{ -790,  260},{ -790,  260},{ -320,  240},{  -40,  120},{    0,  -40},
  {  -50,  -40},{ -110,  120},{ -390,  240},{ -860,  260},{ -860,  260},{ -390,  240},{ -110,  120},{  -50,  -40},
  {  160, -120},{ -100,   60},{ -320,  180},{ -480,  240},{ -480,  240},{ -320,  180},{ -100,   60},{  160, -120},
  {  200, -240},{  -30,  -40},{ -210,   60},{ -310,  120},{ -310,  120},{ -210,   60},{  -30,  -40},{  200, -240},
  {  290, -460},{   70, -240},{  -80, -120},{ -160,  -40},{ -160,  -40},{  -80, -120},{   70, -240},{  290,  -46}}};

////////////////////////////////////////////////////////////////////////////////
// Derived values
////////////////////////////////////////////////////////////////////////////////

VPair evalPawnValue[ColourNB][2][2][2][SqNB]; // [colour][isDoubled][isIsolated][isBlocked][square]
VPair evalPawnPassed[RankNB];
uint8_t evalWeightEGFactor[128];

////////////////////////////////////////////////////////////////////////////////
// Private prototypes
////////////////////////////////////////////////////////////////////////////////

VPair evaluateDefault(EvalData *data);
void evalGetMatData(const Pos *pos, EvalMatData *matData);
void evalComputeMatData(const Pos *pos, EvalMatData *matData);
void evalGetPawnData(const Pos *pos, EvalPawnData *pawnData);
void evalComputePawnData(const Pos *pos, EvalPawnData *pawnData);
VPair evalPiece(EvalData *data, PieceType type, Sq sq, Colour colour);
Score evalInterpolate(const EvalData *data, const VPair *score);
void evalVPairAdd(VPair *a, const VPair *b);
void evalVPairSub(VPair *a, const VPair *b);
void evalVPairAddMul(VPair *a, const VPair *b, int c);
void evalVPairSubMul(VPair *a, const VPair *b, int c);
#ifdef TUNE
void evalSetValue(void *varPtr, int value);
bool evalOptionNewVPair(const char *name, VPair *score);
#endif
void evalRecalc(void);
EvalMatType evalComputeMatType(const Pos *pos);

////////////////////////////////////////////////////////////////////////////////
// Public functions
////////////////////////////////////////////////////////////////////////////////

void evalInit(void)
{
  // Setup pawn hash table.
  EvalPawnData nullEntryPawn;
  nullEntryPawn.pawns[ColourWhite]=nullEntryPawn.pawns[ColourBlack]=BBAll; // No position can have pawns on all squares.
  evalPawnTable=htableNew(sizeof(EvalPawnData), &nullEntryPawn, evalPawnTableDefaultSizeMb);
  if (evalPawnTable==NULL)
    mainFatalError("Error: Could not allocate pawn hash table.\n");
  uciOptionNewSpin("PawnHash", &htableResizeInterface, evalPawnTable, 1, evalPawnTableMaxSizeMb, evalPawnTableDefaultSizeMb);
  uciOptionNewButton("Clear PawnHash", &htableClearInterface, evalPawnTable);
  
  // Setup mat hash table.
  EvalMatData nullEntryMat;
  nullEntryMat.mat=(matInfoMake(PieceWKing, 0)|matInfoMake(PieceBKing, 0)); // No position can have 0 pieces (kings are always required)
  evalMatTable=htableNew(sizeof(EvalMatData), &nullEntryMat, evalMatTableDefaultSizeMb);
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
  evalOptionNewVPair("RookPawnAffinity", &evalRookPawnAffinity);
  evalOptionNewVPair("RookMobilityFile", &evalRookMobFile);
  evalOptionNewVPair("RookMobilityRank", &evalRookMobRank);
  evalOptionNewVPair("RookOpenFile", &evalRookOpenFile);
  evalOptionNewVPair("RookSemiOpenFile", &evalRookSemiOpenFile);
  evalOptionNewVPair("RookOn7th", &evalRookOn7th);
  evalOptionNewVPair("RookTrapped", &evalRookTrapped);
  evalOptionNewVPair("KingShieldClose", &evalKingShieldClose);
  evalOptionNewVPair("KingShieldFar", &evalKingShieldFar);
  evalOptionNewVPair("Tempo", &evalTempoDefault);
  uciOptionNewSpin("WeightFactor", &evalSetValue, &evalWeightFactor, 1, 1024, evalWeightFactor);
# endif
}

void evalQuit(void)
{
  htableFree(evalPawnTable);
  evalPawnTable=NULL;
  htableFree(evalMatTable);
  evalMatTable=NULL;
}

Score evaluate(const Pos *pos)
{
  // Init data struct.
  EvalData data={.pos=pos};
  
  // Evaluation function depends on material combination.
  evalGetMatData(pos, &data.matData);
  
  // Evaluate.
  VPair score=data.matData.function(&data);
  
  // Material combination offset.
  evalVPairAdd(&score, &data.matData.offset);
  
  // Tempo bonus.
  if (posGetSTM(pos)==ColourWhite)
    evalVPairAdd(&score, &data.matData.tempo);
  else
    evalVPairSub(&score, &data.matData.tempo);
  
  // Interpolate score based on phase of the game and special material combination considerations.
  Score scalarScore=evalInterpolate(&data, &score);
  
  // Drag score towards 0 as we approach 50-move rule
  unsigned int halfMoves=posGetHalfMoveNumber(data.pos);
  scalarScore*=exp2f(-((halfMoves*halfMoves)/(32.0*32.0)));
  
  // Add score offset
  scalarScore+=data.matData.scoreOffset;
  
  // Adjust for side to move
  if (posGetSTM(data.pos)==ColourBlack)
    scalarScore=-scalarScore;
  
  return scalarScore;
}

void evalClear(void)
{
  htableClear(evalPawnTable);
  htableClear(evalMatTable);
}

EvalMatType evalGetMatType(const Pos *pos)
{
  // Grab hash entry for this position key
  uint64_t key=(uint64_t)posGetMatKey(pos);
  EvalMatData *entry=htableGrab(evalMatTable, key);
  
  // If not a match clear entry
  MatInfo mat=posGetMatInfo(pos);
  if (entry->mat!=mat)
  {
    entry->mat=mat;
    entry->type=EvalMatTypeInvalid;
    entry->function=NULL;
  }
  
  // If no data already, compute
  if (entry->type==EvalMatTypeInvalid)
    entry->type=evalComputeMatType(pos);
  
  // Copy data to return it
  EvalMatType type=entry->type;
  
  // We are finished with Entry, release lock
  htableRelease(evalMatTable, key);
  
  return type;
}

////////////////////////////////////////////////////////////////////////////////
// Private functions
////////////////////////////////////////////////////////////////////////////////

VPair evaluateDefault(EvalData *data)
{
  // Init.
  VPair score=VPairZero;
  const Pos *pos=data->pos;
  
  // Pawn base (special case).
  evalGetPawnData(pos, &data->pawnData);
  evalVPairAdd(&score, &data->pawnData.score);
  
  // All pieces.
  const Sq *sq, *sqEnd;
  PieceType type;
  Piece piece;
  for(type=PieceTypePawn;type<=PieceTypeKing;++type)
  {
    // White pieces.
    piece=pieceMake(type, ColourWhite);
    sq=posGetPieceListStart(pos, piece);
    sqEnd=posGetPieceListEnd(pos, piece);
    for(;sq<sqEnd;++sq)
    {
      VPair pieceScore=evalPiece(data, type, *sq, ColourWhite);
      evalVPairAdd(&score, &pieceScore);
    }
    
    // Black pieces.
    piece=pieceMake(type, ColourBlack);
    sq=posGetPieceListStart(pos, piece);
    sqEnd=posGetPieceListEnd(pos, piece);
    for(;sq<sqEnd;++sq)
    {
      VPair pieceScore=evalPiece(data, type, *sq, ColourBlack);
      evalVPairSub(&score, &pieceScore);
    }
  }
  
  return score;
}

void evalGetMatData(const Pos *pos, EvalMatData *matData)
{
  // Grab hash entry for this position key.
  uint64_t key=(uint64_t)posGetMatKey(pos);
  EvalMatData *entry=htableGrab(evalMatTable, key);
  
  // If not a match clear entry
  MatInfo mat=posGetMatInfo(pos);
  if (entry->mat!=mat)
  {
    entry->mat=mat;
    entry->type=EvalMatTypeInvalid;
    entry->function=NULL;
  }
  
  // If no data already, compute.
  if (entry->function==NULL)
    evalComputeMatData(pos, entry);
  
  // Copy data to return it.
  *matData=*entry;
  
  // We are finished with entry, release lock.
  htableRelease(evalMatTable, key);
}

void evalComputeMatData(const Pos *pos, EvalMatData *matData)
{
# define M(P,N) (matInfoMake((P),(N)))
# define G(P) (matInfoGetPieceCount(mat,(P))) // Hard-coded 'mat'.
  
  // Init data.
  assert(matData->mat==posGetMatInfo(pos));
  matData->function=&evaluateDefault;
  matData->offset=VPairZero;
  matData->tempo=evalTempoDefault;
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
  matData->weightEG=evalWeightEGFactor[pieceWeight];
  matData->weightMG=256-matData->weightEG;
  
  // Specific material combinations.
  /* (currently unused)
  unsigned int factor=1024;
  [combination logic here]
  matData->weightMG=(matData->weightMG*factor)/1024;
  matData->weightEG=(matData->weightEG*factor)/1024;
  */
  
  // Material.
  evalVPairAddMul(&matData->offset, &evalMaterial[PieceTypePawn], G(PieceWPawn)-G(PieceBPawn));
  evalVPairAddMul(&matData->offset, &evalMaterial[PieceTypeKnight], G(PieceWKnight)-G(PieceBKnight));
  evalVPairAddMul(&matData->offset, &evalMaterial[PieceTypeBishopL], whiteBishopCount-blackBishopCount);
  evalVPairAddMul(&matData->offset, &evalMaterial[PieceTypeRook], G(PieceWRook)-G(PieceBRook));
  evalVPairAddMul(&matData->offset, &evalMaterial[PieceTypeQueen], G(PieceWQueen)-G(PieceBQueen));
  
  // Knight pawn affinity.
  unsigned int knightAffW=G(PieceWKnight)*G(PieceWPawn);
  unsigned int knightAffB=G(PieceBKnight)*G(PieceBPawn);
  evalVPairAddMul(&matData->offset, &evalKnightPawnAffinity, knightAffW-knightAffB);
  
  // Rook pawn affinity.
  unsigned int rookAffW=G(PieceWRook)*G(PieceWPawn);
  unsigned int rookAffB=G(PieceBRook)*G(PieceBPawn);
  evalVPairAddMul(&matData->offset, &evalRookPawnAffinity, rookAffW-rookAffB);
  
  // Bishop pair bonus
  if (wBishopL && wBishopD)
    evalVPairAdd(&matData->offset, &evalBishopPair);
  if (bBishopL && bBishopD)
    evalVPairSub(&matData->offset, &evalBishopPair);
  
# undef G
# undef M
}

void evalGetPawnData(const Pos *pos, EvalPawnData *pawnData)
{
  // Grab hash entry for this position key.
  uint64_t key=(uint64_t)posGetPawnKey(pos);
  EvalPawnData *entry=htableGrab(evalPawnTable, key);
  
  // If not a match recompute data.
  if (entry->pawns[ColourWhite]!=posGetBBPiece(pos, PieceWPawn) ||
      entry->pawns[ColourBlack]!=posGetBBPiece(pos, PieceBPawn))
    evalComputePawnData(pos, entry);
  
  // Copy data to return it.
  *pawnData=*entry;
  
  // We are finished with Entry, release lock.
  htableRelease(evalPawnTable, key);
}

void evalComputePawnData(const Pos *pos, EvalPawnData *pawnData)
{
  // Init.
  pawnData->score=VPairZero;
  BB occ=posGetBBAll(pos);
  BB pawns[ColourNB], frontSpan[ColourNB], rearSpan[ColourNB], attacks[ColourNB];
  BB attacksFill[ColourNB], doubled[ColourNB], isolated[ColourNB];
  BB blocked[ColourNB], influence[ColourNB], fill[ColourNB];
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
  blocked[ColourWhite]=(pawns[ColourWhite] & bbSouthOne(occ));
  blocked[ColourBlack]=(pawns[ColourBlack] & bbNorthOne(occ));
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
  const Sq *sq, *sqEnd;
  for(colour=ColourWhite;colour<=ColourBlack;++colour)
  {
    sq=posGetPieceListStart(pos, pieceMake(PieceTypePawn, colour));
    sqEnd=posGetPieceListEnd(pos, pieceMake(PieceTypePawn, colour));
    for(;sq<sqEnd;++sq)
    {
      BB bb=bbSq(*sq);
      bool isDoubled=((bb & doubled[colour])!=BBNone);
      bool isIsolated=((bb & isolated[colour])!=BBNone);
      bool isBlocked=((bb & blocked[colour])!=BBNone);
      evalVPairAdd(&pawnData->score, &evalPawnValue[colour][isDoubled][isIsolated][isBlocked][*sq]);
    }
  }
}

VPair evalPiece(EvalData *data, PieceType type, Sq sq, Colour colour)
{
  // Init.
  VPair score=VPairZero;
  const Pos *pos=data->pos;
  Sq adjSq=(colour==ColourWhite ? sq : sqFlip(sq));
  BB bb=bbSq(sq);
  
  // PST.
  evalVPairAdd(&score, &evalPST[type][adjSq]);
  
  // Passed pawns.
  if (bb & data->pawnData.passed[colour])
    evalVPairAdd(&score, &evalPawnPassed[sqRank(adjSq)]);
  
  // Bishop mobility.
  if (type==PieceTypeBishopL || type==PieceTypeBishopD)
  {
    BB attacks=attacksBishop(sq, posGetBBAll(pos));
    evalVPairAddMul(&score, &evalBishopMob, bbPopCount(attacks));
  }
  
  // Rooks.
  if (type==PieceTypeRook)
  {
    BB rankBB=bbRank(sqRank(sq));
    
    // Mobility.
    BB attacks=attacksRook(sq, posGetBBAll(pos));
    evalVPairAddMul(&score, &evalRookMobFile, bbPopCount(attacks & bbFileFill(bb)));
    evalVPairAddMul(&score, &evalRookMobRank, bbPopCount(attacks & rankBB));
    
    // Open and semi-open files.
    if (bb & data->pawnData.openFiles)
      evalVPairAdd(&score, &evalRookOpenFile);
    else if (bb & data->pawnData.semiOpenFiles[colour])
      evalVPairAdd(&score, &evalRookSemiOpenFile);
    
    // Rook on 7th.
    BB oppPawns=posGetBBPiece(pos, pieceMake(PieceTypePawn, colourSwap(colour)));
    Sq adjOppKingSq=(colour==ColourWhite ? posGetKingSq(pos, ColourBlack) : sqFlip(posGetKingSq(pos, ColourWhite)));
    if (sqRank(adjSq)==Rank7 && ((rankBB & oppPawns) || sqRank(adjOppKingSq)==Rank8))
      evalVPairAdd(&score, &evalRookOn7th);
    
    // Trapped.
    BB kingBB=posGetBBPiece(pos, pieceMake(PieceTypeKing, colour));
    if (colour==ColourWhite)
    {
      if (((bb & (bbSq(SqG1) | bbSq(SqH1))) && (kingBB & (bbSq(SqF1) | bbSq(SqG1)))) ||
          ((bb & (bbSq(SqA1) | bbSq(SqB1))) && (kingBB & (bbSq(SqB1) | bbSq(SqC1)))))
        evalVPairAdd(&score, &evalRookTrapped);
    }
    else
    {
      if (((bb & (bbSq(SqG8) | bbSq(SqH8))) && (kingBB & (bbSq(SqF8) | bbSq(SqG8)))) ||
          ((bb & (bbSq(SqA8) | bbSq(SqB8))) && (kingBB & (bbSq(SqB8) | bbSq(SqC8)))))
        evalVPairAdd(&score, &evalRookTrapped);
    }
  }
  
  // Kings.
  if (type==PieceTypeKing)
  {
    // Pawn shield.
    BB pawns=posGetBBPiece(pos, pieceMake(PieceTypePawn, colour));
    BB set=bbForwardOne(bbWestOne(bb) | bb | bbEastOne(bb), colour);
    BB shieldClose=(pawns & set);
    BB shieldFar=(pawns & bbForwardOne(set, colour));
    evalVPairAddMul(&score, &evalKingShieldClose, bbPopCount(shieldClose));
    evalVPairAddMul(&score, &evalKingShieldFar, bbPopCount(shieldFar));
  }
  
  return score;
}

Score evalInterpolate(const EvalData *data, const VPair *score)
{
  // Interpolate and also scale to centi-pawns
  return ((data->matData.weightMG*score->mg+data->matData.weightEG*score->eg)*100)/(evalMaterial[PieceTypePawn].mg*256);
}

void evalVPairAdd(VPair *a, const VPair *b)
{
  a->mg+=b->mg;
  a->eg+=b->eg;
}

void evalVPairSub(VPair *a, const VPair *b)
{
  a->mg-=b->mg;
  a->eg-=b->eg;
}

void evalVPairAddMul(VPair *a, const VPair *b, int c)
{
  a->mg+=b->mg*c;
  a->eg+=b->eg*c;
}

void evalVPairSubMul(VPair *a, const VPair *b, int c)
{
  a->mg-=b->mg*c;
  a->eg-=b->eg*c;
}

#ifdef TUNE
void evalSetValue(void *varPtr, int value)
{
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
  for(file=FileA;file<=FileD;++file)
  {
    if (var==&evalPawnFiles[file].mg)
      evalPawnFiles[fileMirror(file)].mg=value;
    else if (var==&evalPawnFiles[file].eg)
      evalPawnFiles[fileMirror(file)].eg=value;
  }
  
  // Recalculate dervied values (such as passed pawn table).
  evalRecalc();
  
  // Clear now-invalid material and pawn tables etc.
  evalClear();
}

bool evalOptionNewVPair(const char *name, VPair *score)
{
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
#endif

void evalRecalc(void)
{
  // Pawn PST.
  Sq sq;
  const BB centre=(bbSq(SqD4)|bbSq(SqE4)|
                   bbSq(SqD5)|bbSq(SqE5));
  const BB outerCentre=(bbSq(SqC3)|bbSq(SqD3)|bbSq(SqE3)|bbSq(SqF3)|
                        bbSq(SqC4)|                      bbSq(SqF4)|
                        bbSq(SqC5)|                      bbSq(SqF5)|
                        bbSq(SqC6)|bbSq(SqD6)|bbSq(SqE6)|bbSq(SqF6));
  for(sq=0;sq<SqNB;++sq)
  {
    if (sqRank(sq)==Rank1 || sqRank(sq)==Rank8)
      continue;
    BB bb=bbSq(sq);
    evalVPairAdd(&evalPST[PieceTypePawn][sq], &evalPawnFiles[sqFile(sq)]);
    evalVPairAdd(&evalPST[PieceTypePawn][sq], &evalPawnRanks[sqRank(sq)]);
    if (bb & centre)
      evalVPairAdd(&evalPST[PieceTypePawn][sq], &evalPawnCentre);
    else if (bb & outerCentre)
      evalVPairAdd(&evalPST[PieceTypePawn][sq], &evalPawnOuterCentre);
  }
  
  // Pawn table.
  unsigned int isDoubled, isIsolated, isBlocked;
  for(isDoubled=0;isDoubled<2;++isDoubled)
  for(isIsolated=0;isIsolated<2;++isIsolated)
  for(isBlocked=0;isBlocked<2;++isBlocked)
  for(sq=0;sq<SqNB;++sq)
  {
    // Calculate score for white.
    VPair *score=&evalPawnValue[ColourWhite][isDoubled][isIsolated][isBlocked][sq];
    *score=VPairZero;
    if (isDoubled)
      evalVPairAdd(score, &evalPawnDoubled);
    if (isIsolated)
      evalVPairAdd(score, &evalPawnIsolated);
    if (isBlocked)
      evalVPairAdd(score, &evalPawnBlocked);
    
    // Flip square and negate score for black.
    evalPawnValue[ColourBlack][isDoubled][isIsolated][isBlocked][sqFlip(sq)]=VPairZero;
    evalVPairSub(&evalPawnValue[ColourBlack][isDoubled][isIsolated][isBlocked][sqFlip(sq)], score);
  }
  
  // Generate passed pawn array from quadratic coefficients.
  Rank rank;
  for(rank=0;rank<RankNB;++rank)
  {
    evalPawnPassed[rank]=VPairZero;
    evalVPairAddMul(&evalPawnPassed[rank], &evalPawnPassedQuadA, rank*rank);
    evalVPairAddMul(&evalPawnPassed[rank], &evalPawnPassedQuadB, rank);
    evalVPairAdd(&evalPawnPassed[rank], &evalPawnPassedQuadC);
  }
  
  // Calculate factor for each material weight.
  unsigned int weight;
  for(weight=0;weight<128;++weight)
  {
    unsigned int factor=(255.0*exp2f(-(float)(weight*weight)/((float)evalWeightFactor)));
    assert(factor<256);
    evalWeightEGFactor[weight]=factor;
  }
}

EvalMatType evalComputeMatType(const Pos *pos)
{
# define M(P,N) (matInfoMake((P),(N)))
  MatInfo mat=posGetMatInfo(pos);
  if (mat==(M(PieceWKnight,2)|M(PieceWKing,1)|M(PieceBKing,1)) ||
      mat==(M(PieceBKnight,2)|M(PieceWKing,1)|M(PieceBKing,1)))
    return EvalMatTypeKNNvK;
  else if (mat==(M(PieceWPawn,1)|M(PieceWKing,1)|M(PieceBKing,1)) ||
           mat==(M(PieceBPawn,1)|M(PieceWKing,1)|M(PieceBKing,1)))
    return EvalMatTypeKPvK;
  else if (mat==(M(PieceWBishopL,1)|M(PieceWPawn,1)|M(PieceWKing,1)|M(PieceBKing,1)) ||
           mat==(M(PieceWBishopD,1)|M(PieceWPawn,1)|M(PieceWKing,1)|M(PieceBKing,1)) ||
           mat==(M(PieceBBishopL,1)|M(PieceBPawn,1)|M(PieceWKing,1)|M(PieceBKing,1)) ||
           mat==(M(PieceBBishopD,1)|M(PieceBPawn,1)|M(PieceWKing,1)|M(PieceBKing,1)))
    return EvalMatTypeKBPvK;
  
  return EvalMatTypeOther;
# undef M
}
