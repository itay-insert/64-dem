#include <stdint.h>
#include "lowlevel.h"
#include "vga.h"
#include "rtc.h"
#include "ports.h"
#include "memory.h"
#include "uint_definitions.h"
#include "paging.h"
#include "cpudir.h"
#include "gdt.h"




#define KernelEntry info_buffer64[0]
#define KernelStart info_buffer64[1]
#define KernelEnd info_buffer64[2]
#define Framebuffer_base info_buffer64[3]
#define MemoryMapSize info_buffer64[4]
#define DescriptorSize info_buffer64[5]
#define BitmapSize info_buffer64[6]
#define RSDP info_buffer64[7]
#define PixelMode info_buffer[0]
#define Horizontal_res info_buffer[1]
#define Vertical_res info_buffer[2]
#define PixelsPerScanline info_buffer[3]

#define RGB 0
#define BGR 1

int paging_enabled = 0;


u64 APIC_base = 0;

void kernel_main(u64 *info_buffer64, int *info_buffer, u64 stack, EFI_MEMORY_DESCRIPTOR *memory_map) {
    if (paging_enabled == 0) {
        paging_enabled = 1;
        u8 *bitmap = (u8 *)stack;
        PAGING_SETUP_DESCRIPTOR ps = {info_buffer64, info_buffer, bitmap, memory_map};
        SetupPaging(ps);
    }
    setup_gdt();
    APIC_base = discover_APIC();
    vga_init(Framebuffer_base, Horizontal_res, Vertical_res, PixelsPerScanline, PixelMode);
    if (PixelMode == RGB) {
        printf("Pixel format: RGB\n");
    } else if (PixelMode == BGR) {
        printf("Pixel format: BGR\n");
    }

    

    printf("Paging: [");
    Set_GlobalTextColor(Green);
    printf("OK");
    Set_GlobalTextColor(LightGray);
    printf("]\n");

    printf("GDT reload: [");
    Set_GlobalTextColor(Green);
    printf("OK");
    Set_GlobalTextColor(LightGray);
    printf("]\n");
    
    u64 ram_size = (BitmapSize * 8) / 0x40000;
    printf("an estimate of %lu Gigabytes of ram detected\n", ram_size);
    cpu_info();
    if (GbPageSupport == 1) {
        printf("1 Gb Page support: [");
        Set_GlobalTextColor(Green);
        printf("OK");
        Set_GlobalTextColor(LightGray);
        printf("]\n");
    } else if (GbPageSupport != 1) {
        printf("1 Gb Page support: [");
        Set_GlobalTextColor(Red);
        printf("ERR");
        Set_GlobalTextColor(LightGray);
        printf("]\n");
    }
    EFI_MEMORY_DESCRIPTOR alloc = alloc_frame(5);
    u64 addr = alloc.PhysicalStart;
    free_frame(alloc);
    alloc = alloc_frame(1);
    if (alloc.PhysicalStart == addr) {
        printf("bitmap allocator: [");
        Set_GlobalTextColor(Green);
        printf("OK");
        Set_GlobalTextColor(LightGray);
        printf("]\n");
    } else {
        printf("bitmap allocator: [");
        Set_GlobalTextColor(Red);
        printf("ERR");
        Set_GlobalTextColor(LightGray);
        printf("]\n");
    }
    free_frame(alloc);
    printf("stack_top= 0x%lx\n", stack);
    printf("KernelStart = 0x%lx  KernelEntry = 0x%lx  KernelEnd = 0x%lx\nFramebuffer_base = 0x%lx  Local_APIC = 0x%lx  RSDP = 0x%lx\n",
         KernelStart, KernelEntry, KernelEnd, Framebuffer_base, APIC_base, RSDP);
    printf("the clock:");
    draw_cursor(LightGray);
    printf("\n");
    clock();
    while (1) {   }
    
}