#include "attacks.h"
#include "pos.h"
#include "search.h"
#include "uci.h"

int main()
{
  AttacksInit();
  PosInit();
  SearchInit();
  
  UCILoop();
  
  SearchQuit();
  
  return 0;
}
