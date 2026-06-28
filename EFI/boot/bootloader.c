#include <stdint.h>
#include <efi.h>
#include <efilib.h>

// The true UEFI Entry Point
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

    Print(L"reading file...\n");

    EFI_FILE_PROTOCOL *root;
    EFI_FILE_PROTOCOL *file;

/* Open filesystem root */
    uefi_call_wrapper(
        fs->OpenVolume,
        2,
        fs,
        &root
    );

/* Open file from root */
    uefi_call_wrapper(
        root->Open,
        5,
        root,
        &file,
        L"sign.bin",
        EFI_FILE_MODE_READ,
        0
    );

/* Read file */
    UINTN size = 32000;
    UINT8 file_buffer[32000];

    uefi_call_wrapper(
        file->Read,
        3,
        file,
        &size,
        file_buffer
    );

    if (file_buffer[0] == 'M' & file_buffer[1] == 'A') {
        Print(L"file read successfull!");
    } else {
        Print(L"file read failed");
    }
    
    
    __asm__ __volatile__("cli");

    while (1) {
        __asm__ __volatile__("hlt");
    }

    return 0; // EFI_SUCCESS
}