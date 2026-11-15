#include <stddef.h>
#include <stdbool.h>
#include "uint_definitions.h"
#include "sys.h"
#include "stdio.h"

int main() {
    while (1) {
        printf("A:/");
        char buffer[256] = {0};
        int index = 0;
        again:
        u8 key = read_key(true, false);
        if (key != 0) {
            buffer[index] = key;
            index = (index + 1) % 256;
            buffer[index] = '\0';
            kprintf(buffer+((index-1) % 256));
        }
        u8 scancode = read_key(false, false);
        if (scancode != 0x1C && scancode != 0x01) goto again;
    
        if (scancode == 0x01)
            return 0;

        printf("\n");

    }
}