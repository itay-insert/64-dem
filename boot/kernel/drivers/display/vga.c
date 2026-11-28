#include <stdint.h>
#include <stdarg.h>
#include <stdbool.h>
#include "uint_definitions.h"
#include "x86-64/memory/memory.h"
#include "drivers/display/font.h"
#include "x86-64/ports.h"
#include "x86-64/memory/kmalloc.h"


#ifndef VGA_MIRROR_TO_QEMU_DEBUGCON
#define VGA_MIRROR_TO_QEMU_DEBUGCON 1
#endif

#define QEMU_DEBUGCON_PORT 0xE9

bool memory_console = false;

void qemu_debug_putc(char ch) {
#if VGA_MIRROR_TO_QEMU_DEBUGCON
    outb(QEMU_DEBUGCON_PORT, (u8)ch);
#else
    (void)ch;
#endif
}

void qemu_debug_print(const char *str) {
    while (*str != '\0') {
        qemu_debug_putc(*str++);
    }
}

void qemu_debug_hex_u64(u64 value) {
    static const char digits[] = "0123456789abcdef";
    qemu_debug_print("0x");
    for (int shift = 60; shift >= 0; shift -= 4) {
        qemu_debug_putc(digits[(value >> shift) & 0x0f]);
    }
}


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


#define STDOUT 0
#define FB 1

extern void print_kernel_memory_hardware(bool cons);

u32 vga16_color[16] = {
    0x000000, 0x0000aa, 0x00aa00, 0x00aaaa, 
    0xaa0000, 0xaa00aa, 0xaa5500, 0xaaaaaa,
    0x555555, 0x5555ff, 0x55ff55, 0x55ffff,
    0xff5555, 0xff55ff, 0xffff55, 0xffffff
};

static u64 front_buffer_addr = 0;
u64 fb_addr = 0;

bool *diff = NULL;
u64 diff_size = 0;

int hor_res = 0;
int ver_res = 0;
int coulmns = 0;
int rows = 0;

int p_scanln = 0;
int p_format = RGB;
int size = 0;

int mode = STDOUT;

char memcons[0xffff] = {0};
int memcons_index = 0;

static const u64 memcons_size = sizeof memcons;

void vga_init(u64 fb_base, int hres, int vres, int p_scan, int p_mode) {
    front_buffer_addr = fb_base;
    size = p_scan * vres;
    fb_addr = (u64)kmalloc(size<<2);

    mode = STDOUT;

    coulmns = (hres + 7) >> 3;
    rows = (vres + 15) >> 4;
    diff = (bool *)kmalloc(coulmns*rows);
    diff_size = (u64)coulmns*rows;

    if (fb_addr == 0 || diff == NULL) {
        qemu_debug_print("[VGA] buffer allocation failed\n");
        for (;;) __asm__ volatile("hlt");
    }

    u32 *back = (u32 *)fb_addr;
    for (int i = 0; i < size; i++)
        back[i] = 0;

    for (int i = 0; i < (coulmns*rows); i++) 
        diff[i] = false;
    
    hor_res = hres;
    p_scanln = p_scan;
    ver_res = vres;
	p_format = p_mode;
}


static inline void update_diff(int x, int y) {
    if (x < 0 || x >= hor_res || y < 0 || y >= ver_res)
        return;
    x = x >> 3; // : 8
    y = y >> 4;
    int place = y * coulmns + x;
    diff[place] = true;
} 

static void present(void) {
    u32 *real = (u32 *)front_buffer_addr;
    u32 *back = (u32 *)fb_addr;
    for (u64 i = 0; i < diff_size; i++) {
        if (diff[i]) {
            diff[i] = false;
            int y = (int)(i / coulmns);
            int x = (int)(i % coulmns);
            x = x << 3;
            y = y << 4;
            for (int i = y; i < (y+16) && i < ver_res; i++) {
                for (int j = x; j < (x+8) && j < hor_res; j++) {
                    int place = (i * p_scanln) + j;
                    real[place] = back[place];
                }
            }
        }
    }
}

void draw_pixel(int x, int y, u32 color) {
    if (x < 0 || x >= hor_res || y < 0 || y >= ver_res)
        return;
    u32 *vga = (u32 *)fb_addr;
    int addr = (y * p_scanln) + x;
    if (addr < size) {
		if (p_format == BGR) {
			u32 BgrColor = (color & MaskBlue) << 16;
			BgrColor += color & MaskGreen;
			BgrColor += (color & MaskRed) >> 16; 
            if (vga[addr] != BgrColor) {
			    vga[addr] = BgrColor;
                update_diff(x, y);
            }
		} else if (p_format == RGB) {
            if (vga[addr] != color) {
			    vga[addr] = color;
                update_diff(x, y);
            }
		}
    }
}

void draw_16color(int x, int y, int color_index) {
    if (x < 0 || x >= hor_res || y < 0 || y >= ver_res)
        return;
    u32 *vga = (u32 *)fb_addr;
    int addr = (y * p_scanln) + x;
    u32 color = vga16_color[color_index];
    if (addr < size) {
		if (p_format == BGR) {
			u32 BgrColor = (color & MaskBlue) << 16;
			BgrColor += color & MaskGreen;
			BgrColor += (color & MaskRed) >> 16;
            if (vga[addr] != BgrColor) {
			    vga[addr] = BgrColor;
                update_diff(x, y);
            } 
		} else if (p_format == RGB) {
            if (vga[addr] != color) {
                vga[addr] = color;
                update_diff(x, y);
            }
		}
    }
}


void draw_16color_phys(int x, int y, int color_index) {
    if (x < 0 || x >= hor_res || y < 0 || y >= ver_res)
        return;
    u32 *vga = (u32 *)front_buffer_addr;
    int addr = (y * p_scanln) + x;
    u32 color = vga16_color[color_index];
    if (addr < size) {
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
int background_color = Black;

void Set_GlobalTextColor(int color_index) {
    global_textcolor = color_index;
}


void draw_char(char ch, int coulmn, int row) {
    qemu_debug_putc(ch);
    if (memory_console) {
        memcons[memcons_index] = ch;
        memcons_index++;
        return;
    }

    coulmn = coulmn << 3;
    row = row << 4;
    int font_index = (unsigned char)ch * 16;
    for (int i = 0; i < 16; i++) {
        u8 byte = font_data[font_index+i];
        for (u8 j = 0; j < 8; j++) {
            u8 chk = check_byte(byte, j, 1);
            if (chk == 1) {
                draw_16color(coulmn, row, global_textcolor);
            } else {
				draw_16color(coulmn, row, background_color);
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


static struct Cursor drawn_cursor;
static bool cursor_visible;

void hide_cursor(void) {
    if (!cursor_visible)
        return;
    int x = drawn_cursor.coulmn * 8;
    int y = drawn_cursor.row * 16;
    u32 *front = (u32 *)front_buffer_addr;
    const u32 *back = (const u32 *)fb_addr;
    for (int row = y + 14; row < y + 16 && row < ver_res; ++row)
        for (int col = x; col < x + 8 && col < hor_res; ++col)
            front[row * p_scanln + col] = back[row * p_scanln + col];
    cursor_visible = false;
}

void draw_cursor(int color_index) {
    hide_cursor();
    int x = cursor.coulmn * 8;
    int y = cursor.row * 16;
    if (x < 0 || y < 0 || x >= hor_res || y >= ver_res)
        return;
    drawn_cursor = cursor;
    cursor_visible = true;

    for (int i = 14; i < 16; i++) {
        for (int j = 0; j < 8; j++) {
            draw_16color_phys(x + j, y + i, color_index);
        }
    }
}


typedef struct {
	int coulmn;
	int row;
} text_data;

text_data scroll(text_data td)
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
            update_diff(x, y);
        }
    }

    // Clear the bottom 16 pixels
    for (int y = ver_res - scroll_pixels; y < ver_res; y++) {
        for (int x = 0; x < p_scanln; x++) {
            draw_pixel(x, y, vga16_color[Black]);
            update_diff(x, y);
        }
    }

    present();
    td.row--;
    return td;
}


static inline text_data cursor_inc(text_data td) {
	cursor.coulmn++;
	if (cursor.coulmn >= (p_scanln >> 3)) {
        cursor.coulmn = 0;
		cursor.row++;
    }
	if (cursor.row >= (ver_res >> 4)) {
        td = scroll(td);
        cursor.row = (ver_res >> 4) - 1;
    }

    return td;
}


static inline void cursor_dec(void) {
	cursor.coulmn--;
	if (cursor.coulmn < 0) {
        cursor.coulmn = (p_scanln >> 3);
		cursor.row--;
    }

}


text_data print_unsigned(unsigned int x, text_data td)
{
    char buffer[10];   // enough for a 32-bit unsigned int
    int i = 0;

    do {
        buffer[i++] = '0' + (x % 10);
        x /= 10;
    } while (x != 0);

    while (i > 0) {
        draw_char(buffer[--i], cursor.coulmn, cursor.row);
		td = cursor_inc(td);
	}

    return td;
}

text_data print_unsigned_long(unsigned long x, text_data td)
{
    char buffer[20];
    int i = 0;

    do {
        buffer[i++] = '0' + (x % 10);
        x /= 10;
    } while (x != 0);

    while (i > 0) {
        draw_char(buffer[--i], cursor.coulmn, cursor.row);
		td = cursor_inc(td);
	}

    return td;
}

text_data print_int(int x, text_data td)
{
    if (x < 0) {
        draw_char('-', cursor.coulmn, cursor.row);
		td = cursor_inc(td);

        // Handle the most negative int safely
        unsigned int u = (unsigned int)(-(x + 1)) + 1;

        td = print_unsigned(u, td);
    } else {
        td = print_unsigned((unsigned int)x, td);
    }

    return td;
}

text_data print_long(long x, text_data td)
{ 
    if (x < 0) {
        draw_char('-', cursor.coulmn, cursor.row);
		td = cursor_inc(td);

        // Handle LONG_MIN safely
        unsigned long u = (unsigned long)(-(x + 1)) + 1;

        td = print_unsigned_long(u, td);
    } else {
        td = print_unsigned_long((unsigned long)x, td);
    }

    return td;
}

text_data print_byte_hex(u8 b, text_data td)
{
    char hex[] = "0123456789ABCDEF";

    char high = hex[(b >> 4) & 0xF];
    char low  = hex[b & 0xF];

    draw_char(high, cursor.coulmn, cursor.row);
	td = cursor_inc(td);

    draw_char(low, cursor.coulmn, cursor.row);
	td = cursor_inc(td);
    return td;
}

text_data print_word_hex(u16 w, text_data td)
{
    td = print_byte_hex((u8)(w >> 8), td);
    td = print_byte_hex((u8)(w & 0xFF), td);
    return td;
}

text_data print_dword_hex(u32 d, text_data td)
{
    td = print_word_hex((u16)(d >> 16), td);
    td = print_word_hex((u16)(d & 0xFFFF), td);
    return td;
}

text_data print_qword_hex(u64 q, text_data td)
{
    td = print_dword_hex((u32)(q >> 32), td);
    td = print_dword_hex((u32)(q & 0xFFFFFFFFULL), td);
    return td;
}

static int clamp_coordinate(int value, int limit) {
    if (value < 0 || limit <= 0)
        return 0;
    return value >= limit ? limit - 1 : value;
}


void cursor_Setpos(int coulmn, int row) {
    cursor.coulmn = clamp_coordinate(coulmn, coulmns);
    cursor.row = clamp_coordinate(row, rows);
}

text_data printf(const char *str, ...) {
	va_list args;
	va_start (args, str);
	
	text_data td;

	td.coulmn = cursor.coulmn;
	td.row = cursor.row;

    const char *p_str = str;
    while (*p_str != '\0') {
        if (*p_str == '\n') {
            qemu_debug_putc('\n');
            if (memory_console) {
                memcons[memcons_index] = '\n';
                memcons_index++;
            }
            cursor.coulmn = 0;
            cursor.row++;
			if (cursor.row >= (ver_res >> 4)) {
        		td = scroll(td);
        		cursor.row = (ver_res >> 4) - 1;
    		}
        } else if (*p_str == '%') {
			p_str++;
			switch (*p_str) {

				case 'd':
					td = print_int(va_arg(args, int), td);
					break;

				case 'u':
					if (p_str[1] == 'x') {
						td = print_dword_hex(va_arg(args, unsigned int), td);
						p_str++;
					} else {
						td = print_unsigned(va_arg(args, unsigned int), td);
					}
					break;
				
				case 'l':
					p_str++;
					if (*p_str == 'd') {
						td = print_long(va_arg(args, long), td);
					} else if (*p_str == 'u') {
						td = print_unsigned_long(va_arg(args, unsigned long), td);
					} else if (*p_str == 'x') {
						td = print_qword_hex(va_arg(args, u64), td);
					} else {
						draw_char('%', cursor.coulmn, cursor.row);
						td = cursor_inc(td);
						draw_char('l', cursor.coulmn, cursor.row);
						td = cursor_inc(td);
						p_str--;
					}
					break;
				
				case 'b':
					td = print_byte_hex((u8)va_arg(args, int), td);
					break;

				case 's': {
					const char *value = va_arg(args, const char *);
					if (value == 0)
						value = "(null)";
					while (*value != '\0') {
						draw_char(*value, cursor.coulmn, cursor.row);
						td = cursor_inc(td);
						value++;
					}
					break;
				}
				case 'c': {
                    char ch = (char)va_arg(args, int);
                    draw_char(ch, cursor.coulmn, cursor.row);
                    td = cursor_inc(td);
                    break;
                }

				case 'w':
					td = print_word_hex((u16)va_arg(args, int), td);
					break;
				
				default:
					p_str--;
					draw_char((char)*p_str, cursor.coulmn, cursor.row);
					td = cursor_inc(td);
					break;
			}
		}
		else {
            draw_char((char)*p_str, cursor.coulmn, cursor.row);
			td = cursor_inc(td);
        }
		p_str++;
    }

    if (memory_console) {
        cursor_Setpos(td.coulmn, td.row);
    }

    present();
	va_end(args);
	return td;
}


void console_move_cursor(int columns, int lines) {
    cursor_Setpos(cursor.coulmn + columns, cursor.row + lines);
}

void console_put_char(unsigned char ch) {
    switch (ch) {
        case '\b': console_move_cursor(-1, 0); return;
        case '\r': cursor.coulmn = 0; return;
        case '\t': console_move_cursor(8 - cursor.coulmn % 8, 0); return;
        case '\n':
            qemu_debug_putc('\n');
            if (cursor.row + 1 < rows)
                ++cursor.row;
            else
                scroll((text_data){0});
            return;
    }

    if (ch < ' ' || ch == 0x7f)
        return;
    draw_char((char)ch, cursor.coulmn, cursor.row);
    if (++cursor.coulmn == coulmns) {
        cursor.coulmn = 0;
        if (cursor.row + 1 < rows)
            ++cursor.row;
        else
            scroll((text_data){0});
    }
}

void console_present(void) {
    present();
}


u8 str_size(const char *str) {
    u8 count = 0;
    while (*str != '\0') {
        str++;
        count++;
    }

    return count;
}



void kprintf(const char *str) {
    const char *p_str = str;
    while (*p_str != '\0') {
        if (*p_str == '\n') {
            qemu_debug_putc('\n');
            cursor.coulmn = 0;
            cursor.row++;
			if (cursor.row >= (ver_res >> 4)) {
                text_data td = {0};
                scroll(td);
                cursor.row = (ver_res >> 4) - 1;
    		}
		} else {
            draw_char((char)*p_str, cursor.coulmn, cursor.row);
            text_data td = {0};
            cursor_inc(td);
        }
		p_str++;
    }

    present();
}



void backspace(void) {
    hide_cursor();
    cursor_dec();
    printf(" ");
    cursor_dec();
    draw_cursor(LightGray);
}

void set_begin(void) {
    cursor.coulmn = 0;
}


void set_start(void) {
    cursor.row = 0;
    cursor.coulmn = 0;
}


const char *digits = "0123456789";

void erase_from_cursor(void) {
    for (int col = cursor.coulmn; col < coulmns; ++col)
        draw_char(' ', col, cursor.row);
}


void enable_reverse_video(void) {
    int bg = background_color; // swap background and foreground colors
    int gt = global_textcolor;
    background_color = gt;
    global_textcolor = bg;
}


void reset_video(void) {
    background_color = Black;  // reset background and foreground colors to defualt
    global_textcolor = LightGray;
}


void clear_screen(void) {
    for (int i = 0; i < ver_res; i++) { // fill screen framebuffer with black
        for (int j = 0; j < p_scanln; j++) {
            draw_pixel(j, i, 0);
        }
    }

    present();
}




static void clear_row(int row) {
    for (int coulmn = 0; coulmn < coulmns; coulmn++)
        draw_char(' ', coulmn, row); // fill coulmns with blank

}


static void copy_row_to_row(int dest, int src) {
    for (int coulmn = 0; coulmn < coulmns; coulmn++)
        diff[(dest * coulmns) + coulmn] = true; // update diff on destination row for screen update

    dest = dest << 4;
    src = src << 4;

    u32 *vga = (u32 *)fb_addr;
    for (int y = 0; y < 16; y++) {
        for (int x = 0; x < p_scanln; x++) 
            vga[((dest+y) * p_scanln) + x] = vga[((src+y) * p_scanln) + x]; // copy source row to destination
    }
    
}


static inline void shift_row_down(int row) {
    if (row >= (rows-1)) { // if last row... clear it
        clear_row(row);
        return;
    }

    copy_row_to_row(row+1, row); // else copy it to the row below
    
}



static inline void shift_row_up(int row) {
    if (row == 0) {
        clear_row(row);
        return;
    }


    copy_row_to_row(row-1, row);
}



void make_blank_row(void) {
    for (int row = (rows - 1); row > (cursor.row-1); row--) {
        shift_row_down(row);
    }

    clear_row(cursor.row);
}


void delete_row(void) {
    clear_row(cursor.row);
    for (int row = (cursor.row+1); row < rows; row++) {
        shift_row_up(row);
    }

    clear_row(rows-1);
}


void make_blank_rows(int n) {
    int row_org = cursor.row;

    if (n > rows - cursor.row)
        n = rows - cursor.row;

    for (int i = 0; i < n; i++) {
        make_blank_row();
        cursor.row++;
    }

    cursor.row = row_org;
}


void delete_rows(int n) {
    int row_org = cursor.row;

    if (n > rows - cursor.row)
        n = rows - cursor.row;

    for (int i = 0; i < n; i++) {
        delete_row();
        cursor.row++;
    }

    cursor.row = row_org;
}



u64 getshellsize(void) {
    u64 ret = ((u64)rows << 32) + (u64)coulmns;
    return ret;
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


u64 Cursor_Getpos(void) {
    u64 Cursor_pos = ((u64)cursor.row << 32) + (u64)cursor.coulmn;
    return Cursor_pos;
}


void cpy_memcons(void *buffer) {
    memset(memcons, 0, memcons_size);
    memcons_index = 0;
    print_kernel_memory_hardware(true);
    memcpy(buffer, memcons, memcons_size);
}


u64 get_memcons_size(void) {
    return memcons_size;
}


void ClearRow(int row) {
    if (row >= rows)
        return;

    int y = row << 4;
    // multiply by 16
    
    for (int i = y; i < (y+16); i++) {
        for (int j = 0; j < p_scanln; j++)
            draw_pixel(j, i, 0);
    }
}