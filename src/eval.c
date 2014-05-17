#include <math.h>
#include <stdlib.h>
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

const spair_t EvalMaterial[8]={{0,0},{90,130},{330,300},{300,330},{500,550},{1000,1100},{0,0},{0,0}};

evalpawndata_t *EvalPawnTable=NULL;
size_t EvalPawnTableSize=0;

////////////////////////////////////////////////////////////////////////////////
// Private prototypes
////////////////////////////////////////////////////////////////////////////////

evalpawndata_t EvalPawns(const pos_t *Pos);
inline void EvalComputePawns(const pos_t *Pos, evalpawndata_t *Data);
inline spair_t EvalKnight(const pos_t *Pos, sq_t Sq);
inline spair_t EvalBishop(const pos_t *Pos, sq_t Sq);
inline spair_t EvalRook(const pos_t *Pos, sq_t Sq);
inline spair_t EvalQueen(const pos_t *Pos, sq_t Sq);
inline spair_t EvalKing(const pos_t *Pos, sq_t Sq);
inline score_t EvalInterpolate(const pos_t *Pos, const spair_t *Score);
void EvalPawnResize(size_t SizeMB);
inline bool EvalPawnRead(const pos_t *Pos, evalpawndata_t *Data);
inline void EvalPawnWrite(const pos_t *Pos, evalpawndata_t *Data);
inline void EvalSPairAdd(spair_t *A, spair_t B);
inline void EvalSPairSub(spair_t *A, spair_t B);
inline void EvalSPairAddMul(spair_t *A, spair_t B, int C);

////////////////////////////////////////////////////////////////////////////////
// Public functions
////////////////////////////////////////////////////////////////////////////////

void EvalInit()
{
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
    EvalSPairAdd(&Score, EvalKnight(Pos, *Sq));
  Sq=PosGetPieceListStart(Pos, bknight);
  SqEnd=PosGetPieceListEnd(Pos, bknight);
  for(;Sq<SqEnd;++Sq)
    EvalSPairSub(&Score, EvalKnight(Pos, *Sq));
  
  // Bishops
  Sq=PosGetPieceListStart(Pos, wbishop);
  SqEnd=PosGetPieceListEnd(Pos, wbishop);
  for(;Sq<SqEnd;++Sq)
    EvalSPairAdd(&Score, EvalBishop(Pos, *Sq));
  Sq=PosGetPieceListStart(Pos, bbishop);
  SqEnd=PosGetPieceListEnd(Pos, bbishop);
  for(;Sq<SqEnd;++Sq)
    EvalSPairSub(&Score, EvalBishop(Pos, *Sq));
  
  // Rooks
  Sq=PosGetPieceListStart(Pos, wrook);
  SqEnd=PosGetPieceListEnd(Pos, wrook);
  for(;Sq<SqEnd;++Sq)
    EvalSPairAdd(&Score, EvalRook(Pos, *Sq));
  Sq=PosGetPieceListStart(Pos, brook);
  SqEnd=PosGetPieceListEnd(Pos, brook);
  for(;Sq<SqEnd;++Sq)
    EvalSPairSub(&Score, EvalRook(Pos, *Sq));
  
  // Queens
  Sq=PosGetPieceListStart(Pos, wqueen);
  SqEnd=PosGetPieceListEnd(Pos, wqueen);
  for(;Sq<SqEnd;++Sq)
    EvalSPairAdd(&Score, EvalQueen(Pos, *Sq));
  Sq=PosGetPieceListStart(Pos, bqueen);
  SqEnd=PosGetPieceListEnd(Pos, bqueen);
  for(;Sq<SqEnd;++Sq)
    EvalSPairSub(&Score, EvalQueen(Pos, *Sq));
  
  // Kings
  EvalSPairAdd(&Score, EvalKing(Pos, PosGetKingSq(Pos, white)));
  EvalSPairSub(&Score, EvalKing(Pos, PosGetKingSq(Pos, black)));
  
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

inline void EvalComputePawns(const pos_t *Pos, evalpawndata_t *Data)
{
  // Init
  Data->Pawns[white]=PosGetBBPiece(Pos, wpawn);
  Data->Pawns[black]=PosGetBBPiece(Pos, bpawn);
  Data->Score.MG=Data->Score.EG=0;
  
  // Material
  int PawnNet=PosPieceCount(Pos, wpawn)-PosPieceCount(Pos, bpawn);
  EvalSPairAddMul(&Data->Score, EvalMaterial[pawn], PawnNet);
}

inline spair_t EvalKnight(const pos_t *Pos, sq_t Sq)
{
  spair_t Score={0,0};
  
  // Material
  EvalSPairAdd(&Score, EvalMaterial[knight]);
  
  return Score;
}

inline spair_t EvalBishop(const pos_t *Pos, sq_t Sq)
{
  spair_t Score={0,0};
  
  // Material
  EvalSPairAdd(&Score, EvalMaterial[bishop]);
  
  return Score;
}

inline spair_t EvalRook(const pos_t *Pos, sq_t Sq)
{
  spair_t Score={0,0};
  
  // Material
  EvalSPairAdd(&Score, EvalMaterial[rook]);
  
  return Score;
}

inline spair_t EvalQueen(const pos_t *Pos, sq_t Sq)
{
  spair_t Score={0,0};
  
  // Material
  EvalSPairAdd(&Score, EvalMaterial[queen]);
  
  return Score;
}

inline spair_t EvalKing(const pos_t *Pos, sq_t Sq)
{
  spair_t Score={0,0};
  
  return Score;
}

inline score_t EvalInterpolate(const pos_t *Pos, const spair_t *Score)
{
  // Find weights of middle/endgame
  int MinCount=PosPieceCount(Pos, wknight)+PosPieceCount(Pos, wbishop)+
                        PosPieceCount(Pos, bknight)+PosPieceCount(Pos, bbishop);
  int RCount=PosPieceCount(Pos, wrook)+PosPieceCount(Pos, brook);
  int QCount=PosPieceCount(Pos, wqueen)+PosPieceCount(Pos, bqueen);
  int W=MinCount+2*RCount+4*QCount;
  int WeightEG=floorf(65536.0*exp2f(-((W*W)/144.0)));
  int WeightMG=65536-WeightEG;
  assert(WeightMG>=0 && WeightMG<=65536 && WeightEG>=0 && WeightEG<=65536);
  
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
      EvalPawnTable=Ptr;
      EvalPawnTableSize=Entries;
      return;
    }
    Entries/=2;
  }
  
  // Could not allocate 
  free(EvalPawnTable);
  EvalPawnTable=NULL;
  EvalPawnTableSize=0;
}

inline bool EvalPawnRead(const pos_t *Pos, evalpawndata_t *Data)
{
  if (EvalPawnTable==NULL)
    return false;
  
  int Index=(PosGetPawnKey(Pos) & (EvalPawnTableSize-1));
  *Data=EvalPawnTable[Index];
  return (Data->Pawns[white]==PosGetBBPiece(Pos, wpawn) &&
          Data->Pawns[black]==PosGetBBPiece(Pos, bpawn));
}

inline void EvalPawnWrite(const pos_t *Pos, evalpawndata_t *Data)
{
  if (EvalPawnTable==NULL)
    return;
  int Index=(PosGetPawnKey(Pos) & (EvalPawnTableSize-1));
  EvalPawnTable[Index]=*Data;
}

inline void EvalSPairAdd(spair_t *A, spair_t B)
{
  A->MG+=B.MG;
  A->EG+=B.EG;
}

inline void EvalSPairSub(spair_t *A, spair_t B)
{
  A->MG-=B.MG;
  A->EG-=B.EG;
}

inline void EvalSPairAddMul(spair_t *A, spair_t B, int C)
{
  A->MG+=B.MG*C;
  A->EG+=B.EG*C;
}
