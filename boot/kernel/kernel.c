#include <stdint.h>
#include "vga.h"

#define u8 uint8_t
#define u16 uint16_t
#define u32 uint32_t
#define u64 uint64_t

#define i8 int8_t
#define i16 int16_t
#define i32 int32_t
#define i64 int64_t 


void main(u64 fb_base, int hres, int vres, int p_scan) {
    vga_init(fb_base, hres, vres, p_scan);
    printf("Hello from kernel :)\n");
    while (1) { 
        draw_cursor(7);
    }
}