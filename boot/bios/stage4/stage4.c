#include <stdbool.h>
#include "uint_definitions.h"
#include "efi_memory_types.h"
#include "vga.h"
#include "elf.h"

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
    i32 pixel_mode; // 0 = RGB, 1 = BGR
    i32 horizontal_resolution;
    i32 vertical_resolution;
    i32 pixels_per_scanline;
    i32 info_size;
    u32 framebuffer_address;
} __attribute__((packed)) vbe_data;

/* RealModeWrapper requires the VBE result buffer at or above 0x10000.
   The stage 4 stack starts at 0x10000 and grows below that address. */

u32 find_rsdp_legacy(void)
{
    const char signature[8] = "RSD PTR ";

    // 1. Search EBDA
    u16 ebda_segment = *(u16 *)0x40E;
    u32 ebda_address = ((u32)ebda_segment) << 4;

    for (u32 addr = ebda_address;
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
    for (u32 addr = 0xE0000;
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

static vbe_data vga = { .info_size = sizeof(vbe_data) };

extern u32 RealModeWrapper(bios_services *table, u32 service, u32 arg1, u32 arg2, u32 arg3, u32 *secondary);

static u32 extract_size(const char *str) {
    u32 size = 0;
    while (*str != '\0') {
        str++;
        size++;
    }
    return size;
}

static inline bool cmpstr(const char *ptr0, const char *ptr1, int length) {
    int ec = 0;
    for (int i = 0; i < length; i++) {
        if (ptr0[i] == ptr1[i]) 
            ec++;
    }
    if (ec == length)
        return true;
    else return false;
}

u32 open_path(const char *path, bios_services *Services, u32 Sector) {
    RealModeWrapper(Services, bios_disk_services, Sector, 0x90000, 32, 0);
    char *ptr = (char *)0x90000;
    while (1) {
        if (*ptr == 0)
            return 1;
        
        if (cmpstr(ptr, path, 11))
            return (u32)ptr;

        ptr += 32;
    }
    
}

#define PT_LOAD 1

u32 calc_size(void *ptr, u16 ephentsize, u16 ephnum) {
    u32 size = 0;
    for (u16 i = 0; i < ephnum; i++) {
        u32 *p_type = (u32 *)ptr;
        if (*p_type == PT_LOAD) {
            u64 *p_memsz = (u64 *)((u8 *)ptr + P_MEMSZ);
            size += (u32)*p_memsz;
        }
        ptr = (void *)((u8 *)ptr + ephentsize);
    }

    return size;

}

u32 find_free_region(u32 pages) {
    for (u32 i = 0; i < efi_descriptor_count; i++) 
        if (efi_memory_map[i].PhysicalStart >= 0x100000 && efi_memory_map[i].Type == EfiConventionalMemory
             && efi_memory_map[i].NumberOfPages >= (u64)pages)
             return (u32)efi_memory_map[i].PhysicalStart;
    
    return 1;
}

static inline u64 find_memory_size(void) {
    u64 size = 0;
    for (u32 i = 0; i < efi_descriptor_count; i++) 
        size += efi_memory_map[i].NumberOfPages;
    
    return (size<<12);
}

static void *memcpy(void *Destination, void *Source, int size) {
    u8 *src = (u8 *)Source;
    u8 *dst = (u8 *)Destination;
    
    for (int i = 0; i < size; i++) 
        dst[i] = src[i];

    return Destination;
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

    u32 res = RealModeWrapper(Services, SetVbe, (u32)(uintptr_t)&vga, 0, 0, 0);
    if (res != 0) {
        String = Strings[3];
        RealModeWrapper(Services, println, (u32)String, extract_size(String), 0, 0);
        while (1);
    } else {
        vga_init(vga.framebuffer_address, vga.horizontal_resolution,
                 vga.vertical_resolution, vga.pixels_per_scanline,
                 vga.pixel_mode);

        printf("Vbe Mode set seccussfully, resolution: %dx%d\n", vga.horizontal_resolution, vga.vertical_resolution);
    }

    printf("finding rsdp address... ");
    u32 rsdp = find_rsdp_legacy();
    if (rsdp != 0)
        printf("rsdp address found and is: %lu\n", rsdp);

    printf("Preparing to load kernel...\n");
    u32 BOOT = open_path("BOOT       ", Services, Services->first_data_lba);
    printf("BOOT: 0x%ux\n", BOOT);
    u16 *cluhigh = (u16 *)(BOOT + 0x14);
    u16 *clulow = (u16 *)(BOOT + 0x1A);
    u32 cluster = (*cluhigh << 16) + *clulow;
    cluster = RealModeWrapper(Services, convert_cluster_to_lba, cluster, 0, 0, 0);
    u32 KNL = open_path("KERNEL  ELF", Services, cluster);
    printf("KERNEL.ELF: 0x%ux\n", KNL);
    cluhigh = (u16 *)(KNL + 0x14);
    clulow = (u16 *)(KNL + 0x1A);
    cluster = (*cluhigh << 16) + *clulow;
    cluster = RealModeWrapper(Services, convert_cluster_to_lba, cluster, 0, 0, 0);
    RealModeWrapper(Services, bios_disk_services, cluster, 0x90000, 32, 0);
    u64 *ephoff = (u64 *)(0x90000+E_PHOFF);
    u16 *ephentsize = (u16 *)(0x90000+E_PHENTSIZE);
    u16 *ephnum = (u16 *)(0x90000+E_PHNUM);
    void *ph = (void *)(0x90000+*ephoff);
    u32 size = calc_size(ph, *ephentsize, *ephnum);
    size = (size + 4095) & ~0xFFF; 
    printf("Kernel size: 0x%ux\n", size);
    u32 bitmap_size = ((((u32)find_memory_size() >> 12) >> 3) + 4095) & ~0xfff;
    printf("stack size: 0x00041000\n");
    printf("bitmap size: 0x%ux\n", bitmap_size);
    u32 mmsz = efi_descriptor_count * sizeof(EFI_MEMORY_DESCRIPTOR);
    printf("memory map size: 0x%ux\n", mmsz);
    u32 buf_size64 = 72;
    printf("buf_size64: 72\n");
    u32 buf_size = 24;
    printf("buf_sz: 24\n");
    u32 total_size = size + 0x41000 + bitmap_size + mmsz + buf_size64 + buf_size;
    total_size = (total_size + 4095) & ~0xFFF;
    printf("Total kernel size: 0x%ux\n", total_size);
    u32 kernelstart = find_free_region(total_size>>12);
    printf("KernelStart: 0x%ux\n", kernelstart);
    u32 stack = addr + size + 0x41000;
    EFI_MEMORY_DESCRIPTOR *MMP = (EFI_MEMORY_DESCRIPTOR *)(stack+bitmap_size);

    u64 *p_buff64 = (u64 *)((u32)MMP+mmsz);
    int *p_buff = (int *)((u32)p_buff64 + buf_size64);

    memcpy(MMP, efi_memory_map, mmsz);
    
    p_buff[0] = vga.pixel_mode;
    
    p_buff[1] = vga.horizontal_resolution;
    p_buff[2] = vga.vertical_resolution;
    p_buff[3] = vga.pixels_per_scanline;
    p_buff[4] = (int)(buf_size+buf_size64);
    p_buff[5] = (int)0;


    p_buff64[0] = (u64)kernelstart;
    p_buff64[1] = (u64)kernelstart;
    p_buff64[2] = (u64)((u32)p_buff+24);
    p_buff64[3] = (u64)vga.framebuffer_address;
    p_buff64[4] = (u64)mmsz;
    p_buff64[5] = (u64)sizeof(EFI_MEMORY_DESCRIPTOR);
    p_buff64[6] = (u64)bitmap_size;
    p_buff64[7] = (u64)rsdp;
    p_buff64[8] = (u64)1;

    

    while (1);
}
