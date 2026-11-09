/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The firmware description tables the kernel keeps and hands out as a data block
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "wmipriv.h"
#define NDEBUG
#include <debug.h>

/* PRIVATE DATA ***************************************************************/

#define WMIP_TAG 'imWD'

/* What the entry point the firmware left says about itself */
#define WMIP_SMBIOS_ANCHOR "_SM_"
#define WMIP_SMBIOS3_ANCHOR "_SM3_"

/* The copy the kernel keeps of the structure table, and what it says */
static PUCHAR WmipSMBiosTable = NULL;
static ULONG WmipSMBiosTableLength = 0;
static UCHAR WmipSMBiosMajorVersion = 0;
static UCHAR WmipSMBiosMinorVersion = 0;
static UCHAR WmipSMBiosDmiRevision = 0;
static BOOLEAN WmipSMBiosUsed20CallingMethod = FALSE;

/* PRIVATE FUNCTIONS **********************************************************/

/* Whether a run of bytes starts with the text the caller names */
static
BOOLEAN
WmipAnchorMatches(
    _In_reads_bytes_(Length) const UCHAR *Bytes,
    _In_ SIZE_T Length,
    _In_ PCSTR Anchor)
{
    SIZE_T i;

    for (i = 0; i < Length; i++)
    {
        if (Anchor[i] == '\0') return FALSE;
        if (Bytes[i] != (UCHAR)Anchor[i]) return FALSE;
    }

    return TRUE;
}

/* Whether the bytes of a header add up to nothing, as the firmware promises */
static
BOOLEAN
WmipChecksumIsZero(
    _In_reads_bytes_(Length) const UCHAR *Bytes,
    _In_ ULONG Length)
{
    UCHAR Sum = 0;
    ULONG i;

    for (i = 0; i < Length; i++) Sum += Bytes[i];

    return Sum == 0;
}

/* Takes a copy of the structure table the firmware left in memory */
static
BOOLEAN
WmipCopyTable(
    _In_ PHYSICAL_ADDRESS Address,
    _In_ ULONG Length)
{
    PVOID Mapping;
    PUCHAR Copy;

    if (Length == 0 || Address.QuadPart == 0) return FALSE;

    Mapping = MmMapIoSpace(Address, Length, MmCached);
    if (Mapping == NULL) return FALSE;

    Copy = ExAllocatePoolZero(NonPagedPool, Length, WMIP_TAG);

    if (Copy != NULL) RtlCopyMemory(Copy, Mapping, Length);

    MmUnmapIoSpace(Mapping, Length);

    if (Copy == NULL) return FALSE;

    WmipSMBiosTable = Copy;
    WmipSMBiosTableLength = Length;

    return TRUE;
}

/* Reads a thirty-two bit entry point and keeps what it points at */
static
BOOLEAN
WmipCaptureFrom20EntryPoint(
    _In_ PSMBIOS_TABLE_HEADER Header)
{
    PHYSICAL_ADDRESS Address;

    if (!WmipAnchorMatches((const UCHAR *)Header->Signature,
                           sizeof(WMIP_SMBIOS_ANCHOR) - 1,
                           WMIP_SMBIOS_ANCHOR))
    {
        return FALSE;
    }

    if (Header->Length < sizeof(SMBIOS_TABLE_HEADER)) return FALSE;

    if (!WmipChecksumIsZero((const UCHAR *)Header, Header->Length)) return FALSE;

    Address.QuadPart = Header->StructureTableAddress;

    if (!WmipCopyTable(Address, Header->StructureTableLength)) return FALSE;

    WmipSMBiosMajorVersion = Header->MajorVersion;
    WmipSMBiosMinorVersion = Header->MinorVersion;
    WmipSMBiosDmiRevision = Header->Revision;
    WmipSMBiosUsed20CallingMethod = TRUE;

    return TRUE;
}

/* Reads a sixty-four bit entry point and keeps what it points at */
static
BOOLEAN
WmipCaptureFrom30EntryPoint(
    _In_ PSMBIOS3_TABLE_HEADER Header)
{
    PHYSICAL_ADDRESS Address;

    if (!WmipAnchorMatches(Header->Signature,
                           sizeof(WMIP_SMBIOS3_ANCHOR) - 1,
                           WMIP_SMBIOS3_ANCHOR))
    {
        return FALSE;
    }

    if (Header->Length < sizeof(SMBIOS3_TABLE_HEADER)) return FALSE;

    if (!WmipChecksumIsZero((const UCHAR *)Header, Header->Length)) return FALSE;

    Address.QuadPart = (LONGLONG)Header->StructureTableAddress;

    if (!WmipCopyTable(Address, Header->StructureTableMaximumSize)) return FALSE;

    WmipSMBiosMajorVersion = Header->MajorVersion;
    WmipSMBiosMinorVersion = Header->MinorVersion;
    WmipSMBiosDmiRevision = Header->Docrev;
    WmipSMBiosUsed20CallingMethod = FALSE;

    return TRUE;
}

/* PUBLIC FUNCTIONS ***********************************************************/

VOID
NTAPI
WmipCaptureSMBiosFromLoader(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PLOADER_PARAMETER_EXTENSION Extension;
    PHYSICAL_ADDRESS Anchor;
    PVOID EntryPoint;

    if (WmipSMBiosTable != NULL) return;
    if (LoaderBlock == NULL || LoaderBlock->Extension == NULL) return;

    Extension = LoaderBlock->Extension;

    if (Extension->SMBiosEPSHeader == NULL) return;

    /*
     * The loader reports where the firmware's entry point lies, so it is read
     * through a mapping of its own rather than as a pointer.
     */
    Anchor.QuadPart = (LONGLONG)(ULONG_PTR)Extension->SMBiosEPSHeader;

    EntryPoint = MmMapIoSpace(Anchor, sizeof(SMBIOS3_TABLE_HEADER), MmCached);
    if (EntryPoint == NULL) return;

    if (!WmipCaptureFrom30EntryPoint(EntryPoint))
    {
        WmipCaptureFrom20EntryPoint(EntryPoint);
    }

    MmUnmapIoSpace(EntryPoint, sizeof(SMBIOS3_TABLE_HEADER));
}

PUCHAR
NTAPI
WmipGetRawSMBiosTableData(
    _Out_ PULONG Length)
{
    *Length = WmipSMBiosTableLength;

    return WmipSMBiosTable;
}

NTSTATUS
NTAPI
WmipQueryRawSMBiosTables(
    _Inout_ ULONG *InOutBufferSize,
    _Out_opt_ PVOID OutBuffer)
{
    PMSSmBios_RawSMBiosTables Tables;
    ULONG Needed;

    PAGED_CODE();

    if (InOutBufferSize == NULL) return STATUS_INVALID_PARAMETER;

    if (WmipSMBiosTable == NULL || WmipSMBiosTableLength == 0)
    {
        *InOutBufferSize = 0;
        return STATUS_NOT_SUPPORTED;
    }

    Needed = FIELD_OFFSET(MSSmBios_RawSMBiosTables, SMBiosData) +
             WmipSMBiosTableLength;

    if (OutBuffer == NULL || *InOutBufferSize < Needed)
    {
        *InOutBufferSize = Needed;
        return STATUS_BUFFER_TOO_SMALL;
    }

    Tables = OutBuffer;

    Tables->Used20CallingMethod = WmipSMBiosUsed20CallingMethod;
    Tables->SmbiosMajorVersion = WmipSMBiosMajorVersion;
    Tables->SmbiosMinorVersion = WmipSMBiosMinorVersion;
    Tables->DmiRevision = WmipSMBiosDmiRevision;
    Tables->Size = WmipSMBiosTableLength;

    RtlCopyMemory(Tables->SMBiosData,
                  WmipSMBiosTable,
                  WmipSMBiosTableLength);

    *InOutBufferSize = Needed;

    return STATUS_SUCCESS;
}

/* Whether the kernel answers for this identifier without asking anybody */
BOOLEAN
NTAPI
WmipIsKernelDataBlock(
    _In_ LPCGUID Guid)
{
    if (Guid == NULL) return FALSE;

    return IsEqualGUID(Guid, &MSSmBios_RawSMBiosTables_GUID) != 0;
}

NTSTATUS
NTAPI
WmipQueryKernelDataBlock(
    _In_ LPCGUID Guid,
    _Inout_ PULONG InOutBufferSize,
    _Out_writes_bytes_opt_(*InOutBufferSize) PVOID OutBuffer)
{
    PWNODE_ALL_DATA Wnode;
    NTSTATUS Status;
    ULONG Available;
    ULONG DataSize;
    ULONG Header;

    PAGED_CODE();

    if (Guid == NULL || InOutBufferSize == NULL) return STATUS_INVALID_PARAMETER;

    if (!WmipIsKernelDataBlock(Guid)) return STATUS_WMI_GUID_NOT_FOUND;

    Header = (FIELD_OFFSET(WNODE_ALL_DATA, OffsetInstanceDataAndLength) + 7) & ~7u;
    Available = *InOutBufferSize;
    DataSize = 0;

    Status = WmipQueryRawSMBiosTables(&DataSize, NULL);
    if (Status != STATUS_BUFFER_TOO_SMALL && !NT_SUCCESS(Status)) return Status;

    if (OutBuffer == NULL || Available < Header + DataSize)
    {
        *InOutBufferSize = Header + DataSize;
        return STATUS_BUFFER_TOO_SMALL;
    }

    Wnode = OutBuffer;
    RtlZeroMemory(Wnode, Header);

    Status = WmipQueryRawSMBiosTables(&DataSize, (PUCHAR)Wnode + Header);
    if (!NT_SUCCESS(Status)) return Status;

    Wnode->WnodeHeader.BufferSize = Header + DataSize;
    Wnode->WnodeHeader.Guid = *Guid;
    Wnode->WnodeHeader.Flags = WNODE_FLAG_ALL_DATA |
                               WNODE_FLAG_FIXED_INSTANCE_SIZE;
    KeQuerySystemTime(&Wnode->WnodeHeader.TimeStamp);

    Wnode->DataBlockOffset = Header;
    Wnode->InstanceCount = 1;
    Wnode->FixedInstanceSize = DataSize;

    *InOutBufferSize = Header + DataSize;

    return STATUS_SUCCESS;
}

/* EOF */
