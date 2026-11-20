#include <stdint.h>
#include <stdarg.h>
#include "uint_definitions.h"

#ifndef VGA_H
#define VGA_H

#include "c_compat.h"

KERNEL_EXTERN_C_BEGIN

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
void qemu_debug_putc(char ch);
void qemu_debug_print(const char *str);
void qemu_debug_hex_u64(u64 value);
void draw_char(char ch, int coulmn, int row);
void draw_cursor(int color_index);
void hide_cursor(void);

typedef struct {
	int coulmn;
	int row;
} text_data;

text_data printf(const char *str, ...);
void console_move_cursor(int columns, int lines);
void console_put_char(unsigned char ch);
void console_present(void);
void cursor_Setpos(int coulmn, int row);
u8 str_size(const char *str);
void kprintf(const char *str);

extern const char *digits;


static inline unsigned int ascii_to_digit(char ch) {
    for (int i = 0; i < 10; i++) {
        if (ch == digits[i])
            return i;
    }

    return 0xFFFFFFFF; // for failure
}

KERNEL_EXTERN_C_END

#endif
