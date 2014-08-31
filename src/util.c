#include <string.h>

#include "util.h"

uint64_t utilNextPowTwo64(uint64_t x)
{
  x--;
  x|=x>>1;
  x|=x>>2;
  x|=x>>4;
  x|=x>>8;
  x|=x>>16;
  x|=x>>32;
  x++;
  return x;
}

bool utilIsPowTwo64(uint64_t x)
{
  return (x&(x-1))==0;
}

bool utilStrEqual(const char *a, const char *b)
{
  return (strcmp(a, b)==0);
}
