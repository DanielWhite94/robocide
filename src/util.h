#ifndef UTIL_H
#define UTIL_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define utilMin(a,b) ({ __typeof__ (a) _a = (a); __typeof__ (b) _b = (b); _a < _b ? _a : _b; })
#define utilMax(a,b) ({ __typeof__ (a) _a = (a); __typeof__ (b) _b = (b); _a > _b ? _a : _b; })

#define STATICASSERT3(pre,post) pre ## post
#define STATICASSERT2(pre,post) STATICASSERT3(pre,post)
#define STATICASSERT(cond) typedef struct { int static_assertion_failed : !!(cond); } STATICASSERT2(static_assertion_failed_,__COUNTER__)

bool utilStrEqual(const char *a, const char *b);

void utilRandSeed(uint64_t seed);
uint64_t utilRand64(void);

unsigned utilFileCountLines(FILE *file); // expects file pointer to be positioned at the start of the file (and the function will move it back there before returning)

#endif
