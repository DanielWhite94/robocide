#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "attacks.h"
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
  bb_t Pawns[2], Passed[2], Doubled[2], SemiOpenFiles[2], OpenFiles;
  spair_t Score;
}evalpawndata_t;

typedef struct
{
  hkey_t Mat;
  spair_t (*Function)(const pos_t *Pos);
  spair_t Offset, Tempo;
  uint8_t Factor, WeightEG;
}evalmatdata_t;

// Tunable values
const spair_t EvalMaterial[8]={{0,0},{900,1300},{3250,3250},{3250,3250},{3250,3250},{5000,5000},{10000,10000},{0,0}};
const spair_t EvalPawnDoubled={-100,-200};
const spair_t EvalPawnIsolated={-300,-200};
const spair_t EvalPawnBlocked={-100,-100};
const spair_t EvalPawnPassed[8]={{0,0},{100,150},{200,350},{250,650},{650,1050},{1050,1550},{1500,2150},{0,0}};
const spair_t EvalKnightPawnAffinity={30,30};
const spair_t EvalBishopPair={500,500};
const spair_t EvalRookPawnAffinity={-70,-70};
const spair_t EvalRookMobFile={20,30};
const spair_t EvalRookMobRank={10,20};
const spair_t EvalRookOpenFile={100,50};
const spair_t EvalRookSemiOpenFile={50,20};
const spair_t EvalRookOn7th={50,100};
const spair_t EvalKingShieldClose={150,0};
const spair_t EvalKingShieldFar={50,0};
const spair_t EvalTempoDefault={0,0};

spair_t EvalPawnPST[64]={
{  -30, -410},{ -150, -400},{ -230, -380},{ -270, -370},{ -270, -370},{ -230, -380},{ -150, -400},{  -30, -410},
{ -150, -380},{    0, -350},{  -60, -340},{  -90, -320},{  -90, -320},{  -60, -340},{    0, -350},{ -150, -380},
{ -210, -300},{  -40, -270},{   70, -250},{   40, -220},{   40, -220},{   70, -250},{  -40, -270},{ -210, -300},
{ -220, -190},{  -50, -160},{   70, -120},{  210,  -30},{  210,  -30},{   70, -120},{  -50, -160},{ -220, -190},
{ -190,  -50},{  -20,  -20},{  110,   10},{  240,  100},{  240,  100},{  110,   10},{  -20,  -20},{ -190,  -50},
{ -100,  120},{   50,  140},{  170,  170},{  150,  200},{  150,  200},{  170,  170},{   50,  140},{ -100,  120},
{   20,  330},{  180,  350},{  110,  370},{   80,  380},{   80,  380},{  110,  370},{  180,  350},{   20,  330},
{  210,  580},{   90,  590},{   10,  610},{  -20,  620},{  -20,  620},{   10,  610},{   90,  590},{  210,  58}};
spair_t EvalKnightPST[64]={
{ -170, -120},{ -120,  -60},{  -80,  -30},{  -60,  -10},{  -60,  -10},{  -80,  -30},{ -120,  -60},{ -170, -120},
{ -110,  -60},{  -60,  -10},{  -30,   20},{  -10,   30},{  -10,   30},{  -30,   20},{  -60,  -10},{ -110,  -60},
{  -70,  -30},{  -20,   20},{   10,   50},{   20,   60},{   20,   60},{   10,   50},{  -20,   20},{  -70,  -30},
{  -40,  -10},{   10,   30},{   30,   60},{   40,   70},{   40,   70},{   30,   60},{   10,   30},{  -40,  -10},
{  -10,  -10},{   30,   30},{   60,   60},{   60,   70},{   60,   70},{   60,   60},{   30,   30},{  -10,  -10},
{    0,  -30},{   40,   20},{   70,   50},{   80,   60},{   80,   60},{   70,   50},{   40,   20},{    0,  -30},
{  -10,  -60},{   40,  -10},{   70,   20},{   90,   30},{   90,   30},{   70,   20},{   40,  -10},{  -10,  -60},
{  -20, -120},{   20,  -60},{   60,  -30},{   80,  -10},{   80,  -10},{   60,  -30},{   20,  -60},{  -20, -12}};
spair_t EvalBishopPST[64]={
{ -110, -150},{  -60,  -80},{  -30,  -40},{  -20,  -10},{  -20,  -10},{  -30,  -40},{  -60,  -80},{ -110, -150},
{  -60,  -80},{  -20,  -10},{    0,   20},{   20,   40},{   20,   40},{    0,   20},{  -20,  -10},{  -60,  -80},
{  -30,  -40},{    0,   20},{   40,   60},{   60,   80},{   60,   80},{   40,   60},{    0,   20},{  -30,  -40},
{  -20,  -10},{   20,   40},{   60,   80},{  120,   90},{  120,   90},{   60,   80},{   20,   40},{  -20,  -10},
{  -20,  -10},{   20,   40},{   60,   80},{  120,   90},{  120,   90},{   60,   80},{   20,   40},{  -20,  -10},
{  -30,  -40},{    0,   20},{   40,   60},{   60,   80},{   60,   80},{   40,   60},{    0,   20},{  -30,  -40},
{  -60,  -80},{  -20,  -10},{    0,   20},{   20,   40},{   20,   40},{    0,   20},{  -20,  -10},{  -60,  -80},
{ -110, -150},{  -60,  -80},{  -30,  -40},{  -20,  -10},{  -20,  -10},{  -30,  -40},{  -60,  -80},{ -110, -15}};
const spair_t EvalKingPST[64]={
{  570, -460},{  570, -240},{  410, -120},{  330,  -40},{  330,  -40},{  410, -120},{  570, -240},{  570, -460},
{  560, -240},{  320,  -40},{  140,   60},{   30,  120},{   30,  120},{  140,   60},{  320,  -40},{  560, -240},
{  370, -120},{  110,   60},{ -110,  180},{ -260,  240},{ -260,  240},{ -110,  180},{  110,   60},{  370, -120},
{  240,  -40},{  -40,  120},{ -320,  240},{ -790,  260},{ -790,  260},{ -320,  240},{  -40,  120},{  240,  -40},
{  170,  -40},{ -110,  120},{ -390,  240},{ -860,  260},{ -860,  260},{ -390,  240},{ -110,  120},{  170,  -40},
{  160, -120},{ -100,   60},{ -320,  180},{ -480,  240},{ -480,  240},{ -320,  180},{ -100,   60},{  160, -120},
{  200, -240},{  -30,  -40},{ -210,   60},{ -310,  120},{ -310,  120},{ -210,   60},{  -30,  -40},{  200, -240},
{  290, -460},{   70, -240},{  -80, -120},{ -160,  -40},{ -160,  -40},{  -80, -120},{   70, -240},{  290, -46}};

evalpawndata_t *EvalPawnTable=NULL;
size_t EvalPawnTableSize=0;
evalmatdata_t *EvalMatTable=NULL;
size_t EvalMatTableSize=0;

////////////////////////////////////////////////////////////////////////////////
// Private prototypes
////////////////////////////////////////////////////////////////////////////////

spair_t EvaluateDefault(const pos_t *Pos);
evalmatdata_t EvalMatCombo(const pos_t *Pos);
void EvalComputeMat(const pos_t *Pos, evalmatdata_t *Data);
evalpawndata_t EvalPawns(const pos_t *Pos);
static inline void EvalComputePawns(const pos_t *Pos, evalpawndata_t *Data);
static inline spair_t EvalKnight(const pos_t *Pos, sq_t Sq, col_t Colour, const evalpawndata_t *PawnData);
static inline spair_t EvalBishop(const pos_t *Pos, sq_t Sq, col_t Colour, const evalpawndata_t *PawnData);
static inline spair_t EvalRook(const pos_t *Pos, sq_t Sq, col_t Colour, const evalpawndata_t *PawnData);
static inline spair_t EvalQueen(const pos_t *Pos, sq_t Sq, col_t Colour, const evalpawndata_t *PawnData);
static inline spair_t EvalKing(const pos_t *Pos, sq_t Sq, col_t Colour, const evalpawndata_t *PawnData);
static inline score_t EvalInterpolate(const pos_t *Pos, const spair_t *Score, const evalmatdata_t *Data);
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
  
  // Init pawn and mat hash tables
  EvalPawnResize(1); // 1mb
  EvalMatResize(16); // 16kb
}

void EvalQuit()
{
  EvalPawnFree();
  EvalMatFree();
}

score_t Evaluate(const pos_t *Pos)
{
  // Evaluation function depends on material combination
  evalmatdata_t Data=EvalMatCombo(Pos);
  
  // Evaluate
  spair_t Score=(*Data.Function)(Pos);
  
  // Material combination offset
  EvalSPairAdd(&Score, Data.Offset);
  
  // Tempo bonus
  if (PosGetSTM(Pos)==white)
    EvalSPairAdd(&Score, Data.Tempo);
  else
    EvalSPairSub(&Score, Data.Tempo);
  
  // Interpolate score based on phase of the game
  score_t ScalarScore=EvalInterpolate(Pos, &Score, &Data);
  
  // Material combination scaling
  ScalarScore=(ScalarScore*(((int32_t)Data.Factor)))/128;
  
  // Adjust for side to move
  if (PosGetSTM(Pos)==black)
    ScalarScore=-ScalarScore;
  
  return ScalarScore;
}

////////////////////////////////////////////////////////////////////////////////
// Private functions
////////////////////////////////////////////////////////////////////////////////

spair_t EvaluateDefault(const pos_t *Pos)
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
    EvalSPairAdd(&Score, EvalKnight(Pos, *Sq, white, &PawnData));
  Sq=PosGetPieceListStart(Pos, bknight);
  SqEnd=PosGetPieceListEnd(Pos, bknight);
  for(;Sq<SqEnd;++Sq)
    EvalSPairSub(&Score, EvalKnight(Pos, *Sq, black, &PawnData));
  
  // Bishops
  Sq=PosGetPieceListStart(Pos, wbishopl);
  SqEnd=PosGetPieceListEnd(Pos, wbishopl);
  for(;Sq<SqEnd;++Sq)
    EvalSPairAdd(&Score, EvalBishop(Pos, *Sq, white, &PawnData));
  Sq=PosGetPieceListStart(Pos, wbishopd);
  SqEnd=PosGetPieceListEnd(Pos, wbishopd);
  for(;Sq<SqEnd;++Sq)
    EvalSPairAdd(&Score, EvalBishop(Pos, *Sq, white, &PawnData));
  Sq=PosGetPieceListStart(Pos, bbishopl);
  SqEnd=PosGetPieceListEnd(Pos, bbishopl);
  for(;Sq<SqEnd;++Sq)
    EvalSPairSub(&Score, EvalBishop(Pos, *Sq, black, &PawnData));
  Sq=PosGetPieceListStart(Pos, bbishopd);
  SqEnd=PosGetPieceListEnd(Pos, bbishopd);
  for(;Sq<SqEnd;++Sq)
    EvalSPairSub(&Score, EvalBishop(Pos, *Sq, black, &PawnData));
  
  // Rooks
  Sq=PosGetPieceListStart(Pos, wrook);
  SqEnd=PosGetPieceListEnd(Pos, wrook);
  for(;Sq<SqEnd;++Sq)
    EvalSPairAdd(&Score, EvalRook(Pos, *Sq, white, &PawnData));
  Sq=PosGetPieceListStart(Pos, brook);
  SqEnd=PosGetPieceListEnd(Pos, brook);
  for(;Sq<SqEnd;++Sq)
    EvalSPairSub(&Score, EvalRook(Pos, *Sq, black, &PawnData));
  
  // Queens
  Sq=PosGetPieceListStart(Pos, wqueen);
  SqEnd=PosGetPieceListEnd(Pos, wqueen);
  for(;Sq<SqEnd;++Sq)
    EvalSPairAdd(&Score, EvalQueen(Pos, *Sq, white, &PawnData));
  Sq=PosGetPieceListStart(Pos, bqueen);
  SqEnd=PosGetPieceListEnd(Pos, bqueen);
  for(;Sq<SqEnd;++Sq)
    EvalSPairSub(&Score, EvalQueen(Pos, *Sq, black, &PawnData));
  
  // Kings
  EvalSPairAdd(&Score, EvalKing(Pos, PosGetKingSq(Pos, white), white, &PawnData));
  EvalSPairSub(&Score, EvalKing(Pos, PosGetKingSq(Pos, black), black, &PawnData));
  
  return Score;
}

evalmatdata_t EvalMatCombo(const pos_t *Pos)
{
  evalmatdata_t Data;
  if (!EvalMatRead(Pos, &Data))
  {
    EvalComputeMat(Pos, &Data);
    EvalMatWrite(Pos, &Data);
  }
  
  return Data;
}

void EvalComputeMat(const pos_t *Pos, evalmatdata_t *Data)
{
  #define M(P,N) (POSMAT_MAKE((P),(N)))
  
  // Init data
  Data->Mat=PosGetMat(Pos);
  Data->Function=&EvaluateDefault;
  Data->Offset.MG=Data->Offset.EG=0;
  Data->Tempo=EvalTempoDefault;
  Data->Factor=128;
  uint64_t Mat=(Data->Mat & ~(POSMAT_MASK(wking) | POSMAT_MASK(bking)));
  assert(Mat!=0);
  bool WBishopL=((Mat & POSMAT_MASK(wbishopl))!=0);
  bool WBishopD=((Mat & POSMAT_MASK(wbishopd))!=0);
  bool BBishopL=((Mat & POSMAT_MASK(bbishopl))!=0);
  bool BBishopD=((Mat & POSMAT_MASK(bbishopd))!=0);
  
  // Find weight for endgame
  int MinCount=POSMAT_GET(Mat, wknight)+POSMAT_GET(Mat, wbishopl)+POSMAT_GET(Mat, wbishopd)+
               POSMAT_GET(Mat, bknight)+POSMAT_GET(Mat, bbishopl)+POSMAT_GET(Mat, bbishopd);
  int RCount=POSMAT_GET(Mat, wrook)+POSMAT_GET(Mat, brook);
  int QCount=POSMAT_GET(Mat, wqueen)+POSMAT_GET(Mat, bqueen);
  int Weight=MinCount+2*RCount+4*QCount;
  Data->WeightEG=floorf(255.0*exp2f(-((Weight*Weight)/144.0)));
  
  // Knight pawn affinity
  int KnightAffW=POSMAT_GET(Mat, wknight)*(POSMAT_GET(Mat, wpawn)-5);
  int KnightAffB=POSMAT_GET(Mat, bknight)*(POSMAT_GET(Mat, bpawn)-5);
  EvalSPairAddMul(&Data->Offset, EvalKnightPawnAffinity, KnightAffW-KnightAffB);
  
  // Rook material
  EvalSPairAddMul(&Data->Offset, EvalMaterial[rook], POSMAT_GET(Mat, wrook)-POSMAT_GET(Mat, brook));
  
  // Rook pawn affinity
  int RookAffW=POSMAT_GET(Mat, wrook)*(POSMAT_GET(Mat, wpawn)-5);
  int RookAffB=POSMAT_GET(Mat, brook)*(POSMAT_GET(Mat, bpawn)-5);
  EvalSPairAddMul(&Data->Offset, EvalRookPawnAffinity, RookAffW-RookAffB);
  
  // Queen material
  EvalSPairAddMul(&Data->Offset, EvalMaterial[queen], POSMAT_GET(Mat, wqueen)-POSMAT_GET(Mat, bqueen));
  
  // Bishop pair bonus
  if (WBishopL && WBishopD)
    EvalSPairAdd(&Data->Offset, EvalBishopPair);
  if (BBishopL && BBishopD)
    EvalSPairSub(&Data->Offset, EvalBishopPair);
  
  #undef M
  return;
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
  Data->Doubled[white]=Data->Doubled[black]=0;
  
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
    EvalSPairAdd(&Data->Score, EvalPawnPST[*Sq]);
    if (Doubled)
    {
      EvalSPairAdd(&Data->Score, EvalPawnDoubled);
      Data->Doubled[white]|=BB;
    }
    else if (Passed)
    {
      EvalSPairAdd(&Data->Score, EvalPawnPassed[SQ_Y(*Sq)]);
      Data->Passed[white]|=BB;
    }
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
    {
      EvalSPairSub(&Data->Score, EvalPawnDoubled);
      Data->Doubled[black]|=BB;
    }
    else if (Passed)
    {
      EvalSPairSub(&Data->Score, EvalPawnPassed[SQ_Y(SQ_FLIP(*Sq))]);
      Data->Passed[black]|=BB;
    }
    if (Isolated)
      EvalSPairSub(&Data->Score, EvalPawnIsolated);
    if (Blocked)
      EvalSPairSub(&Data->Score, EvalPawnBlocked);
  }
}

static inline spair_t EvalKnight(const pos_t *Pos, sq_t Sq, col_t Colour, const evalpawndata_t *PawnData)
{
  spair_t Score={0,0};
  sq_t AdjSq=(Colour==white ? Sq : SQ_FLIP(Sq));
  
  // PST (and material)
  EvalSPairAdd(&Score, EvalKnightPST[AdjSq]);
  
  // Pawn affinity in mat table
  
  return Score;
}

static inline spair_t EvalBishop(const pos_t *Pos, sq_t Sq, col_t Colour, const evalpawndata_t *PawnData)
{
  spair_t Score={0,0};
  sq_t AdjSq=(Colour==white ? Sq : SQ_FLIP(Sq));
  
  // PST (and material)
  EvalSPairAdd(&Score, EvalBishopPST[AdjSq]);
  
  return Score;
}

static inline spair_t EvalRook(const pos_t *Pos, sq_t Sq, col_t Colour, const evalpawndata_t *PawnData)
{
  spair_t Score={0,0};
  bb_t BB=SQTOBB(Sq);
  sq_t AdjSq=(Colour==white ? Sq : SQ_FLIP(Sq));
  bb_t Rank=BBSqToRank(Sq);
  
  // Material and pawn affinity in mat table
  
  // Mobility
  bb_t Attacks=AttacksRook(Sq, PosGetBBAll(Pos));
  EvalSPairAddMul(&Score, EvalRookMobFile, BBPopCount(Attacks & BBFileFill(BB)));
  EvalSPairAddMul(&Score, EvalRookMobRank, BBPopCount(Attacks & Rank));
  
  // Open and semi-open files
  if (BB & PawnData->OpenFiles)
    EvalSPairAdd(&Score, EvalRookOpenFile);
  else if (BB & PawnData->SemiOpenFiles[Colour])
    EvalSPairAdd(&Score, EvalRookSemiOpenFile);
  
  // Rook on 7th
  bb_t OppPawns=PosGetBBPiece(Pos, PIECE_MAKE(pawn, COL_SWAP(Colour)));
  sq_t AdjOppKingSq=(Colour==white ? PosGetKingSq(Pos, black) :
                                     SQ_FLIP(PosGetKingSq(Pos, white)));
  if (SQ_Y(AdjSq)==6 && ((Rank & OppPawns) || SQ_Y(AdjOppKingSq)==7))
    EvalSPairAdd(&Score, EvalRookOn7th);
  
  return Score;
}

static inline spair_t EvalQueen(const pos_t *Pos, sq_t Sq, col_t Colour, const evalpawndata_t *PawnData)
{
  spair_t Score={0,0};
  
  // Material in mat table
  
  return Score;
}

static inline spair_t EvalKing(const pos_t *Pos, sq_t Sq, col_t Colour, const evalpawndata_t *PawnData)
{
  spair_t Score={0,0};
  bb_t BB=SQTOBB(Sq), Set;
  bb_t Pawns=PosGetBBPiece(Pos, PIECE_MAKE(pawn, Colour));
  sq_t AdjSq=(Colour==white ? Sq : SQ_FLIP(Sq));
  
  // PST
  EvalSPairAdd(&Score, EvalKingPST[AdjSq]);
  
  // Pawn shield
  Set=BBForwardOne(BBWestOne(BB) | BB | BBEastOne(BB), Colour);
  bb_t ShieldClose=(Pawns & Set);
  bb_t ShieldFar=(Pawns & BBForwardOne(Set, Colour));
  EvalSPairAddMul(&Score, EvalKingShieldClose, BBPopCount(ShieldClose));
  EvalSPairAddMul(&Score, EvalKingShieldFar, BBPopCount(ShieldFar));
  
  return Score;
}

static inline score_t EvalInterpolate(const pos_t *Pos, const spair_t *Score, const evalmatdata_t *Data)
{
  // Interpolate and also scale to centi-pawns
  int WeightEG=Data->WeightEG;
  int WeightMG=255-WeightEG;
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
