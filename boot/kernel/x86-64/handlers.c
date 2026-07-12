#include "uint_definitions.h"
#include "vga.h"



void page_fault(u64 address, u64 error_code) {
    printf("Page fault at address: 0x%lx\nerror_code: %lu", address, error_code);
}