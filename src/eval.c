#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "attacks.h"
#include "bb.h"
#include "bitbase.h"
#include "colour.h"
#include "eval.h"
#include "htable.h"
#include "robocide.h"
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
  BB attacksPiece[PieceNB], attacksColour[ColourNB], attacksAll;
  BB attacksPieceLE[PieceNB]; // Attacks by pieces of same colour as given piece at with value no greater.
};

////////////////////////////////////////////////////////////////////////////////
// Tunable values
////////////////////////////////////////////////////////////////////////////////

TUNECONST VPair evalMaterial[PieceTypeNB]={
  [PieceTypeNone]={0,0},
  [PieceTypePawn]={900,1100},
  [PieceTypeKnight]={3500,3200},
  [PieceTypeBishopL]={3810,3070},
  [PieceTypeBishopD]={3810,3070},
  [PieceTypeRook]={4950,5350},
  [PieceTypeQueen]={10000,10000},
  [PieceTypeKing]={0,0}};
TUNECONST VPair evalKnightMob={40,20};
TUNECONST VPair evalBishopPair={400,0};
TUNECONST VPair evalBishopMob={50,30};
TUNECONST VPair evalOppositeBishopFactor={256,192}; // /256.
TUNECONST VPair evalRookFileMob={50,0};
TUNECONST VPair evalRookRankMob={40,0};
TUNECONST VPair evalQueenMob={0,0};
TUNECONST VPair evalTempoDefault={35,0};
TUNECONST VPair evalTempoKQKQ={200,200};
TUNECONST VPair evalTempoKQQKQQ={500,500};
TUNECONST Value evalHalfMoveFactor=2048;
TUNECONST Value evalWeightFactor=144;

////////////////////////////////////////////////////////////////////////////////
// Derived values
////////////////////////////////////////////////////////////////////////////////

int evalHalfMoveFactors[128];
uint8_t evalWeightEGFactors[128];

////////////////////////////////////////////////////////////////////////////////
// Private prototypes
////////////////////////////////////////////////////////////////////////////////

VPair evaluateDefault(EvalData *data);
VPair evaluateKPvK(EvalData *data);
void evalGetMatData(const Pos *pos, EvalMatData *matData);
void evalComputeMatData(const Pos *pos, EvalMatData *matData);
void evalGetPawnData(EvalData *data);
void evalComputePawnData(EvalData *data, EvalPawnData *pawnData);
VPair evalPiece1(EvalData *data, PieceType type, Sq sq, Colour colour);
VPair evalPiece2(EvalData *data, PieceType type, Sq sq, Colour colour);
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
  evalOptionNewVPair("KnightMobility", &evalKnightMob);
  evalOptionNewVPair("BishopPair", &evalBishopPair);
  evalOptionNewVPair("BishopMobility", &evalBishopMob);
  uciOptionNewSpin("OppositeBishopFactorMG", &evalSetValue, &evalOppositeBishopFactor.mg, 0, 512, evalOppositeBishopFactor.mg);
  uciOptionNewSpin("OppositeBishopFactorEG", &evalSetValue, &evalOppositeBishopFactor.eg, 0, 512, evalOppositeBishopFactor.eg);
  evalOptionNewVPair("RookFileMobility", &evalRookFileMob);
  evalOptionNewVPair("RookRankMobility", &evalRookRankMob);
  evalOptionNewVPair("QueenMobility", &evalQueenMob);
  evalOptionNewVPair("Tempo", &evalTempoDefault);
  evalOptionNewVPair("TempoKQKQ", &evalTempoKQKQ);
  evalOptionNewVPair("TempoKQQKQQ", &evalTempoKQQKQQ);
  uciOptionNewSpin("HalfMoveFactor", &evalSetValue, &evalHalfMoveFactor, 1, 32768, evalHalfMoveFactor);
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
  assert(halfMoves<128);
  scalarScore=(scalarScore*evalHalfMoveFactors[halfMoves])/256;
  
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
  memset(data->attacksPiece, 0, sizeof(data->attacksPiece));
  memset(data->attacksPieceLE, 0, sizeof(data->attacksPieceLE));
  data->attacksColour[ColourWhite]=data->attacksColour[ColourBlack]=BBNone;
  data->attacksAll=BBNone;
  PieceType type, type2;
  Piece piece;
  const Sq *sq, *sqEnd;

  // 1st pass.

  // Pawns (special case).
//  evalGetPawnData(data);
//  evalVPairAdd(&score, &data->pawnData.score);
/*
  // Pieces.
  for(type=PieceTypeKnight;type<=PieceTypeKing;++type) {
    // White pieces.
    piece=pieceMake(type, ColourWhite);
    sq=posGetPieceListStart(pos, piece);
    sqEnd=posGetPieceListEnd(pos, piece);
    for(;sq<sqEnd;++sq)
    {
      VPair pieceScore=evalPiece1(data, type, *sq, ColourWhite);
      evalVPairAdd(&score, &pieceScore);
    }
    data->attacksColour[ColourWhite]|=data->attacksPiece[piece];
    for(type2=type;type2<=PieceTypeKing;++type2)
      data->attacksPieceLE[pieceMake(type2,ColourWhite)]|=data->attacksPiece[piece];

    // Black pieces.
    piece=pieceMake(type, ColourBlack);
    sq=posGetPieceListStart(pos, piece);
    sqEnd=posGetPieceListEnd(pos, piece);
    for(;sq<sqEnd;++sq)
    {
      VPair pieceScore=evalPiece1(data, type, *sq, ColourBlack);
      evalVPairSub(&score, &pieceScore);
    }
    data->attacksColour[ColourBlack]|=data->attacksPiece[piece];
    for(type2=PieceTypePawn;type2<=type;++type2)
      data->attacksPieceLE[pieceMake(type2,ColourBlack)]|=data->attacksPiece[piece];
  }
  data->attacksAll=(data->attacksColour[ColourWhite]|data->attacksColour[ColourBlack]);
*/
  // 2nd pass.

  // Pieces only (pawns only have single pass).
  for(type=PieceTypeKnight;type<=PieceTypeKing;++type)
  {
    // White pieces.
    piece=pieceMake(type, ColourWhite);
    sq=posGetPieceListStart(pos, piece);
    sqEnd=posGetPieceListEnd(pos, piece);
    for(;sq<sqEnd;++sq)
    {
      VPair pieceScore=evalPiece2(data, type, *sq, ColourWhite);
      evalVPairAdd(&score, &pieceScore);
    }
    
    // Black pieces.
    piece=pieceMake(type, ColourBlack);
    sq=posGetPieceListStart(pos, piece);
    sqEnd=posGetPieceListEnd(pos, piece);
    for(;sq<sqEnd;++sq)
    {
      VPair pieceScore=evalPiece2(data, type, *sq, ColourBlack);
      evalVPairSub(&score, &pieceScore);
    }
  }
  
  return score;
}

VPair evaluateKPvK(EvalData *data)
{
  // Use tablebase to find exact result.
  BitBaseResult result=bitbaseProbe(data->pos);
  switch(result)
  {
    case BitBaseResultDraw:
      data->matData.offset=VPairZero;
      data->matData.tempo=VPairZero;
      data->matData.scoreOffset=0;
      return VPairZero;
    break;
    case BitBaseResultWin:
    {
      Colour attacker=(posGetBBPiece(data->pos, PieceWPawn)!=BBNone ? ColourWhite : ColourBlack);
      data->matData.scoreOffset+=(attacker==ColourWhite ? ScoreHardWin : -ScoreHardWin);
      return evaluateDefault(data);
    }
    break;
  }
  
  assert(false);
  return VPairZero;
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
  if (entry->type==EvalMatTypeInvalid)
    entry->type=evalGetMatType(pos);
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
  matData->weightEG=evalWeightEGFactors[pieceWeight];
  matData->weightMG=256-matData->weightEG;
  
  // Specific material combinations.
  unsigned int factor=1024;
  switch(matData->type)
  {
    case EvalMatTypeInvalid:
      assert(false);
    break;
    case EvalMatTypeOther:
      assert(mat); // KvK should already be handled.
      const MatInfo matPawns=matInfoMakeMaskPieceType(PieceTypePawn);
      const MatInfo matMinors=(matInfoMakeMaskPieceType(PieceTypeKnight) |
                               matInfoMakeMaskPieceType(PieceTypeBishopL) |
                               matInfoMakeMaskPieceType(PieceTypeBishopD));
      const MatInfo matMajors=(matInfoMakeMaskPieceType(PieceTypeRook)|matInfoMakeMaskPieceType(PieceTypeQueen));
      const MatInfo matWhite=matInfoMakeMaskColour(ColourWhite);
      const MatInfo matBlack=matInfoMakeMaskColour(ColourBlack);
      
      if (!(mat & matPawns))
      {
        // Pawnless.
        if ((mat & matMinors)==mat)
        {
          // Minors only.
          switch(minorCount)
          {
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
              else
              {
                assert(mat==(M(PieceWBishopL,1)|M(PieceBKnight,1)) || // KBvKN.
                       mat==(M(PieceWBishopD,1)|M(PieceBKnight,1)) ||
                       mat==(M(PieceBBishopL,1)|M(PieceWKnight,1)) ||
                       mat==(M(PieceBBishopD,1)|M(PieceWKnight,1)) ||
                       mat==(M(PieceWBishopL,1)|M(PieceBBishopD,1)) || // KBvKB (opposite bishops)
                       mat==(M(PieceWBishopD,1)|M(PieceBBishopL,1)) ||
                       mat==(M(PieceWKnight,1)|M(PieceBKnight,1))); // KNvKN.
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
                       mat==(M(PieceBBishopL,1)|M(PieceBBishopD,1)|M(PieceWBishopL,1)) ||
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
        }
        else if ((mat & matMajors)==mat)
        {
          // Majors only.
          
          // Single side with material should be easy win (at least a rook ahead).
          if ((mat & matWhite)==mat)
            matData->scoreOffset+=ScoreEasyWin;
          else if ((mat & matBlack)==mat)
            matData->scoreOffset-=ScoreEasyWin;
          else if (mat==(M(PieceWQueen,1)|M(PieceBRook,1))|| // KQvKR.
                   mat==(M(PieceBQueen,1)|M(PieceWRook,1)))
            factor/=2;
          else if (mat==(M(PieceWQueen,1)|M(PieceBQueen,1))) // KQvKQ.
            matData->tempo=evalTempoKQKQ;
          else if (mat==(M(PieceWQueen,2)|M(PieceBQueen,2))) // KQQvKQQ.
            matData->tempo=evalTempoKQQKQQ;
        }
        else
        {
          // Mix of major and minor pieces.
          switch(minorCount+rookCount+queenCount)
          {
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
      // Special evaluation function.
      matData->function=&evaluateKPvK;
    break;
    case EvalMatTypeKBPvK:
    break;
  }
  matData->weightMG=(matData->weightMG*factor)/1024;
  matData->weightEG=(matData->weightEG*factor)/1024;
  
  // Opposite coloured bishop endgames are drawish.
  if ((wBishopL^wBishopD) && (bBishopL^bBishopD) && (wBishopL^bBishopL))
  {
    matData->weightMG=(matData->weightMG*evalOppositeBishopFactor.mg)/256;
    matData->weightEG=(matData->weightEG*evalOppositeBishopFactor.eg)/256;
  }
  
  // Material.
  evalVPairAddMul(&matData->offset, &evalMaterial[PieceTypePawn], G(PieceWPawn)-G(PieceBPawn));
  evalVPairAddMul(&matData->offset, &evalMaterial[PieceTypeKnight], G(PieceWKnight)-G(PieceBKnight));
  evalVPairAddMul(&matData->offset, &evalMaterial[PieceTypeBishopL], whiteBishopCount-blackBishopCount);
  evalVPairAddMul(&matData->offset, &evalMaterial[PieceTypeRook], G(PieceWRook)-G(PieceBRook));
  evalVPairAddMul(&matData->offset, &evalMaterial[PieceTypeQueen], G(PieceWQueen)-G(PieceBQueen));

  // Bishop pair bonus.
  if (wBishopL && wBishopD)
    evalVPairAdd(&matData->offset, &evalBishopPair);
  if (bBishopL && bBishopD)
    evalVPairSub(&matData->offset, &evalBishopPair);

# undef G
# undef M
}

void evalGetPawnData(EvalData *data)
{
  // Grab hash entry for this position key.
  const Pos *pos=data->pos;
  uint64_t key=(uint64_t)posGetPawnKey(pos);
  EvalPawnData *entry=htableGrab(evalPawnTable, key);
  
  // If not a match recompute data.
  if (entry->pawns[ColourWhite]!=posGetBBPiece(pos, PieceWPawn) ||
      entry->pawns[ColourBlack]!=posGetBBPiece(pos, PieceBPawn))
    evalComputePawnData(data, entry);
  
  // Copy data to return it.
  data->pawnData=*entry;
  
  // We are finished with Entry, release lock.
  htableRelease(evalPawnTable, key);
}

void evalComputePawnData(EvalData *data, EvalPawnData *pawnData)
{
  // Init.
  pawnData->score=VPairZero;
  const Pos *pos=data->pos;
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

  // Attack data.
  data->attacksPiece[PieceWPawn]=attacks[ColourWhite];
  data->attacksPiece[PieceBPawn]=attacks[ColourBlack];
  PieceType type;
  for(type=PieceTypePawn;type<=PieceTypeKing;++type)
  {
    data->attacksPieceLE[pieceMake(type, ColourWhite)]|=data->attacksPiece[PieceWPawn];
    data->attacksPieceLE[pieceMake(type, ColourBlack)]|=data->attacksPiece[PieceBPawn];
  }
}

VPair evalPiece1(EvalData *data, PieceType type, Sq sq, Colour colour) {
  // Init.
  VPair score=VPairZero;
  const Pos *pos=data->pos;
  Sq adjSq=(colour==ColourWhite ? sq : sqFlip(sq));
  BB bb=bbSq(sq);
  BB occ=posGetBBAll(pos);
  Piece piece=pieceMake(type, colour);

  // Attacks.
  BB attacks=attacksPieceType(type, sq, occ);
  data->attacksPiece[piece]|=attacks;

  // Piece-specific.
  switch(type)
  {
    case PieceTypeKnight: break;
    case PieceTypeBishopL:
    case PieceTypeBishopD: break;
    case PieceTypeRook: break;
    case PieceTypeQueen: break;
    case PieceTypeKing: break;
    default: assert(false); break;
  }

  return score;
}

VPair evalPiece2(EvalData *data, PieceType type, Sq sq, Colour colour)
{
  // Init.
  VPair score=VPairZero;
  const Pos *pos=data->pos;
  BB bb=bbSq(sq);

  // Piece-specific.
  switch(type)
  {
    case PieceTypeKnight:
    {
      // Mobility.
      BB attacks=attacksKnight(sq);
      evalVPairAddMul(&score, &evalKnightMob, bbPopCount(attacks));
    }
    break;
    case PieceTypeBishopL:
    case PieceTypeBishopD:
    {
      // Mobility.
      BB attacks=attacksPieceType(PieceTypeBishopL, sq, posGetBBAll(pos)); // this will be cached eventaully (if these changes prove useful)
      evalVPairAddMul(&score, &evalBishopMob, bbPopCount(attacks));
    }
    break;
    case PieceTypeRook:
    {
      BB rankBB=bbRank(sqRank(sq));
      BB attacks=attacksPieceType(PieceTypeRook, sq, posGetBBAll(pos));

      // Mobility.
      evalVPairAddMul(&score, &evalRookFileMob, bbPopCount(attacks & bbFileFill(bb)));
      evalVPairAddMul(&score, &evalRookRankMob, bbPopCount(attacks & rankBB));
    }
    break;
    case PieceTypeQueen:
    {
      BB attacks=attacksPieceType(PieceTypeQueen, sq, posGetBBAll(pos));

      // Mobility.
      evalVPairAddMul(&score, &evalQueenMob, bbPopCount(attacks));
    }
    break;
    case PieceTypeKing: break;
    default: assert(false); break;
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
  // Calculate factor for number of half moves since capture/pawn move.
  unsigned int i;
  for(i=0;i<128;++i)
  {
    float factor=exp2f(-((float)(i*i)/((float)evalHalfMoveFactor)));
    assert(factor>=0.0 && factor<=1.0);
    evalHalfMoveFactors[i]=floorf(255.0*factor);
  }
  
  // Calculate factor for each material weight.
  for(i=0;i<128;++i)
  {
    float factor=exp2f(-(float)(i*i)/((float)evalWeightFactor));
    assert(factor>=0.0 && factor<=1.0);
    evalWeightEGFactors[i]=floorf(255.0*factor);
  }
}

EvalMatType evalComputeMatType(const Pos *pos)
{
# define MAKE(p,n) matInfoMake((p),(n))
# define MASK(t) matInfoMakeMaskPieceType(t)
  
  // Grab material info.
  MatInfo mat=posGetMatInfo(pos);
  
  // Compute material infos (done at compile time, hopefully).
  const MatInfo matKings=(MAKE(PieceWKing,1)|MAKE(PieceBKing,1));
  const MatInfo matKNvK=(MAKE(PieceWKnight,1)|matKings);
  const MatInfo matKvKN=(MAKE(PieceBKnight,1)|matKings);
  const MatInfo matBishopsL=(MASK(PieceTypeBishopL)|matKings);
  const MatInfo matBishopsD=(MASK(PieceTypeBishopD)|matKings);
  
  // If only pieces are bishops and all share same colour squares, draw.
  if ((mat & ~matBishopsL)==0 || (mat & ~matBishopsD)==0)
    return EvalMatTypeDraw;
  
  // Check for known combinations.
  unsigned int pieceCount=bbPopCount(posGetBBAll(pos));
  assert(pieceCount>=2 && pieceCount<=32);
  switch(pieceCount)
  {
    case 2:
      // This should be handled by same-bishop code above.
      assert(false);
    break;
    case 3:
      if (mat==matKNvK || mat==matKvKN)
        return EvalMatTypeDraw;
      else if (mat==(MAKE(PieceWPawn,1)|matKings) || mat==(MAKE(PieceBPawn,1)|matKings))
        return EvalMatTypeKPvK;
    break;
    case 4:
      if (mat==(MAKE(PieceWKnight,2)|matKings) || mat==(MAKE(PieceBKnight,2)|matKings))
        return EvalMatTypeKNNvK;
      else if (mat==(MAKE(PieceWBishopL,1)|MAKE(PieceWPawn,1)|matKings) ||
               mat==(MAKE(PieceWBishopD,1)|MAKE(PieceWPawn,1)|matKings) ||
               mat==(MAKE(PieceBBishopL,1)|MAKE(PieceBPawn,1)|matKings) ||
               mat==(MAKE(PieceBBishopD,1)|MAKE(PieceBPawn,1)|matKings))
        return EvalMatTypeKBPvK;
    break;
  }
  
  // Other combination.
  return EvalMatTypeOther;
  
# undef MASK
# undef MAKE
}
