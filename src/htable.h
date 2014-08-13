#ifndef HTABLE_H
#define HTABLE_H

#include <stdbool.h>
#include <stdint.h>

typedef struct htable_t htable_t;

htable_t *HTableNew(size_t EntrySize, const void *NullEntry, unsigned int SizeMB); // EntrySize must be a power of two, SizeMB>0
void HTableFree(htable_t *Table);
bool HTableResize(htable_t *Table, unsigned int SizeMB); // SizeMB>0
void HTableResizeInterface(int SizeMB, void *Table); // interface for UCI spin option code
void HTableClear(htable_t *Table);
void HTableClearInterface(void *Table); // interface for UCI button option code
void *HTableGrab(htable_t *Table, uint64_t Key); // will never return NULL
void HTableRelease(htable_t *Table, uint64_t Key); // should be called after Grab() to release entry

#endif
