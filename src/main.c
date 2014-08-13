#include <stdio.h>
#include <stdlib.h>
#include "attacks.h"
#include "bb.h"
#include "eval.h"
#include "main.h"
#include "pos.h"
#include "search.h"
#include "uci.h"

int main()
{
  BBInit();
  AttacksInit();
  PosInit();
  EvalInit();
  SearchInit();
  
  UCILoop();
  
  SearchQuit();
  EvalQuit();
  
  return 0;
}

void mainFatalError(const char *Format, ...)
{
  va_list ap;
  va_start(ap, Format);
  vfprintf(stderr, Format, ap);
  va_end(ap);
  exit(EXIT_FAILURE);
}
