#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "htable.h"
#include "util.h"

struct htable_t
{
  size_t EntrySize;
  uint64_t Mask;
  void *NullEntry;
  void *Entries;
};

////////////////////////////////////////////////////////////////////////////////
// Private prototypes
////////////////////////////////////////////////////////////////////////////////

size_t HTableSize(const htable_t *HTable); // number of entries
void *HTableIndexToEntry(htable_t *Table, uint64_t Index);
void *HTableKeyToEntry(htable_t *Table, uint64_t Key);

////////////////////////////////////////////////////////////////////////////////
// Public functions
////////////////////////////////////////////////////////////////////////////////

htable_t *HTableNew(size_t EntrySize, const void *NullEntry, unsigned int SizeMB)
{
  assert(IsPowTwo64(sizeof(EntrySize)));
  assert(SizeMB>0);
  
  // Allocate memory
  htable_t *Table=malloc(sizeof(htable_t));
  void *NullEntryMem=malloc(EntrySize);
  if (Table==NULL || NullEntryMem==NULL)
  {
    free(Table);
    free(NullEntryMem);
    return NULL;
  }
  
  // Set state
  Table->EntrySize=EntrySize;
  Table->Mask=0;
  Table->NullEntry=NullEntryMem;
  memcpy(Table->NullEntry, NullEntry, EntrySize);
  Table->Entries=NULL;
  
  // Set to desired size
  if (!HTableResize(Table, SizeMB))
  {
    HTableFree(Table);
    return NULL;
  }
  
  return Table;
}

void HTableFree(htable_t *Table)
{
  free(Table->NullEntry);
  free(Table->Entries);
  free(Table);
}

bool HTableResize(htable_t *Table, unsigned int SizeMB)
{
  assert(SizeMB>0);
  
  // Calculate greatest power of two number of entries we can fit in SizeMB
  uint64_t Entries=(((uint64_t)SizeMB)*1024llu*1024llu)/Table->EntrySize;
  Entries=NextPowTwo64(Entries+1)/2;
  
  // Attempt to allocate table
  while(Entries>0)
  {
    void *Ptr=realloc(Table->Entries, Entries*Table->EntrySize);
    if (Ptr!=NULL)
    {
      // Success
      Table->Entries=Ptr;
      Table->Mask=Entries-1;
      HTableClear(Table);
      return true;
    }
    Entries/=2;
  }
  return false;
}

void HTableResizeInterface(int SizeMB, void *Table)
{
  HTableResize(Table, SizeMB);
}

void HTableClear(htable_t *Table)
{
  size_t I, Size=HTableSize(Table);
  for(I=0;I<Size;++I)
    memcpy(HTableIndexToEntry(Table, I), Table->NullEntry, Table->EntrySize);
}

void HTableClearInterface(void *Table)
{
  HTableClear(Table);
}

void *HTableGrab(htable_t *Table, uint64_t Key)
{
  return HTableKeyToEntry(Table, Key);
}

void HTableRelease(htable_t *Table, uint64_t Key)
{
  // No-op (exists in case locks are added later)
}

////////////////////////////////////////////////////////////////////////////////
// Private functions
////////////////////////////////////////////////////////////////////////////////

size_t HTableSize(const htable_t *HTable)
{
  return (HTable->Mask+1);
}

void *HTableIndexToEntry(htable_t *Table, uint64_t Index)
{
  assert(Index<HTableSize(Table));
  return ((void *)(((char *)Table->Entries)+(Index*Table->EntrySize)));
}

void *HTableKeyToEntry(htable_t *Table, uint64_t Key)
{
  return HTableIndexToEntry(Table, Key & Table->Mask);
}
