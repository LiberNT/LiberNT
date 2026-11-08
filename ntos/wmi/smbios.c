/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         GPL - See COPYING in the top level directory
 * FILE:            ntoskrnl/wmi/smbios.c
 * PURPOSE:         I/O Windows Management Instrumentation (WMI) Support
 * PROGRAMMERS:     Alex Ionescu (alex.ionescu@reactos.org)
 */

/* INCLUDES *****************************************************************/

#include <ntoskrnl.h>
#include <wmiguid.h>
#include <wmidata.h>
#include <wmistr.h>

#include "wmip.h"

#define NDEBUG
#include <debug.h>


/* FUNCTIONS *****************************************************************/

typedef struct _SMBIOS21_ENTRY_POINT
{
 	CHAR AnchorString[4];
 	UCHAR Checksum;
 	UCHAR Length;
 	UCHAR MajorVersion;
 	UCHAR MinorVersion;
 	USHORT MaxStructureSize;
 	UCHAR EntryPointRevision;
 	CHAR FormattedArea[5];
 	CHAR AnchorString2[5];
 	UCHAR Checksum2;
 	USHORT TableLength;
 	ULONG TableAddress;
 	USHORT NumberOfStructures;
 	UCHAR BCDRevision;
} SMBIOS21_ENTRY_POINT, *PSMBIOS21_ENTRY_POINT;

typedef struct _SMBIOS30_ENTRY_POINT
{
 	CHAR AnchorString[5];
 	UCHAR Checksum;
 	UCHAR Length;
 	UCHAR MajorVersion;
 	UCHAR MinorVersion;
 	UCHAR Docref;
    UCHAR Revision;
    UCHAR Reserved;
    ULONG TableMaxSize;
    ULONG64 TableAddress;
} SMBIOS30_ENTRY_POINT, *PSMBIOS30_ENTRY_POINT;

static
BOOLEAN
GetEntryPointData(
    _In_ const UCHAR *EntryPointAddress,
    _Out_ PULONG64 TableAddress,
    _Out_ PULONG TableSize,
    _Out_ PMSSmBios_RawSMBiosTables BiosTablesHeader)
{
    PSMBIOS21_ENTRY_POINT EntryPoint21;
    PSMBIOS30_ENTRY_POINT EntryPoint30;
    UCHAR Checksum;
    ULONG i;

    /* Check for SMBIOS 2.1 entry point */
    EntryPoint21 = (PSMBIOS21_ENTRY_POINT)EntryPointAddress;
    if (RtlEqualMemory(EntryPoint21->AnchorString, "_SM_", 4))
    {
        if (EntryPoint21->Length < FIELD_OFFSET(SMBIOS21_ENTRY_POINT, BCDRevision) + sizeof(UCHAR) ||
            EntryPoint21->Length > 32)
            return FALSE;

        /* Calculate the checksum */
        Checksum = 0;
        for (i = 0; i < EntryPoint21->Length; i++)
        {
            Checksum += EntryPointAddress[i];
        }

        if (Checksum != 0)
            return FALSE;

        *TableAddress = EntryPoint21->TableAddress;
        *TableSize = EntryPoint21->TableLength;
        BiosTablesHeader->Used20CallingMethod = 0;
        BiosTablesHeader->SmbiosMajorVersion = EntryPoint21->MajorVersion;
        BiosTablesHeader->SmbiosMinorVersion = EntryPoint21->MinorVersion;
        BiosTablesHeader->DmiRevision = EntryPoint21->BCDRevision;
        BiosTablesHeader->Size = EntryPoint21->TableLength;
        return TRUE;
    }

    /* Check for SMBIOS 3.0 entry point */
    EntryPoint30 = (PSMBIOS30_ENTRY_POINT)EntryPointAddress;
    if (RtlEqualMemory(EntryPoint30->AnchorString, "_SM3_", 5))
    {
        if (EntryPoint30->Length < sizeof(SMBIOS30_ENTRY_POINT) ||
            EntryPoint30->Length > 32)
            return FALSE;

        /* Calculate the checksum */
        Checksum = 0;
        for (i = 0; i < EntryPoint30->Length; i++)
        {
            Checksum += EntryPointAddress[i];
        }

        if (Checksum != 0)
            return FALSE;

        *TableAddress = EntryPoint30->TableAddress;
        *TableSize = EntryPoint30->TableMaxSize;
        BiosTablesHeader->Used20CallingMethod = 0;
        BiosTablesHeader->SmbiosMajorVersion = EntryPoint30->MajorVersion;
        BiosTablesHeader->SmbiosMinorVersion = EntryPoint30->MinorVersion;
        BiosTablesHeader->DmiRevision = 3;
        BiosTablesHeader->Size = EntryPoint30->TableMaxSize;
        return TRUE;
    }

    return FALSE;
}

/* SMBIOS entry point metadata captured from the loader block at WMI init:
 * UEFI systems have no 0xF0000 BIOS range to scan. */
static MSSmBios_RawSMBiosTables WmipSMBiosHeader;
static ULONG64 WmipSMBiosTableAddress;
static ULONG WmipSMBiosTableSize;

VOID
NTAPI
WmipCaptureSMBiosFromLoader(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PHYSICAL_ADDRESS Phys;
    PUCHAR Mapping;
    MSSmBios_RawSMBiosTables Header;
    ULONG64 TableAddress;
    ULONG TableSize;

    if (LoaderBlock == NULL ||
        LoaderBlock->Extension == NULL ||
        LoaderBlock->Extension->SMBiosEPSHeader == NULL)
    {
        return;
    }

    /* The loader stores the identity-mapped physical address of the EPS. */
    Phys.QuadPart = (ULONGLONG)(ULONG_PTR)LoaderBlock->Extension->SMBiosEPSHeader;
    Mapping = MmMapIoSpace(Phys, 32, MmCached);
    if (Mapping == NULL)
        return;

    /* Use the same parser as the legacy BIOS scan for both entry-point formats. */
    RtlZeroMemory(&Header, sizeof(Header));
    if (GetEntryPointData(Mapping, &TableAddress, &TableSize, &Header) &&
        TableAddress != 0 && TableSize != 0)
    {
        WmipSMBiosHeader = Header;
        WmipSMBiosTableAddress = TableAddress;
        WmipSMBiosTableSize = TableSize;
    }

    MmUnmapIoSpace(Mapping, 32);
}

_At_(*OutTableData, __drv_allocatesMem(Mem))
NTSTATUS
NTAPI
WmipGetRawSMBiosTableData(
    _Outptr_opt_result_buffer_(*OutDataSize) PVOID *OutTableData,
    _Out_ PULONG OutDataSize)
{
    static const SIZE_T SearchSize = 0x10000;
    static const ULONG HeaderSize = FIELD_OFFSET(MSSmBios_RawSMBiosTables, SMBiosData);
    PHYSICAL_ADDRESS PhysicalAddress;
    PUCHAR EntryPointMapping;
    MSSmBios_RawSMBiosTables BiosTablesHeader;
    PVOID BiosTables, TableMapping;
    ULONG Offset, TableSize;
    ULONG64 TableAddress = 0;

    if (WmipSMBiosTableAddress != 0)
    {
        /* Entry point handed over by the loader (UEFI firmware). */
        TableAddress = WmipSMBiosTableAddress;
        TableSize = WmipSMBiosTableSize;
        BiosTablesHeader = WmipSMBiosHeader;
    }
    else if (ExpFirmwareType != FirmwareTypeUefi)
    {
    /* This is where the range for the entry point starts */
    PhysicalAddress.QuadPart = 0xF0000;

    /* Map the range into the system address space */
    EntryPointMapping = MmMapIoSpace(PhysicalAddress, SearchSize, MmCached);
    if (EntryPointMapping == NULL)
    {
        DPRINT1("Failed to map range for SMBIOS entry point\n");
        return STATUS_UNSUCCESSFUL;
    }

    /* Loop the table memory in 16 byte steps */
    for (Offset = 0; Offset <= (0x10000 - 32); Offset += 16)
    {
        /* Check if we have an entry point here and get it's data */
        if (GetEntryPointData(EntryPointMapping + Offset,
                              &TableAddress,
                              &TableSize,
                              &BiosTablesHeader))
        {
            break;
        }
    }

    /* Unmap the entry point */
    MmUnmapIoSpace(EntryPointMapping, SearchSize);
    }

    /* Did we find anything */
    if (TableAddress == 0)
    {
        DPRINT1("Could not find the SMBIOS entry point\n");
        return STATUS_NOT_FOUND;
    }

    /* Check if the caller asked for the buffer */
    if (OutTableData != NULL)
    {
        /* Allocate a buffer for the result */
        BiosTables = ExAllocatePoolWithTag(PagedPool,
                                           HeaderSize + TableSize,
                                           'BTMS');
        if (BiosTables == NULL)
        {
            DPRINT1("Failed to allocate %lu bytes for the SMBIOS table\n",
                    HeaderSize + TableSize);
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        /* Copy the header */
        RtlCopyMemory(BiosTables, &BiosTablesHeader, HeaderSize);

        /* This is where the table is */
        PhysicalAddress.QuadPart = TableAddress;

        /* Map the table into the system address space */
        TableMapping = MmMapIoSpace(PhysicalAddress, TableSize, MmCached);
        if (TableMapping == NULL)
        {
            ExFreePoolWithTag(BiosTables, 'BTMS');
            return STATUS_UNSUCCESSFUL;
        }

        /* Copy the table */
        RtlCopyMemory((PUCHAR)BiosTables + HeaderSize, TableMapping, TableSize);

        /* Unmap the table */
        MmUnmapIoSpace(TableMapping, TableSize);

        *OutTableData = BiosTables;
    }

    *OutDataSize = HeaderSize + TableSize;
    return STATUS_SUCCESS;
}


NTSTATUS
NTAPI
WmipQueryRawSMBiosTables(
    _Inout_ ULONG *InOutBufferSize,
    _Out_opt_ PVOID OutBuffer)
{
    static const WCHAR InstanceName[] = L"SMBiosData";
    const ULONG NameBytes = sizeof(InstanceName) - sizeof(WCHAR);
    const ULONG DataOffset = ALIGN_UP_BY(FIELD_OFFSET(WNODE_ALL_DATA, OffsetInstanceDataAndLength[1]), sizeof(ULONGLONG));
    NTSTATUS Status;
    PVOID TableData = NULL;
    ULONG TableSize, NameOffsetsOffset, NameOffset, ResultSize;
    PWNODE_ALL_DATA AllData;
    PUCHAR Base;

    Status = WmipGetRawSMBiosTableData(NULL, &TableSize);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("WmipGetRawSMBiosTableData failed: 0x%08lx\n", Status);
        return Status;
    }

    NameOffsetsOffset = ALIGN_UP_BY(DataOffset + TableSize, sizeof(ULONG));
    NameOffset = NameOffsetsOffset + sizeof(ULONG);
    ResultSize = ALIGN_UP_BY(NameOffset + sizeof(USHORT) + NameBytes, sizeof(ULONGLONG));
    if (OutBuffer == NULL || *InOutBufferSize < ResultSize)
    {
        *InOutBufferSize = ResultSize;
        return STATUS_BUFFER_TOO_SMALL;
    }

    Status = WmipGetRawSMBiosTableData(&TableData, &TableSize);
    if (!NT_SUCCESS(Status))
        return Status;
    if (ALIGN_UP_BY(ALIGN_UP_BY(DataOffset + TableSize, sizeof(ULONG)) + sizeof(ULONG) + sizeof(USHORT) + NameBytes,
                    sizeof(ULONGLONG)) != ResultSize)
    {
        ExFreePoolWithTag(TableData, 'BTMS');
        return STATUS_DEVICE_DATA_ERROR;
    }

    RtlZeroMemory(OutBuffer, ResultSize);
    Base = OutBuffer;
    AllData = OutBuffer;
    AllData->WnodeHeader.BufferSize = ResultSize;
    AllData->WnodeHeader.Guid = MSSmBios_RawSMBiosTables_GUID;
    AllData->WnodeHeader.Flags = WNODE_FLAG_ALL_DATA | WNODE_FLAG_STATIC_INSTANCE_NAMES;
    AllData->DataBlockOffset = DataOffset;
    AllData->InstanceCount = 1;
    AllData->OffsetInstanceNameOffsets = NameOffsetsOffset;
    AllData->OffsetInstanceDataAndLength[0].OffsetInstanceData = DataOffset;
    AllData->OffsetInstanceDataAndLength[0].LengthInstanceData = TableSize;
    RtlCopyMemory(Base + DataOffset, TableData, TableSize);
    *(PULONG)(Base + NameOffsetsOffset) = NameOffset;
    *(PUSHORT)(Base + NameOffset) = (USHORT)NameBytes;
    RtlCopyMemory(Base + NameOffset + sizeof(USHORT), InstanceName, NameBytes);
    ExFreePoolWithTag(TableData, 'BTMS');

    *InOutBufferSize = ResultSize;
    return STATUS_SUCCESS;
}

