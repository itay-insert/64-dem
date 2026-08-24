#include <stdint.h>
#include "uint_definitions.h"

#ifndef RTC_H
#define RTC_H

#include "c_compat.h"

KERNEL_EXTERN_C_BEGIN

u8 rtc_read(u8 rtc_reg);

typedef struct {
    u8 year, month, day;
    u8 hour, min, sec;
} rtc_data;

rtc_data get_dateAndTime(void);
void clock(void);

KERNEL_EXTERN_C_END
#endif
