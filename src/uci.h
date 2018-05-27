#ifndef UCI_H
#define UCI_H

#include <stdarg.h>
#include <stdbool.h>

void uciLoop(void);

bool uciWrite(const char *format, ...);

bool uciOptionNewCheck(const char *name, void(*function)(void *userData, bool value), void *userData, bool initial);
bool uciOptionNewSpin(const char *name, void(*function)(void *userData, long long int value), void *userData, long long int initial, long long int min, long long int max);
bool uciOptionNewCombo(const char *name, void(*function)(void *userData, const char *value), void *userData, const char *initial, size_t optionCount, ...);
bool uciOptionNewButton(const char *name, void(*function)(void *userData), void *userData);
bool uciOptionNewString(const char *name, void(*function)(void *userData, const char *value), void *userData, const char *initial);

#endif
