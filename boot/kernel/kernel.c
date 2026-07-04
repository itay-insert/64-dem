#include <stdint.h>
#include "lowlevel.h"
#include "vga.h"
#include "rtc.h"
#include "ports.h"


#define u8 uint8_t
#define u16 uint16_t
#define u32 uint32_t
#define u64 uint64_t

#define i8 int8_t
#define i16 int16_t
#define i32 int32_t
#define i64 int64_t 

#define KernelEntry info_buffer64[0]
#define KernelStart info_buffer64[1]
#define KernelEnd info_buffer64[2]
#define Framebuffer_base info_buffer64[3]
#define PixelMode info_buffer[0]
#define Horizontal_res info_buffer[1]
#define Vertical_res info_buffer[2]
#define PixelsPerScanline info_buffer[3]

#define Black 0
#define Blue 1
#define Green 2
#define Cyan 3
#define Red 4
#define Purple 5
#define Brown 6
#define LightGray 7
#define DarkGray 8
#define LightBlue 9
#define LightGreen 10
#define LightCyan 11
#define LightRed 12
#define Pink 13
#define Yellow 14
#define White 15

typedef unsigned int UINT32;
typedef unsigned long long UINT64; 


typedef UINT64 EFI_PHYSICAL_ADDRESS;
typedef UINT64 EFI_VIRTUAL_ADDRESS;

typedef struct {
    UINT32 Type;
    EFI_PHYSICAL_ADDRESS PhysicalStart;
    EFI_VIRTUAL_ADDRESS VirtualStart;
    UINT64 NumberOfPages;
    UINT64 Attribute;
} EFI_MEMORY_DESCRIPTOR;

void kernel_main(u64 *info_buffer64, int *info_buffer, u64 stack, EFI_MEMORY_DESCRIPTOR *memory_map) {
    rtc_data rt = get_dateAndTime();
    vga_init(Framebuffer_base, Horizontal_res, Vertical_res, PixelsPerScanline, PixelMode);
    printf("stack_top = %lx  memory_mapStart = %lx\n", stack, memory_map);
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