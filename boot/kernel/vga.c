#include <stdint.h>

#define u8 uint8_t
#define u16 uint16_t
#define u32 uint32_t
#define u64 uint64_t

#define i8 int8_t
#define i16 int16_t
#define i32 int32_t
#define i64 int64_t 


u32 vga16_color[16] = {
    0x000000, 0x0000aa, 0x00aa00, 0x00aaaa, 
    0xaa0000, 0xaa00aa, 0xaa5500, 0xaaaaaa,
    0x555555, 0x5555ff, 0x55ff55, 0x55ffff,
    0xff5555, 0xff55ff, 0xffff55, 0xffffff
};


u64 fb_addr = 0;
int hor_res = 0;
int ver_res = 0;
int size = 0;

void vga_init(u64 fb_base, int hres, int vres) {
    fb_addr = fb_base;
    size = hres * vres;
    hor_res = hres;
    ver_res = vres;
}

void draw_pixel(int x, int y, u32 color) {
    u32 *vga = (u32 *)fb_addr;
    int addr = (y * hor_res) + x;
    if (addr <= size) {
        vga[addr] = color;
    }
}

void draw_16color(int x, int y, int color_index) {
    u32 *vga = (u32 *)fb_addr;
    int addr = (y * hor_res) + x;
    u32 color = vga16_color[color_index];
    if (addr <= size) {
        vga[addr] = color;
    }
}



