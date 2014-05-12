#include "util.h"

uint64_t NextPowTwo64(uint64_t X)
{
  X--;
  X|=X>>1;
  X|=X>>2;
  X|=X>>4;
  X|=X>>8;
  X|=X>>16;
  X|=X>>32;
  X++;
  return X;
}
