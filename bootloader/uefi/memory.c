/*
 * PROJECT:     LiberNT Boot Loader
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     UEFI memory map and boot services exit
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <blefi.h>
#include "../winload/winload.h"

#define UEFI_MAP_SLACK_DESCRIPTORS 32
#define UEFI_MAP_RETRIES           4
#define UEFI_MAX_RUNTIME_RANGES    64

typedef struct _UEFI_RUNTIME_RANGE
{
    EFI_PHYSICAL_ADDRESS PhysicalStart;
    UINT64 PageCount;
    UINT64 VirtualStart;
} UEFI_RUNTIME_RANGE;

static EFI_MEMORY_DESCRIPTOR* UefiMap;
static UINTN UefiMapCapacity;
static UINT32 UefiMapDescriptorVersion;
static PFREELDR_MEMORY_DESCRIPTOR UefiLoaderMap;
static UINTN UefiLoaderMapCapacity;
static UEFI_RUNTIME_RANGE UefiRuntimeRanges[UEFI_MAX_RUNTIME_RANGES];
static ULONG UefiRuntimeRangeCount;
static PEFI_FIRMWARE_INFORMATION UefiRuntimeInformation;
static PVIRTUAL_EFI_RUNTIME_SERVICES UefiRuntimeServices;
static UINTN UefiExitMapSize;
static UINTN UefiExitDescriptorSize;

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
    EFI_STATUS Status;
    ULONG Retry;

    for (Retry = 0; Retry < UEFI_MAP_RETRIES; Retry++)
    {
        *MapSize = UefiMapCapacity;
        Status = BootServices->GetMemoryMap(MapSize,
                                            UefiMap,
                                            MapKey,
                                            DescriptorSize,
                                            &UefiMapDescriptorVersion);
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

static
EFI_MEMORY_DESCRIPTOR*
UefiMapEntry(
    _In_ UINTN Index,
    _In_ UINTN DescriptorSize)
{
    return (EFI_MEMORY_DESCRIPTOR*)((PUCHAR)UefiMap + Index * DescriptorSize);
}

BOOLEAN
FwPrepareRuntimeServices(
    _Inout_ PEFI_FIRMWARE_INFORMATION Information,
    _Inout_ PVIRTUAL_EFI_RUNTIME_SERVICES Services)
{
    EFI_MEMORY_DESCRIPTOR* Entry;
    UINTN MapKey, MapSize, DescriptorSize, i;
    UEFI_RUNTIME_RANGE* Range;
    BOOLEAN Device;

    UefiRuntimeInformation = NULL;
    UefiRuntimeServices = NULL;
    UefiRuntimeRangeCount = 0;

    Information->FirmwareVersion = GlobalSystemTable->Hdr.Revision;
    Information->VirtualEfiRuntimeServices = NULL;

    if (!UefiReadMemoryMap(TRUE, &MapKey, &MapSize, &DescriptorSize))
        return FALSE;

    for (i = 0; i < MapSize / DescriptorSize; i++)
    {
        Entry = UefiMapEntry(i, DescriptorSize);
        if (!(Entry->Attribute & EFI_MEMORY_RUNTIME) || Entry->NumberOfPages == 0)
            continue;

        if (UefiRuntimeRangeCount == UEFI_MAX_RUNTIME_RANGES)
            return FALSE;

        Range = &UefiRuntimeRanges[UefiRuntimeRangeCount];
        Device = (Entry->Type == EfiMemoryMappedIO || Entry->Type == EfiMemoryMappedIOPortSpace);
        if (!OslArchMapFirmwareRuntime(Entry->PhysicalStart,
                                       Entry->NumberOfPages,
                                       Device,
                                       Entry->Type == EfiRuntimeServicesCode,
                                       &Range->VirtualStart))
        {
            return FALSE;
        }

        Range->PhysicalStart = Entry->PhysicalStart;
        Range->PageCount = Entry->NumberOfPages;
        UefiRuntimeRangeCount++;
    }

    UefiRuntimeInformation = Information;
    UefiRuntimeServices = Services;
    return TRUE;
}

static
BOOLEAN
UefiAssignRuntimeAddress(
    _Inout_ EFI_MEMORY_DESCRIPTOR* Entry)
{
    UINT64 End = Entry->PhysicalStart + Entry->NumberOfPages * EFI_PAGE_SIZE;
    UEFI_RUNTIME_RANGE* Range;
    ULONG i;

    for (i = 0; i < UefiRuntimeRangeCount; i++)
    {
        Range = &UefiRuntimeRanges[i];
        if (Entry->PhysicalStart >= Range->PhysicalStart &&
            End <= Range->PhysicalStart + Range->PageCount * EFI_PAGE_SIZE)
        {
            Entry->VirtualStart = Range->VirtualStart + (Entry->PhysicalStart - Range->PhysicalStart);
            return TRUE;
        }
    }

    return FALSE;
}

static
VOID
UefiSetVirtualAddressMap(
    _In_ UINTN MapSize,
    _In_ UINTN DescriptorSize)
{
    PEFI_FIRMWARE_INFORMATION Information = UefiRuntimeInformation;
    PVIRTUAL_EFI_RUNTIME_SERVICES Services = UefiRuntimeServices;
    EFI_RUNTIME_SERVICES* Runtime = GlobalSystemTable->RuntimeServices;
    EFI_MEMORY_DESCRIPTOR* Entry;
    ULONG Missed = 0;
    EFI_STATUS Status;
    UINTN i;

    if (!Information || !Services)
        return;

    for (i = 0; i < MapSize / DescriptorSize; i++)
    {
        Entry = UefiMapEntry(i, DescriptorSize);
        if ((Entry->Attribute & EFI_MEMORY_RUNTIME) && !UefiAssignRuntimeAddress(Entry))
            Missed++;
    }

    Information->MissedMappingsCount = Missed;
    if (Missed != 0)
    {
        Information->SetVirtualAddressMapStatus = (ULONG)STATUS_UNSUCCESSFUL;
        return;
    }

    Status = Runtime->SetVirtualAddressMap(MapSize, DescriptorSize, UefiMapDescriptorVersion, UefiMap);
    if (EFI_ERROR(Status))
    {
        Information->SetVirtualAddressMapStatus = (ULONG)STATUS_UNSUCCESSFUL;
        return;
    }

    Services->GetTime = (ULONG_PTR)Runtime->GetTime;
    Services->SetTime = (ULONG_PTR)Runtime->SetTime;
    Services->GetWakeupTime = (ULONG_PTR)Runtime->GetWakeupTime;
    Services->SetWakeupTime = (ULONG_PTR)Runtime->SetWakeupTime;
    Services->SetVirtualAddressMap = (ULONG_PTR)Runtime->SetVirtualAddressMap;
    Services->ConvertPointer = (ULONG_PTR)Runtime->ConvertPointer;
    Services->GetVariable = (ULONG_PTR)Runtime->GetVariable;
    Services->GetNextVariableName = (ULONG_PTR)Runtime->GetNextVariableName;
    Services->SetVariable = (ULONG_PTR)Runtime->SetVariable;
    Services->GetNextHighMonotonicCount = (ULONG_PTR)Runtime->GetNextHighMonotonicCount;
    Services->ResetSystem = (ULONG_PTR)Runtime->ResetSystem;
    Services->UpdateCapsule = (ULONG_PTR)Runtime->UpdateCapsule;
    Services->QueryCapsuleCapabilities = (ULONG_PTR)Runtime->QueryCapsuleCapabilities;
    Services->QueryVariableInfo = (ULONG_PTR)Runtime->QueryVariableInfo;

    Information->SetVirtualAddressMapStatus = (ULONG)STATUS_SUCCESS;
    Information->VirtualEfiRuntimeServices = PaToVa(Services);
}

VOID
UefiLeaveBootServices(VOID)
{
    UINTN MapKey;
    BOOLEAN MayAllocate = TRUE;
    ULONG Retry;

    for (Retry = 0; Retry < UEFI_MAP_RETRIES; Retry++)
    {
        if (!UefiReadMemoryMap(MayAllocate, &MapKey, &UefiExitMapSize, &UefiExitDescriptorSize))
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

VOID
FwSetVirtualAddressMap(VOID)
{
    UefiSetVirtualAddressMap(UefiExitMapSize, UefiExitDescriptorSize);
}

VOID
UefiExitBootServices(VOID)
{
    UefiLeaveBootServices();
    FwSetVirtualAddressMap();
}
