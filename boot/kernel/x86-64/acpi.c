#include "uint_definitions.h"
#include "memory.h"
#include "vga.h"

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


u64 rsdp_address = 0;

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
            return addr;
    }


    // 2. Search BIOS area
    for (u64 addr = 0xE0000;
         addr < 0x100000;
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
            return addr;
    }

    return 0;
}




void rsdp_init(u64 rsdp_base) {
    rsdp_address = rsdp_base;
}


u64 discover_APICIO(void)
{
    if (!rsdp_address)
        return 0;

    RSDPDescriptor20 *rsdp = (RSDPDescriptor20*)rsdp_address;

    printf("rsdp revision = %b  rsdt_address = 0%lx  xsdt_address = 0x%lx\n", rsdp->revision, rsdp->rsdt_address, rsdp->xsdt_address);

    if (memcmp(rsdp->signature, "RSD PTR ", 8) != 0)
        return 0;

    if (rsdp->revision < 2 || rsdp->xsdt_address == 0) {
        ACPISDTHeader *rsdt = (ACPISDTHeader *)(u64)rsdp->rsdt_address;
        u32 entries = (rsdt->length - sizeof(ACPISDTHeader)) / 4;
        u32 *tables = (u32 *)((u8 *)rsdt + sizeof(ACPISDTHeader));

        for (u32 i = 0; i < entries; i++) {
            ACPISDTHeader *table = (ACPISDTHeader *)(u64)tables[i];

            if (memcmp(table->signature, "APIC", 4) == 0) {
                MADT *madt = (MADT *)table;

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
                        return (u64)ioapic->address;
                    }

                    madt_ptr += entry->length;
                }

                return 0;

            }

        }

        return 0;
    }


   
    ACPISDTHeader *xsdt = (ACPISDTHeader*)rsdp->xsdt_address;

    if (memcmp(xsdt->signature, "XSDT", 4) != 0)
        return 0;

    if (xsdt->length < sizeof(ACPISDTHeader))
        return 0;

    u8 *ptr = (u8*)xsdt + sizeof(ACPISDTHeader);
    u8 *end = (u8*)xsdt + xsdt->length;

    while (ptr + 8 <= end)
    {
        u64 table_phys = *(u64*)ptr;
        ACPISDTHeader *table = (ACPISDTHeader*)table_phys;

        if (memcmp(table->signature, "APIC", 4) == 0)
        {
            MADT *madt = (MADT*)table;

            u8 *madt_ptr = madt->entries;
            u8 *madt_end = (u8*)madt + madt->header.length;

            while (madt_ptr + 2 <= madt_end)
            {
                MADTEntryHeader *entry = (MADTEntryHeader*)madt_ptr;

                if (entry->length < 2)
                    break;

                if (madt_ptr + entry->length > madt_end)
                    break;

                if (entry->type == 1 && entry->length >= sizeof(MADTIOAPIC))
                {
                    MADTIOAPIC *ioapic = (MADTIOAPIC*)entry;
                    return (u64)ioapic->address;
                }

                madt_ptr += entry->length;
            }

            return 0;
        }

        ptr += 8;
    }

    return 0;
}