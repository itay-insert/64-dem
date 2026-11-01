#pragma once
#include "uint_definitions.h"
#include "x86-64/acpi.h"

typedef struct {
    ACPIAddress reset_reg;
    u8 reset_value;
} ResetDescriptor;

typedef struct {
    u64 pm1a_type;
    u64 pm1b_type;
    uint32_t pm1a_event;
    uint32_t pm1b_event;
    uint32_t pm1a_control;
    uint32_t pm1b_control;
    uint32_t pm2_control;
    uint32_t pm_timer;
    ACPISDTHeader *dsdt;
    ResetDescriptor Reset;
} AcpiPmRegs;

class AcpiPM {

    public:
        AcpiPmRegs regs;
        AcpiPM();
        void AcpiReset(void);
        void AcpiShutDown(void);

};
