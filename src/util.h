#ifndef UTIL_H
#define UTIL_H

#include <stdbool.h>
#include <stdint.h>

#define utilMin(a,b) ({ __typeof__ (a) _a = (a); __typeof__ (b) _b = (b); _a < _b ? _a : _b; })
#define utilMax(a,b) ({ __typeof__ (a) _a = (a); __typeof__ (b) _b = (b); _a > _b ? _a : _b; })

#define STATICASSERT3(pre,post) pre ## post
#define STATICASSERT2(pre,post) STATICASSERT3(pre,post)
#define STATICASSERT(cond) typedef struct { int static_assertion_failed : !!(cond); } STATICASSERT2(static_assertion_failed_,__COUNTER__)

uint64_t utilNextPowTwo64(uint64_t x);
bool utilIsPowTwo64(uint64_t x);
bool utilStrEqual(const char *a, const char *b);
void utilRandSeed(uint64_t seed);
uint64_t utilRand64(void);

#endif
