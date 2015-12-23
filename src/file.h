#ifndef FILE_H
#define FILE_H

#include <stdbool.h>

// Files are guaranteed to be consecutive starting from 0.
typedef enum { FileA, FileB, FileC, FileD, FileE, FileF, FileG, FileH, FileNB } File;

bool fileIsValid(File file);

char fileToChar(File file);
File fileFromChar(char c);

File fileMirror(File file);

#endif
