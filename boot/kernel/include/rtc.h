#include <stdint.h>

#define u8 uint8_t
#define u16 uint16_t
#define u32 uint32_t
#define u64 uint64_t

#define i8 int8_t
#define i16 int16_t
#define i32 int32_t
#define i64 int64_t 

#ifndef RTC_H
#define RTC_H

u8 rtc_read(u8 rtc_reg);

typedef struct {
    u8 year, month, day;
    u8 hour, min, sec;
} rtc_data;

rtc_data get_dateAndTime(void);
#endif