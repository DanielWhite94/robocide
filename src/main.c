#include "attacks.h"
#include "bb.h"
#include "eval.h"
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
