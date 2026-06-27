#include <stdint.h>
#include <efi.h>
#include <efilib.h>

// The true UEFI Entry Point
EFI_STATUS efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
    InitializeLib(ImageHandle, SystemTable);

    EFI_STATUS status;

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

    EFI_MEMORY_DESCRIPTOR *desc = memory_map;

    const CHAR16 *MemTypeToStr(UINT32 type)
    {
        switch (type)
        {
            case 0:  return L"Reserved";
            case 1:  return L"LoaderCode";
            case 2:  return L"LoaderData"; 
            case 3:  return L"BootSC";  // bootServices Code
            case 4:  return L"BootSD";  // bootServices Data
            case 5:  return L"RtCode"; // Runtime Code
            case 6:  return L"RtData"; // Runtime Data
            case 7:  return L"Ram"; // usable memory
            case 8:  return L"Broken"; // unusable memory
            case 9:  return L"ACPIRC";  // acpi reclaim
            case 10: return L"ACPINVS";
            case 11: return L"MMIO";
            case 12: return L"MMIOPS"; // MMIO Port Space
            case 13: return L"PalCode";
            default: return L"Unknown";
        }
    }

    const CHAR16 *typetostr(UINT32 type)
    {
        switch (type) 
        {
            case 8: return L"Ram";
            default: return L"Reserved";
        }
    }

    int count = 0;

    for (
        UINTN offset = 0;
        offset < memory_map_size;
        offset += descriptor_size
    )   {
        desc = (EFI_MEMORY_DESCRIPTOR *)((UINT8 *)memory_map + offset);

        if (count == 5) {
            count = 0;
            Print(L"\n");
        } else count++;
        

        Print(L"Tp=%s", MemTypeToStr(desc->Type));

        Print(
            L" Pgs=%lu  St=%lx       ",
            desc->NumberOfPages,
            desc->PhysicalStart
        );
    }

    __asm__ __volatile__("cli");

    while (1) {
        __asm__ __volatile__("hlt");
    }

    return 0; // EFI_SUCCESS
}