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
        u8 *bitmap = (u8 *)stack;
        PAGING_SETUP_DESCRIPTOR ps = {info_buffer64, info_buffer, bitmap, memory_map};
        SetupPaging(ps);
        paging_enabled = 1;
    }
    rtc_data rt = get_dateAndTime();
    vga_init(Framebuffer_base, Horizontal_res, Vertical_res, PixelsPerScanline, PixelMode);
    printf("%d\n", GbPageSupport);
    printf("bitmapSize= %lx\n", BitmapSize);
    printf("stack_top= %lx  memory_mapStart=%lx  info_buffer=%lx  info_buffer64=%lx  MemMapsz=%lu  dsz=%lu\n", stack, memory_map, info_buffer, info_buffer64, MemoryMapSize, DescriptorSize);
    printf("KernelEntry = 0x%lx\nKernelStart = 0x%lx\nKernelEnd = 0x%lx\nFramebuffer_base = 0x%lx \n",
         KernelEntry, KernelStart, KernelEnd, Framebuffer_base);
    printf("\n");
    text_data td = printf("%b/%b/20%b    %b:%b:%b", rt.day, rt.month, rt.year, rt.hour, rt.min, rt.sec);
    cursor_Setpos(td.coulmn, td.row);
    u8 old_sec = rt.sec;
    while (1) {
        rt = get_dateAndTime();
        if (rt.sec != old_sec) {
            td = printf("%b/%b/20%b    %b:%b:%b", rt.day, rt.month, rt.year, rt.hour, rt.min, rt.sec);
            cursor_Setpos(td.coulmn, td.row);
            old_sec = rt.sec;
        }
    }
    
}