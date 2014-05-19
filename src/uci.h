#ifndef UCI_H
#define UCI_H

#include <stdbool.h>
#include <stdarg.h>

void UCILoop();
bool UCIOptionNewCheck(const char *Name, void(*Function)(bool Value), bool Default);
bool UCIOptionNewSpin(const char *Name, void(*Function)(int Value), int Min, int Max, int Default);
bool UCIOptionNewCombo(const char *Name, void(*Function)(const char *Value), const char *Default, int OptionCount, ...);
bool UCIOptionNewButton(const char *Name, void(*Function)(void));
bool UCIOptionNewString(const char *Name, void(*Function)(const char *Value), const char *Default);

#endif
