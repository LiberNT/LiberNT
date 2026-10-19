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
static PVOID UefiRuntimeMapCopy;
static UINTN UefiRuntimeMapCopyCapacity;
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

    UefiRuntimeMapCopyCapacity = UefiMapCapacity;
    UefiRuntimeMapCopy = MmAllocateMemoryWithType(UefiRuntimeMapCopyCapacity, LoaderMemoryData);
    if (!UefiRuntimeMapCopy ||
        !MempSetupPaging((PFN_NUMBER)((ULONG_PTR)UefiRuntimeMapCopy >> EFI_PAGE_SHIFT),
                         EFI_SIZE_TO_PAGES(UefiRuntimeMapCopyCapacity),
                         TRUE))
    {
        UefiRuntimeMapCopy = NULL;
        UefiRuntimeMapCopyCapacity = 0;
    }

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

    if (UefiRuntimeMapCopy && MapSize <= UefiRuntimeMapCopyCapacity)
    {
        RtlCopyMemory(UefiRuntimeMapCopy, UefiMap, MapSize);
        Information->EfiMemoryMap = PaToVa(UefiRuntimeMapCopy);
        Information->EfiMemoryMapSize = (ULONG)MapSize;
        Information->EfiMemoryMapDescriptorSize = (ULONG)DescriptorSize;
    }
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

static
ULONG64
UefiHiberOverlap(
    _In_reads_(RangeCount) PLNT_HIBER_RANGE Ranges,
    _In_ ULONG64 RangeCount,
    _In_ ULONG64 BasePage,
    _In_ ULONG64 PageCount,
    _In_ BOOLEAN Claim)
{
    ULONG64 End = BasePage + PageCount;
    ULONG64 Low = 0, High = RangeCount, Middle;
    ULONG64 Start, Stop, Total = 0;
    EFI_PHYSICAL_ADDRESS Address;

    while (Low < High)
    {
        Middle = Low + (High - Low) / 2;
        if (Ranges[Middle].BasePage + Ranges[Middle].PageCount <= BasePage)
            Low = Middle + 1;
        else
            High = Middle;
    }

    for (; Low < RangeCount && Ranges[Low].BasePage < End; Low++)
    {
        Start = max(Ranges[Low].BasePage, BasePage);
        Stop = min(Ranges[Low].BasePage + Ranges[Low].PageCount, End);
        if (Start >= Stop)
            continue;

        if (Claim)
        {
            Address = Start << EFI_PAGE_SHIFT;
            if (EFI_ERROR(GlobalSystemTable->BootServices->AllocatePages(AllocateAddress,
                                                                         EfiLoaderData,
                                                                         Stop - Start,
                                                                         &Address)))
            {
                continue;
            }
            OslHiberMarkOwned(Start, Stop - Start);
        }

        Total += Stop - Start;
    }

    return Total;
}

static
BOOLEAN
UefiHiberIsRam(
    _In_ UINT32 Type)
{
    return (Type == EfiLoaderCode || Type == EfiLoaderData ||
            Type == EfiBootServicesCode || Type == EfiBootServicesData ||
            Type == EfiConventionalMemory);
}

BOOLEAN
FwHiberCheckMemoryMap(
    _In_ PLNT_HIBER_HEADER Header,
    _In_reads_(RangeCount) PLNT_HIBER_RANGE Ranges,
    _In_ ULONG64 RangeCount,
    _Out_ PULONG64 HighestPage)
{
    EFI_MEMORY_DESCRIPTOR* Entry;
    PLNT_HIBER_RUNTIME_RANGE Saved;
    UINTN MapKey, MapSize, DescriptorSize, i;
    ULONG64 BasePage, Covered = 0;
    ULONG Matched = 0, j;

    *HighestPage = 0;

    if (BootServicesExitedFlag || !UefiReadMemoryMap(TRUE, &MapKey, &MapSize, &DescriptorSize))
        return FALSE;

    for (i = 0; i < MapSize / DescriptorSize; i++)
    {
        Entry = UefiMapEntry(i, DescriptorSize);
        if (Entry->NumberOfPages == 0)
            continue;

        BasePage = Entry->PhysicalStart >> EFI_PAGE_SHIFT;

        if (Entry->Attribute & EFI_MEMORY_RUNTIME)
        {
            for (j = 0; j < Header->RuntimeRangeCount; j++)
            {
                Saved = &Header->RuntimeRanges[j];
                if (Saved->PhysicalStart == Entry->PhysicalStart &&
                    Saved->PageCount == Entry->NumberOfPages &&
                    Saved->Type == Entry->Type &&
                    Saved->Attribute == Entry->Attribute)
                {
                    break;
                }
            }

            if (j == Header->RuntimeRangeCount)
                return FALSE;
            Matched++;
        }

        if (!UefiHiberIsRam(Entry->Type))
        {
            if (UefiHiberOverlap(Ranges, RangeCount, BasePage, Entry->NumberOfPages, FALSE) != 0)
                return FALSE;
            continue;
        }

        Covered += UefiHiberOverlap(Ranges, RangeCount, BasePage, Entry->NumberOfPages, FALSE);
        if (BasePage + Entry->NumberOfPages > *HighestPage)
            *HighestPage = BasePage + Entry->NumberOfPages;
    }

    return Matched == Header->RuntimeRangeCount && Covered == Header->TargetPages;
}

VOID
FwHiberClaimFreeMemory(
    _In_reads_(RangeCount) PLNT_HIBER_RANGE Ranges,
    _In_ ULONG64 RangeCount)
{
    EFI_MEMORY_DESCRIPTOR* Entry;
    UINTN MapKey, MapSize, DescriptorSize, i;

    if (!UefiReadMemoryMap(TRUE, &MapKey, &MapSize, &DescriptorSize))
        return;

    for (i = 0; i < MapSize / DescriptorSize; i++)
    {
        Entry = UefiMapEntry(i, DescriptorSize);
        if (Entry->Type == EfiConventionalMemory && Entry->NumberOfPages != 0)
        {
            UefiHiberOverlap(Ranges, RangeCount, Entry->PhysicalStart >> EFI_PAGE_SHIFT,
                             Entry->NumberOfPages, TRUE);
        }
    }
}

VOID
FwHiberClaimPool(
    _In_ ULONG64 PageCount)
{
    EFI_MEMORY_DESCRIPTOR* Entry;
    EFI_PHYSICAL_ADDRESS Address;
    UINTN MapKey, MapSize, DescriptorSize, i;
    ULONG64 BasePage, Count;

    if (!UefiReadMemoryMap(TRUE, &MapKey, &MapSize, &DescriptorSize))
        return;

    for (i = 0; i < MapSize / DescriptorSize && PageCount != 0; i++)
    {
        Entry = UefiMapEntry(i, DescriptorSize);
        if (Entry->Type != EfiConventionalMemory)
            continue;

        BasePage = Entry->PhysicalStart >> EFI_PAGE_SHIFT;
        Count = Entry->NumberOfPages;
        if (BasePage == 0)
        {
            BasePage++;
            Count = Count ? Count - 1 : 0;
        }
        Count = min(Count, PageCount);
        if (Count == 0)
            continue;

        Address = BasePage << EFI_PAGE_SHIFT;
        if (EFI_ERROR(GlobalSystemTable->BootServices->AllocatePages(AllocateAddress, EfiLoaderData, Count, &Address)))
            continue;

        OslHiberAddPoolRun(BasePage, Count);
        PageCount -= Count;
    }
}

BOOLEAN
FwHiberExitBootServices(
    _In_ PLNT_HIBER_HEADER Header)
{
    EFI_MEMORY_DESCRIPTOR* Entry;
    UINTN i;
    ULONG j;

    UefiLeaveBootServices();

    for (i = 0; i < UefiExitMapSize / UefiExitDescriptorSize; i++)
    {
        Entry = UefiMapEntry(i, UefiExitDescriptorSize);
        if (!(Entry->Attribute & EFI_MEMORY_RUNTIME) || Entry->NumberOfPages == 0)
            continue;

        for (j = 0; j < Header->RuntimeRangeCount; j++)
        {
            if (Header->RuntimeRanges[j].PhysicalStart == Entry->PhysicalStart &&
                Header->RuntimeRanges[j].PageCount == Entry->NumberOfPages)
            {
                Entry->VirtualStart = Header->RuntimeRanges[j].VirtualStart;
                break;
            }
        }

        if (j == Header->RuntimeRangeCount)
            return FALSE;
    }

    if (Header->RuntimeRangeCount == 0)
        return TRUE;

    return !EFI_ERROR(GlobalSystemTable->RuntimeServices->SetVirtualAddressMap(UefiExitMapSize,
                                                                                UefiExitDescriptorSize,
                                                                                UefiMapDescriptorVersion,
                                                                                UefiMap));
}

VOID
UefiExitBootServices(VOID)
{
    UefiLeaveBootServices();
    FwSetVirtualAddressMap();
}
