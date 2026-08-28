#include "uint_definitions.h"
#include "x86-64/ports.h"
#include "x86-64/paging.h"
#include "x86-64/acpi.h"
#include "x86-64/memory/memory.h"
#include "drivers/display/vga.h"
#include "drivers/timers/timer.hpp"
#include "drivers/timers/pm.hpp"
#include "drivers/timers/hpet.hpp"



void SimpleTimer::Timer_init(PM_ret desc, const char *name, const char *sign) {
    u64 addr = desc.io_base;
    Mode = desc.code;
    if (Mode == MMIO) {
        Timer_base = addr + BASE;
        create_mapping(Timer_base, addr, 1, 0x13, KernelPML4);
        flush_pages(Timer_base, 1);
        printf("Kernel: %s is MMIO, mapped to virtual address: 0x%lx\n", name, Timer_base);
    } else {
        Timer_base = addr;
        printf("Kernel: %s is port IO, starts at IO address: 0x%w\n", name, (u16)Timer_base);
    }
    strcpy(signature, sign);
}


Timer::Timer() {

    const char *signs[] = {
        "FACP",
        "HPET"
    };

    const char *names[] = {
        "PM timer",
        "HPET"
    };

    
    for (int i = 0; i < 2; i++) {
        ACPI_ret ret = ACPI_discovery(signs[i]);
        instances += (ret.status == 0);
        if (ret.status != 0) {
            printf("Kernel: %s not found\n", names[i]);
        } else {
            if (memcmp(signs[i], "FACP", 4) == 0) {
                PMTimer pm;            
                Src = &pm;    
            } else if (memcmp(signs[i], "HPET", 4) == 0) {
                HPET hpet;
                Src = &hpet;
            } 
        }
    }
    

}

