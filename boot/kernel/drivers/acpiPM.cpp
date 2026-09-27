#include "uint_definitions.h"
#include "x86-64/io.h"
#include "x86-64/acpi.h"
#include "x86-64/paging.h"
#include "drivers/display/vga.h"
#include "drivers/acpiPM.hpp"


AcpiPM::AcpiPM() {
    ACPI_ret ret = ACPI_discovery("FACP");
    FADT *fadt = ret.table.table0;
    regs.pm1a_event = fadt->pm1a_event_block;
    regs.pm1b_event = fadt->pm1b_event_block;
    regs.pm1a_control = fadt->pm1a_control_block;
    regs.pm1b_control = fadt->pm1b_control_block;
    regs.pm_timer = fadt->pm_timer_block;
    if (!(fadt->flags & (1 << 10))) {
        printf("ACPI: Acpi Reset Unavailable\n");
        regs.Reset.reset_reg.address = 0x64; // result to keyboard controller reset if 
        regs.Reset.reset_reg.address_space_id = IO; // Acpi reset unavailable
        regs.Reset.reset_value = 0xFE;
    } else {
        ACPIAddress ResetRegister = fadt->reset_register;
        regs.Reset.reset_reg = ResetRegister;
        if (ResetRegister.address_space_id == MMIO) {
            create_mapping(((BASE+ResetRegister.address) & ~0xFFFULL), (ResetRegister.address & ~0xFFFULL),
        1, 0x13, KernelPML4);
            regs.Reset.reset_reg.address += BASE;
        }
        regs.Reset.reset_value = fadt->reset_value;
    }
}


void AcpiPM::AcpiReset() {
    u8 res_val = regs.Reset.reset_value;
    ACPIAddress ResReg = regs.Reset.reset_reg;
    u64 ResBase = ResReg.address;
    if (ResBase == 0x64 && ResReg.address_space_id == IO) {
        while (inb(ResBase) & 0x02);
        outb(ResBase, res_val);
    } else {
        io_outb(ResBase, res_val, ResReg.address_space_id);
    }

    for (;;)
        asm volatile ("hlt");
}
