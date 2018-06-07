#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "htable.h"
#include "thread.h"
#include "util.h"

struct HTable {
	size_t entrySize;
	size_t entryCount;
	void *entries;
	unsigned lockCountMask;
	SpinLock *locks;
};

////////////////////////////////////////////////////////////////////////////////
// Private prototypes
////////////////////////////////////////////////////////////////////////////////

size_t htableGetEntryCount(const HTable *table);

void *htableIndexToEntry(HTable *table, uint64_t index);
uint64_t htableKeyToIndex(const HTable *table, HTableKey key);

////////////////////////////////////////////////////////////////////////////////
// Public functions
////////////////////////////////////////////////////////////////////////////////

HTable *htableNew(size_t entrySize, unsigned int sizeMb, unsigned lockCountShift) {
	// Sanity checks.
	assert(sizeMb>0);

	unsigned lockCount=(1u<<lockCountShift);

	// Allocate memory.
	HTable *table=malloc(sizeof(HTable));
	SpinLock *locks=malloc(lockCount*sizeof(SpinLock));
	if (table==NULL || locks==NULL) {
		free(table);
		free(locks);
		return NULL;
	}

	// Set state.
	table->entrySize=entrySize;
	table->entryCount=0;
	table->entries=NULL;
	table->lockCountMask=lockCount-1;
	table->locks=locks;

	// Create locks
	int i;
	for(i=0; i<lockCount; ++i)
		spinLockInit(&table->locks[i], false);

	// Set to desired size.
	if (!htableResize(table, sizeMb)) {
		htableFree(table);
		return NULL;
	}

	return table;
}

void htableFree(HTable *table) {
	free(table->locks);
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
	memset(table->entries, 0, table->entryCount*table->entrySize);
}

void htableClearInterface(void *table) {
	htableClear(table);
}

void *htableGrab(HTable *table, HTableKey key) {
	uint64_t index=htableKeyToIndex(table, key);
	spinLockWait(&table->locks[index&table->lockCountMask]);
	return htableIndexToEntry(table, index);
}

void htableRelease(HTable *table, HTableKey key) {
	uint64_t index=htableKeyToIndex(table, key);
	spinLockPost(&table->locks[index&table->lockCountMask]);
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

uint64_t htableKeyToIndex(const HTable *table, HTableKey key) {
	uint64_t index=(((uint64_t)key)*table->entryCount)>>32;
	assert(index<htableGetEntryCount(table));
	return index;
}
