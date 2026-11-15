#ifndef MEMORY_HELPERS_H
#define MEMORY_HELPERS_H

#include <stddef.h>
#include "uint_definitions.h"
#include "x86-64/efi_memory_types.h"
#include "c_compat.h"

KERNEL_EXTERN_C_BEGIN

u64 calculate_pages(EFI_MEMORY_DESCRIPTOR *memory_map, u64 memory_map_size, u64 DescriptorSize);
int memcmp(const void *ptr1, const void *ptr2, size_t count);
void *memset(void *ptr, int value, size_t num);
void *memcpy(void *dest, const void *src, size_t n);
char *strcpy(char *dest, const char *src);
// Convert a NUL-terminated ASCII string to UTF-16LE without a BOM.
// dst_capacity is in bytes; allow 2 * (source length + 1). Buffers must not overlap.
// Returns 1 on success, 0 for invalid arguments, non-ASCII input, or truncation.
// When dst has at least two bytes, output is terminated even on failure.
int ascii_to_utf16le(const char *src, void *dst, size_t dst_capacity);
int strcmp(char *a, char *b);
u8 check_byte(u8 byte, u8 bit, u8 dir);
u8 set_bit(u8 byte, u8 bit, u8 value, u8 dir);

KERNEL_EXTERN_C_END

#endif
