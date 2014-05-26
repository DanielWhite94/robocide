#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "eval.h"
#include "util.h"

////////////////////////////////////////////////////////////////////////////////
// 
////////////////////////////////////////////////////////////////////////////////

typedef struct
{
  score_t MG, EG;
}spair_t;

typedef struct
{
  bb_t Pawns[2];
  spair_t Score;
}evalpawndata_t;

// Tunable values
const spair_t EvalMaterial[8]={{0,0},{90,130},{325,325},{325,325},{325,325},{500,500},{1000,1000},{0,0}};
const spair_t EvalPawnDoubled={-13,-13};
const spair_t EvalPawnIsolated={-30,-30};
const spair_t EvalPawnBlocked={-10,-10};
const spair_t EvalPawnPassed[8]={{0,0},{5,15},{30,35},{65,65},{110,105},{175,155},{250,215},{0,0}};
const spair_t EvalKnightPawnAffinity={6,6};
const spair_t EvalBishopPair={50,50};
const spair_t EvalRookPawnAffinity={-13,-13};
const spair_t EvalKingShieldClose={15,0};
const spair_t EvalKingShieldFar={5,0};
spair_t EvalPawnPST[64]={
{  -3, -41},{ -15, -40},{ -23, -38},{ -27, -37},{ -27, -37},{ -23, -38},{ -15, -40},{  -3, -41},
{ -15, -38},{   0, -35},{  -6, -34},{  -9, -32},{  -9, -32},{  -6, -34},{   0, -35},{ -15, -38},
{ -21, -30},{  -4, -27},{   7, -25},{   4, -22},{   4, -22},{   7, -25},{  -4, -27},{ -21, -30},
{ -22, -19},{  -5, -16},{   7, -12},{  21,  -3},{  21,  -3},{   7, -12},{  -5, -16},{ -22, -19},
{ -19,  -5},{  -2,  -2},{  11,   1},{  24,  10},{  24,  10},{  11,   1},{  -2,  -2},{ -19,  -5},
{ -10,  12},{   5,  14},{  17,  17},{  15,  20},{  15,  20},{  17,  17},{   5,  14},{ -10,  12},
{   2,  33},{  18,  35},{  11,  37},{   8,  38},{   8,  38},{  11,  37},{  18,  35},{   2,  33},
{  21,  58},{   9,  59},{   1,  61},{  -2,  62},{  -2,  62},{   1,  61},{   9,  59},{  21,  58}};
spair_t EvalKnightPST[64]={
{ -17, -12},{ -12,  -6},{  -8,  -3},{  -6,  -1},{  -6,  -1},{  -8,  -3},{ -12,  -6},{ -17, -12},
{ -11,  -6},{  -6,  -1},{  -3,   2},{  -1,   3},{  -1,   3},{  -3,   2},{  -6,  -1},{ -11,  -6},
{  -7,  -3},{  -2,   2},{   1,   5},{   2,   6},{   2,   6},{   1,   5},{  -2,   2},{  -7,  -3},
{  -4,  -1},{   1,   3},{   3,   6},{   4,   7},{   4,   7},{   3,   6},{   1,   3},{  -4,  -1},
{  -1,  -1},{   3,   3},{   6,   6},{   6,   7},{   6,   7},{   6,   6},{   3,   3},{  -1,  -1},
{   0,  -3},{   4,   2},{   7,   5},{   8,   6},{   8,   6},{   7,   5},{   4,   2},{   0,  -3},
{  -1,  -6},{   4,  -1},{   7,   2},{   9,   3},{   9,   3},{   7,   2},{   4,  -1},{  -1,  -6},
{  -2, -12},{   2,  -6},{   6,  -3},{   8,  -1},{   8,  -1},{   6,  -3},{   2,  -6},{  -2, -12}};
spair_t EvalBishopPST[64]={
{ -11, -15},{  -6,  -8},{  -3,  -4},{  -2,  -1},{  -2,  -1},{  -3,  -4},{  -6,  -8},{ -11, -15},
{  -6,  -8},{  -2,  -1},{   0,   2},{   2,   4},{   2,   4},{   0,   2},{  -2,  -1},{  -6,  -8},
{  -3,  -4},{   0,   2},{   4,   6},{   6,   8},{   6,   8},{   4,   6},{   0,   2},{  -3,  -4},
{  -2,  -1},{   2,   4},{   6,   8},{  12,   9},{  12,   9},{   6,   8},{   2,   4},{  -2,  -1},
{  -2,  -1},{   2,   4},{   6,   8},{  12,   9},{  12,   9},{   6,   8},{   2,   4},{  -2,  -1},
{  -3,  -4},{   0,   2},{   4,   6},{   6,   8},{   6,   8},{   4,   6},{   0,   2},{  -3,  -4},
{  -6,  -8},{  -2,  -1},{   0,   2},{   2,   4},{   2,   4},{   0,   2},{  -2,  -1},{  -6,  -8},
{ -11, -15},{  -6,  -8},{  -3,  -4},{  -2,  -1},{  -2,  -1},{  -3,  -4},{  -6,  -8},{ -11, -15}};
const spair_t EvalKingPST[64]={
{  57, -94},{  57, -51},{  41, -24},{  33, -10},{  33, -10},{  41, -24},{  57, -51},{  57, -94},
{  56, -51},{  32, -10},{  14,  15},{   3,  27},{   3,  27},{  14,  15},{  32, -10},{  56, -51},
{  37, -24},{  11,  15},{ -11,  39},{ -26,  49},{ -26,  49},{ -11,  39},{  11,  15},{  37, -24},
{  24, -10},{  -4,  27},{ -32,  49},{ -79,  55},{ -79,  55},{ -32,  49},{  -4,  27},{  24, -10},
{  17, -10},{ -11,  27},{ -39,  49},{ -86,  55},{ -86,  55},{ -39,  49},{ -11,  27},{  17, -10},
{  16, -24},{ -10,  15},{ -32,  39},{ -48,  49},{ -48,  49},{ -32,  39},{ -10,  15},{  16, -24},
{  20, -51},{  -3, -10},{ -21,  15},{ -31,  27},{ -31,  27},{ -21,  15},{  -3, -10},{  20, -51},
{  29, -94},{   7, -51},{  -8, -24},{ -16, -10},{ -16, -10},{  -8, -24},{   7, -51},{  29, -94}};

evalpawndata_t *EvalPawnTable=NULL;
size_t EvalPawnTableSize=0;

////////////////////////////////////////////////////////////////////////////////
// Private prototypes
////////////////////////////////////////////////////////////////////////////////

evalpawndata_t EvalPawns(const pos_t *Pos);
static inline void EvalComputePawns(const pos_t *Pos, evalpawndata_t *Data);
static inline spair_t EvalKnight(const pos_t *Pos, sq_t Sq, col_t Colour);
static inline spair_t EvalBishop(const pos_t *Pos, sq_t Sq, col_t Colour);
static inline spair_t EvalRook(const pos_t *Pos, sq_t Sq, col_t Colour);
static inline spair_t EvalQueen(const pos_t *Pos, sq_t Sq, col_t Colour);
static inline spair_t EvalKing(const pos_t *Pos, sq_t Sq, col_t Colour);
static inline score_t EvalInterpolate(const pos_t *Pos, const spair_t *Score);
void EvalPawnResize(size_t SizeMB);
void EvalPawnFree();
void EvalPawnReset();
static inline bool EvalPawnRead(const pos_t *Pos, evalpawndata_t *Data);
static inline void EvalPawnWrite(const pos_t *Pos, evalpawndata_t *Data);
static inline void EvalSPairAdd(spair_t *A, spair_t B);
static inline void EvalSPairSub(spair_t *A, spair_t B);
static inline void EvalSPairAddMul(spair_t *A, spair_t B, int C);
static inline void EvalSPairSubMul(spair_t *A, spair_t B, int C);

////////////////////////////////////////////////////////////////////////////////
// Public functions
////////////////////////////////////////////////////////////////////////////////

void EvalInit()
{
  // Add material to PSTs
  sq_t Sq;
  for(Sq=0;Sq<64;++Sq)
  {
    EvalSPairAdd(&EvalPawnPST[Sq], EvalMaterial[pawn]);
    EvalSPairAdd(&EvalKnightPST[Sq], EvalMaterial[knight]);
    EvalSPairAdd(&EvalBishopPST[Sq], EvalMaterial[bishopl]);
  }
  
  // Init pawn hash table
  EvalPawnResize(1); // 1mb
}

void EvalQuit()
{
  free(EvalPawnTable);
  EvalPawnTable=NULL;
  EvalPawnTableSize=0;
}

score_t Evaluate(const pos_t *Pos)
{
  spair_t Score={0,0};
  const sq_t *Sq, *SqEnd;
  
  // Pawns
  evalpawndata_t PawnData=EvalPawns(Pos);
  EvalSPairAdd(&Score, PawnData.Score);
  
  // Knights
  Sq=PosGetPieceListStart(Pos, wknight);
  SqEnd=PosGetPieceListEnd(Pos, wknight);
  for(;Sq<SqEnd;++Sq)
    EvalSPairAdd(&Score, EvalKnight(Pos, *Sq, white));
  Sq=PosGetPieceListStart(Pos, bknight);
  SqEnd=PosGetPieceListEnd(Pos, bknight);
  for(;Sq<SqEnd;++Sq)
    EvalSPairSub(&Score, EvalKnight(Pos, *Sq, black));
  
  // Bishops
  Sq=PosGetPieceListStart(Pos, wbishopl);
  SqEnd=PosGetPieceListEnd(Pos, wbishopl);
  for(;Sq<SqEnd;++Sq)
    EvalSPairAdd(&Score, EvalBishop(Pos, *Sq, white));
  Sq=PosGetPieceListStart(Pos, wbishopd);
  SqEnd=PosGetPieceListEnd(Pos, wbishopd);
  for(;Sq<SqEnd;++Sq)
    EvalSPairAdd(&Score, EvalBishop(Pos, *Sq, white));
  Sq=PosGetPieceListStart(Pos, bbishopl);
  SqEnd=PosGetPieceListEnd(Pos, bbishopl);
  for(;Sq<SqEnd;++Sq)
    EvalSPairSub(&Score, EvalBishop(Pos, *Sq, black));
  Sq=PosGetPieceListStart(Pos, bbishopd);
  SqEnd=PosGetPieceListEnd(Pos, bbishopd);
  for(;Sq<SqEnd;++Sq)
    EvalSPairSub(&Score, EvalBishop(Pos, *Sq, black));
  
  // Rooks
  Sq=PosGetPieceListStart(Pos, wrook);
  SqEnd=PosGetPieceListEnd(Pos, wrook);
  for(;Sq<SqEnd;++Sq)
    EvalSPairAdd(&Score, EvalRook(Pos, *Sq, white));
  Sq=PosGetPieceListStart(Pos, brook);
  SqEnd=PosGetPieceListEnd(Pos, brook);
  for(;Sq<SqEnd;++Sq)
    EvalSPairSub(&Score, EvalRook(Pos, *Sq, black));
  
  // Queens
  Sq=PosGetPieceListStart(Pos, wqueen);
  SqEnd=PosGetPieceListEnd(Pos, wqueen);
  for(;Sq<SqEnd;++Sq)
    EvalSPairAdd(&Score, EvalQueen(Pos, *Sq, white));
  Sq=PosGetPieceListStart(Pos, bqueen);
  SqEnd=PosGetPieceListEnd(Pos, bqueen);
  for(;Sq<SqEnd;++Sq)
    EvalSPairSub(&Score, EvalQueen(Pos, *Sq, black));
  
  // Kings
  EvalSPairAdd(&Score, EvalKing(Pos, PosGetKingSq(Pos, white), white));
  EvalSPairSub(&Score, EvalKing(Pos, PosGetKingSq(Pos, black), black));
  
  // Bishop pair
  uint64_t Mat=PosGetMat(Pos);
  if ((Mat & POSMAT_MASK(wbishopl)) && (Mat & POSMAT_MASK(wbishopd)))
    EvalSPairAdd(&Score, EvalBishopPair);
  if ((Mat & POSMAT_MASK(bbishopl)) && (Mat & POSMAT_MASK(bbishopd)))
    EvalSPairSub(&Score, EvalBishopPair);
  
  // Interpolate score based on phase of the game
  score_t ScalarScore=EvalInterpolate(Pos, &Score);
  
  // Adjust for side to move
  if (PosGetSTM(Pos)==black)
    ScalarScore=-ScalarScore;
  
  return ScalarScore;
}

////////////////////////////////////////////////////////////////////////////////
// Private functions
////////////////////////////////////////////////////////////////////////////////

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
    EvalSPairAdd(&Data->Score, EvalPawnPST[*Sq]);
    if (Doubled)
      EvalSPairAdd(&Data->Score, EvalPawnDoubled);
    else if (Passed)
      EvalSPairAdd(&Data->Score, EvalPawnPassed[SQ_Y(*Sq)]);
    if (Isolated)
      EvalSPairAdd(&Data->Score, EvalPawnIsolated);
    if (Blocked)
      EvalSPairAdd(&Data->Score, EvalPawnBlocked);
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
    EvalSPairSub(&Data->Score, EvalPawnPST[SQ_FLIP(*Sq)]);
    if (Doubled)
      EvalSPairSub(&Data->Score, EvalPawnDoubled);
    else if (Passed)
      EvalSPairSub(&Data->Score, EvalPawnPassed[SQ_Y(SQ_FLIP(*Sq))]);
    if (Isolated)
      EvalSPairSub(&Data->Score, EvalPawnIsolated);
    if (Blocked)
      EvalSPairSub(&Data->Score, EvalPawnBlocked);
  }
}

static inline spair_t EvalKnight(const pos_t *Pos, sq_t Sq, col_t Colour)
{
  spair_t Score={0,0};
  sq_t AdjSq=(Colour==white ? Sq : SQ_FLIP(Sq));
  
  // PST (and material)
  EvalSPairAdd(&Score, EvalKnightPST[AdjSq]);
  
  // Pawn affinity
  int PawnCount=PosPieceCount(Pos, PIECE_MAKE(pawn, Colour));
  EvalSPairAddMul(&Score, EvalKnightPawnAffinity, PawnCount-5);
  
  return Score;
}

static inline spair_t EvalBishop(const pos_t *Pos, sq_t Sq, col_t Colour)
{
  spair_t Score={0,0};
  sq_t AdjSq=(Colour==white ? Sq : SQ_FLIP(Sq));
  
  // PST (and material)
  EvalSPairAdd(&Score, EvalBishopPST[AdjSq]);
  
  return Score;
}

static inline spair_t EvalRook(const pos_t *Pos, sq_t Sq, col_t Colour)
{
  spair_t Score={0,0};
  
  // Material
  EvalSPairAdd(&Score, EvalMaterial[rook]);
  
  // Pawn affinity
  int PawnCount=PosPieceCount(Pos, PIECE_MAKE(pawn, Colour));
  EvalSPairAddMul(&Score, EvalRookPawnAffinity, PawnCount-5);
  
  return Score;
}

static inline spair_t EvalQueen(const pos_t *Pos, sq_t Sq, col_t Colour)
{
  spair_t Score={0,0};
  
  // Material
  EvalSPairAdd(&Score, EvalMaterial[queen]);
  
  return Score;
}

static inline spair_t EvalKing(const pos_t *Pos, sq_t Sq, col_t Colour)
{
  spair_t Score={0,0};
  bb_t BB=SQTOBB(Sq), Set;
  bb_t Pawns=PosGetBBPiece(Pos, PIECE_MAKE(pawn, Colour));
  sq_t AdjSq=(Colour==white ? Sq : SQ_FLIP(Sq));
  
  // PST (and material)
  EvalSPairAdd(&Score, EvalKingPST[AdjSq]);
  
  // Pawn shield
  Set=BBForwardOne(BBWestOne(BB) | BB | BBEastOne(BB), Colour);
  bb_t ShieldClose=(Pawns & Set);
  bb_t ShieldFar=(Pawns & BBForwardOne(Set, Colour));
  EvalSPairAddMul(&Score, EvalKingShieldClose, BBPopCount(ShieldClose));
  EvalSPairAddMul(&Score, EvalKingShieldFar, BBPopCount(ShieldFar));
  
  return Score;
}

static inline score_t EvalInterpolate(const pos_t *Pos, const spair_t *Score)
{
  // Find weights of middle/endgame
  uint64_t Mat=PosGetMat(Pos);
  int MinCount=POSMAT_GET(Mat, wknight)+POSMAT_GET(Mat, wbishopl)+POSMAT_GET(Mat, wbishopd)+
               POSMAT_GET(Mat, bknight)+POSMAT_GET(Mat, bbishopl)+POSMAT_GET(Mat, bbishopd);
  int RCount=POSMAT_GET(Mat, wrook)+POSMAT_GET(Mat, brook);
  int QCount=POSMAT_GET(Mat, wqueen)+POSMAT_GET(Mat, bqueen);
  int W=MinCount+2*RCount+4*QCount;
  int WeightEG=floorf(256.0*exp2f(-((W*W)/144.0)));
  int WeightMG=256-WeightEG;
  assert(WeightMG>=0 && WeightMG<=256 && WeightEG>=0 && WeightEG<=256);
  
  // Interpolate and also scale to centi-pawns
  return ((WeightMG*Score->MG+WeightEG*Score->EG)*100)/
          (WeightMG*EvalMaterial[pawn].MG+WeightEG*EvalMaterial[pawn].EG);
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

static inline void EvalSPairAdd(spair_t *A, spair_t B)
{
  A->MG+=B.MG;
  A->EG+=B.EG;
}

static inline void EvalSPairSub(spair_t *A, spair_t B)
{
  A->MG-=B.MG;
  A->EG-=B.EG;
}

static inline void EvalSPairAddMul(spair_t *A, spair_t B, int C)
{
  A->MG+=B.MG*C;
  A->EG+=B.EG*C;
}

static inline void EvalSPairSubMul(spair_t *A, spair_t B, int C)
{
  A->MG-=B.MG*C;
  A->EG-=B.EG*C;
}
