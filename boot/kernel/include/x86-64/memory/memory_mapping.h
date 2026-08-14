#ifndef MEMORY_MAPPING_H
#define MEMORY_MAPPING_H

#include "uint_definitions.h"

void SetBitmapBase(u8 *bitmap);
void fill_bitmap(u64 count, u64 PageCount, u8 *bitmap);
int run_simulation(u64 count, u64 pages, u8 *bitmap);

#endif
