/*
 * PROJECT:     LiberNT Bluetooth USB transport
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Intel TLV controller identification and secure firmware initialization
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "bthusb.h"
#include "intel.h"

#define NDEBUG
#include <debug.h>

typedef struct _BTHUSB_INTEL_VERSION
{
    ULONG CnviTop;
    ULONG CnvrTop;
    ULONG CnviBt;
    UCHAR ImageType;
    UCHAR SecureEngine;
} BTHUSB_INTEL_VERSION;

static ULONG
BthUsbIntelGet32(const UCHAR *Data)
{
    return (ULONG)Data[0] | ((ULONG)Data[1] << 8) | ((ULONG)Data[2] << 16) | ((ULONG)Data[3] << 24);
}

static USHORT
BthUsbIntelNamePart(ULONG Top)
{
    USHORT Part = (USHORT)(((Top & 0xfff) << 4) | ((Top >> 24) & 0xf));
    return (USHORT)((Part >> 8) | (Part << 8));
}

static NTSTATUS
BthUsbIntelParseVersion(const UCHAR *Data, ULONG Length, BTHUSB_INTEL_VERSION *Version)
{
    ULONG Offset = 1, Seen = 0;

    RtlZeroMemory(Version, sizeof(*Version));
    if (!Length || Data[0]) return STATUS_DEVICE_DATA_ERROR;
    while (Offset < Length)
    {
        UCHAR Type, Size;

        if (Length - Offset < 2) return STATUS_DEVICE_DATA_ERROR;
        Type = Data[Offset++];
        Size = Data[Offset++];
        if (Size > Length - Offset) return STATUS_DEVICE_DATA_ERROR;
        switch (Type)
        {
            case 0x10:
            case 0x11:
            case 0x12:
                if (Size != 4) return STATUS_DEVICE_DATA_ERROR;
                if (Type == 0x10) Version->CnviTop = BthUsbIntelGet32(Data + Offset);
                if (Type == 0x11) Version->CnvrTop = BthUsbIntelGet32(Data + Offset);
                if (Type == 0x12) Version->CnviBt = BthUsbIntelGet32(Data + Offset);
                Seen |= 1u << (Type - 0x10);
                break;
            case 0x1c:
                if (Size != 1) return STATUS_DEVICE_DATA_ERROR;
                Version->ImageType = Data[Offset];
                Seen |= 8;
                break;
            case 0x2f:
                if (Size != 1) return STATUS_DEVICE_DATA_ERROR;
                Version->SecureEngine = Data[Offset];
                Seen |= 16;
                break;
            case 0x2e:
                if (Size != 1) return STATUS_DEVICE_DATA_ERROR;
                if (Data[Offset]) return STATUS_NOT_SUPPORTED;
                break;
        }
        Offset += Size;
    }
    if ((Seen & 15) != 15 || (Version->ImageType == 1 && !(Seen & 16)))
        return STATUS_DEVICE_DATA_ERROR;
    if (((Version->CnviBt >> 8) & 0xff) != 0x37 ||
        (((Version->CnviBt >> 16) & 0x3f) != 0x17 &&
         ((Version->CnviBt >> 16) & 0x3f) != 0x18 &&
         ((Version->CnviBt >> 16) & 0x3f) != 0x19) ||
        (Version->ImageType != 1 && Version->ImageType != 3) || Version->SecureEngine > 1)
        return STATUS_NOT_SUPPORTED;
    return STATUS_SUCCESS;
}

static NTSTATUS
BthUsbIntelReadVersion(PBTHUSB_DEVICE Dev, BTHUSB_INTEL_VERSION *Version)
{
    UCHAR Parameter = 0xff, Result[255];
    ULONG Length;
    NTSTATUS Status;

    Status = BthUsbHciCommand(Dev, 0xfc05, &Parameter, 1, Result, sizeof(Result), &Length);
    if (!NT_SUCCESS(Status)) return Status;
    return BthUsbIntelParseVersion(Result, Length, Version);
}

static NTSTATUS
BthUsbIntelReadFile(const BTHUSB_INTEL_VERSION *Version, PCWSTR Suffix, PUCHAR *Buffer, PULONG Length)
{
    WCHAR Name[128];
    UNICODE_STRING Path;
    OBJECT_ATTRIBUTES Attributes;
    IO_STATUS_BLOCK IoStatus;
    FILE_STANDARD_INFORMATION Info;
    HANDLE File;
    NTSTATUS Status;

    *Buffer = NULL;
    *Length = 0;
    Status = RtlStringCbPrintfW(Name, sizeof(Name), L"\\SystemRoot\\System32\\drivers\\intel\\ibt-%04x-%04x.%s",
                               BthUsbIntelNamePart(Version->CnviTop), BthUsbIntelNamePart(Version->CnvrTop), Suffix);
    if (!NT_SUCCESS(Status)) return Status;
    RtlInitUnicodeString(&Path, Name);
    InitializeObjectAttributes(&Attributes, &Path, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    Status = ZwCreateFile(&File, GENERIC_READ | SYNCHRONIZE, &Attributes, &IoStatus, NULL,
                          FILE_ATTRIBUTE_NORMAL, FILE_SHARE_READ, FILE_OPEN,
                          FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE, NULL, 0);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("BTHUSB: Intel firmware %wZ could not be opened: 0x%08lx\n", &Path, Status);
        return Status;
    }
    Status = ZwQueryInformationFile(File, &IoStatus, &Info, sizeof(Info), FileStandardInformation);
    if (NT_SUCCESS(Status) && (Info.EndOfFile.QuadPart <= 0 || Info.EndOfFile.QuadPart > 16 * 1024 * 1024))
        Status = STATUS_INVALID_IMAGE_FORMAT;
    if (NT_SUCCESS(Status))
    {
        *Length = (ULONG)Info.EndOfFile.QuadPart;
        *Buffer = ExAllocatePoolWithTag(PagedPool, *Length, BTHUSB_TAG);
        if (!*Buffer) Status = STATUS_INSUFFICIENT_RESOURCES;
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZwReadFile(File, NULL, NULL, NULL, &IoStatus, *Buffer, *Length, NULL, NULL);
        if (NT_SUCCESS(Status) && IoStatus.Information != *Length) Status = STATUS_END_OF_FILE;
    }
    ZwClose(File);
    if (!NT_SUCCESS(Status) && *Buffer)
    {
        ExFreePoolWithTag(*Buffer, BTHUSB_TAG);
        *Buffer = NULL;
    }
    return Status;
}

static NTSTATUS
BthUsbIntelValidateFirmware(const UCHAR *Data, ULONG Length, PULONG BootAddress)
{
    ULONG Offset = 964;
    BOOLEAN Found = FALSE;

    *BootAddress = 0;
    if (Length <= Offset || Data[0] != 6 || BthUsbIntelGet32(Data + 8) != 0x10000 ||
        Data[644] != 6 || BthUsbIntelGet32(Data + 652) != 0x20000 || (Length - Offset) % 4)
        return STATUS_INVALID_IMAGE_FORMAT;
    while (Offset < Length)
    {
        ULONG Size;

        if (Length - Offset < 3) return STATUS_INVALID_IMAGE_FORMAT;
        Size = 3 + (ULONG)Data[Offset + 2];
        if (Size > Length - Offset) return STATUS_INVALID_IMAGE_FORMAT;
        if (Data[Offset] == 0x0e && Data[Offset + 1] == 0xfc)
        {
            if (Size < 10 || Found) return STATUS_INVALID_IMAGE_FORMAT;
            *BootAddress = BthUsbIntelGet32(Data + Offset + 3);
            Found = TRUE;
        }
        Offset += Size;
    }
    return Found ? STATUS_SUCCESS : STATUS_INVALID_IMAGE_FORMAT;
}

static NTSTATUS
BthUsbIntelSecureSend(PBTHUSB_DEVICE Dev, UCHAR Type, const UCHAR *Data, ULONG Length)
{
    UCHAR Parameters[253];
    ULONG Size;
    NTSTATUS Status;

    ASSERT(Dev->IntelBootloader);
    Parameters[0] = Type;
    while (Length)
    {
        Size = min(Length, 252);
        RtlCopyMemory(Parameters + 1, Data, Size);
        Status = BthUsbHciCommand(Dev, 0xfc09, Parameters, (UCHAR)(Size + 1), NULL, 0, NULL);
        if (!NT_SUCCESS(Status)) return Status;
        if (Dev->IntelDownloadComplete && !NT_SUCCESS(Dev->IntelDownloadStatus))
            return Dev->IntelDownloadStatus;
        Length -= Size;
        Data += Size;
    }
    return STATUS_SUCCESS;
}

BOOLEAN
BthUsbIntelEvent(PBTHUSB_DEVICE Dev, const UCHAR *Event, ULONG Length)
{
    if (!Dev->IntelBootloader || Length < 3 || Event[0] != 0xff) return FALSE;
    if (Event[2] == 6)
    {
        Dev->IntelDownloadComplete = TRUE;
        Dev->IntelDownloadStatus = Length == 7 && Event[1] == 5 && !Event[3] ? STATUS_SUCCESS : STATUS_DEVICE_DATA_ERROR;
        return TRUE;
    }
    if (Event[2] == 2)
    {
        Dev->IntelBootComplete = Length == 9 && Event[1] == 7;
        return TRUE;
    }
    return FALSE;
}

static NTSTATUS
BthUsbIntelWait(PBTHUSB_DEVICE Dev, BOOLEAN Boot)
{
    ULONGLONG Deadline = KeQueryInterruptTime() + 5 * 10000000ULL;
    PBTHUSB_PACKET Packet;
    LARGE_INTEGER Timeout;
    ULONGLONG Now;

    for (;;)
    {
        KeClearEvent(&Dev->EventArrived);
        while ((Packet = BthUsbTakePacket(Dev, BthUsbRxEvent)) != NULL)
        {
            BthUsbIntelEvent(Dev, Packet->Data, Packet->Length);
            ExFreePoolWithTag(Packet, BTHUSB_TAG);
        }
        if (Dev->Stopping) return STATUS_CANCELLED;
        if (Boot ? Dev->IntelBootComplete : Dev->IntelDownloadComplete)
            return Boot ? STATUS_SUCCESS : Dev->IntelDownloadStatus;
        Now = KeQueryInterruptTime();
        if (Now >= Deadline) return STATUS_IO_TIMEOUT;
        Timeout.QuadPart = -(LONGLONG)(Deadline - Now);
        KeWaitForSingleObject(&Dev->EventArrived, Executive, KernelMode, FALSE, &Timeout);
    }
}

static VOID
BthUsbIntelSetOperational(PBTHUSB_DEVICE Dev)
{
    KIRQL OldIrql;

    KeAcquireSpinLock(&Dev->Lock, &OldIrql);
    Dev->IntelBootloader = FALSE;
    Dev->Rx[BthUsbRxAcl].Length = 0;
    Dev->Rx[BthUsbRxAcl].Discard = FALSE;
    KeReleaseSpinLock(&Dev->Lock, OldIrql);
}

NTSTATUS
BthUsbIntelInitialize(PBTHUSB_DEVICE Dev)
{
    BTHUSB_INTEL_VERSION Version, Running;
    PUCHAR Firmware = NULL, Ddc = NULL;
    ULONG Length, DdcLength, Offset, End, BootAddress;
    UCHAR Reset[11] = {0x01, 0xfc, 8, 0, 1, 0, 1};
    NTSTATUS Status;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    Status = BthUsbIntelReadVersion(Dev, &Version);
    if (!NT_SUCCESS(Status)) return Status;
    if (Version.ImageType == 3)
    {
        BthUsbIntelSetOperational(Dev);
        return STATUS_SUCCESS;
    }
    Status = BthUsbIntelReadFile(&Version, L"sfi", &Firmware, &Length);
    if (!NT_SUCCESS(Status)) return Status;
    Status = BthUsbIntelValidateFirmware(Firmware, Length, &BootAddress);
    if (!NT_SUCCESS(Status)) goto Done;
    Status = BthUsbIntelReadFile(&Version, L"ddc", &Ddc, &DdcLength);
    if (Status == STATUS_OBJECT_NAME_NOT_FOUND || Status == STATUS_OBJECT_PATH_NOT_FOUND)
    {
        DdcLength = 0;
        Status = STATUS_SUCCESS;
    }
    if (!NT_SUCCESS(Status)) goto Done;
    for (Offset = 0; Offset < DdcLength; Offset = End)
    {
        ULONG Size = (ULONG)Ddc[Offset] + 1;
        if (Size < 3 || Size > 255 || Size > DdcLength - Offset)
        {
            Status = STATUS_INVALID_IMAGE_FORMAT;
            goto Done;
        }
        End = Offset + Size;
    }
    Dev->IntelDownloadComplete = FALSE;
    Dev->IntelDownloadStatus = STATUS_DEVICE_NOT_READY;
    Status = BthUsbIntelSecureSend(Dev, 0, Firmware + (Version.SecureEngine ? 644 : 0), 128);
    if (!NT_SUCCESS(Status)) goto Done;
    Status = BthUsbIntelSecureSend(Dev, 3, Firmware + (Version.SecureEngine ? 772 : 128), Version.SecureEngine ? 96 : 256);
    if (!NT_SUCCESS(Status)) goto Done;
    Status = BthUsbIntelSecureSend(Dev, 2, Firmware + (Version.SecureEngine ? 868 : 388), Version.SecureEngine ? 96 : 256);
    if (!NT_SUCCESS(Status)) goto Done;
    Offset = End = 964;
    while (End < Length)
    {
        End += 3 + (ULONG)Firmware[End + 2];
        if ((End - Offset) % 4) continue;
        Status = BthUsbIntelSecureSend(Dev, 1, Firmware + Offset, End - Offset);
        if (!NT_SUCCESS(Status)) goto Done;
        Offset = End;
    }
    Status = BthUsbIntelWait(Dev, FALSE);
    if (!NT_SUCCESS(Status)) goto Done;
    Dev->IntelBootComplete = FALSE;
    for (Offset = 0; Offset < 4; Offset++) Reset[7 + Offset] = (UCHAR)(BootAddress >> (8 * Offset));
    Status = BthUsbWrite(Dev, HciPacketCommand, Reset, sizeof(Reset), NULL);
    if (!NT_SUCCESS(Status)) goto Done;
    Status = BthUsbIntelWait(Dev, TRUE);
    if (!NT_SUCCESS(Status)) goto Done;
    BthUsbIntelSetOperational(Dev);
    Status = BthUsbIntelReadVersion(Dev, &Running);
    if (!NT_SUCCESS(Status)) goto Done;
    if (Running.ImageType != 3)
    {
        Status = STATUS_DEVICE_NOT_READY;
        goto Done;
    }
    for (Offset = 0; Offset < DdcLength; Offset += End)
    {
        End = (ULONG)Ddc[Offset] + 1;
        Status = BthUsbHciCommand(Dev, 0xfc8b, Ddc + Offset, (UCHAR)End, NULL, 0, NULL);
        if (!NT_SUCCESS(Status)) goto Done;
    }
    DPRINT1("BTHUSB: Intel firmware running, boot address 0x%08lx, DDC bytes %lu\n", BootAddress, DdcLength);
Done:
    if (Ddc) ExFreePoolWithTag(Ddc, BTHUSB_TAG);
    ExFreePoolWithTag(Firmware, BTHUSB_TAG);
    return Status;
}
