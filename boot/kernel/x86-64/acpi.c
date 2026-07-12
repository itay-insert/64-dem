#include "uint_definitions.h"
#include "memory.h"

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

void rsdp_init(u64 rsdp_base) {
    rsdp_address = rsdp_base;
}


u64 discover_APICIO(void) {
    RSDPDescriptor20 *rsdp = (RSDPDescriptor20 *)rsdp_address;

    u64 xsdt_address = rsdp->xsdt_address;

    ACPISDTHeader *xsdt = (ACPISDTHeader *)xsdt_address;

    u32 entries = (xsdt->length - sizeof(ACPISDTHeader)) / 8;

    u64 *table_addresses = (u64 *)((u8 *)xsdt + sizeof(ACPISDTHeader));

    for (u32 i = 0; i < entries; i++) {
        ACPISDTHeader *table = (ACPISDTHeader *)table_addresses[i];

        if (memcmp(table->signature, "APIC", 4) == 0) {
            MADT *madt = (MADT *)table;

            u8 *ptr = madt->entries;

            while (ptr < ((u8 *)madt + madt->header.length)) {
                MADTEntryHeader *entry = (MADTEntryHeader *)ptr;

                if (entry->type == 1) {
                    MADTIOAPIC *ioapic = (MADTIOAPIC *)entry;

                    return (u64)ioapic->address;
                }

                ptr += entry->length;
            }

            return 1;
        }
    }

    return 1;

}
