#include <stdint.h>
#include "tss.h"


struct tss64 tss = {0};

static uint8_t double_fault_stack[16384] __attribute__((aligned(16)));

void tss_init(void) {
    tss.ist1 = (uint64_t)(double_fault_stack + sizeof(double_fault_stack));
    tss.iomap_base = sizeof(tss);
}

