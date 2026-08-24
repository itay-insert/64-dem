#include "uint_definitions.h"
#include "x86-64/ports.h"
#include "x86-64/paging.h"
#include "x86-64/acpi.h"
#include "drivers/display/vga.h"
#include "drivers/timers/timer.hpp"





Timer::Timer() {
    ACPI_ret ret = ACPI_discovery("FACP");
    timers[0] = (ret.status == 0);
    if (ret.status != 0) {
        printf("Kernel: PM timer not found\n");
    } 
    ret = ACPI_discovery("HPET");
    timers[1] = (ret.status == 0);
    if (ret.status != 0) {
        printf("Kernel: HPET not found\n");
    }

}