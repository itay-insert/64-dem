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
#include "pci.h"
#include "pci_names.h"
#include "boot_info.h"

#define RGB 0
#define BGR 1

/* Set to 0 after verifying exception delivery on the target machines. */
#define TEST_EXCEPTION_AFTER_ACPI 1

int paging_enabled = 0;


u64 APIC_base = 0;

u64 IO_APIC = 0;

void kernel_main(BOOT_INFO64 *info64, BOOT_INFO32 *info32, u64 stack, EFI_MEMORY_DESCRIPTOR *memory_map) {
    qemu_debug_print("[kernel] entered kernel_main\n");

    if (paging_enabled == 0) {
        qemu_debug_print("[kernel] starting paging setup\n");
        vga_init(info64->framebuffer_base, info32->horizontal_resolution, info32->vertical_resolution, info32->pixels_per_scanline, info32->pixel_mode);
        paging_enabled = 1;
        u8 *bitmap = (u8 *)stack;
        PAGING_SETUP_DESCRIPTOR ps = {info64, info32, bitmap, memory_map};
        SetupPaging(ps);
    }
    qemu_debug_print("[kernel] paging setup complete\n");
    vga_init(info64->framebuffer_base, info32->horizontal_resolution, info32->vertical_resolution, info32->pixels_per_scanline, info32->pixel_mode);
    qemu_debug_print("[kernel] VGA initialized\n");
    qemu_debug_print("[kernel] GOP width=");
    qemu_debug_hex_u64((u64)info32->horizontal_resolution);
    qemu_debug_print(" height=");
    qemu_debug_hex_u64((u64)info32->vertical_resolution);
    qemu_debug_print(" stride=");
    qemu_debug_hex_u64((u64)info32->pixels_per_scanline);
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

    u64 ram_size = calculate_pages(memory_map, info64->memory_map_size, info64->descriptor_size);
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
    u64 attribute = alloc.Attribute;
    free_frame(alloc);
    alloc = alloc_frame(1);

    if (alloc.PhysicalStart == addr && alloc.Attribute == 0 && attribute == 0) {
        printf("bitmap allocator: [");
        Set_GlobalTextColor(Green);
        printf("OK");
        Set_GlobalTextColor(LightGray);
        printf("]\n");
        free_frame(alloc);
    } else {
        printf("bitmap allocator: [");
        Set_GlobalTextColor(Red);
        printf("ERR");
        Set_GlobalTextColor(LightGray);
        printf("]\n");
    }

    alloc = kmalloc((info64->kernel_end & ~0xfff)+0x1000, 1);
    addr = alloc.PhysicalStart;
    int *ptr = (int *)alloc.VirtualStart;
    *ptr = 1;

    u64 *PML4 = (u64 *)KernelPML4;
    
    EFI_MEMORY_DESCRIPTOR alloc1 = kmalloc((info64->kernel_end & ~0xfff)+0x2000, 1);
    if (*ptr == 1 && addr != alloc1.PhysicalStart) {
        printf("kmalloc: [");
        Set_GlobalTextColor(Green);
        printf("OK");
        Set_GlobalTextColor(LightGray);
        printf("]\n");
    } else {
        printf("kmalloc: [");
        Set_GlobalTextColor(Red);
        printf("ERR");
        Set_GlobalTextColor(LightGray);
        printf("]\n");
        PAGING_LOOKUP_DESCRIPTOR lookup = paging_lookup(alloc.VirtualStart, PML4);
        printf("error_status: %lu  physical_start: 0x%lx  virtual_start: 0x%lx pages: 0x%lu  lookup: %d\n",
             alloc1.Attribute, alloc1.PhysicalStart, alloc1.VirtualStart, alloc1.NumberOfPages, lookup.status);
    }

    addr = alloc1.PhysicalStart;

    kfree(alloc1);
    kfree(alloc);

    alloc = kmalloc((info64->kernel_end & ~0xfff)+0x1000, 1);
    if (alloc.PhysicalStart < addr) {
        printf("kfree: [");
        Set_GlobalTextColor(Green);
        printf("OK");
        Set_GlobalTextColor(LightGray);
        printf("]\n");
    } else {
        printf("kfree: [");
        Set_GlobalTextColor(Red);
        printf("ERR");
        Set_GlobalTextColor(LightGray);
        printf("]\n");
    }
    
    kfree(alloc);

    if (info32->pixel_mode == RGB) {
        printf("Pixel format: RGB\n");
    } else if (info32->pixel_mode == BGR) {
        printf("Pixel format: BGR\n");
    }

    setup_execptions();

    disable_pic();

    APIC_base = discover_APIC();


    create_mapping(APIC_base, APIC_base-BASE, 1, 0x13, PML4);

    flush_pages(APIC_base, 1);

    printf("Validating rsdp... ");

    info64->rsdp = info64->rsdp + BASE;

    char *sign = (char *)info64->rsdp;
    if (memcmp(sign, "RSD PTR ", 8) == 0) {
        printf("rsdp valid, using rsdp... ");
        rsdp_init(info64->rsdp);
        ACPI_ret ioapic = ACPI_discovery("APIC");
        if (ioapic.status == 1) {
            printf("IO_APIC discovery failed... trying legacy... ");
            u64 legacy_rsdp = find_rsdp_legacy();
            char *legacy_sign = (char *)legacy_rsdp;
            if (memcmp(legacy_sign, "RSD PTR ", 8) == 0) {
                printf("legacy rsdp valid, using legacy rsdp... ");
                rsdp_init(legacy_rsdp);
                ioapic = ACPI_discovery("APIC");
                if (ioapic.status == 1) {
                    printf("IO_APIC discovery failed... assuming 0xFEC00000\n");
                    ioapic.Address = 0xFFFF9000FEC00000;
                } else {
                    printf("IO_APIC = 0x%lx \n", ioapic.Address);
                    IO_APIC = ioapic.Address;
                }
            } else {
                printf("no valid rsdp found... assuming IO_APIC is 0xFEC00000\n");
                IO_APIC = 0xFFFF9000FEC00000;
            }
        } else {
            printf("IO_APIC = 0x%lx \n", ioapic.Address);
            IO_APIC = ioapic.Address;
        }
    } else {
        printf("rsdp is not valid, resulting into rsdp legacy... ");
        u64 legacy_rsdp = find_rsdp_legacy();
        char *legacy_sign = (char *)legacy_rsdp;
        if (memcmp(legacy_sign, "RSD PTR ", 8) == 0) {
            printf("legacy rsdp valid, using legacy rsdp... ");
            rsdp_init(legacy_rsdp);
            ACPI_ret ioapic = ACPI_discovery("APIC");
            if (ioapic.status == 1) {
                printf("IO_APIC discovery failed... assuming 0xFEC00000\n");
                IO_APIC = 0xFFFF9000FEC00000;
            } else {
                printf("IO_APIC = 0x%lx \n", ioapic.Address);
                IO_APIC = ioapic.Address;
            }
        } else {
            printf("no valid rsdp found... assuming IO_APIC is 0xFEC00000\n");
            IO_APIC = 0xFFFF9000FEC00000;
        }
    }

    create_mapping(IO_APIC, IO_APIC-BASE, 1, 0x13, PML4);

    flush_pages(IO_APIC, 1);

    APIC_init();

    ACPI_ret ret = ACPI_discovery("FACP");

    const char *types[] = {
        "MMIO",
        "Port i/o",
    };

    if (ret.status == 1) {
        printf("error ");
    } else if (ret.status == 0) {
        printf("PM_address = 0x%lx   ", ret.simple_timer.io_base);
        printf("PM_type: ");
        printf(types[ret.simple_timer.code]);
    
        printf(" ");
    }


    ret = ACPI_discovery("HPET");

    if (ret.status == 1) {
        printf("error ");
    } else if (ret.status == 0) {
        printf("HPET_address = 0x%lx   ", ret.simple_timer.io_base);
        printf("HPET_type: ");
        printf(types[ret.simple_timer.code]);

        printf(" ");
    }
    
    enable_interrupts();
    printf("interrupts enabled!\n");

    pci_init();

    PCI_PATH_RET boot_path =
        Discover_BootDevice(info64->device_path, info32->device_path_size);
    if (boot_path.PCI_status == PCI_STATUS_SUCCESS) {
        printf("PCI devices in boot path: %u\n",
               (unsigned int)boot_path.DeviceCount);

        for (u32 i = 0; i < boot_path.DeviceCount; i++) {
            PCI_ret *device = &boot_path.Devices[i];
            printf("  [%u] %u:%u.%u vendor=0x%ux device=0x%ux\n",
                   (unsigned int)i,
                   (unsigned int)device->Bus,
                   (unsigned int)device->Device,
                   (unsigned int)device->Function,
                   (unsigned int)device->vendor_id,
                   (unsigned int)device->device_id);
            printf("      %s  %s\n",
                   PCI_vendor_name(device->vendor_id),
                   PCI_device_name(device->vendor_id,
                                   device->device_id));
        }
    } else {
        printf("Boot PCI path discovery failed: %d (found %u devices)\n",
               boot_path.PCI_status,
               (unsigned int)boot_path.DeviceCount);
    }

    cpu_info();
    printf("KernelStart = 0x%lx  KernelEntry = 0x%lx  KernelEnd = 0x%lx\nFramebuffer_base = 0x%lx  Local_APIC = 0x%lx  IO_APIC = 0x%lx\n",
         info64->kernel_start, info64->kernel_entry, info64->kernel_end, info64->framebuffer_base, APIC_base, IO_APIC);
    printf("the clock:");
    draw_cursor(LightGray);
    printf("\n");
    clock();
    while (1) {   }
    
}
