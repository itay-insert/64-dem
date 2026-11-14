#ifndef SYS_H
#define SYS_H
#include "uint_definitions.h"
#include <stdbool.h>

void kprintf(const char *str);
u8 read_key(bool ascii, bool wait);
void *malloc(u64 Size);
void free(void *alloc);
void *realloc(void *alloc, u64 old_size, u64 new_size);
void sleep_ms(u64 ms);

#endif