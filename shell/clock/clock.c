#include <stddef.h>
#include <stdbool.h>
#include "uint_definitions.h"
#include "sys.h"
#include "stdio.h"


int main() {
    rtc_data rt = get_dateAndTime();
    printf("%b/%b/20%b    %b:%b:%b", rt.day, rt.month, rt.year, rt.hour, rt.min, rt.sec);
    set_begin();

    while (1) {
        rtc_data new_rt = get_dateAndTime();
        if (new_rt.sec != rt.sec) {
            rt = new_rt;
            printf("%b/%b/20%b    %b:%b:%b", rt.day, rt.month, rt.year, rt.hour, rt.min, rt.sec);
            set_begin();
        }

        u8 scancode = read_key(false, false);

        if (scancode == 0x01)
            return 0;

    }
}