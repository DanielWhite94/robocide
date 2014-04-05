#include "bb.h"

const bb_t BBB1=(((bb_t)1)<< 1), BBC1=(((bb_t)1)<< 2), BBD1=(((bb_t)1)<< 3),
           BBE1=(((bb_t)1)<< 4), BBF1=(((bb_t)1)<< 5), BBG1=(((bb_t)1)<< 6),
           BBB8=(((bb_t)1)<<57), BBC8=(((bb_t)1)<<58), BBD8=(((bb_t)1)<<59),
           BBE8=(((bb_t)1)<<60), BBF8=(((bb_t)1)<<61), BBG8=(((bb_t)1)<<62);
const bb_t BBFileA=0x0101010101010101llu, BBFileB=0x0202020202020202llu,
           BBFileC=0x0404040404040404llu, BBFileD=0x0808080808080808llu,
           BBFileE=0x1010101010101010llu, BBFileF=0x2020202020202020llu,
           BBFileG=0x4040404040404040llu, BBFileH=0x8080808080808080llu;
const bb_t BBRank1=0x00000000000000FFllu, BBRank2=0x000000000000FF00llu,
           BBRank3=0x0000000000FF0000llu, BBRank4=0x00000000FF000000llu,
           BBRank5=0x000000FF00000000llu, BBRank6=0x0000FF0000000000llu,
           BBRank7=0x00FF000000000000llu, BBRank8=0xFF00000000000000llu;

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
