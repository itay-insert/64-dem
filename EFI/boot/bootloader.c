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
        if (p_type == PT_LOAD) {
            if (p_memsz > p_filesz) {
                p_entry += (p_memsz + 4095) & ~4095;
            } else {
                p_entry += (p_filesz + 4095) & ~4095;
            }
        }
        p_header += e_phentsize;
    }
    return p_entry;
}

void handle_that(u64 p_offset, u64 p_filesz, VOID *buf, EFI_FILE_PROTOCOL *file) {
    if (p_offset-p_filesz <= 4095) {
        uefi_call_wrapper(
            file->SetPosition,
            2,
            file,
            p_offset
        );
        uefi_call_wrapper(
            file->Read,
            3,
            file,
            &size,
            file_buffer
        );
        memcpy(buf, file_buffer, 4095-p_offset);
        buf += 4095 - p_offset;
    }
    p_offset = p_offset >> 12;
    for (u64 i = 0; i < p_offset; i++) {
        uefi_call_wrapper(
            file->Read,
            3,
            file,
            &size,
            file_buffer
        );

        memcpy(buf, file_buffer, (size_t)p_filesz);
        buf += 4096;
    }
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
        if (p_type == PT_LOAD) {
            if (p_memsz > p_filesz) {
                char *fix_buf = buf;
                fix_buf = fix_buf + p_vaddr + p_filesz;
                for (u64 i = 0; i < (p_memsz-p_filesz); i++) {
                    fix_buf[i] = 0;
                }
            } 

            buf += p_vaddr;


            if (p_offset + p_filesz > 4095) {
                handle_that(p_offset+p_filesz, p_filesz, buf, file);
            } else {
                VOID *segment = (VOID *)(e.file_base+p_offset);
                memcpy(buf, segment, (size_t)p_filesz);
            }

            buf = buf - p_vaddr;

            if (p_memsz > p_filesz) {
                e.e_entry += (p_memsz + 4095) & ~4095;
            } else {
                e.e_entry += (p_filesz + 4095) & ~4095;
            }

        }
        p_header += e.e_phentsize;

    }
    return e.e_entry;
    
}


aloc_data elf_loader(u64 file_base, u64 p_entry, EFI_FILE_PROTOCOL *file) {
    aloc_data ad = {0, 0};
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
    
    u64 kernel_end = find_end(file_base, kernel_start);
    UINTN pages = kernel_end >> 12;

    EFI_PHYSICAL_ADDRESS addr = 0;

    uefi_call_wrapper(
        gBS->AllocatePages,
        4,
        AllocateAnyPages,
        pages,
        &addr
    );

    aloc_data ad = elf_loader(file_base, (u64)addr, file);

    uefi_call_wrapper(
        gop->SetMode,
        2,
        gop,
        mode_to_set
    );
    
    u32 *fb = (u32 *)gop->Mode->FrameBufferBase;

    fb[0] = 0xffffff;


    __asm__ __volatile__("cli");

    while (1) {
        __asm__ __volatile__("hlt");
    }

    return 0; // EFI_SUCCESS
}