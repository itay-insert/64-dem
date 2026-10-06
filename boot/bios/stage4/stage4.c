#include "uint_definitions.h"
#include "efi_memory_types.h"

typedef enum {
    bios_disk_services = 0,
    convert_cluster_to_lba = 1,
    convert_lba_to_cluster = 2,
    convert_sector_count = 3,
    find_next_cluster = 4,
    println = 5,
    read_memory_map = 6,
    SetVbe = 7,
} Service_id;

// u32 RealModeWrapper(bios_services *table, u32 service, u32 arg1,
//                    u32 arg2, u32 arg3, u32 *secondary);
// service 0: disk read (arg1=LBA, arg2=physical buffer, arg3=sectors),
//            returns 0 on success or 1 on BIOS error.
// service 1: cluster to LBA (arg1=cluster), returns LBA.
// service 2: LBA to cluster (arg1=LBA), returns cluster; secondary=remainder.
// service 3: sector count (arg1=sectors), returns clusters; secondary=remainder.
// service 4: next cluster (arg1=current cluster), returns next cluster.
// service 5: print (arg1=physical string address, arg2=length), returns 0.
// service 6: E820 map (arg1=physical address of 1024 entries), returns count
//            or 0xffffffff on BIOS error or map overflow.
// Invalid service/arguments return 0xffffffff. Pointers must be below 1 MiB.

volatile char* vga_memory = (volatile char*)(0xB8000);

void putc(char ch, char color, int pos) {
    vga_memory[pos*2] = ch;
    vga_memory[pos*2+1] = color;
}

typedef struct {
    u16 bios_disk_services;
    u16 convert_cluster_to_lba;
    u16 convert_lba_to_cluster;
    u16 convert_sector_count;
    u16 find_next_cluster;
    u16 println;

    u16 code_seg;
    u8  boot_drive;
    u32 partition_start;
    u32 fat_start;
    u8  sectors_per_cluster;
    u8  number_of_fats;
    u32 root_cluster;
    u32 first_data_lba;
} __attribute__((packed)) bios_services;

typedef struct {
    u64 base;
    u64 length;
    u32 type;
    u32 attributes;
} __attribute__((packed)) E820Entry;

E820Entry E820map[1024] = {0};
EFI_MEMORY_DESCRIPTOR efi_memory_map[1024] = {0};

u32 e820_entry_count = 0;
u32 efi_descriptor_count = 0;

typedef char e820_entry_must_be_24_bytes[(sizeof(E820Entry) == 24) ? 1 : -1];

static u32 e820_to_efi_type(u32 type) {
    switch (type) {
    case 1: return EfiConventionalMemory;
    case 3: return EfiACPIReclaimMemory;
    case 4: return EfiACPIMemoryNVS;
    case 5: return EfiUnusableMemory;
    case 7: return EfiPersistentMemory;
    default: return EfiReservedMemoryType;
    }
}

u32 convert_e820_to_efi(const E820Entry *entries, u32 count,
                        EFI_MEMORY_DESCRIPTOR *descriptors) {
    u32 written = 0;
    for (u32 i = 0; i < count && i < 1024; ++i) {
        u64 base = entries[i].base;
        u64 length = entries[i].length;
        if (length == 0 || length > ~(u64)0 - base) {
            continue;
        }

        u32 type = e820_to_efi_type(entries[i].type);
        u64 end = base + length;
        u64 first_page;
        u64 end_page;
        /* Usable memory needs complete pages. Cover any partial page of a
           non-usable region so it cannot be released by the allocator. */
        if (type == EfiConventionalMemory) {
            first_page = (base >> 12) + ((base & 0xfff) != 0);
            end_page = end >> 12;
        } else {
            first_page = base >> 12;
            end_page = (end >> 12) + ((end & 0xfff) != 0);
        }
        if (end_page <= first_page) {
            continue;
        }

        EFI_MEMORY_DESCRIPTOR *descriptor = &descriptors[written++];
        descriptor->Type = type;
        descriptor->PhysicalStart = first_page << 12;
        descriptor->VirtualStart = 0;
        descriptor->NumberOfPages = end_page - first_page;
        descriptor->Attribute = 0;
        if (descriptor->Type == EfiConventionalMemory ||
            descriptor->Type == EfiACPIReclaimMemory ||
            descriptor->Type == EfiACPIMemoryNVS ||
            descriptor->Type == EfiPersistentMemory) {
            descriptor->Attribute = 0;
        }
        if ((entries[i].attributes & 2) != 0 ||
            descriptor->Type == EfiPersistentMemory) {
            descriptor->Attribute |= 0;
        }
    }
    return written;
}

typedef struct {
    i32 pixel_mode;
    i32 horizontal_resolution;
    i32 vertical_resolution;
    i32 pixels_per_scanline;
    i32 info_size;
    u32 framebuffer_address;
} __attribute__((packed)) vbe_data;

extern u32 RealModeWrapper(bios_services *table, u32 service, u32 arg1, u32 arg2, u32 arg3, u32 *secondary);

static u32 extract_size(const char *str) {
    u32 size = 0;
    while (*str != '\0') {
        str++;
        size++;
    }
    return size;
}


void main(bios_services *Services) {

    e820_entry_count = RealModeWrapper(Services, read_memory_map,
        (u32)(uintptr_t)E820map, 0, 0, 0);
    efi_descriptor_count = 0;
    if (e820_entry_count != 0xffffffff) {
        efi_descriptor_count = convert_e820_to_efi(
            E820map, e820_entry_count, efi_memory_map);
    }

    const char *Strings[] = {
        "Testing Real Mode Wrapper ",
        "Real Mode Wrapper: ",
        "OK",
        "ERR",
    };

    const char *String = Strings[0];
    int ret = RealModeWrapper(Services, println, (u32)(uintptr_t)String, extract_size(String), 0, 0);

    String = Strings[1];
    RealModeWrapper(Services, println, (u32)String, extract_size(String), 0, 0);
    if (ret == 0) {
        String = Strings[2];
        RealModeWrapper(Services, println, (u32)String, extract_size(String), 0, 0);
    } else {
        String = Strings[3];
        RealModeWrapper(Services, println, (u32)String, extract_size(String), 0, 0);
    }


    while (1);
}
