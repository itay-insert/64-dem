#include <stdbool.h>
#include "uint_definitions.h"
#include "sys.h"


int main() {

    kprintf("Hello World!\n");

    while (1) {

        again:
        u8 scancode = read_key(false, false);

        if (scancode == 0x01)
            return 0;
        u8 key = read_key(true, false);
        if (key == 0) goto again;
        

        char buffer[2] = {0};
        buffer[0] = key;
        buffer[1] = '\0';
        kprintf(buffer);
    }
}
