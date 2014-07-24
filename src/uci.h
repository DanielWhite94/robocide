#ifndef UCI_H
#define UCI_H

#include <stdbool.h>
#include <stdarg.h>

void UCILoop();
bool UCIOptionNewCheck(const char *Name, void(*Function)(bool Value, void *UserData), void *UserData, bool Default);
bool UCIOptionNewSpin(const char *Name, void(*Function)(int Value, void *UserData), void *UserData, int Min, int Max, int Default);
bool UCIOptionNewCombo(const char *Name, void(*Function)(const char *Value, void *UserData), void *UserData, const char *Default, int OptionCount, ...);
bool UCIOptionNewButton(const char *Name, void(*Function)(void *UserData), void *UserData);
bool UCIOptionNewString(const char *Name, void(*Function)(const char *Value, void *UserData), void *UserData, const char *Default);

#endif
