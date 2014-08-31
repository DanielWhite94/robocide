#ifndef HTABLE_H
#define HTABLE_H

#include <stdbool.h>
#include <stdint.h>

typedef struct HTable HTable;

HTable *htableNew(size_t entrySize, const void *nullEntry, unsigned int sizeMb); // entrySize must be a power of two, sizeMb>0
void htableFree(HTable *table);
bool htableResize(HTable *table, unsigned int sizeMb); // SizeMb>0
void htableResizeInterface(void *table, int sizeMb); // interface for UCI spin option code
void htableClear(HTable *table);
void htableClearInterface(void *table); // interface for UCI button option code
void *htableGrab(HTable *table, uint64_t key); // will never return NULL
void htableRelease(HTable *table, uint64_t key); // should be called after Grab() to release entry

#endif
