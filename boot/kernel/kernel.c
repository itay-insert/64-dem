#include <stdint.h>
#include "uint_definitions.h"
#include "lowlevel.h"
#include "vga.h"
#include "rtc.h"
#include "ports.h"
#include "memory.h"
#include "paging.h"
#include "cpudir.h"
#include "tss.h"
#include "gdt.h"
#include "acpi.h"
#include "idt.h"
#include "apic.h"




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
#define InfoSize info_buffer[4]

#define RGB 0
#define BGR 1

/* Set to 0 after verifying exception delivery on the target machines. */
#define TEST_EXCEPTION_AFTER_ACPI 1

int paging_enabled = 0;


u64 APIC_base = 0;

u64 IO_APIC = 0;

void kernel_main(u64 *info_buffer64, int *info_buffer, u64 stack, EFI_MEMORY_DESCRIPTOR *memory_map) {
    qemu_debug_print("[kernel] entered kernel_main\n");

    if (paging_enabled == 0) {
        qemu_debug_print("[kernel] starting paging setup\n");
        vga_init(Framebuffer_base, Horizontal_res, Vertical_res, PixelsPerScanline, PixelMode);
        paging_enabled = 1;
        u8 *bitmap = (u8 *)stack;
        PAGING_SETUP_DESCRIPTOR ps = {info_buffer64, info_buffer, bitmap, memory_map};
        SetupPaging(ps);
    }
    qemu_debug_print("[kernel] paging setup complete\n");
    vga_init(Framebuffer_base, Horizontal_res, Vertical_res, PixelsPerScanline, PixelMode);
    qemu_debug_print("[kernel] VGA initialized\n");
    qemu_debug_print("[kernel] GOP width=");
    qemu_debug_hex_u64((u64)Horizontal_res);
    qemu_debug_print(" height=");
    qemu_debug_hex_u64((u64)Vertical_res);
    qemu_debug_print(" stride=");
    qemu_debug_hex_u64((u64)PixelsPerScanline);
    qemu_debug_print("\n");

    tss_init();
    setup_gdt();

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

    u64 ram_size = calculate_pages(memory_map, MemoryMapSize, DescriptorSize);
    u64 gib = ram_size >> 18;
    u64 remainder = ram_size & 0x3ffff;
    remainder = (remainder * 1000) >> 18;
    printf("%lu.%lu GiB of ram detected\n", gib, remainder); 

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
     if (PixelMode == RGB) {
        printf("Pixel format: RGB\n");
    } else if (PixelMode == BGR) {
        printf("Pixel format: BGR\n");
    }

    setup_execptions();

    disable_pic();

    APIC_base = discover_APIC();

    u64 *PML4 = (u64 *)KernelPML4;

    create_mapping(APIC_base, APIC_base-BASE, 1, 0x13, PML4);

    flush_pages(APIC_base, 1);

    printf("Validating rsdp... ");

    RSDP = RSDP + BASE;

    char *sign = (char *)RSDP;
    if (memcmp(sign, "RSD PTR ", 8) == 0) {
        printf("rsdp valid, using rsdp... ");
        rsdp_init(RSDP);
        IO_APIC = discover_IOAPIC();
        if (IO_APIC == 1) {
            printf("IO_APIC discovery failed... trying legacy... ");
            u64 legacy_rsdp = find_rsdp_legacy();
            char *legacy_sign = (char *)legacy_rsdp;
            if (memcmp(legacy_sign, "RSD PTR ", 8) == 0) {
                printf("legacy rsdp valid, using legacy rsdp... ");
                rsdp_init(legacy_rsdp);
                IO_APIC = discover_IOAPIC();
                if (IO_APIC == 1) {
                    printf("IO_APIC discovery failed... assuming 0xFEC00000\n");
                    IO_APIC = 0xFFFF9000FEC00000;
                } else {
                    printf("IO_APIC = 0x%lx \n", IO_APIC);
                }
            } else {
                printf("no valid rsdp found... assuming IO_APIC is 0xFEC00000\n");
                IO_APIC = 0xFFFF9000FEC00000;
            }
        } else {
            printf("IO_APIC = 0x%lx \n", IO_APIC);
        }
    } else {
        printf("rsdp is not valid, resulting into rsdp legacy... ");
        u64 legacy_rsdp = find_rsdp_legacy();
        char *legacy_sign = (char *)legacy_rsdp;
        if (memcmp(legacy_sign, "RSD PTR ", 8) == 0) {
            printf("legacy rsdp valid, using legacy rsdp... ");
            rsdp_init(legacy_rsdp);
            IO_APIC = discover_IOAPIC();
            if (IO_APIC == 1) {
                printf("IO_APIC discovery failed... assuming 0xFEC00000\n");
                IO_APIC = 0xFFFF9000FEC00000;
            } else {
                printf("IO_APIC = 0x%lx \n", IO_APIC);
            }
        } else {
            printf("no valid rsdp found... assuming IO_APIC is 0xFEC00000\n");
            IO_APIC = 0xFFFF9000FEC00000;
        }
    }

    create_mapping(IO_APIC, IO_APIC-BASE, 1, 0x13, PML4);

    flush_pages(IO_APIC, 1);

    APIC_init();

    PM_ret pm = discover_PM_timer();

    const char *types[] = {
        "MMIO",
        "Port i/o",
        "error"
    };

    printf("PM_address = 0x%lx   ", pm.io_base);
    printf("PM_type: ");
    printf(types[pm.code]);
    
    printf(" ");
    
    enable_interrupts();
    printf("interrupts enabled!\n");

    printf("PML4= 0x%lx  ", KernelPML4);

    printf("BitmapSize = 0x%lx  stack_top = 0x%lx", BitmapSize, stack);


    cpu_info();
    printf("KernelStart = 0x%lx  KernelEntry = 0x%lx  KernelEnd = 0x%lx\nFramebuffer_base = 0x%lx  Local_APIC = 0x%lx  IO_APIC = 0x%lx\n",
         KernelStart, KernelEntry, KernelEnd, Framebuffer_base, APIC_base, IO_APIC);
    printf("the clock:");
    draw_cursor(LightGray);
    printf("\n");
    clock();
    while (1) {   }
    
}
