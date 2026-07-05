#ifndef MEMORY_H
#define MEMORY_H

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

u8 check_byte(u8 byte, u8 bit, u8 dir);
u8 set_bit(u8 byte, u8 bit, u8 value, u8 dir);

typedef struct {
    u64 code;
    u64 descriptorEntry;
    EFI_MEMORY_DESCRIPTOR *memory_map;
    EFI_PHYSICAL_ADDRESS physical_start;
    u32 Type;
    u64 Pagenum;
    u64 count;
} alloc_debug;

void allocator_init(u8 *bitmap, EFI_MEMORY_DESCRIPTOR *memory_map, u64 memory_map_size, u64 DescriptorSize, u64 kernel_start, u64 kernel_end, u64 bitmap_size);

#endif