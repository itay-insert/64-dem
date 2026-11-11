#include <stdint.h>
#include "uint_definitions.h"
#include "x86-64/lowlevel.h"
#include "drivers/display/vga.h"
#include "drivers/timers/rtc.h"
#include "x86-64/ports.h"
#include "x86-64/memory/memory.h"
#include "x86-64/paging.h"
#include "x86-64/cpudir.h"
#include "x86-64/tss.h"
#include "x86-64/gdt.h"
#include "x86-64/acpi.h"
#include "x86-64/idt.h"
#include "x86-64/apic.h"
#include "drivers/pci/pci.h"
#include "drivers/pci/pci_names.h"
#include "drivers/dev.h"
#include "boot_info.h"
#include "drivers/acpiPM.hpp"
#include "drivers/timers/timer.hpp"
#include "drivers/pcspkr.h"
#include "drivers/keyboard/keyboard.hpp"
#include "drivers/keyboard/ps2.hpp"
#include "drivers/keyboard/hid.hpp"
#include "drivers/usb/usb.hpp"
#include "drivers/storage/storage.hpp"
#include "x86-64/tsc.h"
#ifdef NVME_QEMU_PROBE
#include "../../tests/nvme_qemu_probe.hpp"
#endif
#include "filesystem/mbr.hpp"
#include "filesystem/fs.hpp"

#define RGB 0
#define BGR 1

/* Set to 0 after verifying exception delivery on the target machines. */
#define TEST_EXCEPTION_AFTER_ACPI 1

int paging_enabled = 0;


u64 APIC_base = 0;

u64 IO_APIC = 0;

u64 Kernel_end = 0;

u64 timeradd = 0;
u64 acpiadd = 0;

u64 usb_addr = 0;

extern volatile u64 keyboard_irq_count;

GlobalKeyboard *globalkeyboard = NULL;

extern "C" void kernel_main(BOOT_INFO64 *info64, BOOT_INFO32 *info32, u64 stack, EFI_MEMORY_DESCRIPTOR *memory_map) {
    wait_secs(1);

    qemu_debug_print("[kernel] entered kernel_main\n");

    if (paging_enabled == 0) {
        qemu_debug_print("[kernel] starting paging setup\n");
        paging_enabled = 1;
        u8 *bitmap = (u8 *)stack;
        PAGING_SETUP_DESCRIPTOR ps = {info64, info32, bitmap, memory_map};
        SetupPaging(ps);
    }
    Kernel_end = info64->kernel_end;
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


    if (info32->pixel_mode == RGB) {
        printf("Pixel format: RGB\n");
    } else if (info32->pixel_mode == BGR) {
        printf("Pixel format: BGR\n");
    }

    setup_execptions();

    disable_pic();

    APIC_base = discover_APIC();


    create_mapping(APIC_base, APIC_base-BASE, 1, 0x13, KernelPML4);

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

    create_mapping(IO_APIC, IO_APIC-BASE, 1, 0x13, KernelPML4);

    flush_pages(IO_APIC, 1);

    APIC_init();
    
    AcpiPM acpi;
    acpiadd = reinterpret_cast<u64>(&acpi);

    ioapic_route_keyboard((u8)(ioapic_read(0x20) >> 24));

    pci_init();
    pci_detect_devices();

    Timer timer;
    timeradd = reinterpret_cast<u64>(&timer);

    GlobalUSB USB;
    usb_addr = (u64)&USB;

    // HIDK::init needs USB discovery and both global addresses to be ready.
    globalkeyboard = new GlobalKeyboard;

    printf("ps/2 controller init status: 0x%b\n", ps2_controller_init());

    u32 low = ioapic_read_register(0x12);
    u32 high = ioapic_read_register(0x13);

    printf("IOAPIC Keyboard route low: 0x%ux\n", low);
    printf("IOAPIC Keyboard route high: 0x%ux\n", high);

    u32 id_reg = lapic_read(0x20);
    u8 apic_id = (u8)(id_reg >> 24);

    printf("Local APIC ID register: 0x%ux\n", id_reg);
    printf("Local APIC ID: 0x%b\n", apic_id);


    enable_interrupts();
    printf("interrupts enabled!\n");

    if (info64->device_path != 1) {
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
    }

    printf("Timer signature: %s\n", timer.Src->hw.signature);
    printf("Timer IO base: 0x%lx\n", timer.Src->hw.Timer_base);


    for (int i = 0; i < 3; i++) {
        printf(".");
        wait_ms(*timer.Src, 1000);
    }

    storage disks;
    printf("Storage: %u drives registered\n", (u32)disks.drive_count);
#ifdef NVME_QEMU_PROBE
    nvme_qemu_probe(disks);
#endif


    printf("\n");
    printf("Current timer tick is: %lx\n", timer.Src->read());
    printf("KernelStart = 0x%lx  KernelEntry = 0x%lx  KernelEnd = 0x%lx\nFramebuffer_base = 0x%lx  Local_APIC = 0x%lx  IO_APIC = 0x%lx\n",
         info64->kernel_start, info64->kernel_entry, info64->kernel_end, info64->framebuffer_base, APIC_base, IO_APIC);
    
    disk_drive *boot_drive = disks.find_boot_drive();
    if (boot_drive != nullptr) {
        printf("found boot drive in drive 0x%b:\n", boot_drive->disk_index);
        printf("vendor: %s\n", boot_drive->vendor);
    }

    MBR boot_partition;
    boot_partition.SetBootDrive(boot_drive);

    BootFileSystem fat32;
    fat32.drive = boot_partition.BootDrive;
    u32 lba = boot_partition.DiscoverFileSystem();

    fat32.fs_init(boot_drive, &boot_partition, lba, boot_partition.SizeInBytes);


    printf("the clock:");
    draw_cursor(LightGray);
    printf("\n");

    
    u64 old_keyboard_irq = keyboard_irq_count;
    while (1) {
        USB.service_interrupts(*timer.Src);
        if (keyboard_irq_count != old_keyboard_irq) {
            old_keyboard_irq = keyboard_irq_count;
            text_data td = printf("Current keyboard irq: %lu\n", keyboard_irq_count);
            if (globalkeyboard)
                td = printf("%s", globalkeyboard->ascii);
            cursor_Setpos(td.coulmn, td.row-1);
        }
    }    
    
    
}
