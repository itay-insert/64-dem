#include <stdint.h>
#include <stdarg.h>
#include "uint_definitions.h"

#ifndef VGA_H
#define VGA_H

#define Black 0
#define Blue 1
#define Green 2
#define Cyan 3
#define Red 4
#define Purple 5
#define Brown 6
#define LightGray 7
#define DarkGray 8
#define LightBlue 9
#define LightGreen 10
#define LightCyan 11
#define LightRed 12
#define Pink 13
#define Yellow 14
#define White 15


void vga_init(u64 fb_base, int hres, int vres, int p_scan, int p_mode);
void draw_pixel(int x, int y, u32 color);
void draw_16color(int x, int y, int color_index);
void Set_GlobalTextColor(int color_index);
void draw_char(char ch, int coulmn, int row);
void draw_cursor(int color_index);

typedef struct {
	int coulmn;
	int row;
} text_data;

text_data printf(char str[], ...);
void move_cursor(int amt);
void cursor_Setpos(int coulmn, int row);


#endif