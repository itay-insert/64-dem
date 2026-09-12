#include <stddef.h>
#include <stdbool.h>
#include "drivers/display/vga.h"
#include "x86-64/paging.h"
#include "x86-64/spinlock.h"
#include "x86-64/memory/frame_allocator.h"
#include "x86-64/memory/va_alloc.h"
#include "x86-64/memory/va_alloc_internal.h"
#include "x86-64/memory/dma_internal.h"
#include "x86-64/memory/memory_diagnostics.h"

#define PAGE_SIZE 4096ULL
#define USER_SPACE_START 0x0000000040000000ULL
#define USER_SPACE_END   0x00007FFFFFFFFFFFULL
#define KERNEL_HEAP_END  0xFFFF8FFFFFFFFFFFULL
#define KERNEL_SPACE_START 0xFFFFC00000000000ULL
#define ADDRESS_SPACE_END  0xFFFFFFFFFFFFFFFFULL
#define BOOT_STACK_SIZE (64ULL * PAGE_SIZE)

extern spinlock_t bitmap_lock;
extern u64 Kernel_end;

static void print_size(u64 bytes) {
    static const u64 kib = 1024ULL;
    static const u64 mib = 1024ULL * 1024ULL;
    static const u64 gib = 1024ULL * 1024ULL * 1024ULL;
    u64 unit = 1;
    const char *name = "B";

    if (bytes >= gib) {
        unit = gib;
        name = "GiB";
    } else if (bytes >= mib) {
        unit = mib;
        name = "MiB";
    } else if (bytes >= kib) {
        unit = kib;
        name = "KiB";
    }

    if (unit == 1) {
        printf("%lu B", bytes);
    } else {
        u64 whole = bytes / unit;
        u64 decimal = ((bytes % unit) * 10ULL) / unit;
        printf("%lu.%lu %s", whole, decimal, name);
    }
}

static void print_virtual_size(u64 bytes) {
    static const u64 kib = 1024ULL;
    static const u64 gib = 1024ULL * 1024ULL * 1024ULL;
    static const u64 tib = 1024ULL * 1024ULL * 1024ULL * 1024ULL;
    u64 unit;
    const char *name;

    if (bytes >= tib) {
        unit = tib;
        name = "TiB";
    } else if (bytes >= gib) {
        unit = gib;
        name = "GiB";
    } else {
        unit = kib;
        name = "KiB";
    }

    u64 whole = bytes / unit;
    u64 decimal = ((bytes % unit) * 10ULL) / unit;
    printf("%lu.%lu %s", whole, decimal, name);
}

static unsigned int count_bits(u8 value) {
    unsigned int count = 0;
    while (value != 0) {
        count += value & 1U;
        value >>= 1;
    }
    return count;
}

void print_physical_memory_stats(void) {
    u64 used_pages = 0;
    u64 capacity_pages;
    u64 free_pages;
    u64 kernel_used_pages;
    u64 reserved_pages;
    u64 page_table_pages;
    u8 *bitmap = (u8 *)bitmap_base;

    spin_lock(&bitmap_lock);
    for (u64 i = 0; i < bitmapSize; i++)
        used_pages += count_bits(bitmap[i]);
    capacity_pages = bitmapSize * 8ULL;
    spin_unlock(&bitmap_lock);

    free_pages = capacity_pages - used_pages;
    kernel_used_pages = allocator_usable_pages >= free_pages ?
        allocator_usable_pages - free_pages : 0;
    reserved_pages = capacity_pages - allocator_usable_pages;
    page_table_pages =
        __atomic_load_n(&paging_allocated_pages, __ATOMIC_RELAXED);

    printf("Physical memory (bitmap): free=");
    print_size(free_pages * PAGE_SIZE);
    printf(" kernel-used=");
    print_size(kernel_used_pages * PAGE_SIZE);
    printf(" (paging=");
    print_size(page_table_pages * PAGE_SIZE);
    printf(") reserved/holes=");
    print_size(reserved_pages * PAGE_SIZE);
    printf(" capacity=");
    print_size(capacity_pages * PAGE_SIZE);
    printf("\n");
}

static u64 tree_free_bytes(const va_node *node) {
    if (node == NULL)
        return 0;
    return node->length + tree_free_bytes(node->va_left) +
           tree_free_bytes(node->va_right);
}

static bool tree_contains_range(const va_node *node, u64 base, u64 length) {
    if (node == NULL)
        return false;
    if (base >= node->virtual_base && length <= node->length &&
        base - node->virtual_base <= node->length - length)
        return true;
    return tree_contains_range(node->va_left, base, length) ||
           tree_contains_range(node->va_right, base, length);
}

void print_virtual_memory_stats(void) {
    va_ret probe = va_alloc(1, 0x03);
    bool allocated = probe.status == 0 && (probe.base & (PAGE_SIZE - 1)) == 0;
    bool memory_ok = false;
    bool returned = false;

    if (allocated) {
        volatile u64 *word = (volatile u64 *)probe.base;
        const u64 pattern = 0x5641544553544F4BULL;
        *word = pattern;
        memory_ok = *word == pattern;
        va_free(probe);
        spin_lock(&va_lock);
        returned = tree_contains_range(va_kstart, probe.base, PAGE_SIZE);
        spin_unlock(&va_lock);
    }

    spin_lock(&va_lock);
    u64 free_bytes = tree_free_bytes(va_kstart) + tree_free_bytes(va_ustart);
    spin_unlock(&va_lock);

    u64 kernel_low_start = (Kernel_end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    u64 capacity = (KERNEL_HEAP_END - kernel_low_start + 1ULL) +
                   (ADDRESS_SPACE_END - KERNEL_SPACE_START + 1ULL) +
                   (USER_SPACE_END - USER_SPACE_START + 1ULL);
    u64 used_bytes = free_bytes <= capacity ? capacity - free_bytes : 0;

    printf("Virtual address allocator: self-test=[%s] free=",
           allocated && memory_ok && returned ? "OK" : "ERR");
    print_virtual_size(free_bytes);
    printf(" used=");
    print_virtual_size(used_bytes);
    printf(" capacity=");
    print_virtual_size(capacity);
    printf("\n");
}

static void print_buffer(const char *name, u64 start, u64 bytes) {
    u64 end = bytes == 0 ? start : start + bytes - 1ULL;
    printf("  %s 0x%lx-0x%lx (", name, start, end);
    print_size(bytes);
    printf(")\n");
}

void print_core_kernel_buffers(const BOOT_INFO64 *info64,
                               const BOOT_INFO32 *info32,
                               u64 bitmap_address,
                               const EFI_MEMORY_DESCRIPTOR *memory_map) {
    if (info64 == NULL || info32 == NULL || memory_map == NULL) {
        printf("Core kernel buffers: [ERR invalid boot information]\n");
        return;
    }

    u64 framebuffer_bytes = (u64)(u32)info32->vertical_resolution *
                            (u64)(u32)info32->pixels_per_scanline * 4ULL;
    u64 boot_info_bytes = info64->memory_map_size + (u64)(u32)info32->info_size;
    u64 va_metadata_bytes;
    u64 dma_metadata_bytes;

    spin_lock(&va_lock);
    va_metadata_bytes = va_header == NULL ? 0 :
        (u64)(va_metadata_pages + 1) * PAGE_SIZE;
    spin_unlock(&va_lock);

    spin_lock(&dma_lock);
    dma_metadata_bytes = dma_header == NULL ? 0 :
        (u64)(dma_metadata_pages + 1) * PAGE_SIZE;
    spin_unlock(&dma_lock);

    printf("Core kernel buffers:\n");
    print_buffer("kernel image", info64->kernel_start,
                 info64->kernel_end - info64->kernel_start);
    print_buffer("boot stack", bitmap_address - BOOT_STACK_SIZE, BOOT_STACK_SIZE);
    print_buffer("frame bitmap", bitmap_address, info64->bitmap_size);
    print_buffer("UEFI map/info", (u64)memory_map, boot_info_bytes);
    print_buffer("framebuffer", info64->framebuffer_base, framebuffer_bytes);
    print_buffer("PML4 root", (u64)KernelPML4, PAGE_SIZE);
    u64 page_table_pages =
        __atomic_load_n(&paging_allocated_pages, __ATOMIC_RELAXED);
    printf("  all page tables (non-contiguous) ");
    print_size(page_table_pages * PAGE_SIZE);
    printf(" (%lu pages)\n", page_table_pages);
    print_buffer("VA metadata", VA_TREE, va_metadata_bytes);
    print_buffer("DMA metadata", DMA_POOL, dma_metadata_bytes);
}

typedef enum {
    MAP_FREE,
    MAP_RESERVED,
    MAP_ACPI_NVS
} map_kind;

static map_kind descriptor_kind(const EFI_MEMORY_DESCRIPTOR *desc) {
    if (desc->Type == EfiConventionalMemory)
        return MAP_FREE;
    if (desc->Type == EfiACPIMemoryNVS)
        return MAP_ACPI_NVS;
    return MAP_RESERVED;
}

static const char *map_kind_name(map_kind kind) {
    if (kind == MAP_FREE)
        return "free";
    if (kind == MAP_ACPI_NVS)
        return "ACPI NVS";
    return "reserved";
}

static const EFI_MEMORY_DESCRIPTOR *descriptor_at(
    const EFI_MEMORY_DESCRIPTOR *memory_map, u64 descriptor_size, u64 index) {
    return (const EFI_MEMORY_DESCRIPTOR *)
        ((const u8 *)memory_map + descriptor_size * index);
}

static void print_physical_map(const BOOT_INFO64 *info64,
                               const EFI_MEMORY_DESCRIPTOR *memory_map) {
    u64 count = info64->memory_map_size / info64->descriptor_size;
    u64 emitted = 0;
    u64 cursor = 0;
    bool have_cursor = false;
    bool have_run = false;
    u64 run_start = 0;
    u64 run_end = 0;
    map_kind run_kind = MAP_RESERVED;

    while (emitted < count) {
        const EFI_MEMORY_DESCRIPTOR *next = NULL;
        for (u64 i = 0; i < count; i++) {
            const EFI_MEMORY_DESCRIPTOR *candidate =
                descriptor_at(memory_map, info64->descriptor_size, i);
            if (candidate->NumberOfPages == 0)
                continue;
            if (have_cursor && candidate->PhysicalStart < cursor)
                continue;
            if (next == NULL || candidate->PhysicalStart < next->PhysicalStart)
                next = candidate;
        }
        if (next == NULL)
            break;

        u64 start = next->PhysicalStart;
        u64 bytes = next->NumberOfPages * PAGE_SIZE;
        u64 end = start + bytes;
        map_kind kind = descriptor_kind(next);

        if (have_run && run_kind == kind && run_end == start) {
            run_end = end;
        } else {
            if (have_run) {
                printf("  0x%lx-0x%lx %s (", run_start, run_end - 1ULL,
                       map_kind_name(run_kind));
                print_size(run_end - run_start);
                printf(")\n");
            }
            run_start = start;
            run_end = end;
            run_kind = kind;
            have_run = true;
        }

        cursor = end;
        have_cursor = true;
        emitted++;
    }

    if (have_run) {
        printf("  0x%lx-0x%lx %s (", run_start, run_end - 1ULL,
               map_kind_name(run_kind));
        print_size(run_end - run_start);
        printf(")\n");
    }
}

static void print_virtual_map(const BOOT_INFO64 *info64,
                              const BOOT_INFO32 *info32,
                              u64 bitmap_address,
                              const EFI_MEMORY_DESCRIPTOR *memory_map) {
    u64 framebuffer_bytes = (u64)(u32)info32->vertical_resolution *
                            (u64)(u32)info32->pixels_per_scanline * 4ULL;
    u64 kernel_heap_start = (info64->kernel_end + PAGE_SIZE - 1) &
                            ~(PAGE_SIZE - 1);

    printf("Virtual layout:\n");
    printf("  0x%lx-0x%lx user allocator\n", USER_SPACE_START, USER_SPACE_END);
    printf("  0x%lx-0x%lx kernel image\n", info64->kernel_start,
           info64->kernel_end - 1ULL);
    printf("  0x%lx-0x%lx kernel VA allocator\n", kernel_heap_start,
           KERNEL_HEAP_END);
    printf("  0x%lx-0x%lx firmware/MMIO window\n", BASE,
           fb_virtual - 1ULL);
    printf("  0x%lx-0x%lx framebuffer\n", info64->framebuffer_base,
           info64->framebuffer_base + framebuffer_bytes - 1ULL);
    printf("  0x%lx PCI/ECAM window\n", PCI_BASE);
    printf("  0x%lx-0x%lx DMA data window\n", DMA_BASE, DMA_POOL - 1ULL);
    printf("  0x%lx DMA metadata\n", DMA_POOL);
    printf("  0x%lx VA metadata\n", VA_TREE);
    printf("  0x%lx-0x%lx high kernel VA allocator\n",
           KERNEL_SPACE_START, ADDRESS_SPACE_END);
    printf("  0x%lx frame bitmap; 0x%lx UEFI map/info\n",
           bitmap_address, (u64)memory_map);
}

void print_compressed_memory_map(const BOOT_INFO64 *info64,
                                 const BOOT_INFO32 *info32,
                                 u64 bitmap_address,
                                 const EFI_MEMORY_DESCRIPTOR *memory_map) {
    if (info64 == NULL || info32 == NULL || memory_map == NULL ||
        info64->descriptor_size < sizeof(EFI_MEMORY_DESCRIPTOR)) {
        printf("Memory map: [ERR invalid boot information]\n");
        return;
    }

    printf("Physical memory map (compressed):\n");
    print_physical_map(info64, memory_map);
    print_virtual_map(info64, info32, bitmap_address, memory_map);
}
