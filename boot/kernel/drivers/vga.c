#include <stdint.h>
#include <stdarg.h>
#include "uint_definitions.h"
#include "memory.h"
#include "font.h"


#define RGB 0
#define BGR 1
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

#define MaskBlue 0xff
#define MaskGreen 0xff00
#define MaskRed 0xff0000

#define Forwards 0
#define Backwards 1

u32 vga16_color[16] = {
    0x000000, 0x0000aa, 0x00aa00, 0x00aaaa, 
    0xaa0000, 0xaa00aa, 0xaa5500, 0xaaaaaa,
    0x555555, 0x5555ff, 0x55ff55, 0x55ffff,
    0xff5555, 0xff55ff, 0xffff55, 0xffffff
};


u64 fb_addr = 0;
int hor_res = 0;
int ver_res = 0;
int p_scanln = 0;
int p_format = RGB;
int size = 0;

void vga_init(u64 fb_base, int hres, int vres, int p_scan, int p_mode) {
    fb_addr = fb_base;
    size = hres * vres;
    hor_res = hres;
    p_scanln = p_scan;
    ver_res = vres;
	p_format = p_mode;
}

void draw_pixel(int x, int y, u32 color) {
    u32 *vga = (u32 *)fb_addr;
    int addr = (y * p_scanln) + x;
    if (addr <= size) {
		if (p_format == BGR) {
			u32 BgrColor = (color & MaskBlue) << 16;
			BgrColor += color & MaskGreen;
			BgrColor += (color & MaskRed) >> 16; 
			vga[addr] = BgrColor;
		} else if (p_format == RGB) {
			vga[addr] = color;
		}
    }
}

void draw_16color(int x, int y, int color_index) {
    u32 *vga = (u32 *)fb_addr;
    int addr = (y * p_scanln) + x;
    u32 color = vga16_color[color_index];
    if (addr <= size) {
		if (p_format == BGR) {
			u32 BgrColor = (color & MaskBlue) << 16;
			BgrColor += color & MaskGreen;
			BgrColor += (color & MaskRed) >> 16;
			vga[addr] = BgrColor; 
		} else if (p_format == RGB) {
			vga[addr] = color;
		}
    }
}

int global_textcolor = LightGray;

void Set_GlobalTextColor(int color_index) {
    global_textcolor = color_index;
}


void draw_char(char ch, int coulmn, int row) {
    coulmn = coulmn << 3;
    row = row << 4;
    int font_index = (int)ch << 4;
    for (int i = 0; i < 16; i++) {
        u8 byte = font_data[font_index+i];
        for (u8 j = 0; j < 8; j++) {
            u8 chk = check_byte(byte, j, 1);
            if (chk == 1) {
                draw_16color(coulmn, row, global_textcolor);
            } else {
				draw_16color(coulmn, row, Black);
			}
            coulmn++;
        }
        coulmn -= 8;
        row++;
    }
}

struct Cursor {
    int coulmn;
    int row;
};

struct Cursor cursor = {0, 0};


void draw_cursor(int color_index) {
    int x = cursor.coulmn << 3;
    int y = cursor.row << 4;

    for (int i = 14; i < 16; i++) {
        for (int j = 0; j < 8; j++) {
            draw_16color(x + j, y + i, color_index);
        }
    }
}

void scroll(void)
{
    u32 *fb = (u32 *)fb_addr;

    int bytes_per_pixel = 4;
    int line_size = p_scanln * bytes_per_pixel;

    int scroll_pixels = 16; // one character row

    // Move everything up by 16 pixels
    for (int y = 0; y < ver_res - scroll_pixels; y++) {
        u32 *dst = (u32 *)((u64)fb + y * line_size);
        u32 *src = (u32 *)((u64)fb + (y + scroll_pixels) * line_size);

        for (int x = 0; x < p_scanln; x++) {
            dst[x] = src[x];
        }
    }

    // Clear the bottom 16 pixels
    for (int y = ver_res - scroll_pixels; y < ver_res; y++) {
        for (int x = 0; x < p_scanln; x++) {
            draw_pixel(x, y, vga16_color[Black]);
        }
    }
}

static inline void cursor_inc(void) {
	cursor.coulmn++;
	if (cursor.coulmn >= (p_scanln >> 3)) {
        cursor.coulmn = 0;
		cursor.row++;
    }
	if (cursor.row >= (ver_res >> 4)) {
        scroll();
        cursor.row = (ver_res >> 4) - 1;
    }
}

void print_unsigned(unsigned int x)
{
    char buffer[10];   // enough for a 32-bit unsigned int
    int i = 0;

    do {
        buffer[i++] = '0' + (x % 10);
        x /= 10;
    } while (x != 0);

    while (i > 0) {
        draw_char(buffer[--i], cursor.coulmn, cursor.row);
		cursor_inc();
	}
}

void print_unsigned_long(unsigned long x)
{
    char buffer[20];
    int i = 0;

    do {
        buffer[i++] = '0' + (x % 10);
        x /= 10;
    } while (x != 0);

    while (i > 0) {
        draw_char(buffer[--i], cursor.coulmn, cursor.row);
		cursor_inc();
	}
}

void print_int(int x)
{
    if (x < 0) {
        draw_char('-', cursor.coulmn, cursor.row);
		cursor_inc();

        // Handle the most negative int safely
        unsigned int u = (unsigned int)(-(x + 1)) + 1;

        print_unsigned(u);
    } else {
        print_unsigned((unsigned int)x);
    }
}

void print_long(long x)
{ 
    if (x < 0) {
        draw_char('-', cursor.coulmn, cursor.row);
		cursor_inc();

        // Handle LONG_MIN safely
        unsigned long u = (unsigned long)(-(x + 1)) + 1;

        print_unsigned_long(u);
    } else {
        print_unsigned_long((unsigned long)x);
    }
}

void print_byte_hex(u8 b)
{
    char hex[] = "0123456789ABCDEF";

    char high = hex[(b >> 4) & 0xF];
    char low  = hex[b & 0xF];

    draw_char(high, cursor.coulmn, cursor.row);
	cursor_inc();

    draw_char(low, cursor.coulmn, cursor.row);
	cursor_inc();
}

void print_word_hex(u16 w)
{
    print_byte_hex((u8)(w >> 8));
    print_byte_hex((u8)(w & 0xFF));
}

void print_dword_hex(u32 d)
{
    print_word_hex((u16)(d >> 16));
    print_word_hex((u16)(d & 0xFFFF));
}

void print_qword_hex(u64 q)
{
    print_dword_hex((u32)(q >> 32));
    print_dword_hex((u32)(q & 0xFFFFFFFFULL));
}


typedef struct {
	int coulmn;
	int row;
} text_data;

text_data printf(char str[], ...) {
	va_list args;
	va_start (args, str);
	
	text_data td;

	td.coulmn = cursor.coulmn;
	td.row = cursor.row;

    char *p_str = str;
    while (*p_str != '\0') {
        if (*p_str == '\n') {
            cursor.coulmn = 0;
            cursor.row++;
			if (cursor.row >= (ver_res >> 4)) {
        		scroll();
        		cursor.row = (ver_res >> 4) - 1;
    		}
        } else if (*p_str == '%') {
			p_str++;
			switch (*p_str) {

				case 'd':
					print_int(va_arg(args, int));
					break;

				case 'u':
					if (p_str[1] == 'x') {
						print_dword_hex(va_arg(args, unsigned int));
						p_str++;
					} else {
						print_unsigned(va_arg(args, unsigned int));
					}
					break;
				
				case 'l':
					p_str++;
					if (*p_str == 'd') {
						print_long(va_arg(args, long));
					} else if (*p_str == 'u') {
						print_unsigned_long(va_arg(args, unsigned long));
					} else if (*p_str == 'x') {
						print_qword_hex(va_arg(args, u64));
					} else {
						draw_char('%', cursor.coulmn, cursor.row);
						cursor_inc();
						draw_char('l', cursor.coulmn, cursor.row);
						cursor_inc();
						p_str--;
					}
					break;
				
				case 'b':
					print_byte_hex((u8)va_arg(args, int));
					break;
				
				case 'w':
					print_byte_hex((u16)va_arg(args, int));
					break;
				
				default:
					p_str--;
					draw_char((char)*p_str, cursor.coulmn, cursor.row);
					cursor_inc();
					break;
			}
		}
		else {
            draw_char((char)*p_str, cursor.coulmn, cursor.row);
			cursor_inc();
        }
		p_str++;
    }

	va_end(args);
	return td;
}

void move_cursor(int amt) {
	int cols = p_scanln >> 3;

	int pos = cursor.row * cols + cursor.coulmn;

	pos += amt;

	if (pos < 0)
        pos = 0;

    cursor.row = pos / cols;
    cursor.coulmn = pos % cols;
}

void cursor_Setpos(int coulmn, int row) {
	int cols = p_scanln >> 3;

	int pos = row * cols + coulmn;
	
	if (pos < 0)
		pos = 0;
	
	cursor.coulmn = pos % cols;
	cursor.row = pos / cols;
}