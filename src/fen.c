#include <stdlib.h>
#include <string.h>
#include "fen.h"

bool FENRead(fen_t *Data, const char *String)
{
  // Fen requires 4-6 fields separated by spaces
  const char *Fields[6];
  Fields[0]=String;
  int I;
  for(I=1;I<6;++I)
  {
    char *Next=strstr(Fields[I-1], " ");
    if (Next==NULL)
      break;
    Fields[I]=Next+1;
  }
  if (I<=3)
    return false; // Not enough fields given
  
  // Clear data struct
  memset(Data->Array, empty, 64*sizeof(piece_t));
  Data->STM=white;
  Data->CastRights=castrights_none;
  Data->EPSq=sqinvalid;
  Data->HalfMoveClock=0;
  Data->FullMoveNumber=1;
  
  // 1. Piece placement
  const char *Char=Fields[0];
  int X=0, Y=7;
  while(*Char!=' ') // We know a space exists before the null byte
    switch(*Char++)
    {
      case '1': X+=1; break;
      case '2': X+=2; break;
      case '3': X+=3; break;
      case '4': X+=4; break;
      case '5': X+=5; break;
      case '6': X+=6; break;
      case '7': X+=7; break;
      case '8': X+=8; break;
      case '/':
        if (X!=8)
          return false;
        X=0;
        --Y;
      break;
      case 'P': Data->Array[XYTOSQ(X++,Y)]=wpawn; break;
      case 'N': Data->Array[XYTOSQ(X++,Y)]=wknight; break;
      case 'B': Data->Array[XYTOSQ(X++,Y)]=wbishop; break;
      case 'R': Data->Array[XYTOSQ(X++,Y)]=wrook; break;
      case 'Q': Data->Array[XYTOSQ(X++,Y)]=wqueen; break;
      case 'K': Data->Array[XYTOSQ(X++,Y)]=wking; break;
      case 'p': Data->Array[XYTOSQ(X++,Y)]=bpawn; break;
      case 'n': Data->Array[XYTOSQ(X++,Y)]=bknight; break;
      case 'b': Data->Array[XYTOSQ(X++,Y)]=bbishop; break;
      case 'r': Data->Array[XYTOSQ(X++,Y)]=brook; break;
      case 'q': Data->Array[XYTOSQ(X++,Y)]=bqueen; break;
      case 'k': Data->Array[XYTOSQ(X++,Y)]=bking; break;
      default: return false; break;
    }
  if (Y!=0 || X!=8)
    return false;
  
  // 2. Active colour
  switch(Fields[1][0])
  {
    case 'w': Data->STM=white; break;
    case 'b': Data->STM=black; break;
    default: return false; break;
  }
  
  // 3. Castling availability
  Char=Fields[2];
  while(*Char!=' ') // We know a space exists before the null byte
    switch(*Char++)
    {
      case 'K': Data->CastRights|=castrights_K; break;
      case 'Q': Data->CastRights|=castrights_Q; break;
      case 'k': Data->CastRights|=castrights_k; break;
      case 'q': Data->CastRights|=castrights_q; break;
      case '-': break;
      default: return false; break;
    }
  
  // 4. En passent target square
  if (Fields[3][0]!='\0' && Fields[3][1]!='\0')
  {
    int X=(Fields[3][0]-'a'), Y=(Fields[3][1]-'1');
    if (X>=0 && X<8 && Y>=0 && Y<8)
      Data->EPSq=XYTOSQ(X,Y);
  }
  
  // 5. Halfmove clock
  if (Fields[4]!=NULL)
    Data->HalfMoveClock=atoi(Fields[4]);
  
  // 6. Fullmove number
  if (Fields[5]!=NULL)
    Data->FullMoveNumber=atoi(Fields[5]);
  
  return true;
}
