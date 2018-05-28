#ifndef MAIN_H
#define MAIN_H

#include <stdarg.h>

void mainFatalError(const char *format, ...) __attribute__ ((noreturn));

#endif
