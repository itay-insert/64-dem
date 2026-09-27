#pragma once
#include "uint_definitions.h"
#include "x86-64/acpi.h"

typedef struct {
    ACPIAddress reset_reg;
    u8 reset_value;
} ResetDescriptor;

typedef struct {
    uint32_t pm1a_event;
    uint32_t pm1b_event;
    uint32_t pm1a_control;
    uint32_t pm1b_control;
    uint32_t pm2_control;
    uint32_t pm_timer;
    ResetDescriptor Reset;
} AcpiPmRegs;

class AcpiPM {

    public:
        AcpiPmRegs regs;
        AcpiPM();
        void AcpiReset(void);

};
