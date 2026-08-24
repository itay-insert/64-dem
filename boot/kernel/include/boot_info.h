#ifndef BOOT_INFO_H
#define BOOT_INFO_H

#include <stddef.h>
#include "uint_definitions.h"
#include "c_compat.h"

/* Typed views of the buffers constructed by bootloader.c. */
typedef struct __attribute__((packed, aligned(8))) {
    u64 kernel_entry;
    u64 kernel_start;
    u64 kernel_end;
    u64 framebuffer_base;
    u64 memory_map_size;
    u64 descriptor_size;
    u64 bitmap_size;
    u64 rsdp;
    u64 device_path;
} BOOT_INFO64;

typedef struct __attribute__((packed, aligned(4))) {
    i32 pixel_mode;
    i32 horizontal_resolution;
    i32 vertical_resolution;
    i32 pixels_per_scanline;
    i32 info_size;
    i32 device_path_size;
} BOOT_INFO32;

KERNEL_STATIC_ASSERT(sizeof(BOOT_INFO64) == 72, "BOOT_INFO64 size");
KERNEL_STATIC_ASSERT(KERNEL_ALIGNOF(BOOT_INFO64) == 8, "BOOT_INFO64 alignment");
KERNEL_STATIC_ASSERT(offsetof(BOOT_INFO64, kernel_entry) == 0, "kernel_entry offset");
KERNEL_STATIC_ASSERT(offsetof(BOOT_INFO64, kernel_start) == 8, "kernel_start offset");
KERNEL_STATIC_ASSERT(offsetof(BOOT_INFO64, kernel_end) == 16, "kernel_end offset");
KERNEL_STATIC_ASSERT(offsetof(BOOT_INFO64, framebuffer_base) == 24, "framebuffer_base offset");
KERNEL_STATIC_ASSERT(offsetof(BOOT_INFO64, memory_map_size) == 32, "memory_map_size offset");
KERNEL_STATIC_ASSERT(offsetof(BOOT_INFO64, descriptor_size) == 40, "descriptor_size offset");
KERNEL_STATIC_ASSERT(offsetof(BOOT_INFO64, bitmap_size) == 48, "bitmap_size offset");
KERNEL_STATIC_ASSERT(offsetof(BOOT_INFO64, rsdp) == 56, "rsdp offset");
KERNEL_STATIC_ASSERT(offsetof(BOOT_INFO64, device_path) == 64, "device_path offset");

KERNEL_STATIC_ASSERT(sizeof(BOOT_INFO32) == 24, "BOOT_INFO32 size");
KERNEL_STATIC_ASSERT(KERNEL_ALIGNOF(BOOT_INFO32) == 4, "BOOT_INFO32 alignment");
KERNEL_STATIC_ASSERT(offsetof(BOOT_INFO32, pixel_mode) == 0, "pixel_mode offset");
KERNEL_STATIC_ASSERT(offsetof(BOOT_INFO32, horizontal_resolution) == 4, "horizontal_resolution offset");
KERNEL_STATIC_ASSERT(offsetof(BOOT_INFO32, vertical_resolution) == 8, "vertical_resolution offset");
KERNEL_STATIC_ASSERT(offsetof(BOOT_INFO32, pixels_per_scanline) == 12, "pixels_per_scanline offset");
KERNEL_STATIC_ASSERT(offsetof(BOOT_INFO32, info_size) == 16, "info_size offset");
KERNEL_STATIC_ASSERT(offsetof(BOOT_INFO32, device_path_size) == 20, "device_path_size offset");

#endif
