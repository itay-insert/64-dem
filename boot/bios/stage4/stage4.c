#include "uint_definitions.h"

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

enum bios_service_id {
    BIOS_DISK_READ = 0,
    BIOS_CLUSTER_TO_LBA = 1,
    BIOS_LBA_TO_CLUSTER = 2,
    BIOS_SECTOR_COUNT = 3,
    BIOS_NEXT_CLUSTER = 4,
    BIOS_PRINT = 5
};

extern u32 RealModeWrapper(bios_services *services, u32 service_id,
                           u32 arg1, u32 arg2, u32 arg3, u32 *secondary);

void main(bios_services *Services) {

    putc('P', 0x07, 0); // p for protected mode
    while (1);
}
