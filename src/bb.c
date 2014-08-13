#include <stdio.h>
#include <string.h>
#include "bb.h"

const bb_t BBNone=0x0000000000000000llu, BBAll=0xFFFFFFFFFFFFFFFFllu;
const bb_t BBA1=(((bb_t)1)<<A1), BBB1=(((bb_t)1)<<B1), BBC1=(((bb_t)1)<<C1), BBD1=(((bb_t)1)<<D1),
           BBE1=(((bb_t)1)<<E1), BBF1=(((bb_t)1)<<F1), BBG1=(((bb_t)1)<<G1), BBH1=(((bb_t)1)<<H1),
           BBA8=(((bb_t)1)<<A8), BBB8=(((bb_t)1)<<B8), BBC8=(((bb_t)1)<<C8), BBD8=(((bb_t)1)<<D8),
           BBE8=(((bb_t)1)<<E8), BBF8=(((bb_t)1)<<F8), BBG8=(((bb_t)1)<<G8), BBH8=(((bb_t)1)<<H8);
const bb_t BBFileA=0x0101010101010101llu, BBFileB=0x0202020202020202llu,
           BBFileC=0x0404040404040404llu, BBFileD=0x0808080808080808llu,
           BBFileE=0x1010101010101010llu, BBFileF=0x2020202020202020llu,
           BBFileG=0x4040404040404040llu, BBFileH=0x8080808080808080llu;
const bb_t BBRank1=0x00000000000000FFllu, BBRank2=0x000000000000FF00llu,
           BBRank3=0x0000000000FF0000llu, BBRank4=0x00000000FF000000llu,
           BBRank5=0x000000FF00000000llu, BBRank6=0x0000FF0000000000llu,
           BBRank7=0x00FF000000000000llu, BBRank8=0xFF00000000000000llu;
const bb_t BBLight=0x55AA55AA55AA55AAllu, BBDark=0xAA55AA55AA55AA55llu;

bb_t BBBetween[64][64], BBBeyond[64][64];

const int BBScanForwardTable[64]={
   0, 47,  1, 56, 48, 27,  2, 60,
  57, 49, 41, 37, 28, 16,  3, 61,
  54, 58, 35, 52, 50, 42, 21, 44,
  38, 32, 29, 23, 17, 11,  4, 62,
  46, 55, 26, 59, 40, 36, 15, 53,
  34, 51, 20, 43, 31, 22, 10, 45,
  25, 39, 14, 33, 19, 30,  9, 24,
  13, 18,  8, 12,  7,  6,  5, 63
};

void BBInit()
{
  // Note: The code below uses 0x88 coordinates
# define TOSQ(S) ((((S)&0xF0)>>1)|((S)&0x07))
  memset(BBBetween, 0, sizeof(bb_t)*64*64);
  memset(BBBeyond, 0, sizeof(bb_t)*64*64);
  unsigned int Sq1, Sq2, Sq3;
  int DirI, Dir, Dirs[8]={-17,-16,-15,-1,+1,+15,+16,+17};
  // Loop over every square, then every direction, then every square in that
  // direction
  for(Sq1=0;Sq1<128;Sq1=((Sq1+9)&~8))
    for(DirI=0;DirI<8;++DirI)
    {
      sq_t Sq164=TOSQ(Sq1);
      Dir=Dirs[DirI];
      bb_t Set=BBNone;
      for(Sq2=Sq1+Dir;;Sq2+=Dir)
      {
        // Bad square?
        if (Sq2 & 0x88)
          break;
        
        // Set BB_Between array
        sq_t Sq264=TOSQ(Sq2);
        BBBetween[Sq164][Sq264]=Set;
        
        // Set BB_Beyond array
        for(Sq3=Sq2+Dir;!(Sq3 & 0x88);Sq3+=Dir)
          BBBeyond[Sq164][Sq264]|=SQTOBB(TOSQ(Sq3));
        
        // Add current square to set
        Set|=SQTOBB(Sq264);
      }
    }
# undef TOSQ
}

void BBDraw(bb_t Set)
{
  int X, Y;
  for(Y=7;Y>=0;--Y)
  {
    for(X=0;X<8;++X)
      printf(" %i", (SQTOBB(XYTOSQ(X,Y)) & Set)!=0);
    printf("\n");
  }
}
