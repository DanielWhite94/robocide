#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "htable.h"
#include "util.h"

struct HTable {
	size_t entrySize;
	size_t entryCount;
	void *nullEntry;
	void *entries;
};

////////////////////////////////////////////////////////////////////////////////
// Private prototypes
////////////////////////////////////////////////////////////////////////////////

size_t htableGetEntryCount(const HTable *table);

void *htableIndexToEntry(HTable *table, uint64_t index);
void *htableKeyToEntry(HTable *table, HTableKey key);

////////////////////////////////////////////////////////////////////////////////
// Public functions
////////////////////////////////////////////////////////////////////////////////

HTable *htableNew(size_t entrySize, const void *nullEntry, unsigned int sizeMb) {
	// Sanity checks.
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
	table->entryCount=0;
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
	assert(table!=NULL);
	assert(sizeMb>0);

	// Calculate greatest number of entries we can fit in sizeMb (limited by 32 bit key).
	uint64_t entryCount=(((uint64_t)sizeMb)*1024llu*1024llu)/table->entrySize;
	if (entryCount>HTableMaxEntryCount)
		entryCount=HTableMaxEntryCount;

	// Attempt to allocate table.
	while(entryCount>0) {
		// Attempt to resize.
		void *ptr=realloc(table->entries, entryCount*table->entrySize);
		if (ptr!=NULL) {
			// Success - update and clear as indexing scheme will be different.
			table->entries=ptr;
			table->entryCount=entryCount;
			htableClear(table);

			return true;
		}

		// Try again with half as many entries.
		entryCount/=2;
	}

	return false;
}

void htableResizeInterface(void *table, long long int sizeMb) {
	htableResize(table, sizeMb);
}

void htableClear(HTable *table) {
	size_t i, entryCount=htableGetEntryCount(table);
	for(i=0;i<entryCount;++i)
		memcpy(htableIndexToEntry(table, i), table->nullEntry, table->entrySize);
}

void htableClearInterface(void *table) {
	htableClear(table);
}

void *htableGrab(HTable *table, HTableKey key) {
	return htableKeyToEntry(table, key);
}

void htableRelease(HTable *table, HTableKey key) {
	// No-op (exists in case locks are added later)
}

////////////////////////////////////////////////////////////////////////////////
// Private functions
////////////////////////////////////////////////////////////////////////////////

size_t htableGetEntryCount(const HTable *table) {
	return table->entryCount;
}

void *htableIndexToEntry(HTable *table, uint64_t index) {
	assert(index<htableGetEntryCount(table));
	return ((void *)(((char *)table->entries)+(index*table->entrySize)));
}

void *htableKeyToEntry(HTable *table, HTableKey key) {
	uint64_t index=(((uint64_t)key)*table->entryCount)>>32;
	assert(index<htableGetEntryCount(table));

	return htableIndexToEntry(table, index);
}
