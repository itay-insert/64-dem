#include <stdbool.h>
#include "uint_definitions.h"
#include "sys.h"


int main() {

    kprintf("Hello World!\n");

    while (1) {
        u8 key = read_key(true, true);
        char buffer[2] = {0};
        buffer[0] = key;
        buffer[1] = '\0';
        kprintf(buffer);
    }
}
