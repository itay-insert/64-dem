#include <stdint.h>



void main(uint32_t *fb_base) {
    fb_base[0] = 0x0000ff;
    while (1) { }
}