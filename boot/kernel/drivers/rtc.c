#include <stdint.h>
#include "ports.h"
#include "vga.h"

#define u8 uint8_t
#define u16 uint16_t
#define u32 uint32_t
#define u64 uint64_t

#define i8 int8_t
#define i16 int16_t
#define i32 int32_t
#define i64 int64_t 

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

void print_date(void) {
    u8 year = rtc_read(0x09);
    u8 month = rtc_read(0x08);
    u8 day = rtc_read(0x07);
    u8 hour = rtc_read(0x04);
    u8 min = rtc_read(0x02);
    u8 sec = rtc_read(0x00);
    text_data td = printf("%b/%b/20%b  %b:%b:%b\n   date       time",
         day, month, year, hour, min, sec);
    cursor_Setpos(td.coulmn, td.row);
}

