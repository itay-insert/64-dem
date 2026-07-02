#include <stdint.h>
#define u8 uint8_t
#define u16 uint16_t
#define u32 uint32_t
#define u64 uint64_t

#define i8 int8_t
#define i16 int16_t
#define i32 int32_t
#define i64 int64_t 

#ifndef VGA_H
#define VGA_H

void vga_init(u64 fb_base, int hres, int vres, int p_scan, int p_mode);
void draw_pixel(int x, int y, u32 color);
void draw_16color(int x, int y, int color_index);
void Set_GlobalTextColor(int color_index);
void draw_char(char ch, int coulmn, int row);
void draw_cursor(int color_index);
void printf(char str[]);


#endif