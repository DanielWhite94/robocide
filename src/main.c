#include <stdlib.h>
#include "perft.h"
#include "pos.h"

int main()
{
  AttacksInit();
  PosInit();
  
  pos_t *Pos=PosNew(NULL);
  Perft(Pos, 128);
  
  PosFree(Pos);
  
  return EXIT_SUCCESS;
}
