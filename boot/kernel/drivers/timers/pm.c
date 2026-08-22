#include <stdbool.h>
#include "uint_definitions.h"
#include "x86-64/ports.h"
#include "x86-64/paging.h"
#include "x86-64/acpi.h"


#define PM_TIMER_FREQUENCY 3579545ULL
#define PM_TIMER_MASK      0x00ffffffu


