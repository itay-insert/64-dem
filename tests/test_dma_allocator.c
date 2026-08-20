#define _GNU_SOURCE
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>

#ifndef DMA_BASE
#define DMA_BASE UINT64_C(0x100000000)
#endif
#ifndef DMA_POOL
#define DMA_POOL UINT64_C(0x200000000)
#endif
#define TEST_REGION_SIZE (32UL * 1024 * 1024)

#include "x86-64/memory/memory_helpers.h"
#include "x86-64/memory/frame_allocator.h"
#include "x86-64/memory/dma_internal.h"
#include "x86-64/memory/dma.h"

u64 *KernelPML4;

typedef struct {
    u64 virtual_address;
    u64 physical_address;
} test_mapping;

static test_mapping mappings[TEST_REGION_SIZE / 4096 * 2];
static size_t mapping_count;

void create_mapping(u64 virtual_address, u64 physical_address, u64 pages,
                    u16 attributes, u64 *pml4) {
    (void)virtual_address;
    (void)physical_address;
    (void)pages;
    (void)attributes;
    (void)pml4;
    for (u64 page = 0; page < pages; page++) {
        assert(mapping_count < sizeof(mappings) / sizeof(mappings[0]));
        mappings[mapping_count].virtual_address = virtual_address + page * 4096;
        mappings[mapping_count].physical_address = physical_address + page * 4096;
        mapping_count++;
    }
}

void flush_pages(u64 virtual_address, u64 pages) {
    (void)virtual_address;
    (void)pages;
}

PAGING_LOOKUP_DESCRIPTOR paging_lookup(u64 virtual_address, u64 *pml4) {
    (void)pml4;
    PAGING_LOOKUP_DESCRIPTOR result = {0};
    u64 page_address = virtual_address & ~UINT64_C(4095);
    for (size_t i = mapping_count; i > 0; i--) {
        if (mappings[i - 1].virtual_address == page_address) {
            result.physical_address = mappings[i - 1].physical_address |
                                      (virtual_address & 4095);
            return result;
        }
    }
    result.status = 1;
    result.Page_Type = 0;
    return result;
}

static void reset_allocator(void) {
    static u8 bitmap[TEST_REGION_SIZE / 4096 / 8];
    memset(bitmap, 0, sizeof(bitmap));
    bitmap_base = (u64)bitmap;
    bitmapSize = sizeof(bitmap);
    former_count = 0;
    entries = 0;
    dma_header = NULL;
    dma_start = NULL;
    dma_latest = NULL;
    limit = 0;
    metadata_pages = 0;
    dma_top = DMA_POOL;
    mapping_count = 0;
    memset((void *)DMA_BASE, 0, TEST_REGION_SIZE);
    memset((void *)DMA_POOL, 0, TEST_REGION_SIZE);
}

static void test_small_allocation_reserves_descriptor_space(void) {
    reset_allocator();
    dma_ret allocation = allocate_dma(4096);
    assert(allocation.status == 0);
    assert(allocation.virtual_address == DMA_BASE);
    assert(allocation.SizeInPages == 2);
    assert(dma_start->SizeInPages == 2);
    dma_descriptor *descriptor = (dma_descriptor *)(allocation.virtual_address + 8192 - sizeof(*descriptor));
    assert(memcmp(descriptor->sign, "DMA_POOL", 8) == 0);
    assert(descriptor->SizeInPages == 2);
}

static void test_allocations_are_page_aligned_and_non_overlapping(void) {
    reset_allocator();
    dma_ret first = allocate_dma(1);
    dma_ret second = allocate_dma(6000);
    assert(first.status == 0 && second.status == 0);
    assert((first.virtual_address & 4095) == 0);
    assert((second.virtual_address & 4095) == 0);
    assert(second.virtual_address == first.virtual_address + 4096);
}

static void test_free_reuses_exact_hole(void) {
    reset_allocator();
    dma_ret first = allocate_dma(100);
    dma_ret second = allocate_dma(100);
    u64 first_physical = first.physical_address;
    free_dma(first.virtual_address);
    dma_ret replacement = allocate_dma(100);
    assert(replacement.status == 0);
    assert(replacement.virtual_address == first.virtual_address);
    assert(second.virtual_address != replacement.virtual_address);
    assert(replacement.physical_address == first_physical);
}

static void test_adjacent_free_extents_satisfy_larger_request(void) {
    reset_allocator();
    dma_ret first = allocate_dma(100);
    dma_ret second = allocate_dma(100);
    (void)allocate_dma(100);
    free_dma(first.virtual_address);
    free_dma(second.virtual_address);
    dma_ret replacement = allocate_dma(5000);
    assert(replacement.status == 0);
    assert(replacement.virtual_address == first.virtual_address);
}

static void test_free_releases_every_backing_page(void) {
    reset_allocator();
    dma_ret allocation = allocate_dma(5000);
    assert(allocation.status == 0);
    PAGING_LOOKUP_DESCRIPTOR first = paging_lookup(allocation.virtual_address, NULL);
    PAGING_LOOKUP_DESCRIPTOR second = paging_lookup(allocation.virtual_address + 4096, NULL);
    assert(second.physical_address == first.physical_address + 4096);
    assert(check_byte(((u8 *)bitmap_base)[first.physical_address >> 15],
                      (first.physical_address >> 12) & 7, 1) == 1);
    assert(check_byte(((u8 *)bitmap_base)[second.physical_address >> 15],
                      (second.physical_address >> 12) & 7, 1) == 1);
    free_dma(allocation.virtual_address);
    assert(check_byte(((u8 *)bitmap_base)[first.physical_address >> 15],
                      (first.physical_address >> 12) & 7, 1) == 0);
    assert(check_byte(((u8 *)bitmap_base)[second.physical_address >> 15],
                      (second.physical_address >> 12) & 7, 1) == 0);
}

static void test_dma_mapping_is_physically_contiguous(void) {
    reset_allocator();
    dma_ret allocation = allocate_dma(9000);
    assert(allocation.status == 0);
    assert(allocation.SizeInPages == 3);
    PAGING_LOOKUP_DESCRIPTOR first = paging_lookup(allocation.virtual_address, NULL);
    assert(allocation.physical_address == first.physical_address);
    assert(first.status == 0);
    for (u64 page = 1; page < 3; page++) {
        PAGING_LOOKUP_DESCRIPTOR current =
            paging_lookup(allocation.virtual_address + page * 4096, NULL);
        assert(current.status == 0);
        assert(current.physical_address == first.physical_address + page * 4096);
    }
    assert(first.physical_address != allocation.virtual_address);
}

static void test_metadata_grows_past_one_page(void) {
    reset_allocator();
    dma_ret allocations[300];
    for (size_t i = 0; i < 300; i++) {
        allocations[i] = allocate_dma(1);
        if (allocations[i].status != 0 ||
            allocations[i].virtual_address != DMA_BASE + i * 4096) {
            fprintf(stderr, "allocation %zu returned %#llx, expected %#llx\n", i,
                    (unsigned long long)allocations[i].virtual_address,
                    (unsigned long long)(DMA_BASE + i * 4096));
            abort();
        }
    }
    assert(metadata_pages == 1);
    for (size_t i = 0; i < 300; i++) free_dma(allocations[i].virtual_address);
    dma_ret replacement = allocate_dma(1);
    assert(replacement.status == 0);
    assert(replacement.virtual_address == DMA_BASE);
}

int main(void) {
    assert(mmap((void *)DMA_BASE, TEST_REGION_SIZE, PROT_READ | PROT_WRITE,
                MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0) != MAP_FAILED);
    assert(mmap((void *)DMA_POOL, TEST_REGION_SIZE, PROT_READ | PROT_WRITE,
                MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0) != MAP_FAILED);

    test_small_allocation_reserves_descriptor_space();
    test_allocations_are_page_aligned_and_non_overlapping();
    test_free_reuses_exact_hole();
    test_adjacent_free_extents_satisfy_larger_request();
    test_free_releases_every_backing_page();
    test_dma_mapping_is_physically_contiguous();
    test_metadata_grows_past_one_page();
    puts("DMA allocator tests passed");
    return 0;
}
