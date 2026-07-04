#include <stdint.h>
#include <string.h>
#include <efi.h>
#include <efilib.h>

#define u8 uint8_t
#define u16 uint16_t
#define u32 uint32_t
#define u64 uint64_t

#define i8 int8_t
#define i16 int16_t
#define i32 int32_t
#define i64 int64_t 

#define PAGE_SIZE 4096
#define MIN_KERNEL_PHYS  0x100000ULL   // 1 MiB

static EFI_PHYSICAL_ADDRESS align_up(EFI_PHYSICAL_ADDRESS value, UINTN align)
{
    return (value + align - 1) & ~((EFI_PHYSICAL_ADDRESS)align - 1);
}

EFI_PHYSICAL_ADDRESS
FindFreeRegion(
    EFI_MEMORY_DESCRIPTOR *map,
    UINTN map_size,
    UINTN desc_size,
    UINTN pages_needed
)
{
     UINTN entries = map_size / desc_size;

    for (UINTN i = 0; i < entries; i++)
    {
        EFI_MEMORY_DESCRIPTOR *desc =
            (EFI_MEMORY_DESCRIPTOR *)((UINT8 *)map + i * desc_size);

        if (desc->Type != EfiConventionalMemory)
            continue;

        if (desc->NumberOfPages >= pages_needed)
        {
            return desc->PhysicalStart;
        }
    }

    return 1; // no space 
}


#define PT_LOAD 1

UINTN size = 4096;
UINT8 file_buffer[4096];

typedef struct {
    u64 file_base;
    u64 e_entry;
    u16 e_phentsize;
    u16 e_phnum;
} pt_data;

typedef struct {
    u64 entry;
    u64 end;
    u64 start;
} aloc_data;


u64 find_end(u64 file_base, u64 p_entry) {
    void *p_header = (void *)file_base;
    u64 e_phoff = *(u64 *)(p_header+0x20);
    u16 e_phentsize = *(u16 *)(p_header+0x36);
    u16 e_phnum = *(u16 *)(p_header+0x38);
    p_header += e_phoff;
    for (u16 i = 0; i < e_phnum; i++) {
        u32 p_type = *(u32 *)p_header;
        u64 p_filesz = *(u64 *)(p_header+0x20);
        u64 p_memsz = *(u64 *)(p_header+0x28);
        u64 p_align = *(u64 *)(p_header+0x30);
        if (p_type == PT_LOAD) {
            if (p_memsz > p_filesz) {
                if (p_align == 1 || p_align == 0) {
                    p_entry += p_memsz;
                } else {
                    p_entry += (p_memsz + (p_align-1)) & ~(p_align-1);
                }
            } else {
                if (p_align == 1 || p_align == 0) {
                    p_entry += p_filesz;
                } else {
                    p_entry += (p_filesz + (p_align-1)) & ~(p_align-1);
                }
            }
        }
        p_header += e_phentsize;
    }
    return p_entry;
}

void load_segment(u64 p_offset, u64 p_filesz, u64 p_align, VOID *buf, EFI_FILE_PROTOCOL *file) {
    u64 iterations;
    uefi_call_wrapper(
        file->SetPosition,
        2,
        file,
        p_offset
    );
    if (p_align == 0 || p_align == 1) {
        iterations = p_filesz;
        size = 1;
        p_align = 1;
    } else {
        iterations = (p_filesz + (p_align-1)) / p_align;
        size = p_align;
    }
    for (u64 i = 0; i < iterations; i++) {
        uefi_call_wrapper(
            file->Read,
            3,
            file,
            &size,
            file_buffer
        );

        if (p_filesz - (i * p_align) > p_align) {
            memcpy(buf, file_buffer, (size_t)p_align);
        } else {
            memcpy(buf, file_buffer, (size_t)(p_filesz - (i * p_align)));
        }
        
        buf += 4096;
    }

    size = 4096;

    uefi_call_wrapper(
        file->SetPosition,
        2,
        file,
        0
    );

    uefi_call_wrapper(
        file->Read,
        3,
        file,
        &size,
        file_buffer
    );

}


u64 elf_allocator(pt_data e, EFI_FILE_PROTOCOL *file) {
    VOID *buf = (VOID *)e.e_entry;
    void *p_header = (void *)e.file_base;
    for (u16 i = 0; i < e.e_phnum; i++) {
        u32 p_type = *(u32 *)p_header;
        u64 p_offset = *(u64 *)(p_header+0x08);
        u64 p_vaddr = *(u64 *)(p_header+0x10);
        u64 p_filesz = *(u64 *)(p_header+0x20);
        u64 p_memsz = *(u64 *)(p_header+0x28);
        u64 p_align = *(u64 *)(p_header+0x30);
        if (p_type == PT_LOAD) {
            if (p_memsz > p_filesz) {
                char *fix_buf = buf;
                fix_buf = fix_buf + p_vaddr + p_filesz;
                for (u64 i = 0; i < (p_memsz-p_filesz); i++) {
                    fix_buf[i] = 0;
                }
            } 

            buf += p_vaddr;

            load_segment(p_offset, p_filesz, p_align, buf, file);

            buf = buf - p_vaddr;

            if (p_memsz > p_filesz) {
                if (p_align == 1 || p_align == 0) {
                    e.e_entry += p_memsz;
                } else {
                    e.e_entry += (p_memsz + (p_align-1)) & ~(p_align-1);
                }
            } else {
                if (p_align == 1 || p_align == 0) {
                    e.e_entry += p_filesz;
                } else {
                    e.e_entry += (p_filesz + (p_align-1)) & ~(p_align-1);
                }
            }

        }
        p_header += e.e_phentsize;

    }
    return e.e_entry;
    
}


aloc_data elf_loader(u64 file_base, u64 p_entry, EFI_FILE_PROTOCOL *file) {
    aloc_data ad = {0, 0, 0};
    char *magic_ptr = (char *)file_base;
    if (*magic_ptr == 0x7f && magic_ptr[1] == 'E' &&
    magic_ptr[2] == 'L' && magic_ptr[3] == 'F') {
        void *p_header = (void *)file_base;
        u64 e_entry = *(u64 *)(p_header+0x18);
        e_entry = e_entry + p_entry;
        u64 e_phoff = *(u64 *)(p_header+0x20);
        u16 e_phentsize = *(u16 *)(p_header+0x36);
        u16 e_phnum = *(u16 *)(p_header+0x38);
        pt_data e = {file_base+e_phoff, p_entry, e_phentsize, e_phnum};
        ad.end = elf_allocator(e, file);
        ad.entry = e_entry;
        ad.start = p_entry;
    } 
    return ad;
}
// UEFI entry point
EFI_STATUS efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
    InitializeLib(ImageHandle, SystemTable);

    EFI_LOADED_IMAGE *LoadedImage;
    EFI_HANDLE device;
    EFI_DEVICE_PATH *path;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs;

/* Get Loaded Image Protocol */
    uefi_call_wrapper(
        BS->HandleProtocol,
        3,
        ImageHandle,
        &gEfiLoadedImageProtocolGuid,
        (void**)&LoadedImage
    );

/* Device handle (boot device) */
    device = LoadedImage->DeviceHandle;

/* Get device path */
    uefi_call_wrapper(
        BS->HandleProtocol,
        3,
        LoadedImage->DeviceHandle,
        &DevicePathProtocol,
        (VOID **)&path
    );

/* Get filesystem protocol */
    uefi_call_wrapper(
        BS->HandleProtocol,
        3,
        device,
        &FileSystemProtocol,
        (void**)&fs
    );

    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop;

    EFI_GUID gop_guid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;

    EFI_STATUS status = uefi_call_wrapper(
        ST->BootServices->LocateProtocol,
        3,
        &gop_guid,
        NULL,
        (void**)&gop
    );


    
    UINTN memory_map_size = 0;
    EFI_MEMORY_DESCRIPTOR *memory_map = NULL;
    UINTN map_key;
    UINTN descriptor_size;
    UINT32 descriptor_version;

/* First call just gets required size */
    status = uefi_call_wrapper(
        ST->BootServices->GetMemoryMap,
        5,
        &memory_map_size,
        memory_map,
        &map_key,
        &descriptor_size,
        &descriptor_version
    );

    if (status != EFI_BUFFER_TOO_SMALL)
    return status;


/* Add some extra room */
    memory_map_size += 2 * descriptor_size;

    status = uefi_call_wrapper(
        ST->BootServices->AllocatePool,
        3,
        EfiLoaderData,
        memory_map_size,
        (void**)&memory_map
    );

    if (EFI_ERROR(status))
    return status;

/* Second call gets the actual map */
    status = uefi_call_wrapper(
        ST->BootServices->GetMemoryMap,
        5,
        &memory_map_size,
        memory_map,
        &map_key,
        &descriptor_size,
        &descriptor_version
    );

    UINTN mpc = descriptor_size;
    UINTN mmpsz = memory_map_size;

    
    UINT8 *ptr = (UINT8 *)memory_map;
UINTN count = memory_map_size / descriptor_size;

EFI_PHYSICAL_ADDRESS highest_end = 0;

for (UINTN i = 0; i < count; i++) {
    EFI_MEMORY_DESCRIPTOR *desc =
        (EFI_MEMORY_DESCRIPTOR *)(ptr + i * descriptor_size);

    EFI_PHYSICAL_ADDRESS end =
        desc->PhysicalStart + desc->NumberOfPages * 4096;

    if (end > highest_end)
        highest_end = end;
}

// Number of pages needed to cover physical address space.
UINTN total_pages = (highest_end + 4095) / 4096;

// 1 bit per page.
UINTN bitmap_size = (total_pages + 7) / 8;

    if (EFI_ERROR(status))
    return status;

    UINTN mode_to_set = 0;
    int mode_res = 0; 
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *final_info;
    UINTN size_of_final_info;

    for (UINTN i = 0; i < gop->Mode->MaxMode; i++) {
        EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *info;
        UINTN size_of_info;

        status = uefi_call_wrapper(
            gop->QueryMode,
            4,
            gop,
            i,
            &size_of_info,
            &info
        );

        int cur_mode_res = info->HorizontalResolution * info->VerticalResolution;

        if (cur_mode_res > mode_res) {
            mode_res = cur_mode_res;
            mode_to_set = i;
        }
    }

    status = uefi_call_wrapper(
        gop->QueryMode,
        4,
        gop,
        mode_to_set,
        &size_of_final_info,
        &final_info
    );

    Print(L"reading kernel...\n");

    EFI_FILE_PROTOCOL *root;
    EFI_FILE_PROTOCOL *file;


    uefi_call_wrapper(  // find root
        fs->OpenVolume,
        2,
        fs,
        &root
    );


    uefi_call_wrapper( // open file from path
        root->Open,
        5,
        root,
        &file,
        L"boot\\kernel.elf",
        EFI_FILE_MODE_READ,
        0
    );

/* Read file */

    uefi_call_wrapper(
        file->Read,
        3,
        file,
        &size,
        file_buffer
    );

    
    u64 kernel_start = 0;
    u64 file_base = (u64)file_buffer;
    
    u64 buf_sz = 16;
    u64 buf_sz64 = 56;

    u64 kernel_end = find_end(file_base, kernel_start);
    UINTN pages = (kernel_end >> 12) + 65 + ((bitmap_size+mmpsz+buf_sz+buf_sz64 + 4095) / 4096);

    EFI_PHYSICAL_ADDRESS addr = FindFreeRegion(memory_map, memory_map_size, descriptor_size, pages);

    EFI_PHYSICAL_ADDRESS stack = addr + kernel_end + 0x41000; 

    EFI_MEMORY_DESCRIPTOR *MMP = (EFI_MEMORY_DESCRIPTOR *)(stack+bitmap_size);

    u64 *p_buff64 = (u64 *)((u64)MMP + memory_map_size);
    int *p_buff =  (int *)((u64)p_buff64 + buf_sz64);

    UINT8 *src = (UINT8 *)memory_map;
    UINT8 *dst = (UINT8 *)MMP;

    for (UINTN i = 0; i < memory_map_size; i++) {
        dst[i] = src[i];
    }

    aloc_data ad = elf_loader(file_base, (u64)addr, file);

    uefi_call_wrapper(
        gop->SetMode,
        2,
        gop,
        mode_to_set
    );
    
    u64 fb = (u64)gop->Mode->FrameBufferBase;
    
    typedef void (*kernel_entry_t)(u64 *info_buffer64, int *info_buffer, u64 stack_top, EFI_MEMORY_DESCRIPTOR *mmap);
    kernel_entry_t kernel_entry = (kernel_entry_t)(uintptr_t)ad.entry;


    switch (gop->Mode->Info->PixelFormat) {
        case PixelRedGreenBlueReserved8BitPerColor:
            p_buff[0] = 0;
            break;

        case PixelBlueGreenRedReserved8BitPerColor:
            p_buff[0] = 1;
            break;

        default:
            p_buff[0] = 0;
            break;
    }


    p_buff[1] = (int)final_info->HorizontalResolution;
    p_buff[2] = (int)final_info->VerticalResolution;
    p_buff[3] = (int)final_info->PixelsPerScanLine;


    p_buff64[0] = ad.entry;
    p_buff64[1] = ad.start;
    p_buff64[2] = (u64)p_buff+16;
    p_buff64[3] = fb;
    p_buff64[4] = (u64)mmpsz;
    p_buff64[5] = (u64)mpc;
    p_buff64[6] = (u64)bitmap_size;



    while (1) {
        uefi_call_wrapper(
            ST->BootServices->GetMemoryMap,
            5,
            &memory_map_size,
            memory_map,
            &map_key,
            &descriptor_size,
            &descriptor_version
        );

        status = uefi_call_wrapper(
            ST->BootServices->ExitBootServices,
            2,
            ImageHandle,
            map_key
        );

        if (status == EFI_SUCCESS)
        break;
    }

    __asm__ __volatile__("cli");


    kernel_entry(p_buff64, p_buff, (u64)stack, MMP);
    
    while (1) {
        __asm__ __volatile__("hlt");
    }

    return 0; // EFI_SUCCESS
}