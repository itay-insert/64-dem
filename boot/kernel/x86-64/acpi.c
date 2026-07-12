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

#define ACPI_RSDP_V1_LENGTH 20
#define ACPI_RSDP_MAX_LENGTH 4096U
#define ACPI_MAX_TABLE_LENGTH (1024U * 1024U)

static int checksum_ok(const void *address, u32 length) {
    const u8 *bytes = (const u8 *)address;
    u8 sum = 0;

    for (u32 i = 0; i < length; i++)
        sum = (u8)(sum + bytes[i]);

    return sum == 0;
}

static int sdt_ok(const ACPISDTHeader *table) {
    if (table == 0 || table->length < sizeof(ACPISDTHeader) ||
        table->length > ACPI_MAX_TABLE_LENGTH)
        return 0;

    return checksum_ok(table, table->length);
}

static u32 read_u32(const u8 *address) {
    return (u32)address[0] |
           ((u32)address[1] << 8) |
           ((u32)address[2] << 16) |
           ((u32)address[3] << 24);
}

static u64 read_u64(const u8 *address) {
    return (u64)read_u32(address) |
           ((u64)read_u32(address + 4) << 32);
}

void rsdp_init(u64 rsdp_base) {
    rsdp_address = rsdp_base;
}


u64 discover_APICIO(void) {
    if (rsdp_address == 0)
        return 0;

    RSDPDescriptor20 *rsdp = (RSDPDescriptor20 *)rsdp_address;

    if (memcmp(rsdp->signature, "RSD PTR ", 8) != 0 ||
        !checksum_ok(rsdp, ACPI_RSDP_V1_LENGTH))
        return 0;

    ACPISDTHeader *root;
    u32 entry_size;

    if (rsdp->revision >= 2) {
        if (rsdp->length < sizeof(RSDPDescriptor20) ||
            rsdp->length > ACPI_RSDP_MAX_LENGTH ||
            !checksum_ok(rsdp, rsdp->length) || rsdp->xsdt_address == 0)
            return 0;

        root = (ACPISDTHeader *)rsdp->xsdt_address;
        entry_size = 8;
        if (!sdt_ok(root) || memcmp(root->signature, "XSDT", 4) != 0)
            return 0;
    } else {
        if (rsdp->rsdt_address == 0)
            return 0;

        root = (ACPISDTHeader *)(u64)rsdp->rsdt_address;
        entry_size = 4;
        if (!sdt_ok(root) || memcmp(root->signature, "RSDT", 4) != 0)
            return 0;
    }

    u32 payload_length = root->length - sizeof(ACPISDTHeader);
    if ((payload_length % entry_size) != 0)
        return 0;

    u32 entries = payload_length / entry_size;
    u8 *table_addresses = (u8 *)root + sizeof(ACPISDTHeader);

    for (u32 i = 0; i < entries; i++) {
        const u8 *entry_address = table_addresses + ((u64)i * entry_size);
        u64 table_address;
        if (entry_size == 8)
            table_address = read_u64(entry_address);
        else
            table_address = read_u32(entry_address);

        if (table_address == 0)
            continue;

        ACPISDTHeader *table = (ACPISDTHeader *)table_address;

        /* Do not checksum every XSDT/RSDT child.  We only need the MADT, and
           unrelated firmware tables are not necessarily mapped for use yet. */
        if (memcmp(table->signature, "APIC", 4) == 0) {
            if (!sdt_ok(table))
                return 0;

            MADT *madt = (MADT *)table;

            if (madt->header.length < sizeof(MADT))
                return 0;

            u8 *ptr = madt->entries;
            u8 *end = (u8 *)madt + madt->header.length;

            while (ptr + sizeof(MADTEntryHeader) <= end) {
                MADTEntryHeader *entry = (MADTEntryHeader *)ptr;

                if (entry->length < sizeof(MADTEntryHeader) ||
                    entry->length > (u64)(end - ptr))
                    return 0;

                if (entry->type == 1) {
                    if (entry->length < sizeof(MADTIOAPIC))
                        return 0;

                    MADTIOAPIC *ioapic = (MADTIOAPIC *)entry;

                    return (u64)ioapic->address;
                }

                ptr += entry->length;
            }

            return 0;
        }
    }

    return 0;

}
