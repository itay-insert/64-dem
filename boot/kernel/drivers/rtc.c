#include <stdint.h>
#include "ports.h"

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


static inline u8 rtc_read(u8 rtc_reg) {
    outb(0x70, rtc_reg);  // write which register to read from
    return inb(0x71);  // read from register
}

static inline int rtc_is_updating() {
    outb(0x70, StatusA);  // check update status
    return inb(0x71) & 0x80;
}

void rtc_wait_ready(void) {
    while(rtc_is_updating());
}

typedef struct {
    u8 sec, min, hour;
    u8 day, month, year;
} rtc_data;

static inline u8 bcd_to_bin(u8 v) {
    return (v >> 4) * 10 + (v & 0x0F);
}

static inline u8 bin_to_bcd(u8 v) {
    return ((v / 10) << 4) | (v % 10);
}

rtc_data get_DateAndTime(void) {
    rtc_data t1, t2;
    u8 statusb = rtc_read(StatusB);  // check whether we are in bin mode
    int binary_mode = (statusb & 0x04) != 0;

    do {
        rtc_wait_ready();

        t1.sec   = rtc_read(Seconds);  // read date and time twice to confirm
        t1.min   = rtc_read(Minutes);
        t1.hour  = rtc_read(Hours);
        t1.day   = rtc_read(Day);
        t1.month = rtc_read(Month);
        t1.year  = rtc_read(Year);

        rtc_wait_ready();

        t2.sec   = rtc_read(Seconds);
        t2.min   = rtc_read(Minutes);
        t2.hour  = rtc_read(Hours);
        t2.day   = rtc_read(Day);
        t2.month = rtc_read(Month);
        t2.year  = rtc_read(Year);

    } while (t1.sec != t2.sec ||
             t1.min != t2.min ||
             t1.hour != t2.hour ||
             t1.day != t2.day ||
             t1.month != t2.month ||
             t1.year != t2.year);

            
    if (binary_mode) {
        t2.sec   = bin_to_bcd(t2.sec);
        t2.min   = bin_to_bcd(t2.min);
        t2.hour  = bin_to_bcd(t2.hour);
        t2.day   = bin_to_bcd(t2.day);
        t2.month = bin_to_bcd(t2.month);
        t2.year  = bin_to_bcd(t2.year);
    }

    return t2;
}

