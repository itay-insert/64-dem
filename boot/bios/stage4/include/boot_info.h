#ifndef BOOT_INFO_H
#define BOOT_INFO_H

#include <stddef.h>
#include "uint_definitions.h"

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


#endif