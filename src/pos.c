#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "attacks.h"
#include "fen.h"
#include "pos.h"
#include "WELL512a.h"

////////////////////////////////////////////////////////////////////////////////
// 
////////////////////////////////////////////////////////////////////////////////

#ifndef MAX
#define MAX(A,B) (((A)>(B)) ? (A) : (B))
#endif

typedef struct
{
  move_t LastMove;
  unsigned int HalfMoveClock;
  sq_t EPSq;
  castrights_t CastRights;
  piece_t CapPiece;
  sq_t CapSq;
  hkey_t Key;
}posdata_t;

struct pos_t
{
  bb_t BB[16]; // [piecetype]
  uint8_t Array64[64]; // [sq], gives index to PieceList
  sq_t PieceList[16*16]; // [piecetype*16+n], 0<=n<16
  uint8_t PieceListNext[16]; // [piecetype], gives next empty slot
  posdata_t *DataStart, *DataEnd, *Data;
  bb_t BBCol[2], BBAll;
  col_t STM;
  unsigned int FullMoveNumber;
  hkey_t PawnKey, MatKey;
};

char PosPieceToCharArray[16];
char PosPromoCharArray[16];
const char *PosStartFEN="rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
castrights_t PosCastUpdate[64];

hkey_t PosKeySTM, PosKeyPiece[16][64], PosKeyEP[128], PosKeyCastling[16];
hkey_t PosPawnKeyPiece[16][64];
hkey_t PosMatKey[256];

////////////////////////////////////////////////////////////////////////////////
// Private prototypes
////////////////////////////////////////////////////////////////////////////////

void PosClean(pos_t *Pos);
inline void PosPieceAdd(pos_t *Pos, piece_t Piece, sq_t Sq);
inline void PosPieceRemove(pos_t *Pos, sq_t Sq);
inline void PosPieceMove(pos_t *Pos, sq_t FromSq, sq_t ToSq);
inline void PosPieceMoveChange(pos_t *Pos, sq_t FromSq, sq_t ToSq, piece_t ToPiece);
inline move_t *PosGenPseudoNormal(const pos_t *Pos, move_t *Moves, bb_t Allowed);
inline move_t *PosGenPseudoPawnCaptures(const pos_t *Pos, move_t *Moves);
inline move_t *PosGenPseudoPawnQuiets(const pos_t *Pos, move_t *Moves);
inline move_t *PosGenPseudoCast(const pos_t *Pos, move_t *Moves);
bool PosIsConsistent(const pos_t *Pos);
char PosPromoChar(piece_t Piece);
hkey_t PosComputeKey(const pos_t *Pos);
hkey_t PosComputePawnKey(const pos_t *Pos);
hkey_t PosComputeMatKey(const pos_t *Pos);
hkey_t PosRandKey();

////////////////////////////////////////////////////////////////////////////////
// Public functions
////////////////////////////////////////////////////////////////////////////////

void PosInit()
{
  // Piece to character arrays
  memset(PosPieceToCharArray, '?', 16);
  PosPieceToCharArray[empty]='.';
  PosPieceToCharArray[wpawn]='P';
  PosPieceToCharArray[wknight]='N';
  PosPieceToCharArray[wbishopl]='B';
  PosPieceToCharArray[wbishopd]='B';
  PosPieceToCharArray[wrook]='R';
  PosPieceToCharArray[wqueen]='Q';
  PosPieceToCharArray[wking]='K';
  PosPieceToCharArray[bpawn]='p';
  PosPieceToCharArray[bknight]='n';
  PosPieceToCharArray[bbishopl]='b';
  PosPieceToCharArray[bbishopd]='b';
  PosPieceToCharArray[brook]='r';
  PosPieceToCharArray[bqueen]='q';
  PosPieceToCharArray[bking]='k';
  
  memset(PosPromoCharArray, '?', 16);
  PosPromoCharArray[wknight]=PosPromoCharArray[bknight]='n';
  PosPromoCharArray[wbishopl]=PosPromoCharArray[bbishopl]='b';
  PosPromoCharArray[wbishopd]=PosPromoCharArray[bbishopd]='b';
  PosPromoCharArray[wrook]=PosPromoCharArray[brook]='r';
  PosPromoCharArray[wqueen]=PosPromoCharArray[bqueen]='q';
  
  // Array to update castling rights in PosMakeMove()
  memset(PosCastUpdate, 255, 64*sizeof(castrights_t));
  PosCastUpdate[A1]=~castrights_Q;
  PosCastUpdate[A8]=~castrights_q;
  PosCastUpdate[E1]=~castrights_KQ;
  PosCastUpdate[E8]=~castrights_kq;
  PosCastUpdate[H1]=~castrights_K;
  PosCastUpdate[H8]=~castrights_k;
  
  // Hash keys
  unsigned int RandInit[16]={1804289383,846930886,1681692777,1714636915,1957747793,424238335,719885386,1649760492,596516649,1189641421,1025202362,1350490027,783368690,1102520059,2044897763,1967513926};
  InitWELLRNG512a(RandInit);
  PosKeySTM=PosRandKey();
  memset(PosKeyPiece, 0, 16*64*sizeof(hkey_t));
  memset(PosPawnKeyPiece, 0, 16*64*sizeof(hkey_t));
  memset(PosKeyEP, 0, 128*sizeof(hkey_t));
  memset(PosKeyCastling, 0, 16*sizeof(hkey_t));
  int I;
  for(I=0;I<64;++I)
  {
    PosKeyPiece[wpawn][I]=PosRandKey();
    PosKeyPiece[wknight][I]=PosRandKey();
    PosKeyPiece[wbishopl][I]=PosRandKey();
    PosKeyPiece[wbishopd][I]=PosRandKey();
    PosKeyPiece[wrook][I]=PosRandKey();
    PosKeyPiece[wqueen][I]=PosRandKey();
    PosKeyPiece[wking][I]=PosRandKey();
    PosKeyPiece[bpawn][I]=PosRandKey();
    PosKeyPiece[bknight][I]=PosRandKey();
    PosKeyPiece[bbishopl][I]=PosRandKey();
    PosKeyPiece[bbishopd][I]=PosRandKey();
    PosKeyPiece[brook][I]=PosRandKey();
    PosKeyPiece[bqueen][I]=PosRandKey();
    PosKeyPiece[bking][I]=PosRandKey();
    
    PosPawnKeyPiece[wpawn][I]=PosRandKey();
    PosPawnKeyPiece[bpawn][I]=PosRandKey();
  }
  for(I=0;I<8;++I)
    PosKeyEP[XYTOSQ(I,2)]=PosKeyEP[XYTOSQ(I,5)]=PosRandKey();
  PosKeyCastling[castrights_K]=PosRandKey();
  PosKeyCastling[castrights_Q]=PosRandKey();
  PosKeyCastling[castrights_k]=PosRandKey();
  PosKeyCastling[castrights_q]=PosRandKey();
  PosKeyCastling[castrights_KQ]=PosKeyCastling[castrights_K]^PosKeyCastling[castrights_Q];
  PosKeyCastling[castrights_Kk]=PosKeyCastling[castrights_K]^PosKeyCastling[castrights_k];
  PosKeyCastling[castrights_Kq]=PosKeyCastling[castrights_K]^PosKeyCastling[castrights_q];
  PosKeyCastling[castrights_Qk]=PosKeyCastling[castrights_Q]^PosKeyCastling[castrights_k];
  PosKeyCastling[castrights_Qq]=PosKeyCastling[castrights_Q]^PosKeyCastling[castrights_q];
  PosKeyCastling[castrights_kq]=PosKeyCastling[castrights_k]^PosKeyCastling[castrights_q];
  PosKeyCastling[castrights_KQk]=PosKeyCastling[castrights_KQ]^PosKeyCastling[castrights_k];
  PosKeyCastling[castrights_KQq]=PosKeyCastling[castrights_KQ]^PosKeyCastling[castrights_q];
  PosKeyCastling[castrights_Kkq]=PosKeyCastling[castrights_Kk]^PosKeyCastling[castrights_q];
  PosKeyCastling[castrights_Qkq]=PosKeyCastling[castrights_Qk]^PosKeyCastling[castrights_q];
  PosKeyCastling[castrights_KQkq]=PosKeyCastling[castrights_KQ]^PosKeyCastling[castrights_kq];
  
  PosMatKey[0]=0; // for empty squares
  for(I=1;I<256;++I)
    PosMatKey[I]=PosRandKey();
}

pos_t *PosNew(const char *gFEN)
{
  // Create clean position
  pos_t *Pos=malloc(sizeof(pos_t));
  posdata_t *PosData=malloc(sizeof(posdata_t));
  if (Pos==NULL || PosData==NULL)
  {
    free(Pos);
    free(PosData);
    return NULL;
  }
  Pos->DataStart=PosData;
  Pos->DataEnd=PosData+1;
  
  // If no FEN given use initial position
  const char *FEN=(gFEN!=NULL ? gFEN : PosStartFEN);
  
  // Set to FEN
  if (!PosSetToFEN(Pos, FEN))
  {
    PosFree(Pos);
    return NULL;
  }
  
  return Pos;
}

void PosFree(pos_t *Pos)
{
  if (Pos==NULL)
    return;
  free(Pos->DataStart);
  free(Pos);
}

pos_t *PosCopy(const pos_t *Src)
{
  // Allocate memory
  pos_t *Pos=malloc(sizeof(pos_t));
  size_t DataSize=(Src->DataEnd-Src->DataStart);
  posdata_t *PosData=malloc(DataSize*sizeof(posdata_t));
  if (Pos==NULL || PosData==NULL)
  {
    free(Pos);
    free(PosData);
    return NULL;
  }
  
  // Set data
  *Pos=*Src;
  Pos->DataStart=PosData;
  Pos->DataEnd=PosData+DataSize;
  size_t DataLen=(Src->Data-Src->DataStart);
  Pos->Data=PosData+DataLen;
  memcpy(Pos->DataStart, Src->DataStart, (DataLen+1)*sizeof(posdata_t));
  
  assert(PosIsConsistent(Pos));
  
  return Pos;
}

bool PosSetToFEN(pos_t *Pos, const char *String)
{
  // Parse FEN
  fen_t FEN;
  if (String==NULL)
  {
    if (!FENRead(&FEN, "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"))
      return false;
  }
  else if (!FENRead(&FEN, String))
    return false;
  
  // Set position to clean state
  PosClean(Pos);
  
  // Set position to given FEN
  sq_t Sq;
  for(Sq=0;Sq<64;++Sq)
    if (FEN.Array[Sq]!=empty)
      PosPieceAdd(Pos, FEN.Array[Sq], Sq);
  Pos->STM=FEN.STM;
  Pos->FullMoveNumber=FEN.FullMoveNumber;
  Pos->Data->LastMove=MOVE_NULL;
  Pos->Data->HalfMoveClock=FEN.HalfMoveClock;
  Pos->Data->EPSq=FEN.EPSq; // TODO: Only set EPSq if legal ep capture possible
  Pos->Data->CastRights=FEN.CastRights;
  Pos->Data->CapPiece=empty;
  Pos->Data->CapSq=sqinvalid;
  Pos->Data->Key=PosComputeKey(Pos);
  
  assert(PosIsConsistent(Pos));
  
  return true;
}

void PosDraw(const pos_t *Pos)
{
  int X, Y;
  for(Y=7;Y>=0;--Y)
  {
    for(X=0;X<8;++X)
      printf(" %c", PosPieceToChar(PosGetPieceOnSq(Pos, XYTOSQ(X,Y))));
    puts("");
  }
  puts("");
}

inline col_t PosGetSTM(const pos_t *Pos)
{
  return Pos->STM;
}

inline piece_t PosGetPieceOnSq(const pos_t *Pos, sq_t Sq)
{
  assert(SQ_ISVALID(Sq));
  return ((Pos->Array64[Sq])>>4);
}

inline bb_t PosGetBBAll(const pos_t *Pos)
{
  return Pos->BBAll;
}

inline bb_t PosGetBBColour(const pos_t *Pos, col_t Colour)
{
  return Pos->BBCol[Colour];
}

inline bb_t PosGetBBPiece(const pos_t *Pos, piece_t Piece)
{
  assert(PIECE_ISVALID(Piece));
  return Pos->BB[Piece];
}

inline char PosPieceToChar(piece_t Piece)
{
  assert(PIECE_ISVALID(Piece) || Piece==empty);
  return PosPieceToCharArray[Piece];
}

inline int PosPieceCount(const pos_t *Pos, piece_t Piece)
{
  assert(PIECE_ISVALID(Piece));
  return (PosGetPieceListEnd(Pos, Piece)-PosGetPieceListStart(Pos, Piece));
}

bool PosMakeMove(pos_t *Pos, move_t Move)
{
  assert(Move!=MOVE_NULL);
  assert(MOVE_GETCOLOUR(Move)==Pos->STM);
  
  // Use next data entry
  if (Pos->Data+1>=Pos->DataEnd)
  {
    /* We need more space */
    size_t Size=2*(Pos->DataEnd-Pos->DataStart);
    posdata_t *Ptr=realloc(Pos->DataStart, Size*sizeof(posdata_t));
    if (Ptr==NULL)
      return false;
    int DataOffset=Pos->Data-Pos->DataStart;
    Pos->DataStart=Ptr;
    Pos->DataEnd=Ptr+Size;
    Pos->Data=Ptr+DataOffset;
  }
  ++Pos->Data;
  
  // Update position data
  sq_t FromSq=MOVE_GETFROMSQ(Move);
  sq_t ToSq=MOVE_GETTOSQ(Move);
  piece_t Piece=PosGetPieceOnSq(Pos, FromSq);
  Pos->Data->LastMove=Move;
  Pos->Data->HalfMoveClock=(Pos->Data-1)->HalfMoveClock+1;
  Pos->Data->EPSq=sqinvalid;
  Pos->Data->CastRights=(Pos->Data-1)->CastRights & PosCastUpdate[ToSq] & PosCastUpdate[FromSq];
  Pos->Data->CapSq=(MOVE_ISEP(Move) ? ToSq^8 : ToSq);
  Pos->Data->CapPiece=PosGetPieceOnSq(Pos, Pos->Data->CapSq);
  Pos->Data->Key=(Pos->Data-1)->Key;
  Pos->FullMoveNumber+=(Pos->STM==black); // Inc after black's move
  Pos->STM=COL_SWAP(Pos->STM);
  
  // Remove captured piece (if any)
  if (Pos->Data->CapPiece!=empty)
  {
    // Remove piece
    PosPieceRemove(Pos, Pos->Data->CapSq);
    
    // Captures reset 50 move counter
    Pos->Data->HalfMoveClock=0;
  }
  
  // Special cases for pawns
  if (PIECE_TYPE(Piece)==pawn)
  {
    // Move the pawn, potentially promoting
    if (MOVE_ISPROMO(Move))
      PosPieceMoveChange(Pos, FromSq, ToSq, MOVE_GETPROMO(Move));
    else
      PosPieceMove(Pos, FromSq, ToSq);
    
    // Pawn moves reset 50 move counter
    Pos->Data->HalfMoveClock=0;
    
    // If double pawn move check set EP capture square (for next move)
    if (MOVE_ISDP(Move))
      Pos->Data->EPSq=(ToSq+FromSq)/2; // TODO: Only set EPSq if legal ep capture possible
  }
  else
  {
    // Move non-pawn piece (i.e. no promotion to worry about)
    PosPieceMove(Pos, FromSq, ToSq);
    
    // If castling also need to move the rook
    if (MOVE_ISCAST(Move))
    {
      if (ToSq>FromSq)
        PosPieceMove(Pos, ToSq+1, ToSq-1); // Kingside
      else
        PosPieceMove(Pos, ToSq-2, ToSq+1); // Queenside
    }
  }
  
  // Update key
  Pos->Data->Key^=PosKeySTM^PosKeyCastling[Pos->Data->CastRights^(Pos->Data-1)->CastRights]^
                  PosKeyEP[Pos->Data->EPSq]^PosKeyEP[(Pos->Data-1)->EPSq];
  
  // Does move leave STM in check?
  if (PosIsXSTMInCheck(Pos))
  {
    PosUndoMove(Pos);
    return false;
  }
  
  assert(PosIsConsistent(Pos));
  
  return true;
}

void PosUndoMove(pos_t *Pos)
{
  move_t Move=Pos->Data->LastMove;
  sq_t FromSq=MOVE_GETFROMSQ(Move);
  sq_t ToSq=MOVE_GETTOSQ(Move);
  Pos->STM=COL_SWAP(Pos->STM);
  Pos->FullMoveNumber-=(Pos->STM==black);
  
  // Move piece back
  if (MOVE_ISPROMO(Move))
    PosPieceMoveChange(Pos, ToSq, FromSq, PIECE_MAKE(pawn, Pos->STM));
  else
    PosPieceMove(Pos, ToSq, FromSq);
  
  // Replace any captured piece
  if (Pos->Data->CapPiece!=empty)
    PosPieceAdd(Pos, Pos->Data->CapPiece, Pos->Data->CapSq);
  
  // If castling replace the rook
  if (MOVE_ISCAST(Move))
  {
    if (ToSq>FromSq)
      PosPieceMove(Pos, ToSq-1, ToSq+1); // Kingside
    else
      PosPieceMove(Pos, ToSq+1, ToSq-2); // Queenside
  }
  
  // Discard data
  --Pos->Data;
  
  assert(PosIsConsistent(Pos));
}

bool PosIsSqAttackedByColour(const pos_t *Pos, sq_t Sq, col_t C)
{
  bb_t Occ=PosGetBBAll(Pos);
  
  /* Pawns */
  if (BBForwardOne(BBWingify(PosGetBBPiece(Pos, PIECE_MAKE(pawn, C))), C) &
      BBSqToBB(Sq))
    return true;
  
  /* Knights */
  if (AttacksKnight(Sq) & PosGetBBPiece(Pos, PIECE_MAKE(knight, C)))
    return true;
  
  /* Bishops */
  bb_t BishopSet=AttacksBishop(Sq, Occ);
  if (BishopSet & (PosGetBBPiece(Pos, PIECE_MAKE(bishopl, C)) |
                   PosGetBBPiece(Pos, PIECE_MAKE(bishopd, C))))
    return true;
  
  /* Rooks */
  bb_t RookSet=AttacksRook(Sq, Occ);
  if (RookSet & PosGetBBPiece(Pos, PIECE_MAKE(rook, C)))
    return true;
  
  /* Queens */
  if ((BishopSet | RookSet) & PosGetBBPiece(Pos, PIECE_MAKE(queen, C)))
    return true;
  
  /* King */
  if (AttacksKing(Sq) & PosGetBBPiece(Pos, PIECE_MAKE(king, C)))
    return true;
  
  return false;
}

inline sq_t PosGetKingSq(const pos_t *Pos, col_t C)
{
  return Pos->PieceList[PIECE_MAKE(king,C)<<4];
}

inline bool PosIsSTMInCheck(const pos_t *Pos)
{
  return PosIsSqAttackedByColour(Pos, PosGetKingSq(Pos, Pos->STM), COL_SWAP(Pos->STM));
}

inline bool PosIsXSTMInCheck(const pos_t *Pos)
{
  move_t M=Pos->Data->LastMove; // Need to know if last move was castling
  return (PosIsSqAttackedByColour(Pos, PosGetKingSq(Pos, COL_SWAP(Pos->STM)),
          Pos->STM) ||
          (MOVE_ISCAST(M) &&
           (PosIsSqAttackedByColour(Pos, MOVE_GETFROMSQ(M), Pos->STM) ||
            PosIsSqAttackedByColour(Pos, (MOVE_GETTOSQ(M)+MOVE_GETFROMSQ(M))/2,
                                    Pos->STM)
           )));
}

move_t *PosGenPseudoMoves(const pos_t *Pos, move_t *Moves)
{
  // Standard moves (no pawns or castling)
  Moves=PosGenPseudoNormal(Pos, Moves, ~0);
  
  // Pawns
  Moves=PosGenPseudoPawnCaptures(Pos, Moves);
  Moves=PosGenPseudoPawnQuiets(Pos, Moves);
  
  // Castling
  Moves=PosGenPseudoCast(Pos, Moves);
  
  return Moves;
}

move_t *PosGenPseudoCaptures(const pos_t *Pos, move_t *Moves)
{
  // Standard moves (no pawns or castling)
  bb_t Occ=PosGetBBAll(Pos);
  Moves=PosGenPseudoNormal(Pos, Moves, Occ);
  
  // Pawns
  Moves=PosGenPseudoPawnCaptures(Pos, Moves);
  
  return Moves;
}

move_t *PosGenPseudoQuiets(const pos_t *Pos, move_t *Moves)
{
  // Standard moves (no pawns or castling)
  bb_t Occ=PosGetBBAll(Pos);
  bb_t Empty=~Occ;
  Moves=PosGenPseudoNormal(Pos, Moves, Empty);
  
  // Pawns
  Moves=PosGenPseudoPawnQuiets(Pos, Moves);
  
  // Castling
  Moves=PosGenPseudoCast(Pos, Moves);
  
  return Moves;
}

inline move_t PosGenLegalMove(pos_t *Pos)
{
  move_t Moves[MOVES_MAX], *Move;
  move_t *End=PosGenPseudoMoves(Pos, Moves);
  for(Move=Moves;Move<End;++Move)
    if (PosMakeMove(Pos, *Move))
    {
      PosUndoMove(Pos);
      return *Move;
    }
  return MOVE_NULL;
}

inline const sq_t *PosGetPieceListStart(const pos_t *Pos, piece_t Piece)
{
  return &(Pos->PieceList[Piece<<4]);
}

inline const sq_t *PosGetPieceListEnd(const pos_t *Pos, piece_t Piece)
{
  return Pos->PieceList+Pos->PieceListNext[Piece];
}

void PosMoveToStr(move_t Move, char Str[static 6])
{
  // Special case for null move
  if (Move==MOVE_NULL)
  {
    strcpy(Str, "0000");
    return;
  }
  
  sq_t FromSq=MOVE_GETFROMSQ(Move);
  sq_t ToSq=MOVE_GETTOSQ(Move);
  Str[0]=SQ_X(FromSq)+'a';
  Str[1]=SQ_Y(FromSq)+'1';
  Str[2]=SQ_X(ToSq)+'a';
  Str[3]=SQ_Y(ToSq)+'1';
  Str[4]=(MOVE_ISPROMO(Move) ? PosPromoChar(MOVE_GETPROMO(Move)) : '\0');
  Str[5]='\0';
}

move_t PosStrToMove(const pos_t *Pos, const char Str[static 6])
{
  move_t Moves[MOVES_MAX], *Move;
  move_t *End=PosGenPseudoMoves(Pos, Moves);
  char GenStr[8];
  for(Move=Moves;Move<End;++Move)
  {
    PosMoveToStr(*Move, GenStr);
    if (!strcmp(Str, GenStr))
      return *Move;
  }
  return MOVE_NULL;
}

bool PosIsDraw(const pos_t *Pos, int Ply)
{
  // False positives are bad, false negatives are OK
  
  // Repetition (2-fold for repeat of tree pos, 3-fold otherwise)
  posdata_t *Ptr, *EndPtr=Pos->Data-Ply;
  assert(EndPtr>=Pos->DataStart);
  for(Ptr=Pos->Data-2;Ptr>=EndPtr;Ptr-=2)
    if (Ptr->Key==Pos->Data->Key)
      return true;
  int Count=0;
  EndPtr=MAX(Pos->DataStart, Pos->Data-PosGetHalfMoveClock(Pos));
  for(;Ptr>=EndPtr;Ptr-=2)
    Count+=(Ptr->Key==PosGetKey(Pos));
  if (Count>1)
    return true;
  
  // 50-move rule
  if (PosGetHalfMoveClock(Pos)>=100)
    return true;
  
  // Insufficient material
  // Any pawns, rooks or queens are potential mate-givers
  uint64_t Mat=PosGetMat(Pos);
  uint64_t PRQMask=(POSMAT_MASK(wpawn)|POSMAT_MASK(wrook)|POSMAT_MASK(wqueen)|
                    POSMAT_MASK(bpawn)|POSMAT_MASK(brook)|POSMAT_MASK(bqueen));
  if (!(Mat & PRQMask))
  {
    // Remove kings from mat
    Mat&=~(POSMAT_MASK(wking) | POSMAT_MASK(bking));
    
    // Special case: only material is bishops of a single colour
    // (this also covers KvK, with '0 bishops' as it were)
    uint64_t BishopLMask=(POSMAT_MASK(wbishopl) | POSMAT_MASK(bbishopl));
    uint64_t BishopDMask=(POSMAT_MASK(wbishopd) | POSMAT_MASK(bbishopd));
    if ((Mat & ~BishopLMask)==0 ||
        (Mat & ~BishopDMask)==0)
      return true;
    
    // Only one side with material?
    if ((Mat & POSMAT_MASKCOL(white))==0 || (Mat & POSMAT_MASKCOL(black))==0)
    {
      int NCount=POSMAT_GET(Mat, wknight)+POSMAT_GET(Mat, bknight);
      int LB=(Mat & BishopLMask), DB=(Mat & BishopDMask);
      if (NCount<2 && !(LB && DB) && (!NCount || !(LB || DB)))
        return true;
    }
  }
  
  return false;
}

inline unsigned int PosGetHalfMoveClock(const pos_t *Pos)
{
  return Pos->Data->HalfMoveClock;
}

bool PosLegalMoveExist(pos_t *Pos)
{
  return (PosGenLegalMove(Pos)!=MOVE_NULL);
}

inline hkey_t PosGetKey(const pos_t *Pos)
{
  return Pos->Data->Key;
}

inline hkey_t PosGetPawnKey(const pos_t *Pos)
{
  return Pos->PawnKey;
}

inline hkey_t PosGetMatKey(const pos_t *Pos)
{
  return Pos->MatKey;
}

inline uint64_t PosGetMat(const pos_t *Pos)
{
  // Grab piece offsets and subtract 'base' to give literal piece counts
  const uint64_t *PieceListNext=((uint64_t *)Pos->PieceListNext);
  uint64_t White=PieceListNext[0]-0x7060504030201000llu; // HACK assumes a lot
  uint64_t Black=PieceListNext[1]-0xF0E0D0C0B0A09080llu;
  
  // Interleave white and black into a single 64 bit integer (we only need 4
  // bits per piece)
  return ((Black<<4) | White);
}

////////////////////////////////////////////////////////////////////////////////
// Private functions
////////////////////////////////////////////////////////////////////////////////

void PosClean(pos_t *Pos)
{
  memset(Pos->BB, 0, 16*sizeof(bb_t));
  memset(Pos->Array64, 0, 64*sizeof(uint8_t));
  int I;
  for(I=0;I<16;++I)
    Pos->PieceListNext[I]=16*I;
  Pos->BBCol[white]=Pos->BBCol[black]=Pos->BBAll=0;
  Pos->STM=white;
  Pos->FullMoveNumber=1;
  Pos->PawnKey=0;
  Pos->MatKey=0;
  Pos->Data=Pos->DataStart;
  Pos->Data->LastMove=MOVE_NULL;
  Pos->Data->HalfMoveClock=0;
  Pos->Data->EPSq=sqinvalid;
  Pos->Data->CastRights=castrights_none;
  Pos->Data->CapPiece=empty;
  Pos->Data->CapSq=sqinvalid;
  Pos->Data->Key=0;
}

inline void PosPieceAdd(pos_t *Pos, piece_t Piece, sq_t Sq)
{
  // Sanity checks
  assert(PIECE_ISVALID(Piece));
  assert(SQ_ISVALID(Sq));
  assert(PosGetPieceOnSq(Pos, Sq)==empty);
  
  // Update pieces
  Pos->BB[Piece]^=BBSqToBB(Sq);
  Pos->BBCol[PIECE_COLOUR(Piece)]^=BBSqToBB(Sq);
  Pos->BBAll^=BBSqToBB(Sq);
  uint8_t Index=(Pos->PieceListNext[Piece]++);
  Pos->Array64[Sq]=Index;
  Pos->PieceList[Index]=Sq;
  
  // Update hash keys
  Pos->Data->Key^=PosKeyPiece[Piece][Sq];
  Pos->PawnKey^=PosPawnKeyPiece[Piece][Sq];
  Pos->MatKey^=PosMatKey[Index];
}

inline void PosPieceRemove(pos_t *Pos, sq_t Sq)
{
  // Sanity checks
  assert(SQ_ISVALID(Sq));
  assert(PosGetPieceOnSq(Pos, Sq)!=empty);
  
  // Update pieces
  uint8_t Index=Pos->Array64[Sq];
  piece_t Piece=(Index>>4);
  Pos->BB[Piece]^=BBSqToBB(Sq);
  Pos->BBCol[PIECE_COLOUR(Piece)]^=BBSqToBB(Sq);
  Pos->BBAll^=BBSqToBB(Sq);
  uint8_t LastIndex=(--Pos->PieceListNext[Piece]);
  Pos->PieceList[Index]=Pos->PieceList[LastIndex];
  Pos->Array64[Pos->PieceList[Index]]=Index; // Easy to forget this line...
  Pos->Array64[Sq]=(empty<<4);
  
  // Update hash keys
  Pos->Data->Key^=PosKeyPiece[Piece][Sq];
  Pos->PawnKey^=PosPawnKeyPiece[Piece][Sq];
  Pos->MatKey^=PosMatKey[LastIndex];
}

inline void PosPieceMove(pos_t *Pos, sq_t FromSq, sq_t ToSq)
{
  // Sanity checks
  assert(SQ_ISVALID(FromSq) && SQ_ISVALID(ToSq));
  assert(PosGetPieceOnSq(Pos, FromSq)!=empty);
  assert(PosGetPieceOnSq(Pos, ToSq)==empty);
  
  // Update pieces
  uint8_t Index=Pos->Array64[FromSq];
  piece_t Piece=(Index>>4);
  Pos->BB[Piece]^=BBSqToBB(FromSq)^BBSqToBB(ToSq);
  Pos->BBCol[PIECE_COLOUR(Piece)]^=BBSqToBB(FromSq)^BBSqToBB(ToSq);
  Pos->BBAll^=BBSqToBB(FromSq)^BBSqToBB(ToSq);
  Pos->Array64[ToSq]=Index;
  Pos->Array64[FromSq]=(empty<<4);
  Pos->PieceList[Index]=ToSq;
  
  // Update hash keys
  Pos->Data->Key^=PosKeyPiece[Piece][FromSq]^PosKeyPiece[Piece][ToSq];
  Pos->PawnKey^=PosPawnKeyPiece[Piece][FromSq]^PosPawnKeyPiece[Piece][ToSq];
}

inline void PosPieceMoveChange(pos_t *Pos, sq_t FromSq, sq_t ToSq, piece_t ToPiece)
{
  // Sanity checks
  assert(SQ_ISVALID(FromSq) && SQ_ISVALID(ToSq));
  assert(PosGetPieceOnSq(Pos, FromSq)!=empty);
  assert(PosGetPieceOnSq(Pos, ToSq)==empty);
  assert(PIECE_ISVALID(ToPiece));
  assert(PIECE_COLOUR(ToPiece)==PIECE_COLOUR(PosGetPieceOnSq(Pos, FromSq)));
  
  // Update pieces
  PosPieceRemove(Pos, FromSq);
  PosPieceAdd(Pos, ToPiece, ToSq);
}

inline move_t *PosGenPseudoNormal(const pos_t *Pos, move_t *Moves, bb_t Allowed)
{
  bb_t Friendly=PosGetBBColour(Pos, Pos->STM);
  Allowed&=~Friendly; // Don't want to self-capture
  bb_t Occ=PosGetBBAll(Pos);
  bb_t Set;
  const sq_t *Sq, *EndSq;
  move_t RawMove, MoveColour=(Pos->STM<<MOVE_SHIFTCOLOUR);
  piece_t Piece;
  
  // Knights
  Piece=PIECE_MAKE(knight, Pos->STM);
  Sq=PosGetPieceListStart(Pos, Piece);
  EndSq=PosGetPieceListEnd(Pos, Piece);
  for(;Sq<EndSq;++Sq)
  {
    RawMove=(MoveColour | ((*Sq)<<MOVE_SHIFTFROMSQ));
    Set=(AttacksKnight(*Sq) & Allowed);
    while(Set)
      *Moves++=(RawMove | BBScanReset(&Set));
  }
  
  // Bishops (these are split into light/dark squared, so include both)
  Piece=PIECE_MAKE(bishopl, Pos->STM);
  Sq=PosGetPieceListStart(Pos, Piece);
  EndSq=PosGetPieceListEnd(Pos, Piece);
  for(;Sq<EndSq;++Sq)
  {
    RawMove=(MoveColour | ((*Sq)<<MOVE_SHIFTFROMSQ));
    Set=(AttacksBishop(*Sq, Occ) & Allowed);
    while(Set)
      *Moves++=(RawMove | BBScanReset(&Set));
  }
  Piece=PIECE_MAKE(bishopd, Pos->STM);
  Sq=PosGetPieceListStart(Pos, Piece);
  EndSq=PosGetPieceListEnd(Pos, Piece);
  for(;Sq<EndSq;++Sq)
  {
    RawMove=(MoveColour | ((*Sq)<<MOVE_SHIFTFROMSQ));
    Set=(AttacksBishop(*Sq, Occ) & Allowed);
    while(Set)
      *Moves++=(RawMove | BBScanReset(&Set));
  }
  
  // Rooks
  Piece=PIECE_MAKE(rook, Pos->STM);
  Sq=PosGetPieceListStart(Pos, Piece);
  EndSq=PosGetPieceListEnd(Pos, Piece);
  for(;Sq<EndSq;++Sq)
  {
    RawMove=(MoveColour | ((*Sq)<<MOVE_SHIFTFROMSQ));
    Set=(AttacksRook(*Sq, Occ) & Allowed);
    while(Set)
      *Moves++=(RawMove | BBScanReset(&Set));
  }
  
  // Queens
  Piece=PIECE_MAKE(queen, Pos->STM);
  Sq=PosGetPieceListStart(Pos, Piece);
  EndSq=PosGetPieceListEnd(Pos, Piece);
  for(;Sq<EndSq;++Sq)
  {
    RawMove=(MoveColour | ((*Sq)<<MOVE_SHIFTFROMSQ));
    Set=(AttacksQueen(*Sq, Occ) & Allowed);
    while(Set)
      *Moves++=(RawMove | BBScanReset(&Set));
  }
  
  // King
  sq_t KSq=PosGetKingSq(Pos, Pos->STM);
  RawMove=(MoveColour | (KSq<<MOVE_SHIFTFROMSQ));
  Set=(AttacksKing(KSq) & Allowed);
  while(Set)
    *Moves++=(RawMove | BBScanReset(&Set));
  
  return Moves;
}

inline move_t *PosGenPseudoPawnCaptures(const pos_t *Pos, move_t *Moves)
{
  bb_t Opp=PosGetBBColour(Pos, COL_SWAP(Pos->STM));
  bb_t Empty=~PosGetBBAll(Pos);
  bb_t Set, Set2;
  if (PosGetSTM(Pos)==white)
  {
    // Forward promotion
    Set=BBNorthOne(PosGetBBPiece(Pos, wpawn)) & Empty & BBRank8;
    while(Set)
    {
      sq_t ToSq=BBScanReset(&Set);
      *Moves++=((white<<MOVE_SHIFTCOLOUR) | MOVE_MASKPROMO | MOVE_EXTRAQUEEN |
                ((ToSq-8)<<MOVE_SHIFTFROMSQ) | ToSq);
      *Moves++=((white<<MOVE_SHIFTCOLOUR) | MOVE_MASKPROMO | MOVE_EXTRAROOK |
                ((ToSq-8)<<MOVE_SHIFTFROMSQ) | ToSq);
      *Moves++=((white<<MOVE_SHIFTCOLOUR) | MOVE_MASKPROMO | MOVE_EXTRABISHOP |
                ((ToSq-8)<<MOVE_SHIFTFROMSQ) | ToSq);
      *Moves++=((white<<MOVE_SHIFTCOLOUR) | MOVE_MASKPROMO | MOVE_EXTRAKNIGHT |
                ((ToSq-8)<<MOVE_SHIFTFROMSQ) | ToSq);
    }
    
    // Capture left
    Set=BBWestOne(BBNorthOne(PosGetBBPiece(Pos, wpawn))) & Opp;
    Set2=Set & BBRank8;
    Set&=~BBRank8;
    while(Set)
    {
      sq_t ToSq=BBScanReset(&Set);
      *Moves++=((white<<MOVE_SHIFTCOLOUR) | ((ToSq-7)<<MOVE_SHIFTFROMSQ) | ToSq);
    }
    while(Set2)
    {
      sq_t ToSq=BBScanReset(&Set2);
      *Moves++=((white<<MOVE_SHIFTCOLOUR) | MOVE_MASKPROMO | MOVE_EXTRAQUEEN |
                ((ToSq-7)<<MOVE_SHIFTFROMSQ) | ToSq);
      *Moves++=((white<<MOVE_SHIFTCOLOUR) | MOVE_MASKPROMO | MOVE_EXTRAROOK |
                ((ToSq-7)<<MOVE_SHIFTFROMSQ) | ToSq);
      *Moves++=((white<<MOVE_SHIFTCOLOUR) | MOVE_MASKPROMO | MOVE_EXTRABISHOP |
                ((ToSq-7)<<MOVE_SHIFTFROMSQ) | ToSq);
      *Moves++=((white<<MOVE_SHIFTCOLOUR) | MOVE_MASKPROMO | MOVE_EXTRAKNIGHT |
                ((ToSq-7)<<MOVE_SHIFTFROMSQ) | ToSq);
    }
    
    // Capture right
    Set=BBEastOne(BBNorthOne(PosGetBBPiece(Pos, wpawn))) & Opp;
    Set2=Set & BBRank8;
    Set&=~BBRank8;
    while(Set)
    {
      sq_t ToSq=BBScanReset(&Set);
      *Moves++=((white<<MOVE_SHIFTCOLOUR) | ((ToSq-9)<<MOVE_SHIFTFROMSQ) | ToSq);
    }
    while(Set2)
    {
      sq_t ToSq=BBScanReset(&Set2);
      *Moves++=((white<<MOVE_SHIFTCOLOUR) | MOVE_MASKPROMO | MOVE_EXTRAQUEEN |
                ((ToSq-9)<<MOVE_SHIFTFROMSQ) | ToSq);
      *Moves++=((white<<MOVE_SHIFTCOLOUR) | MOVE_MASKPROMO | MOVE_EXTRAROOK |
                ((ToSq-9)<<MOVE_SHIFTFROMSQ) | ToSq);
      *Moves++=((white<<MOVE_SHIFTCOLOUR) | MOVE_MASKPROMO | MOVE_EXTRABISHOP |
                ((ToSq-9)<<MOVE_SHIFTFROMSQ) | ToSq);
      *Moves++=((white<<MOVE_SHIFTCOLOUR) | MOVE_MASKPROMO | MOVE_EXTRAKNIGHT |
                ((ToSq-9)<<MOVE_SHIFTFROMSQ) | ToSq);
    }
    
    // EP captures
    if (Pos->Data->EPSq!=sqinvalid)
    {
      // Left capture
      if (SQ_X(Pos->Data->EPSq)<7 &&
          PosGetPieceOnSq(Pos, Pos->Data->EPSq-7)==wpawn)
        *Moves++=((white<<MOVE_SHIFTCOLOUR) | MOVE_EXTRAEP |
                  ((Pos->Data->EPSq-7)<<MOVE_SHIFTFROMSQ) | Pos->Data->EPSq);
      
      // Right capture
      if (SQ_X(Pos->Data->EPSq)>0 &&
          PosGetPieceOnSq(Pos, Pos->Data->EPSq-9)==wpawn)
        *Moves++=((white<<MOVE_SHIFTCOLOUR) | MOVE_EXTRAEP |
                  ((Pos->Data->EPSq-9)<<MOVE_SHIFTFROMSQ) | Pos->Data->EPSq);
    }
  }
  else
  {
    // Forward promotion
    Set=BBSouthOne(PosGetBBPiece(Pos, bpawn)) & Empty & BBRank1;
    while(Set)
    {
      sq_t ToSq=BBScanReset(&Set);
      *Moves++=((black<<MOVE_SHIFTCOLOUR) | MOVE_MASKPROMO | MOVE_EXTRAQUEEN |
                ((ToSq+8)<<MOVE_SHIFTFROMSQ) | ToSq);
      *Moves++=((black<<MOVE_SHIFTCOLOUR) | MOVE_MASKPROMO | MOVE_EXTRAROOK |
                ((ToSq+8)<<MOVE_SHIFTFROMSQ) | ToSq);
      *Moves++=((black<<MOVE_SHIFTCOLOUR) | MOVE_MASKPROMO | MOVE_EXTRABISHOP |
                ((ToSq+8)<<MOVE_SHIFTFROMSQ) | ToSq);
      *Moves++=((black<<MOVE_SHIFTCOLOUR) | MOVE_MASKPROMO | MOVE_EXTRAKNIGHT |
                ((ToSq+8)<<MOVE_SHIFTFROMSQ) | ToSq);
    }
    
    // Capture left
    Set=BBWestOne(BBSouthOne(PosGetBBPiece(Pos, bpawn))) & Opp;
    Set2=Set & BBRank1;
    Set&=~BBRank1;
    while(Set)
    {
      sq_t ToSq=BBScanReset(&Set);
      *Moves++=((black<<MOVE_SHIFTCOLOUR) | ((ToSq+9)<<MOVE_SHIFTFROMSQ) | ToSq);
    }
    while(Set2)
    {
      sq_t ToSq=BBScanReset(&Set2);
      *Moves++=((black<<MOVE_SHIFTCOLOUR) | MOVE_MASKPROMO | MOVE_EXTRAQUEEN |
                ((ToSq+9)<<MOVE_SHIFTFROMSQ) | ToSq);
      *Moves++=((black<<MOVE_SHIFTCOLOUR) | MOVE_MASKPROMO | MOVE_EXTRAROOK |
                ((ToSq+9)<<MOVE_SHIFTFROMSQ) | ToSq);
      *Moves++=((black<<MOVE_SHIFTCOLOUR) | MOVE_MASKPROMO | MOVE_EXTRABISHOP |
                ((ToSq+9)<<MOVE_SHIFTFROMSQ) | ToSq);
      *Moves++=((black<<MOVE_SHIFTCOLOUR) | MOVE_MASKPROMO | MOVE_EXTRAKNIGHT |
                ((ToSq+9)<<MOVE_SHIFTFROMSQ) | ToSq);
    }
    
    // Capture right
    Set=BBEastOne(BBSouthOne(PosGetBBPiece(Pos, bpawn))) & Opp;
    Set2=Set & BBRank1;
    Set&=~BBRank1;
    while(Set)
    {
      sq_t ToSq=BBScanReset(&Set);
      *Moves++=((black<<MOVE_SHIFTCOLOUR) | ((ToSq+7)<<MOVE_SHIFTFROMSQ) | ToSq);
    }
    while(Set2)
    {
      sq_t ToSq=BBScanReset(&Set2);
      *Moves++=((black<<MOVE_SHIFTCOLOUR) | MOVE_MASKPROMO | MOVE_EXTRAQUEEN |
                ((ToSq+7)<<MOVE_SHIFTFROMSQ) | ToSq);
      *Moves++=((black<<MOVE_SHIFTCOLOUR) | MOVE_MASKPROMO | MOVE_EXTRAROOK |
                ((ToSq+7)<<MOVE_SHIFTFROMSQ) | ToSq);
      *Moves++=((black<<MOVE_SHIFTCOLOUR) | MOVE_MASKPROMO | MOVE_EXTRABISHOP |
                ((ToSq+7)<<MOVE_SHIFTFROMSQ) | ToSq);
      *Moves++=((black<<MOVE_SHIFTCOLOUR) | MOVE_MASKPROMO | MOVE_EXTRAKNIGHT |
                ((ToSq+7)<<MOVE_SHIFTFROMSQ) | ToSq);
    }
    
    // EP captures
    if (Pos->Data->EPSq!=sqinvalid)
    {
      // Left capture
      if (SQ_X(Pos->Data->EPSq)<7 &&
          PosGetPieceOnSq(Pos, Pos->Data->EPSq+9)==bpawn)
        *Moves++=((black<<MOVE_SHIFTCOLOUR) | MOVE_EXTRAEP |
                  ((Pos->Data->EPSq+9)<<MOVE_SHIFTFROMSQ) | Pos->Data->EPSq);
      
      // Right capture
      if (SQ_X(Pos->Data->EPSq)>0 &&
          PosGetPieceOnSq(Pos, Pos->Data->EPSq+7)==bpawn)
        *Moves++=((black<<MOVE_SHIFTCOLOUR) | MOVE_EXTRAEP |
                  ((Pos->Data->EPSq+7)<<MOVE_SHIFTFROMSQ) | Pos->Data->EPSq);
    }
  }
  
  return Moves;
}

inline move_t *PosGenPseudoPawnQuiets(const pos_t *Pos, move_t *Moves)
{
  bb_t Occ=PosGetBBAll(Pos);
  bb_t Empty=~Occ;
  bb_t Set, Set2;
  if (Pos->STM==white)
  {
    // Standard move forward
    Set=Set2=BBNorthOne(PosGetBBPiece(Pos, wpawn)) & Empty & ~BBRank8;
    while(Set)
    {
      sq_t ToSq=BBScanReset(&Set);
      *Moves++=((white<<MOVE_SHIFTCOLOUR) | ((ToSq-8)<<MOVE_SHIFTFROMSQ) | ToSq);
    }
    
    // Double first move
    Set=BBNorthOne(Set2) & Empty & BBRank4;
    while(Set)
    {
      sq_t ToSq=BBScanReset(&Set);
      *Moves++=((white<<MOVE_SHIFTCOLOUR) | MOVE_EXTRADP |
                ((ToSq-16)<<MOVE_SHIFTFROMSQ) | ToSq);
    }
  }
  else
  {
    // Standard move forward
    Set=Set2=BBSouthOne(PosGetBBPiece(Pos, bpawn)) & Empty & ~BBRank1;
    while(Set)
    {
      sq_t ToSq=BBScanReset(&Set);
      *Moves++=((black<<MOVE_SHIFTCOLOUR) | ((ToSq+8)<<MOVE_SHIFTFROMSQ) | ToSq);
    }
    
    // Double first move
    Set=BBSouthOne(Set2) & Empty & BBRank5;
    while(Set)
    {
      sq_t ToSq=BBScanReset(&Set);
      *Moves++=((black<<MOVE_SHIFTCOLOUR) | MOVE_EXTRADP |
                ((ToSq+16)<<MOVE_SHIFTFROMSQ) | ToSq);
    }
  }
  
  return Moves;
}

inline move_t *PosGenPseudoCast(const pos_t *Pos, move_t *Moves)
{
  bb_t Occ=PosGetBBAll(Pos);
  if (Pos->STM==white)
  {
    if ((Pos->Data->CastRights & castrights_K) && !(Occ & (BBF1 | BBG1)))
      *Moves++=((white<<MOVE_SHIFTCOLOUR) | MOVE_EXTRACAST |
                (E1<<MOVE_SHIFTFROMSQ) | G1);
    if ((Pos->Data->CastRights & castrights_Q) && !(Occ & (BBB1 | BBC1 | BBD1)))
      *Moves++=((white<<MOVE_SHIFTCOLOUR) | MOVE_EXTRACAST |
                (E1<<MOVE_SHIFTFROMSQ) | C1);
  }
  else
  {
    if ((Pos->Data->CastRights & castrights_k) && !(Occ & (BBF8 | BBG8)))
      *Moves++=((black<<MOVE_SHIFTCOLOUR) | MOVE_EXTRACAST |
                (E8<<MOVE_SHIFTFROMSQ) | G8);
    if ((Pos->Data->CastRights & castrights_q) && !(Occ & (BBB8 | BBC8 | BBD8)))
      *Moves++=((black<<MOVE_SHIFTCOLOUR) | MOVE_EXTRACAST |
                (E8<<MOVE_SHIFTFROMSQ) | C8);
  }
  
  return Moves;
}

bool PosIsConsistent(const pos_t *Pos)
{
  char Error[512];
  
  // Test bitboards are self consistent
  bb_t WAll=0, BAll=0;
  piece_t Piece, Piece2;
  for(Piece=pawn;Piece<=king;++Piece)
  {
    for(Piece2=Piece+1;Piece2<=king;++Piece2)
    {
      bb_t WP1=Pos->BB[PIECE_MAKE(Piece, white)];
      bb_t BP1=Pos->BB[PIECE_MAKE(Piece, black)];
      bb_t WP2=Pos->BB[PIECE_MAKE(Piece2, white)];
      bb_t BP2=Pos->BB[PIECE_MAKE(Piece2, black)];
      if ((WP1 & WP2) || (WP1 & BP1) || (WP1 & BP2) || (WP2 & BP1) ||
          (WP2 & BP2) || (BP1 & BP2))
      {
        sprintf(Error, "Error: Bitboards for pieces %i and %i intersect (in "
                       "some way, and some colour combo).\n", Piece, Piece2);
        goto error;
      }
    }
    WAll|=Pos->BB[PIECE_MAKE(Piece, white)];
    BAll|=Pos->BB[PIECE_MAKE(Piece, black)];
  }
  if (WAll!=Pos->BBCol[white] || BAll!=Pos->BBCol[black])
  {
    strcpy(Error, "Bitboard 'col[white]' or 'col[black]' error.\n");
    goto error;
  }
  if ((WAll | BAll)!=Pos->BBAll)
  {
    strcpy(Error, "Bitboard 'all' error.\n");
    goto error;
  }
  
  // Test Array64 'pointers' are correct (consistent and agree with bitboards)
  sq_t Sq;
  uint8_t Index;
  for(Sq=0;Sq<64;++Sq)
  {
    Index=Pos->Array64[Sq];
    piece_t Piece=(Index>>4);
    if (Piece!=empty && !PIECE_ISVALID(Piece))
    {
      sprintf(Error, "Invalid piece '%i' derived from array64 index '%i' (%c%c)"
                     ".\n", Piece, Index, SQ_X(Sq)+'a', SQ_Y(Sq)+'1');
      goto error;
    }
    if (Index==0)
    {
      if (Pos->BBAll & BBSqToBB(Sq))
      {
        sprintf(Error, "Piece exists in bitboards but not in array (%c%c).\n",
                SQ_X(Sq)+'a', SQ_Y(Sq)+'1');
        goto error;
      }
      continue; // Special case (the 'null index')
    }
    if ((Pos->BB[Piece] & BBSqToBB(Sq))==0)
    {
      sprintf(Error, "Piece exists in array, but not in bitboards (%c%c).\n",
              SQ_X(Sq)+'a', SQ_Y(Sq)+'1');
      goto error;
    }
    if (Index>=Pos->PieceListNext[Piece])
    {
      sprintf(Error, "Array64 points outside of list for piece '%i' (%c%c).\n",
              Piece, SQ_X(Sq)+'a', SQ_Y(Sq)+'1');
      goto error;
    }
    if (Pos->PieceList[Index]!=Sq)
    {
      sprintf(Error, "Array64 sq %c%c disagrees with piece list sq %c%c at "
                     "index %i.\n", SQ_X(Sq)+'a', SQ_Y(Sq)+'1',
                     SQ_X(Pos->PieceList[Index])+'a',
                     SQ_Y(Pos->PieceList[Index])+'1', Index);
      goto error;
    }
  }
  
  // Test piece lists are correct
  for(Piece=0;Piece<16;++Piece)
  {
    for(Index=(Piece<<4);Index<Pos->PieceListNext[Piece];++Index)
      if ((Pos->BB[Piece] & BBSqToBB(Pos->PieceList[Index]))==0)
      {
        sprintf(Error, "Piece list thinks piece %i exists on %c%c but bitboards"
                       " do not.\n", Piece, SQ_X(Pos->PieceList[Index])+'a',
                       SQ_Y(Pos->PieceList[Index])+'1');
        goto error;
      }
  }
  
  // Test correct bishop pieces are correct (either light or dark)
  for(Sq=0;Sq<64;++Sq)
  {
    if ((PosGetPieceOnSq(Pos, Sq)==wbishopl || PosGetPieceOnSq(Pos, Sq)==bbishopl) && !SQ_ISLIGHT(Sq))
      return false;
    if ((PosGetPieceOnSq(Pos, Sq)==wbishopd || PosGetPieceOnSq(Pos, Sq)==bbishopd) && SQ_ISLIGHT(Sq))
      return false;
  }
  
  // Test hash keys match
  hkey_t TrueKey=PosComputeKey(Pos);
  hkey_t Key=PosGetKey(Pos);
  if (Key!=TrueKey)
  {
    sprintf(Error, "Key is %016"PRIxkey" while true key is %016"PRIxkey".\n",
            Key, TrueKey);
    goto error;
  }
  
  // Test pawn hash keys match
  hkey_t TruePawnKey=PosComputePawnKey(Pos);
  hkey_t PawnKey=PosGetPawnKey(Pos);
  if (PawnKey!=TruePawnKey)
  {
    sprintf(Error, "Pawn key is %016"PRIxkey" while true key is %016"PRIxkey".\n",
            PawnKey, TruePawnKey);
    goto error;
  }
  
  // Test mat hash keys match
  hkey_t TrueMatKey=PosComputeMatKey(Pos);
  hkey_t MatKey=PosGetMatKey(Pos);
  if (MatKey!=TrueMatKey)
  {
    sprintf(Error, "Mat key is %016"PRIxkey" while true key is %016"PRIxkey".\n",
            MatKey, TrueMatKey);
    goto error;
  }
  
  return true;
  
  error:
# ifndef NDEBUG
  printf("---------------------------------\n");
  printf("PosIsConsistent() failed:\n");
  puts(Error);
  PosDraw(Pos);
  printf("---------------------------------\n");
# endif
  return false;
}

char PosPromoChar(piece_t Piece)
{
  assert(PIECE_ISVALID(Piece));
  return PosPromoCharArray[Piece];
}

hkey_t PosComputeKey(const pos_t *Pos)
{
  hkey_t Key=0;
  
  // EP and castling
  Key^=PosKeyEP[Pos->Data->EPSq]^PosKeyCastling[Pos->Data->CastRights];
  
  // Colour
  if (PosGetSTM(Pos)==black)
    Key^=PosKeySTM;
  
  // Pieces
  sq_t Sq;
  for(Sq=0;Sq<64;++Sq)
    Key^=PosKeyPiece[PosGetPieceOnSq(Pos, Sq)][Sq];
  
  return Key;
}

hkey_t PosComputePawnKey(const pos_t *Pos)
{
  // Only piece placement is important for pawn key
  hkey_t Key=0;
  sq_t Sq;
  for(Sq=0;Sq<64;++Sq)
    Key^=PosPawnKeyPiece[PosGetPieceOnSq(Pos, Sq)][Sq];
  
  return Key;
}

hkey_t PosComputeMatKey(const pos_t *Pos)
{
  hkey_t Key=0;
  sq_t Sq;
  for(Sq=0;Sq<64;++Sq)
    Key^=PosMatKey[Pos->Array64[Sq]];
  
  return Key;
}

hkey_t PosRandKey()
{
  hkey_t A=((hkey_t)(WELLRNG512a()*65536)) & 0xFFFF;
  hkey_t B=((hkey_t)(WELLRNG512a()*65536)) & 0xFFFF;
  hkey_t C=((hkey_t)(WELLRNG512a()*65536)) & 0xFFFF;
  hkey_t D=((hkey_t)(WELLRNG512a()*65536)) & 0xFFFF;
  return ((A<<48) | (B<<32) | (C<<16) | D);
}

bool PosIsMovePseudoLegal(const pos_t *Pos, move_t Move)
{
  col_t STM=PosGetSTM(Pos);
  sq_t FromSq=MOVE_GETFROMSQ(Move);
  sq_t ToSq=MOVE_GETTOSQ(Move);
  piece_t FromPiece=PosGetPieceOnSq(Pos, FromSq);
  piece_t ToPiece=PosGetPieceOnSq(Pos, ToSq);
  
  // Special case: null move
  if (Move==MOVE_NULL)
    return false;
  
  // Is move of correct colour?
  if (MOVE_GETCOLOUR(Move)!=STM)
    return false;
  
  // Special case: castling
  if (MOVE_ISCAST(Move))
  {
    bb_t Occ=PosGetBBAll(Pos);
    if (STM==white)
    {
      if (ToSq>FromSq)
        return ((Pos->Data->CastRights & castrights_K) && !(Occ & (BBF1 | BBG1)));
      else
        return ((Pos->Data->CastRights & castrights_Q) && !(Occ & (BBB1 | BBC1 | BBD1)));
    }
    else
    {
      if (ToSq>FromSq)
        return ((Pos->Data->CastRights & castrights_k) && !(Occ & (BBF8 | BBG8)));
      else
        return ((Pos->Data->CastRights & castrights_q) && !(Occ & (BBB8 | BBC8 | BBD8)));
    }
  }
  
  // Is from piece of correct colour?
  if (FromPiece==empty || PIECE_COLOUR(FromPiece)!=STM)
    return false;
  
  // Special case: pawns
  if (PIECE_TYPE(FromPiece)==pawn)
  {
    // This is a bit complicated but we cannot be sure the hash move was also a
    // pawn move. e.g. consider hash move is white rook d5d4, current position
    // has white pawn on d5, but d5d4 is not legal.
    
    int DX=abs(SQ_X(ToSq)-SQ_X(FromSq));
    int DY=(SQ_Y(ToSq)-SQ_Y(FromSq));
    int PDY=(STM==white ? 1 : -1);
    
    if (MOVE_ISEP(Move))
      return (ToSq==Pos->Data->EPSq);
    else if (MOVE_ISDP(Move))
      return (ToPiece==empty && PosGetPieceOnSq(Pos, (ToSq+FromSq)/2)==empty);
    else if (DX==1 && DY==PDY)
      return (ToPiece!=empty && PIECE_COLOUR(ToPiece)==COL_SWAP(STM));
    else if (DX==0 && DY==PDY)
      return (ToPiece==empty);
    else
      return false;
  }
  
  // Was move meant to be a pawn move?
  if (MOVE_ISEP(Move) || MOVE_ISDP(Move) || MOVE_ISPROMO(Move))
    return false;
  
  // Is to piece empty/opp?
  if (ToPiece!=empty && PIECE_COLOUR(ToPiece)==STM)
    return false;
  
  // Valid movement for piece?
  int DX=abs(SQ_X(ToSq)-SQ_X(FromSq));
  int DY=abs(SQ_Y(ToSq)-SQ_Y(FromSq));
  switch(PIECE_TYPE(FromPiece))
  {
    case knight:
      return ((DX==2 && DY==1) || (DX==1 && DY==2));
    break;
    case bishopl:
    case bishopd:
      return ((AttacksBishop(FromSq, PosGetBBAll(Pos)) & SQTOBB(ToSq))!=0);
    break;
    case rook:
      return ((AttacksRook(FromSq, PosGetBBAll(Pos)) & SQTOBB(ToSq))!=0);
    break;
    case queen:
      return ((AttacksQueen(FromSq, PosGetBBAll(Pos)) & SQTOBB(ToSq))!=0);
    break;
    case king:
      return (DX<=1 && DY<=1);
    break;
    default:
      assert(false);
      return false;
    break;
  }
}
