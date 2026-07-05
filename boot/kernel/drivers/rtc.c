#include <stdint.h>
#include "ports.h"
#include "vga.h"
#include "uint_definitions.h"

#define Seconds 0x00
#define Minutes 0x02
#define Hours 0x04
#define Day 0x07
#define Month 0x08
#define Year 0x09
#define StatusA 0x0a
#define StatusB 0x0b


u8 rtc_read(u8 rtc_reg) {
    outb(0x70, rtc_reg | 0x80);  // write which register to read from
    return inb(0x71);  // read from register
}

typedef struct {
    u8 year, month, day;
    u8 hour, min, sec;
} rtc_data;

rtc_data get_dateAndTime(void) {
    rtc_data rt = {0};
    rt.year = rtc_read(0x09);
    rt.month = rtc_read(0x08);
    rt.day = rtc_read(0x07);
    rt.hour = rtc_read(0x04);
    rt.min = rtc_read(0x02);
    rt.sec = rtc_read(0x00);
    return rt;
}

