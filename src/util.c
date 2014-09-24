#include <string.h>

#include "util.h"

uint64_t utilRandState=31;

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

void utilRandSeed(uint64_t seed)
{
  utilRandState=seed;
}

uint64_t utilRand64(void)
{
  // Uses xorshift*.
  utilRandState^=utilRandState>>12;
  utilRandState^=utilRandState<<25;
  utilRandState^=utilRandState>>27;
  return utilRandState*2685821657736338717llu;
}
