#include "uint_definitions.h"
#include "x86-64/io.h"
#include "x86-64/acpi.h"
#include "x86-64/paging.h"
#include "drivers/display/vga.h"
#include "drivers/acpiPM.hpp"



static inline bool is_s5(const char *name) {
    return name[0] == '_' &&
           name[1] == 'S' &&
           name[2] == '5' &&
           name[3] == '_';

}

static inline u64 search_for_s5(u64 data_base, u64 length) {
    for (u64 i = data_base; i < (data_base+length); i++) {
        const char *name = (const char *)i;
        if (is_s5(name))
            return i;
    }
    return 1;
}


static inline u64 aml_parse(u64 aml_base, u64& value) {
    u8 *type = (u8 *)aml_base;
    switch (*type) {
        case 0x00: {
            value = 0;
            return (aml_base + 1);
        }

        case 0x01: {
            value = 1;
            return (aml_base + 1);
        }

        case 0xFF: {
            value = ~0ULL;
            return (aml_base + 1);
        }

        case 0x0A: {
            aml_base++;
            u8 *val8 = (u8 *)aml_base;
            value = (u64)*val8;
            return (aml_base + 1);
        }

        case 0x0B: {
            aml_base++;
            u16 *val16 = (u16 *)aml_base;
            value = (u64)*val16;
            return (aml_base + 2);
        }

        case 0x0C: {
            aml_base++;
            u32 *val32 = (u32 *)aml_base;
            value = (u64)*val32;
            return (aml_base + 4);
        }

        case 0x0E: {
            aml_base++;
            u64 *val64 = (u64 *)aml_base;
            value = *val64;
            return (aml_base + 8);
        }

        default: {
            value = 0xFF;
            return (aml_base + 1);
        }
    }
}

static inline void find_types(u64& pm1a_type, u64& pm1b_type, u64 s5_base) {
    u64 base = s5_base + 7;
    base = aml_parse(base, pm1a_type);
    base = aml_parse(base, pm1b_type);
}


AcpiPM::AcpiPM() {
    regs = {};
    ACPI_ret ret = ACPI_discovery("FACP");
    FADT *fadt = ret.table.table0;
    if (fadt == nullptr) {
        printf("ACPI: FADT unavailable, using keyboard controller reset\n");
        regs.Reset.reset_reg.address = 0x64;
        regs.Reset.reset_reg.address_space_id = IO;
        regs.Reset.reset_value = 0xFE;
        return;
    }
    regs.pm1a_event = fadt->pm1a_event_block;
    regs.pm1b_event = fadt->pm1b_event_block;
    regs.pm1a_control = fadt->pm1a_control_block;
    regs.pm1b_control = fadt->pm1b_control_block;
    regs.pm_timer = fadt->pm_timer_block;
    u64 dsdt = (u64)fadt->dsdt;
    u64 dsdt_virt = dsdt + BASE; // virtual_address = physical_address + 0xFFFF900000000000 for MMIO/acpi mapping
    
    
    create_mapping(dsdt_virt, dsdt, 1, 0x03, KernelPML4);  // map first page of dsdt
    flush_pages(dsdt_virt, 1);
    ACPISDTHeader *dsdt_ptr = (ACPISDTHeader *)dsdt_virt;

    u64 pages = ((u64)dsdt_ptr->length + 4095) >> 12; // convert length to pages

    create_mapping(dsdt_virt, dsdt, pages+1, 0x03, KernelPML4); // map all dsdt pages + 1 for safety 
    flush_pages(dsdt_virt, pages);

    regs.dsdt = dsdt_ptr; // pass dsdt pointer to regs

    u64 length = (u64)regs.dsdt->length - sizeof(ACPISDTHeader);
    u64 data_base = (u64)((u8 *)regs.dsdt + sizeof(ACPISDTHeader));
    u64 s5_base = search_for_s5(data_base, length);
    if (s5_base == 1) {
        printf("ACPI shutdown: failed to find S5 base\n");
        regs.pm1a_type = 0xFF;
        regs.pm1b_type = 0xFF;
    } else {
        find_types(regs.pm1a_type, regs.pm1b_type, s5_base);
    }
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

#define SLP_TYP(x) (((x) & 7) << 10)
#define SLP_EN (1 << 13)

void AcpiPM::AcpiShutDown() {
   
    if (regs.pm1a_control != 0 && regs.pm1a_type != 0xFF) {
        outw(regs.pm1a_control, SLP_TYP(regs.pm1a_type) | SLP_EN);
    }

    if (regs.pm1b_control != 0 && regs.pm1b_type != 0xFF) {
        outw(regs.pm1b_control, SLP_TYP(regs.pm1b_type) | SLP_EN);
    }

}
