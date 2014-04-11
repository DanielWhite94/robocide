#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "perft.h"
#include "pos.h"
#include "search.h"
#include "time.h"
#include "uci.h"

void UCILoop()
{
  // Turn off output buffering (saves us having to call fflush())
  if (setvbuf(stdout, NULL, _IOLBF, 0)!=0)
    return;
  
  // Create 'working' position
  pos_t *Pos=PosNew(NULL);
  if (Pos==NULL)
    return;
  
  // Read lines from the GUI
  char *Line=NULL;
  size_t LineLen=0;
  while(1)
  {
    // Get line from 'GUI' (and strip newline character)
    if (getline(&Line, &LineLen, stdin)==-1)
      break; // Error
    if (Line[strlen(Line)-1]=='\n')
      Line[strlen(Line)-1]='\0';
    
    // Store time command was received
    ms_t RecvTime=TimeGet();
    
    // Parse command
    char *SavePtr, *Part;
    Part=strtok_r(Line, " ", &SavePtr);
    if (!strcmp(Part, "go"))
    {
      // Parse arguments
      ms_t MoveTime=0, TotalTime=0, IncTime=0;
      int MovesToGo=0;
      bool Infinite=false;
      
      while((Part=strtok_r(NULL, " ", &SavePtr))!=NULL)
      {
        if ((!strcmp(Part, "wtime") && PosGetSTM(Pos)==white) ||
            (!strcmp(Part, "btime") && PosGetSTM(Pos)==black))
        {
          if ((Part=strtok_r(NULL, " ", &SavePtr))!=NULL)
            TotalTime=atoll(Part);
        }
        else if ((!strcmp(Part, "winc") && PosGetSTM(Pos)==white) ||
                 (!strcmp(Part, "binc") && PosGetSTM(Pos)==black))
        {
          if ((Part=strtok_r(NULL, " ", &SavePtr))!=NULL)
            IncTime=atoll(Part);
        }
        else if (!strcmp(Part, "movestogo"))
        {
          if ((Part=strtok_r(NULL, " ", &SavePtr))!=NULL)
            MovesToGo=atoi(Part);
        }
        else if (!strcmp(Part, "movetime"))
        {
          if ((Part=strtok_r(NULL, " ", &SavePtr))!=NULL)
            MoveTime=atoll(Part);
        }
        else if (!strcmp(Part, "infinite"))
          Infinite=true;
      }
      
      // Decide how to use our time
      if (MovesToGo<=0)
        MovesToGo=25;
      if (MoveTime==0)
        MoveTime=TotalTime/MovesToGo+IncTime;
      ms_t MaxTime=TotalTime-25;
      if (MoveTime>MaxTime)
        MoveTime=MaxTime;
      
      // Search
      SearchThink(Pos, RecvTime, MoveTime, Infinite);
    }
    else if (!strcmp(Part, "position"))
    {
      // Get position (either startpos or FEN string)
      if ((Part=strtok_r(NULL, " ", &SavePtr))==NULL)
        continue;
      if (!strcmp(Part, "startpos"))
      {
        if (!PosSetToFEN(Pos, NULL))
          continue;
      }
      else if (!strcmp(Part, "fen"))
      {
        char *Start=Part+4;
        char *End=strstr(Start, "moves");
        if (End!=NULL)
          *(End-1)='\0';
        if (!PosSetToFEN(Pos, Start))
          continue;
        if (End!=NULL)
          *(End-1)=' ';
      }
      else
        continue;
      
      // Make any moves given
      bool InMoves=false;
      while((Part=strtok_r(NULL, " ", &SavePtr))!=NULL)
      {
        if (!InMoves && !strcmp(Part, "moves"))
          InMoves=true;
        else if (InMoves)
        {
          move_t Move=PosStrToMove(Pos, Part);
          if (!PosMakeMove(Pos, Move))
            break;
        }
      }
    }
    else if (!strcmp(Part, "isready"))
      puts("readyok");
    else if (!strcmp(Part, "stop"))
      SearchStop();
    else if (!strcmp(Part, "ucinewgame"))
      SearchReset();
    else if (!strcmp(Part, "quit"))
      break;
    else if (!strcmp(Part, "disp"))
      PosDraw(Pos);
    else if (!strcmp(Part, "perft"))
    {
      if ((Part=strtok_r(NULL, " ", &SavePtr))==NULL)
        continue;
      unsigned int Depth=atoi(Part);
      if (Depth>=1)
        Perft(Pos, Depth);
    }
    else if (!strcmp(Part, "divide"))
    {
      if ((Part=strtok_r(NULL, " ", &SavePtr))==NULL)
        continue;
      unsigned int Depth=atoi(Part);
      if (Depth>=1)
        Divide(Pos, Depth);
    }
    else if (!strcmp(Part, "uci"))
      puts("id name robocide\nid author Daniel White\nuciok");
  }
  
  // Clean up
  free(Line);
  PosFree(Pos);
}
