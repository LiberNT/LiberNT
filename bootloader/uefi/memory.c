/*
 * PROJECT:     LiberNT Boot Loader
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     UEFI memory map and boot services exit
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <blefi.h>

#define UEFI_MAP_SLACK_DESCRIPTORS 32
#define UEFI_MAP_RETRIES           4

static EFI_MEMORY_DESCRIPTOR* UefiMap;
static UINTN UefiMapCapacity;
static PFREELDR_MEMORY_DESCRIPTOR UefiLoaderMap;
static UINTN UefiLoaderMapCapacity;

static
TYPE_OF_MEMORY
UefiConvertMemoryType(
    _In_ UINT32 Type)
{
    switch (Type)
    {
        case EfiLoaderCode:
        case EfiLoaderData:
            return LoaderLoadedProgram;

        case EfiBootServicesCode:
        case EfiBootServicesData:
            return LoaderFirmwareTemporary;

        case EfiRuntimeServicesCode:
        case EfiRuntimeServicesData:
            return LoaderFirmwarePermanent;

        case EfiConventionalMemory:
            return LoaderFree;

        case EfiUnusableMemory:
            return LoaderBad;

        case EfiACPIReclaimMemory:
        case EfiACPIMemoryNVS:
            return LoaderSpecialMemory;
    }

    return LoaderReserve;
}

static
BOOLEAN
UefiReadMemoryMap(
    _In_ BOOLEAN MayAllocate,
    _Out_ UINTN* MapKey,
    _Out_ UINTN* MapSize,
    _Out_ UINTN* DescriptorSize)
{
    EFI_BOOT_SERVICES* BootServices = GlobalSystemTable->BootServices;
    UINT32 DescriptorVersion;
    EFI_STATUS Status;
    ULONG Retry;

    for (Retry = 0; Retry < UEFI_MAP_RETRIES; Retry++)
    {
        *MapSize = UefiMapCapacity;
        Status = BootServices->GetMemoryMap(MapSize,
                                            UefiMap,
                                            MapKey,
                                            DescriptorSize,
                                            &DescriptorVersion);
        if (!EFI_ERROR(Status))
            return *DescriptorSize >= sizeof(EFI_MEMORY_DESCRIPTOR);

        if (Status != EFI_BUFFER_TOO_SMALL || !MayAllocate)
            return FALSE;

        if (UefiMap)
            BootServices->FreePool(UefiMap);

        UefiMapCapacity = *MapSize + UEFI_MAP_SLACK_DESCRIPTORS * *DescriptorSize;
        if (EFI_ERROR(BootServices->AllocatePool(EfiLoaderData, UefiMapCapacity, (VOID**)&UefiMap)))
        {
            UefiMap = NULL;
            UefiMapCapacity = 0;
            return FALSE;
        }
    }

    return FALSE;
}

FREELDR_MEMORY_DESCRIPTOR*
UefiMemGetMemoryMap(
    PULONG MaxMemoryMapSize)
{
    EFI_BOOT_SERVICES* BootServices;
    EFI_MEMORY_DESCRIPTOR* Entry;
    UINTN MapKey, MapSize, DescriptorSize, Count, i;
    ULONG Retry;

    *MaxMemoryMapSize = 0;

    if (BootServicesExitedFlag)
        return NULL;

    BootServices = GlobalSystemTable->BootServices;

    for (Retry = 0; Retry < UEFI_MAP_RETRIES; Retry++)
    {
        if (!UefiReadMemoryMap(TRUE, &MapKey, &MapSize, &DescriptorSize))
            return NULL;

        Count = MapSize / DescriptorSize;
        if (Count <= UefiLoaderMapCapacity)
            break;

        if (UefiLoaderMap)
            BootServices->FreePool(UefiLoaderMap);

        UefiLoaderMapCapacity = Count + UEFI_MAP_SLACK_DESCRIPTORS;
        if (EFI_ERROR(BootServices->AllocatePool(EfiLoaderData,
                                                 UefiLoaderMapCapacity * sizeof(*UefiLoaderMap),
                                                 (VOID**)&UefiLoaderMap)))
        {
            UefiLoaderMap = NULL;
            UefiLoaderMapCapacity = 0;
            return NULL;
        }
    }

    if (Retry == UEFI_MAP_RETRIES)
        return NULL;

    Entry = UefiMap;
    for (i = 0; i < Count; i++)
    {
        UefiLoaderMap[i].MemoryType = UefiConvertMemoryType(Entry->Type);
        UefiLoaderMap[i].BasePage = (PFN_NUMBER)(Entry->PhysicalStart >> EFI_PAGE_SHIFT);
        UefiLoaderMap[i].PageCount = (PFN_NUMBER)Entry->NumberOfPages;
        Entry = (EFI_MEMORY_DESCRIPTOR*)((PUCHAR)Entry + DescriptorSize);
    }

    *MaxMemoryMapSize = (ULONG)Count;
    return UefiLoaderMap;
}

FREELDR_MEMORY_DESCRIPTOR*
FwGetMemoryMap(
    _Out_ PULONG Count)
{
    return UefiMemGetMemoryMap(Count);
}

VOID
UefiExitBootServices(VOID)
{
    UINTN MapKey, MapSize, DescriptorSize;
    BOOLEAN MayAllocate = TRUE;
    ULONG Retry;

    for (Retry = 0; Retry < UEFI_MAP_RETRIES; Retry++)
    {
        if (!UefiReadMemoryMap(MayAllocate, &MapKey, &MapSize, &DescriptorSize))
            break;

        MayAllocate = FALSE;

        if (!EFI_ERROR(GlobalSystemTable->BootServices->ExitBootServices(GlobalImageHandle, MapKey)))
        {
            BootServicesExitedFlag = TRUE;
            return;
        }
    }

    FwHalt();
}
