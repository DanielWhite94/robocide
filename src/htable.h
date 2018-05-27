#ifndef HTABLE_H
#define HTABLE_H

#include <stdbool.h>
#include <stdint.h>

#define HTableKeySize 32 // Only lower 32 bits are used, limiting maximum number of entries
#define HTableMaxEntryCount ((1llu)<<(HTableKeySize))
typedef uint32_t HTableKey;

typedef struct HTable HTable;

HTable *htableNew(size_t entrySize, const void *nullEntry, unsigned int sizeMb);
void htableFree(HTable *table);

bool htableResize(HTable *table, unsigned int sizeMb); // SizeMb>0.
void htableResizeInterface(void *table, long long int sizeMb); // Interface for UCI spin option code.

void htableClear(HTable *table);
void htableClearInterface(void *table); // Interface for UCI button option code.

void *htableGrab(HTable *table, HTableKey key); // Will never return NULL.
void htableRelease(HTable *table, HTableKey key); // Should be called after Grab() to release entry.

#endif
