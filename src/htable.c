#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "htable.h"
#include "util.h"

struct HTable {
	size_t entrySize;
	uint64_t mask;
	void *nullEntry;
	void *entries;
};

////////////////////////////////////////////////////////////////////////////////
// Private prototypes
////////////////////////////////////////////////////////////////////////////////

size_t htableSize(const HTable *table); // number of entries.

void *htableIndexToEntry(HTable *table, uint64_t index);
void *htableKeyToEntry(HTable *table, uint64_t key);

////////////////////////////////////////////////////////////////////////////////
// Public functions
////////////////////////////////////////////////////////////////////////////////

HTable *htableNew(size_t entrySize, const void *nullEntry, unsigned int sizeMb) {
	// Sanity checks.
	assert(utilIsPowTwo64(sizeof(entrySize)));
	assert(sizeMb>0);

	// Allocate memory.
	HTable *table=malloc(sizeof(HTable));
	void *nullEntryMem=malloc(entrySize);
	if (table==NULL || nullEntryMem==NULL) {
		free(table);
		free(nullEntryMem);
		return NULL;
	}

	// Set state.
	table->entrySize=entrySize;
	table->mask=0;
	table->nullEntry=nullEntryMem;
	memcpy(table->nullEntry, nullEntry, entrySize);
	table->entries=NULL;

	// Set to desired size.
	if (!htableResize(table, sizeMb)) {
		htableFree(table);
		return NULL;
	}

	return table;
}

void htableFree(HTable *table) {
	free(table->nullEntry);
	free(table->entries);
	free(table);
}

bool htableResize(HTable *table, unsigned int sizeMb) {
	// Sanity checks.
	assert(sizeMb>0);

	// Calculate greatest power of two number of entries we can fit in sizeMb.
	uint64_t entries=(((uint64_t)sizeMb)*1024llu*1024llu)/table->entrySize;
	entries=utilNextPowTwo64(entries+1)/2;

	// Attempt to allocate table.
	while(entries>0) {
		void *ptr=realloc(table->entries, entries*table->entrySize);
		if (ptr!=NULL) {
			// Success.
			table->entries=ptr;
			table->mask=entries-1;
			htableClear(table);
			return true;
		}
		entries/=2;
	}
	return false;
}

void htableResizeInterface(void *table, int sizeMb) {
	htableResize(table, sizeMb);
}

void htableClear(HTable *table) {
	size_t i, size=htableSize(table);
	for(i=0;i<size;++i)
		memcpy(htableIndexToEntry(table, i), table->nullEntry, table->entrySize);
}

void htableClearInterface(void *table) {
	htableClear(table);
}

void *htableGrab(HTable *table, uint64_t key) {
	return htableKeyToEntry(table, key);
}

void htableRelease(HTable *table, uint64_t key) {
	// No-op (exists in case locks are added later)
}

////////////////////////////////////////////////////////////////////////////////
// Private functions
////////////////////////////////////////////////////////////////////////////////

size_t htableSize(const HTable *table) {
	return (table->mask+1);
}

void *htableIndexToEntry(HTable *table, uint64_t index) {
	assert(index<htableSize(table));
	return ((void *)(((char *)table->entries)+(index*table->entrySize)));
}

void *htableKeyToEntry(HTable *table, uint64_t key) {
	return htableIndexToEntry(table, key & table->mask);
}
