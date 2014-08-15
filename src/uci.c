#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "eval.h"
#include "perft.h"
#include "pos.h"
#include "moves.h"
#include "search.h"
#include "see.h"
#include "time.h"
#include "uci.h"

////////////////////////////////////////////////////////////////////////////////
// 
////////////////////////////////////////////////////////////////////////////////

typedef enum
{
  ucioptiontype_check,  // "a checkbox that can either be true or false" */
  ucioptiontype_spin,   // "a spin wheel that can be an integer in a certain
                        //  range"
  ucioptiontype_combo,  // "a combo box that can have different predefined
                        //  strings as a value"
  ucioptiontype_button, // "a button that can be pressed to send a command to
                        //  the engine"
  ucioptiontype_string  // "a text field that has a string as a value"
}ucioptiontype_t;

typedef struct
{
  void(*Function)(bool Value, void *UserData);
  bool Default;
}ucioptioncheck_t;

typedef struct
{
  void(*Function)(int Value, void *UserData);
  int Min, Max, Default;
}ucioptionspin_t;

typedef struct
{
  void(*Function)(const char *Value, void *UserData);
  char *Default;
  char **Options; // Default should also be in this list
  int OptionCount;
}ucioptioncombo_t;

typedef struct
{
  void(*Function)(void *UserData);
}ucioptionbutton_t;

typedef struct
{
  void(*Function)(const char *Value, void *UserData);
  char *Default;
}ucioptionstring_t;

typedef struct
{
  char *Name;
  ucioptiontype_t Type;
  void *UserData;
  union
  {
    ucioptioncheck_t Check;
    ucioptionspin_t Spin;
    ucioptioncombo_t Combo;
    ucioptionbutton_t Button;
    ucioptionstring_t String;
  }Data;
}ucioption_t;

ucioption_t *UCIOptions=NULL;
size_t UCIOptionCount=0;

const char UCIBoolToString[2][8]={"false", "true"};

////////////////////////////////////////////////////////////////////////////////
// Private prototypes
////////////////////////////////////////////////////////////////////////////////

void UCIQuit();
void UCIOptionParseSetOptionString(char *String);
void UCIOptionPrint();
ucioption_t *UCIOptionNewBase();
ucioption_t *UCIOptionFromName(const char *Name);
bool UCIStringToBool(const char *String);

////////////////////////////////////////////////////////////////////////////////
// Public functions
////////////////////////////////////////////////////////////////////////////////

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
    
    // setoption is handled differently
    if (!strncmp(Line, "setoption", 9))
    {
      UCIOptionParseSetOptionString(Line);
      continue;
    }
    
    // Store time command was received
    ms_t RecvTime=TimeGet();
    
    // Parse command
    char *SavePtr, *Part;
    Part=strtok_r(Line, " ", &SavePtr);
    if (Part==NULL)
      continue;
    if (!strcmp(Part, "go"))
    {
      // Parse arguments
      ms_t MoveTime=0, TotalTime=0, IncTime=0;
      int MovesToGo=0;
      bool Infinite=false, Ponder=false;
      
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
        else if (!strcmp(Part, "ponder"))
          Ponder=true;
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
      SearchThink(Pos, RecvTime, MoveTime, Infinite, Ponder);
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
    else if (!strcmp(Part, "ponderhit"))
      SearchPonderHit();
    else if (!strcmp(Part, "isready"))
      puts("readyok");
    else if (!strcmp(Part, "stop"))
      SearchStop();
    else if (!strcmp(Part, "ucinewgame"))
    {
      SearchClear();
      EvalClear();
    }
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
    else if (!strcmp(Part, "see"))
    {
      moves_t Moves;
      MovesInit(&Moves, Pos, true);
      MovesRewind(&Moves, MOVE_INVALID);
      move_t Move;
      while((Move=MovesNext(&Moves))!=MOVE_INVALID)
      {
        sq_t ToSq=MOVE_GETTOSQ(Move);
        if (PosGetPieceOnSq(Pos, ToSq)==empty)
          continue;
        char Str[8];
        PosMoveToStr(Move, Str);
        printf("  %6s %4i\n", Str, SEE(Pos, MOVE_GETFROMSQ(Move), ToSq));
      }
    }
    else if (!strcmp(Part, "uci"))
    {
      puts("id name robocide\nid author Daniel White");
      UCIOptionPrint();
      puts("uciok");
    }
  }
  
  // Clean up
  free(Line);
  PosFree(Pos);
  UCIQuit();
}

bool UCIOptionNewCheck(const char *Name, void(*Function)(bool Value, void *UserData), void *UserData, bool Default)
{
  // Allocate any memory needed
  char *NameMem=malloc(strlen(Name)+1);
  if (NameMem==NULL)
    return false;
  
  // Allocate new option
  ucioption_t *Option=UCIOptionNewBase();
  if (Option==NULL)
  {
    free(NameMem);
    return false;
  }
  
  // Set data
  Option->Name=NameMem;
  strcpy(Option->Name, Name);
  Option->Type=ucioptiontype_check;
  Option->UserData=UserData;
  Option->Data.Check.Function=Function;
  Option->Data.Check.Default=Default;
  
  return true;
}

bool UCIOptionNewSpin(const char *Name, void(*Function)(int Value, void *UserData), void *UserData, int Min, int Max, int Default)
{
  // Allocate any memory needed
  char *NameMem=malloc(strlen(Name)+1);
  if (NameMem==NULL)
    return false;
  
  // Allocate new option
  ucioption_t *Option=UCIOptionNewBase();
  if (Option==NULL)
  {
    free(NameMem);
    return false;
  }
  
  // Set data
  Option->Name=NameMem;
  strcpy(Option->Name, Name);
  Option->Type=ucioptiontype_spin;
  Option->UserData=UserData;
  Option->Data.Spin.Function=Function;
  Option->Data.Spin.Min=Min;
  Option->Data.Spin.Max=Max;
  Option->Data.Spin.Default=Default;
  
  return true;
}

bool UCIOptionNewCombo(const char *Name, void(*Function)(const char *Value, void *UserData), void *UserData, const char *Default, int OptionCount, ...)
{
  // Allocate any memory needed
  char *NameMem=malloc(strlen(Name)+1);
  char *DefaultMem=malloc(strlen(Default)+1);
  char **OptionsMem=malloc(OptionCount*sizeof(char *));
  if (NameMem==NULL || DefaultMem==NULL || OptionsMem==NULL)
  {
    free(NameMem);
    free(DefaultMem);
    free(OptionsMem);
    return false;
  }
  
  // Parse variable number of options
  va_list ap;
  va_start(ap, OptionCount);
  int I;
  for(I=0;I<OptionCount;++I)
  {
    char *Arg=va_arg(ap, char *);
    OptionsMem[I]=malloc(strlen(Arg)+1);
    if (OptionsMem[I]==NULL)
    {
      va_end(ap);
      for(--I;I>=0;--I)
        free(OptionsMem[I]);
      free(NameMem);
      free(DefaultMem);
      free(OptionsMem);
      return false;
    }
    strcpy(OptionsMem[I], Arg);
  }
  va_end(ap);
  
  // Allocate new option
  ucioption_t *Option=UCIOptionNewBase();
  if (Option==NULL)
  {
    free(NameMem);
    free(DefaultMem);
    for(I=0;I<OptionCount;++I)
      free(OptionsMem[I]);
    free(OptionsMem);
    return false;
  }
  
  // Set data
  Option->Name=NameMem;
  strcpy(Option->Name, Name);
  Option->Type=ucioptiontype_combo;
  Option->UserData=UserData;
  Option->Data.Combo.Function=Function;
  Option->Data.Combo.Default=DefaultMem;
  strcpy(Option->Data.Combo.Default, Default);
  Option->Data.Combo.Options=OptionsMem;
  Option->Data.Combo.OptionCount=OptionCount;
  
  return true;
}

bool UCIOptionNewButton(const char *Name, void(*Function)(void *UserData), void *UserData)
{
  // Allocate any memory needed
  char *NameMem=malloc(strlen(Name)+1);
  if (NameMem==NULL)
    return false;
  
  // Allocate new option
  ucioption_t *Option=UCIOptionNewBase();
  if (Option==NULL)
  {
    free(NameMem);
    return false;
  }
  
  // Set data
  Option->Name=NameMem;
  strcpy(Option->Name, Name);
  Option->Type=ucioptiontype_button;
  Option->UserData=UserData;
  Option->Data.Button.Function=Function;
  
  return true;
}

bool UCIOptionNewString(const char *Name, void(*Function)(const char *Value, void *UserData), void *UserData, const char *Default)
{
  // Allocate any memory need
  char *NameMem=malloc(strlen(Name)+1);
  char *DefaultMem=malloc(strlen(Default)+1);
  if (NameMem==NULL || DefaultMem==NULL)
  {
    free(NameMem);
    free(DefaultMem);
    return false;
  }
  
  // Allocate new option
  ucioption_t *Option=UCIOptionNewBase();
  if (Option==NULL)
  {
    free(NameMem);
    free(DefaultMem);
    return false;
  }
  
  // Set data
  Option->Name=NameMem;
  strcpy(Option->Name, Name);
  Option->Type=ucioptiontype_string;
  Option->UserData=UserData;
  Option->Data.String.Function=Function;
  Option->Data.String.Default=DefaultMem;
  strcpy(Option->Data.String.Default, Default);
  
  return true;
}

////////////////////////////////////////////////////////////////////////////////
// Private functions
////////////////////////////////////////////////////////////////////////////////

void UCIQuit()
{
  // Clear up each option
  int I, J;
  for(I=0;I<UCIOptionCount;++I)
  {
    ucioption_t *Option=&UCIOptions[I];
    
    // Name
    free(Option->Name);
    
    // Data
    switch(Option->Type)
    {
      case ucioptiontype_check:
      case ucioptiontype_spin:
      case ucioptiontype_button:
        // Nothing to do
      break;
      case ucioptiontype_combo:
        free(Option->Data.Combo.Default);
        for(J=0;J<Option->Data.Combo.OptionCount;++J)
          free(Option->Data.Combo.Options[J]);
        free(Option->Data.Combo.Options);
      break;
      case ucioptiontype_string:
        free(Option->Data.String.Default);
      break;
    }
  }
  
  // Free array of options
  free(UCIOptions);
  UCIOptions=NULL;
  UCIOptionCount=0;
}

void UCIOptionParseSetOptionString(char *String)
{
  // Check we are actually parsing an 'option' command
  if (strncmp(String, "setoption ", 10))
    return;
  
  // Extract name and value arguments
  char *Name=strstr(String, "name");
  if (Name==NULL)
    return;
  Name+=5; // Skip 'name '
  char *Value=strstr(String, "value");
  char *NameEnd=String+strlen(String);
  if (Value!=NULL)
  {
    NameEnd=Value-1;
    Value+=6; // Skip 'value '
  }
  *NameEnd='\0';
  
  // Find option with given name
  ucioption_t *Option=UCIOptionFromName(Name);
  if (Option==NULL)
    return;
  
  // Each type of option is handled separately
  int IntValue, I;
  bool BoolValue;
  switch(Option->Type)
  {
    case ucioptiontype_check:
      BoolValue=UCIStringToBool(Value);
      (*Option->Data.Check.Function)(BoolValue, Option->UserData);
    break;
    case ucioptiontype_spin:
      IntValue=atoi(Value);
      if (IntValue<Option->Data.Spin.Min)
        IntValue=Option->Data.Spin.Min;
      if (IntValue>Option->Data.Spin.Max)
        IntValue=Option->Data.Spin.Max;
      (*Option->Data.Spin.Function)(IntValue, Option->UserData);
    break;
    case ucioptiontype_combo:
      for(I=0;I<Option->Data.Combo.OptionCount;++I)
        if (!strcmp(Option->Data.Combo.Options[I], Value))
        {
          (*Option->Data.Combo.Function)(Option->Data.Combo.Options[I], Option->UserData);
          break;
        }
    break;
    case ucioptiontype_button:
      (*Option->Data.Button.Function)(Option->UserData);
    break;
    case ucioptiontype_string:
      (*Option->Data.String.Function)(Value, Option->UserData);
    break;
  }
}

void UCIOptionPrint()
{
  int I, J;
  for(I=0;I<UCIOptionCount;++I)
  {
    ucioption_t *Option=&UCIOptions[I];
    switch(Option->Type)
    {
      case ucioptiontype_check:
        printf("option name %s type check default %s\n", Option->Name,
               UCIBoolToString[Option->Data.Check.Default]);
      break;
      case ucioptiontype_spin:
        printf("option name %s type spin default %i min %i max %i\n",
               Option->Name, Option->Data.Spin.Default, Option->Data.Spin.Min,
               Option->Data.Spin.Max);
      break;
      case ucioptiontype_combo:
        printf("option name %s type combo default %s", Option->Name, Option->Data.Combo.Default);
        for(J=0;J<Option->Data.Combo.OptionCount;++J)
          printf(" var %s", Option->Data.Combo.Options[J]);
        printf("\n");
      break;
      case ucioptiontype_button:
        printf("option name %s type button\n", Option->Name);
      break;
      case ucioptiontype_string:
        printf("option name %s type string default %s\n", Option->Name, Option->Data.String.Default);
      break;
    }
  }
}

ucioption_t *UCIOptionNewBase()
{
  ucioption_t *TempPtr=realloc(UCIOptions, (UCIOptionCount+1)*sizeof(ucioption_t));
  if (TempPtr==NULL)
    return NULL;
  UCIOptions=TempPtr;
  ucioption_t *Return=&UCIOptions[UCIOptionCount];
  ++UCIOptionCount;
  return Return;
}

ucioption_t *UCIOptionFromName(const char *Name)
{
  int I;
  for(I=0;I<UCIOptionCount;++I)
    if (!strcmp(UCIOptions[I].Name, Name))
      return &UCIOptions[I];
  return NULL;
}

bool UCIStringToBool(const char *String)
{
  return !strcmp(String, "true");
}
