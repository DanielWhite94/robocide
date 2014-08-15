#include <stdio.h>
#include "perft.h"
#include "time.h"
#include "moves.h"
#include "types.h"

void Perft(pos_t *Pos, unsigned int MaxDepth)
{
  printf("Perft:\n");
  printf("%6s %11s %9s %15s\n", "Depth", "Nodes", "Time", "NPS");
  unsigned int Depth;
  for(Depth=1;Depth<=MaxDepth;++Depth)
  {
    ms_t Time=TimeGet();
    unsigned long long int Nodes=PerftRaw(Pos, Depth);
    Time=TimeGet()-Time;
    
    if (Time>0)
    {
      unsigned long long int NPS=(Nodes*1000llu)/Time;
      printf("%6i %11llu %9llu %4llu,%03llu,%03llunps\n", Depth, Nodes,
             Time, NPS/1000000, (NPS/1000)%1000, NPS%1000);
    }
    else
      printf("%6i %11llu %9i %15s\n", Depth, Nodes, 0, "-");
  }
}

void Divide(pos_t *Pos, unsigned int Depth)
{
  if (Depth<1)
    return;
  
  unsigned long long int Total=0;
  moves_t Moves;
  MovesInit(&Moves, Pos, true);
  MovesRewind(&Moves, MOVE_INVALID);
  move_t Move;
  while((Move=MovesNext(&Moves))!=MOVE_INVALID)
  {
    if (!PosMakeMove(Pos, Move))
      continue;
    unsigned long long int Nodes=PerftRaw(Pos, Depth-1);
    char Str[8];
    PosMoveToStr(Move, Str);
    printf("  %6s %12llu\n", Str, Nodes);
    Total+=Nodes;
    PosUndoMove(Pos);
  }
  printf("Total: %llu\n", Total);
}

unsigned long long int PerftRaw(pos_t *Pos, unsigned int Depth)
{
  if (Depth<1)
    return 1;
  
  unsigned long long int Total=0;
  moves_t Moves;
  MovesInit(&Moves, Pos, true);
  MovesRewind(&Moves, MOVE_INVALID);
  move_t Move;
  while((Move=MovesNext(&Moves))!=MOVE_INVALID)
  {
    if (!PosMakeMove(Pos, Move))
      continue;
    Total+=PerftRaw(Pos, Depth-1);
    PosUndoMove(Pos);
  }
  
  return Total;
}
