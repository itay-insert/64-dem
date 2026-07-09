#include <stdint.h>
#include "lowlevel.h"
#include "vga.h"
#include "rtc.h"
#include "ports.h"
#include "memory.h"
#include "uint_definitions.h"
#include "paging.h"



#define KernelEntry info_buffer64[0]
#define KernelStart info_buffer64[1]
#define KernelEnd info_buffer64[2]
#define Framebuffer_base info_buffer64[3]
#define MemoryMapSize info_buffer64[4]
#define DescriptorSize info_buffer64[5]
#define BitmapSize info_buffer64[6]
#define PixelMode info_buffer[0]
#define Horizontal_res info_buffer[1]
#define Vertical_res info_buffer[2]
#define PixelsPerScanline info_buffer[3]

int paging_enabled = 0;

void kernel_main(u64 *info_buffer64, int *info_buffer, u64 stack, EFI_MEMORY_DESCRIPTOR *memory_map) {
    if (paging_enabled == 0) {
        paging_enabled = 1;
        u8 *bitmap = (u8 *)stack;
        PAGING_SETUP_DESCRIPTOR ps = {info_buffer64, info_buffer, bitmap, memory_map};
        SetupPaging(ps);
    }
    vga_init(Framebuffer_base, Horizontal_res, Vertical_res, PixelsPerScanline, PixelMode);
    printf("Paging enabled!\n");
    printf("PML4 = %lx\n", KernelPML4);
    u64 ram_size = (BitmapSize * 8) / 0x40000;
    printf("%lu Gigabytes of ram\n", ram_size);
    printf("%d\n", GbPageSupport);
    EFI_MEMORY_DESCRIPTOR alloc = alloc_frame(5);
    printf("allocated address= 0x%lx  pages= %lu\n", alloc.PhysicalStart, alloc.NumberOfPages);
    free_frame(alloc);
    alloc = alloc_frame(1);
    printf("allocated address= 0x%lx  pages= %lu\n", alloc.PhysicalStart, alloc.NumberOfPages);
    free_frame(alloc);
    printf("bitmapSize= %lx\n", BitmapSize);
    printf("stack_top= %lx  memory_mapStart=%lx  info_buffer=%lx  info_buffer64=%lx  MemMapsz=%lu  dsz=%lu\n", stack, memory_map, info_buffer, info_buffer64, MemoryMapSize, DescriptorSize);
    printf("KernelEntry = 0x%lx\nKernelStart = 0x%lx\nKernelEnd = 0x%lx\nFramebuffer_base = 0x%lx \n",
         KernelEntry, KernelStart, KernelEnd, Framebuffer_base);
    printf("\n");
    printf("\n");
    clock();
    while (1) {   }
    
}