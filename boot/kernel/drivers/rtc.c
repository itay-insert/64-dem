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

u8 rtc_get_status_a(void) {
    return rtc_read(0x0A);
}

void rtc_wait_for_update(void) {
    while (rtc_get_status_a() & 0x80)
    {
        // wait while RTC is updating
    }
}

typedef struct {
    u8 year, month, day;
    u8 hour, min, sec;
} rtc_data;

rtc_data get_dateAndTime(void)
{
    rtc_data a, b;

    do {
        // Wait until RTC is not updating
        while (rtc_read(0x0A) & 0x80);

        a.year  = rtc_read(0x09);
        a.month = rtc_read(0x08);
        a.day   = rtc_read(0x07);
        a.hour  = rtc_read(0x04);
        a.min   = rtc_read(0x02);
        a.sec   = rtc_read(0x00);

        // Make sure we are outside an update window again
        while (rtc_read(0x0A) & 0x80);

        b.year  = rtc_read(0x09);
        b.month = rtc_read(0x08);
        b.day   = rtc_read(0x07);
        b.hour  = rtc_read(0x04);
        b.min   = rtc_read(0x02);
        b.sec   = rtc_read(0x00);

    } while (
        a.year  != b.year  ||
        a.month != b.month ||
        a.day   != b.day   ||
        a.hour  != b.hour  ||
        a.min   != b.min   ||
        a.sec   != b.sec
    );

    return a;
}

void clock(void) {
    rtc_data rt = get_dateAndTime();
    text_data td = printf("%b/%b/20%b    %b:%b:%b", rt.day, rt.month, rt.year, rt.hour, rt.min, rt.sec);
    cursor_Setpos(td.coulmn, td.row);
    u8 old_sec = rt.sec;
    while (1) {
        rt = get_dateAndTime();
        if (rt.sec != old_sec) {
            td = printf("%b/%b/20%b    %b:%b:%b", rt.day, rt.month, rt.year, rt.hour, rt.min, rt.sec);
            cursor_Setpos(td.coulmn, td.row);
            old_sec = rt.sec;
        }
    }
}
