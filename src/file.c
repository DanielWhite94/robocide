#include <assert.h>

#include "file.h"

bool fileIsValid(File file) {
	return (file>=FileA && file<=FileH);
}

char fileToChar(File file) {
	assert(fileIsValid(file));
	return file+'a';
}

File fileFromChar(char c) {
	assert(c>=fileToChar(FileA) && c<=fileToChar(FileH));
	return c-'a';
}

File fileMirror(File file) {
	assert(fileIsValid(file));
	return file^7;
}
