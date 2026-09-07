#define _GNU_SOURCE
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

#include "x86-64/memory/va_alloc_internal.h"
#include "x86-64/memory/virtual_allocator.h"

#define PAGE_SIZE UINT64_C(4096)
#define USER_SPACE UINT64_C(0x40000000)
#define KERNEL_REGION_END UINT64_C(0xffff8fffffffffff)

u64 Kernel_end;

va_ret va_alloc(u64 pages, u16 attributes);

static int vmalloc_calls;
static int fail_vmalloc_call;
static u64 requested_address[8];
static u64 requested_pages[8];
static u16 requested_attributes[8];

EFI_MEMORY_DESCRIPTOR vmalloc(u64 address, u64 pages, u16 attributes) {
    vmalloc_calls++;
    assert(vmalloc_calls <= (int)(sizeof(requested_address) /
                                  sizeof(requested_address[0])));
    requested_address[vmalloc_calls - 1] = address;
    requested_pages[vmalloc_calls - 1] = pages;
    requested_attributes[vmalloc_calls - 1] = attributes;

    EFI_MEMORY_DESCRIPTOR result = {0};
    if (vmalloc_calls == fail_vmalloc_call) {
        result.Attribute = 1;
        return result;
    }
    result.VirtualStart = address;
    result.NumberOfPages = pages;
    return result;
}

void vfree(EFI_MEMORY_DESCRIPTOR allocation) {
    (void)allocation;
}

static void reset_allocator(void) {
    memset((void *)(uintptr_t)VA_TREE, 0, PAGE_SIZE * 2);
    entries = 0;
    va_header = NULL;
    va_kstart = NULL;
    va_ustart = NULL;
    va_latest = NULL;
    limit = 0;
    metadata_pages = 0;
    va_top = VA_TREE;
    spinlock_init(&va_lock);
    vmalloc_calls = 0;
    fail_vmalloc_call = 0;
    memset(requested_address, 0, sizeof(requested_address));
    memset(requested_pages, 0, sizeof(requested_pages));
    memset(requested_attributes, 0, sizeof(requested_attributes));
    Kernel_end = UINT64_C(0xffff800000123456);
}

static void test_zero_pages_is_rejected_without_side_effects(void) {
    reset_allocator();
    va_ret result = va_alloc(0, 0x03);
    assert(result.status != 0);
    assert(vmalloc_calls == 0);
    assert(va_header == NULL);
    assert(va_lock.locked == 0);
}

static void test_first_kernel_allocation_initializes_and_consumes_range(void) {
    reset_allocator();
    const u64 expected_base = (Kernel_end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    const u64 expected_initial_length = KERNEL_REGION_END - expected_base + 1;
    va_ret result = va_alloc(3, 0x03);

    assert(result.status == 0);
    assert(result.base == expected_base);
    assert(result.pages == 3);
    assert(result.attributes == 0x03);
    assert(vmalloc_calls == 2);
    assert(requested_address[0] == VA_TREE);
    assert(requested_pages[0] == 1);
    assert(requested_address[1] == expected_base);
    assert(requested_pages[1] == 3);
    assert(va_kstart->virtual_base == expected_base + 3 * PAGE_SIZE);
    assert(va_kstart->length == expected_initial_length - 3 * PAGE_SIZE);
    assert(va_kstart->max_length >= va_kstart->length);
    assert(va_lock.locked == 0);
}

static void test_user_attribute_selects_user_space(void) {
    reset_allocator();
    va_ret result = va_alloc(2, 0x07);
    assert(result.status == 0);
    assert(result.base == USER_SPACE);
    assert(requested_address[1] == USER_SPACE);
    assert(va_ustart->virtual_base == USER_SPACE + 2 * PAGE_SIZE);
    assert(va_lock.locked == 0);
}

static void test_backing_allocation_failure_does_not_consume_range(void) {
    reset_allocator();
    fail_vmalloc_call = 2;
    va_ret result = va_alloc(4, 0x03);
    const u64 initial_base = (Kernel_end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    assert(result.status != 0);
    assert(va_kstart->virtual_base == initial_base);
    assert(va_lock.locked == 0);
}

static void test_metadata_allocation_failure_is_reported(void) {
    reset_allocator();
    fail_vmalloc_call = 1;
    va_ret result = va_alloc(1, 0x03);
    assert(result.status != 0);
    assert(va_header == NULL);
    assert(entries == 0);
    assert(va_lock.locked == 0);
}

int main(void) {
    assert(mmap((void *)(uintptr_t)VA_TREE, PAGE_SIZE * 2,
                PROT_READ | PROT_WRITE,
                MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE,
                -1, 0) != MAP_FAILED);

    test_zero_pages_is_rejected_without_side_effects();
    test_first_kernel_allocation_initializes_and_consumes_range();
    test_user_attribute_selects_user_space();
    test_backing_allocation_failure_does_not_consume_range();
    test_metadata_allocation_failure_is_reported();
    puts("virtual-address allocator tests passed");
    return 0;
}
