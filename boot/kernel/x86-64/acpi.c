#include "uint_definitions.h"
#include "memory.h"
#include "vga.h"
#include "paging.h"

#define MADT_sig 0x43495041
#define FACP_sig 0x50434146
#define HPET_sig 0x54455048
#define MCFG_sig 0x4746434D


typedef struct {
    char signature[8];
    u8 checksum;
    char oem_id[6];
    u8 revision;
    u32 rsdt_address;
    u32 length;
    u64 xsdt_address;
    u8 extended_checksum;
    u8 reserved[3];
} __attribute__((packed)) RSDPDescriptor20;


typedef struct {
    char signature[4];
    u32 length;
    u8 revision;
    u8 checksum;
    char oem_id[6];
    char oem_table_id[8];
    u32 oem_revision;
    u32 creator_id;
    u32 creator_revision;
} __attribute__((packed)) ACPISDTHeader;


typedef struct {
	u8 address_space_id;
	u8 register_bit_width;
	u8 register_bit_offset;
	u8 access_size;
	u64 address;
} __attribute__((packed)) ACPIAddress;
	

typedef struct {
    ACPISDTHeader header;

    u32 firmware_control;
    u32 dsdt;

    u8 reserved0;
    u8 preferred_pm_profile;
    u16 sci_interrupt;
    u32 smi_command_port;

    u8 acpi_enable;
    u8 acpi_disable;
    u8 s4bios_request;
    u8 pstate_control;

    u32 pm1a_event_block;             
    u32 pm1b_event_block;             
    u32 pm1a_control_block;           
    u32 pm1b_control_block;           
    u32 pm2_control_block;            
    u32 pm_timer_block;               

    u32 gpe0_block;                   
    u32 gpe1_block;                   

    u8 pm1_event_length;              
    u8 pm1_control_length;            
    u8 pm2_control_length;            
    u8 pm_timer_length;               
    u8 gpe0_length;                 
    u8 gpe1_length;                   
    u8 gpe1_base;                     
    u8 cstate_control;                

    u16 worst_c2_latency;            
    u16 worst_c3_latency;             
    u16 flush_size;                   
    u16 flush_stride;                 

    u8 duty_offset;                   
    u8 duty_width;                    
    u8 day_alarm;                    
    u8 month_alarm;                   
    u8 century;                       

    u16 ia_pc_boot_architecture;      
    u8 reserved1;                     
    u32 flags;                        

    ACPIAddress reset_register;       
    u8 reset_value;                   

    u16 arm_boot_architecture;        
    u8 fadt_minor_version;            

    u64 x_firmware_control;           
    u64 x_dsdt;                       

    ACPIAddress x_pm1a_event_block;   
    ACPIAddress x_pm1b_event_block;   
    ACPIAddress x_pm1a_control_block; 
    ACPIAddress x_pm1b_control_block;
    ACPIAddress x_pm2_control_block;  
    ACPIAddress x_pm_timer_block;     

    ACPIAddress x_gpe0_block;         
    ACPIAddress x_gpe1_block;         

} __attribute__((packed)) FADT;

typedef struct {
    ACPISDTHeader header;
    u32 EventTimerBlockID;
    ACPIAddress Address;
    u8 HPETNumber;
    u16 MinimumTick;
    u8 PageProtection;
} __attribute__((packed)) ACPI_HPET;


typedef struct {
    ACPISDTHeader header;

    u32 lapic_address;
    u32 flags;

    u8 entries[];
} __attribute__((packed)) MADT;


typedef struct {
    u8 type;
    u8 length;
} __attribute__((packed)) MADTEntryHeader;


typedef struct {
    u8 type;
    u8 length;

    u8 ioapic_id;
    u8 reserved;

    u32 address;
    u32 gsi_base;
} __attribute__((packed)) MADTIOAPIC;

typedef struct {
    ACPISDTHeader header;
    u64 reserved;
} __attribute__((packed)) MCFG;

typedef struct {
    u64 BaseAddress;
    u16 SegmentGroup;
    u8 StartBus;
    u8 EndBus;
    u32 Reserved;
} __attribute__((packed)) MCFGEntry;


u64 rsdp_address = 0;

typedef struct {
    u64 io_base;
    int code;
} PM_ret;

typedef struct {
    char signature[4];
    u64 Address;
    PM_ret simple_timer;
    int status;
} ACPI_ret;

u64 find_rsdp_legacy(void)
{
    const char signature[8] = "RSD PTR ";

    // 1. Search EBDA
    u16 ebda_segment = *(u16 *)0x40E;
    u64 ebda_address = ((u64)ebda_segment) << 4;

    for (u64 addr = ebda_address;
         addr < ebda_address + 1024;
         addr += 16)
    {
        char *ptr = (char *)addr;

        int match = 1;

        for (int i = 0; i < 8; i++)
        {
            if (ptr[i] != signature[i])
            {
                match = 0;
                break;
            }
        }

        if (match)
            return addr+BASE;
    }


    // 2. Search BIOS area
    for (u64 addr = 0xFFFF9000000E0000;
         addr < 0xFFFF900000100000;
         addr += 16)
    {
        char *ptr = (char *)addr;

        int match = 1;

        for (int i = 0; i < 8; i++)
        {
            if (ptr[i] != signature[i])
            {
                match = 0;
                break;
            }
        }

        if (match)
            return addr+BASE;
    }

    return 0;
}


void rsdp_init(u64 rsdp_base) {
    rsdp_address = rsdp_base;
}

      
uint32_t acpi_signature(const char sig[4]) {
    return ((uint32_t)sig[0]) |
           ((uint32_t)sig[1] << 8) |
           ((uint32_t)sig[2] << 16) |
           ((uint32_t)sig[3] << 24);
}



ACPI_ret handle_APICIO(MADT *madt, ACPI_ret ret) {
    u8 *madt_ptr = madt->entries;
    u8 *madt_end = (u8*)madt + madt->header.length;

    while (madt_ptr + 2 <= madt_end) {
        MADTEntryHeader *entry = (MADTEntryHeader*)madt_ptr;

        if (entry->length < 2)
            break;

        if (madt_ptr + entry->length > madt_end)
            break;

        if (entry->type == 1 && entry->length >= sizeof(MADTIOAPIC)) {
            MADTIOAPIC *ioapic = (MADTIOAPIC*)entry;
            ret.Address = (u64)ioapic->address + BASE;
            ret.status = 0;
            return ret;
        }

        madt_ptr += entry->length;
                        
    }

    ret.status = 1;
    return ret;
}


ACPI_ret handle_PM_timer(FADT *fadt, ACPI_ret ret) {
    if (fadt->x_pm_timer_block.address != 0) {
        if (fadt->x_pm_timer_block.address_space_id == 0) {
            ret.simple_timer.io_base = fadt->x_pm_timer_block.address;
            ret.simple_timer.code = 0;
            ret.status = 0;
            return ret;
        } else if (fadt->x_pm_timer_block.address_space_id == 1) {
            ret.simple_timer.io_base = (u16)fadt->x_pm_timer_block.address;
            ret.simple_timer.code = 1;
            ret.status = 0;
            return ret;
        }
    } else {
        ret.simple_timer.io_base = (u16)fadt->pm_timer_block;
        ret.simple_timer.code = 1;
        ret.status = 0;
        return ret;
    }
    ret.status = 1;
    return ret;
}

ACPI_ret handle_HPET(ACPI_HPET *hpet, ACPI_ret ret) {
    if (hpet->Address.address_space_id != 0) {
        ret.status = 1;
        return ret;
    }
    ret.simple_timer.io_base = hpet->Address.address;
    ret.simple_timer.code = 0;
    ret.status = 0;
    return ret;
}


ACPI_ret ACPI_discovery(const char *signature) {
    ACPI_ret ret = {0};
    if (!rsdp_address) {
        ret.status = 1;
        return ret;
    }

    RSDPDescriptor20 *rsdp = (RSDPDescriptor20*)rsdp_address;

    if (memcmp(rsdp->signature, "RSD PTR ", 8) != 0) {
        ret.status = 1;
        return ret;
    }

    if (rsdp->revision < 2 || rsdp->xsdt_address == 0) {
        ACPISDTHeader *rsdt = (ACPISDTHeader *)((u64)rsdp->rsdt_address+BASE);
        u32 entries = (rsdt->length - sizeof(ACPISDTHeader)) / 4;
        u32 *tables = (u32 *)((u8 *)rsdt + sizeof(ACPISDTHeader));
        for (u32 i = 0; i < entries; i++) {
            ACPISDTHeader *table = (ACPISDTHeader *)((u64)tables[i] + BASE);
            if (memcmp(table->signature, signature, 4) == 0) {
                switch (acpi_signature(table->signature)) {
                    case MADT_sig:
                        MADT *madt = (MADT *)table;
                        ret = handle_APICIO(madt, ret);
                        return ret;

                    case FACP_sig:
                        FADT *fadt = (FADT *)table;
                        ret = handle_PM_timer(fadt, ret);
                        return ret;
                        
                    case HPET_sig:
                        ACPI_HPET *hpet = (ACPI_HPET *)table;
                        ret = handle_HPET(hpet, ret);
                        return ret;

                    case MCFG_sig:
                        ret.status = 0;
                        ret.Address = (u64)table;
                        return ret;

                    default:
                        ret.status = 1;
                        return ret;
                }
            }

        }
        ret.status = 1;
        return ret;
    }


    ACPISDTHeader *xsdt = (ACPISDTHeader*)(rsdp->xsdt_address+BASE);

    if (memcmp(xsdt->signature, "XSDT", 4) != 0) {
        ret.status = 1;
        return ret;
    }

    if (xsdt->length < sizeof(ACPISDTHeader)) {
        ret.status = 1;
        return ret;
    }


    u8 *ptr = (u8*)xsdt + sizeof(ACPISDTHeader);
    u8 *end = (u8*)xsdt + xsdt->length;

    while (ptr + 8 <= end) {
        u64 table_phys = *(u64*)ptr + BASE;
        ACPISDTHeader *table = (ACPISDTHeader*)table_phys;

        if (memcmp(table->signature, signature, 4) == 0) {
            switch (acpi_signature(table->signature)) {
                case MADT_sig:
                    MADT *madt = (MADT *)table;
                    ret = handle_APICIO(madt, ret);
                    return ret;

                case FACP_sig:
                    FADT *fadt = (FADT *)table;
                    ret = handle_PM_timer(fadt, ret);
                    return ret;
                        
                case HPET_sig:
                    ACPI_HPET *hpet = (ACPI_HPET *)table;
                    ret = handle_HPET(hpet, ret);
                    return ret;

                case MCFG_sig:
                    ret.status = 0;
                    ret.Address = (u64)table;
                    return ret;

                default:
                    ret.status = 1;
                    return ret;
            }

        }


        ptr += 8;
    }

    ret.status = 1;
    return ret;

}