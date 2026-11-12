/*
 * PROJECT:     LiberNT Broadcom PCIe Bluetooth transport
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Firmware boot and controller setup of the Broadcom PCIe Bluetooth function
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "bcmpcibt.h"

#define NDEBUG
#include <debug.h>

static
ULONG
BcmBtElapsedMs(
    _In_ ULONGLONG Start)
{
    return (ULONG)((KeQueryInterruptTime() - Start) / 10000);
}

static
BOOLEAN
BcmBtWaitFor(
    _In_ PBCMBT_DEVICE Dev,
    _In_ LONG Bootstage,
    _In_ LONG Rti,
    _In_ ULONG TimeoutMs)
{
    ULONGLONG Start = KeQueryInterruptTime();
    LARGE_INTEGER Step;

    Step.QuadPart = -10000LL * 10;
    for (;;)
    {
        BcmBtUpdateState(Dev);
        if ((Bootstage < 0 || Dev->Bootstage == Bootstage) && (Rti < 0 || Dev->RtiStatus == Rti))
            return TRUE;
        if (Dev->StopThread || BcmBtElapsedMs(Start) >= TimeoutMs)
            return FALSE;
        KeWaitForSingleObject(&Dev->StateEvent, Executive, KernelMode, FALSE, &Step);
    }
}

static
NTSTATUS
BcmBtReadFile(
    _In_z_ PCWSTR Path,
    _Outptr_result_bytebuffer_(*Size) PUCHAR *Data,
    _Out_ PULONG Size)
{
    FILE_STANDARD_INFORMATION Info;
    OBJECT_ATTRIBUTES Attributes;
    UNICODE_STRING Name;
    IO_STATUS_BLOCK Io;
    PUCHAR Buffer = NULL;
    HANDLE File;
    NTSTATUS Status;

    *Data = NULL;
    *Size = 0;
    RtlInitUnicodeString(&Name, Path);
    InitializeObjectAttributes(&Attributes, &Name, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    Status = ZwCreateFile(&File, GENERIC_READ | SYNCHRONIZE, &Attributes, &Io, NULL, FILE_ATTRIBUTE_NORMAL,
                          FILE_SHARE_READ, FILE_OPEN, FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE, NULL, 0);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = ZwQueryInformationFile(File, &Io, &Info, sizeof(Info), FileStandardInformation);
    if (NT_SUCCESS(Status) && (Info.EndOfFile.QuadPart <= 0 || Info.EndOfFile.QuadPart > BCMBT_FIRMWARE_MAX_SIZE))
        Status = STATUS_INVALID_IMAGE_FORMAT;
    if (NT_SUCCESS(Status))
    {
        Buffer = ExAllocatePoolWithTag(PagedPool, Info.EndOfFile.LowPart, BCMBT_TAG);
        if (!Buffer)
            Status = STATUS_INSUFFICIENT_RESOURCES;
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZwReadFile(File, NULL, NULL, NULL, &Io, Buffer, Info.EndOfFile.LowPart, NULL, NULL);
        if (NT_SUCCESS(Status) && Io.Information != Info.EndOfFile.LowPart)
            Status = STATUS_END_OF_FILE;
    }
    ZwClose(File);
    if (!NT_SUCCESS(Status))
    {
        if (Buffer)
            ExFreePoolWithTag(Buffer, BCMBT_TAG);
        return Status;
    }
    *Data = Buffer;
    *Size = Info.EndOfFile.LowPart;
    return STATUS_SUCCESS;
}

static
NTSTATUS
BcmBtLoadBlob(
    _In_ PBCMBT_DEVICE Dev,
    _In_z_ PCSTR Suffix,
    _Outptr_result_bytebuffer_(*Size) PUCHAR *Data,
    _Out_ PULONG Size)
{
    WCHAR Path[2][260];
    NTSTATUS Status;
    ULONG Index;

    RtlStringCbPrintfW(Path[0], sizeof(Path[0]), BCMBT_FIRMWARE_DIR L"brcmbt%04x%hs-%hs-%hs.%hs",
                       Dev->Chip->ChipId, Dev->Stepping, Dev->BoardType, Dev->Vendor, Suffix);
    RtlStringCbPrintfW(Path[1], sizeof(Path[1]), BCMBT_FIRMWARE_DIR L"brcmbt%04x%hs-%hs.%hs",
                       Dev->Chip->ChipId, Dev->Stepping, Dev->BoardType, Suffix);
    for (Index = 0; Index < RTL_NUMBER_OF(Path); Index++)
    {
        Status = BcmBtReadFile(Path[Index], Data, Size);
        if (NT_SUCCESS(Status))
        {
            DPRINT1("BCMBT: using %S (%lu bytes)\n", Path[Index], *Size);
            return STATUS_SUCCESS;
        }
    }
    DPRINT1("BCMBT: no %S or %S: %lx\n", Path[0], Path[1], Status);
    return Status;
}

NTSTATUS
BcmBtReadPlatform(
    _In_ PBCMBT_DEVICE Dev)
{
    ULONG Length, Node, Phandle = 0, Rid = 0;
    const VOID *Value;
    ULONG64 Reg;
    NTSTATUS Status;

    if (Dev->HaveTree)
        return STATUS_SUCCESS;
    Dev->DartNode = FDT_NO_NODE;
    Status = FdtDriverOpenAlias("bluetooth0", &Dev->Tree);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("BCMBT: the firmware tree describes no bluetooth0 node: %lx\n", Status);
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    Dev->HaveTree = TRUE;

    Value = FdtGetProperty(&Dev->Tree.Fdt, Dev->Tree.Node, "brcm,board-type", &Length);
    if (Value && Length && Length <= sizeof(Dev->BoardType) && ((const CHAR *)Value)[Length - 1] == ANSI_NULL)
        RtlCopyMemory(Dev->BoardType, Value, Length);
    Value = FdtGetProperty(&Dev->Tree.Fdt, Dev->Tree.Node, "local-bd-address", &Length);
    if (Value && Length == sizeof(Dev->PlatformAddress))
    {
        const UCHAR *Address = Value;

        RtlCopyMemory(Dev->PlatformAddress, Address, sizeof(Dev->PlatformAddress));
        Dev->HavePlatformAddress = (Address[0] | Address[1] | Address[2] | Address[3] | Address[4] | Address[5]) != 0;
    }
    Value = FdtGetProperty(&Dev->Tree.Fdt, Dev->Tree.Node, "reg", &Length);
    if (Value && FdtReadCells(Value, Length, 0, 1, &Reg))
        Rid = (ULONG)(Reg >> 8) & 0xFFFF;

    for (Node = FdtDriverFindParentNode(&Dev->Tree, Dev->Tree.Node); Node != FDT_NO_NODE;
         Node = FdtDriverFindParentNode(&Dev->Tree, Node))
    {
        if (!FdtGetProperty(&Dev->Tree.Fdt, Node, "iommu-map", &Length))
            continue;
        if (ApplePcieFindStream(&Dev->Tree, Node, Rid, &Phandle, &Dev->Sid))
            Dev->DartNode = FdtDriverFindPhandle(&Dev->Tree, Phandle);
        break;
    }

    if (!FdtGetProperty(&Dev->Tree.Fdt, Dev->Tree.Node, "brcm,taurus-bf-cal-blob", &Length))
        Length = 0;
    DPRINT1("BCMBT: board type '%s', %s address, %lu-byte beamforming calibration, requester %04lx, DART %s stream %lu\n",
            Dev->BoardType, Dev->HavePlatformAddress ? "platform" : "no platform", Length, Rid,
            Dev->DartNode == FDT_NO_NODE ? "none" : "found", Dev->Sid);
    if (!Dev->BoardType[0])
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    return STATUS_SUCCESS;
}

NTSTATUS
BcmBtReadOtp(
    _In_ PBCMBT_DEVICE Dev)
{
    UCHAR Otp[BCMBT_OTP_SIZE];
    ULONG Index;
    NTSTATUS Status;

    if (Dev->Chip->OtpOffset + sizeof(Otp) > Dev->Bar0Length)
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    for (Index = 0; Index < sizeof(Otp); Index++)
        Otp[Index] = READ_REGISTER_UCHAR(Dev->Bar0 + Dev->Chip->OtpOffset + Index);
    Status = BcmBtParseOtp(Otp, sizeof(Otp), Dev->Stepping, Dev->Vendor);
    DPRINT1("BCMBT: BCM%04x OTP stepping '%s', vendor '%s': %lx\n", Dev->Chip->ChipId, Dev->Stepping, Dev->Vendor,
            Status);
    return Status;
}

static
NTSTATUS
BcmBtBootFirmware(
    _In_ PBCMBT_DEVICE Dev)
{
    PHYSICAL_ADDRESS Low, High, Boundary;
    PUCHAR Image;
    ULONG ImageSize;
    NTSTATUS Status;

    Status = BcmBtLoadBlob(Dev, "bin", &Image, &ImageSize);
    if (!NT_SUCCESS(Status))
        return Status;
    Low.QuadPart = 0;
    High.QuadPart = BcmBtHighestDmaAddress(Dev->DartNode != FDT_NO_NODE);
    Boundary.QuadPart = 0;
    Dev->FirmwareSize = ROUND_TO_PAGES(ImageSize);
    Dev->Firmware = MmAllocateContiguousMemorySpecifyCache(Dev->FirmwareSize, Low, High, Boundary, MmNonCached);
    if (!Dev->Firmware)
    {
        ExFreePoolWithTag(Image, BCMBT_TAG);
        Dev->FirmwareSize = 0;
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlCopyMemory(Dev->Firmware, Image, ImageSize);
    ExFreePoolWithTag(Image, BCMBT_TAG);
    Status = BcmBtMapDma(Dev, Dev->Firmware, Dev->FirmwareSize, &Dev->FirmwareIova);
    if (NT_SUCCESS(Status))
    {
        KeMemoryBarrier();
        BcmBtWrite0(Dev, BCMBT_BAR0_HOST_WINDOW_LO, 0);
        BcmBtWrite0(Dev, BCMBT_BAR0_HOST_WINDOW_HI, 0);
        BcmBtWrite0(Dev, BCMBT_BAR0_HOST_WINDOW_SIZE, BCMBT_DMA_WINDOW_SIZE);
        BcmBtWrite2(Dev, BCMBT_BAR2_FW_LO, (ULONG)Dev->FirmwareIova);
        BcmBtWrite2(Dev, BCMBT_BAR2_FW_HI, (ULONG)(Dev->FirmwareIova >> 32));
        BcmBtWrite2(Dev, BCMBT_BAR2_FW_SIZE, ImageSize);
        BcmBtWrite0(Dev, BCMBT_BAR0_FW_DOORBELL, 0);
        if (!BcmBtWaitFor(Dev, 2, -1, BCMBT_BOOT_TIMEOUT_MS))
        {
            DPRINT1("BCMBT: firmware did not boot: bootstage %ld, RTI %ld\n", Dev->Bootstage, Dev->RtiStatus);
            Status = Dev->StopThread ? STATUS_CANCELLED : STATUS_IO_TIMEOUT;
        }
    }
    BcmBtUnmapDma(Dev, Dev->FirmwareIova, Dev->FirmwareSize);
    MmFreeContiguousMemorySpecifyCache(Dev->Firmware, Dev->FirmwareSize, MmNonCached);
    Dev->Firmware = NULL;
    Dev->FirmwareSize = 0;
    Dev->FirmwareIova = 0;
    return Status;
}

static
NTSTATUS
BcmBtStartRti(
    _In_ PBCMBT_DEVICE Dev)
{
    KIRQL OldIrql;

    BcmBtWrite0(Dev, BCMBT_BAR0_RTI_CONTROL, 1);
    if (!BcmBtWaitFor(Dev, -1, 1, BCMBT_CONTROL_TIMEOUT_MS))
    {
        DPRINT1("BCMBT: RTI did not reach state 1 (%ld)\n", Dev->RtiStatus);
        return STATUS_IO_TIMEOUT;
    }
    BcmBtWrite2(Dev, BCMBT_BAR2_RTI_WINDOW_LO, 0);
    BcmBtWrite2(Dev, BCMBT_BAR2_RTI_WINDOW_HI, 0);
    BcmBtWrite2(Dev, BCMBT_BAR2_RTI_WINDOW_SIZE, BCMBT_DMA_WINDOW_SIZE);
    KeMemoryBarrier();
    BcmBtWrite2(Dev, BCMBT_BAR2_CONTEXT_ADDR_LO, (ULONG)Dev->ContextIova);
    BcmBtWrite2(Dev, BCMBT_BAR2_CONTEXT_ADDR_HI, (ULONG)(Dev->ContextIova >> 32));
    BcmBtWrite0(Dev, BCMBT_BAR0_RTI_CONTROL, 2);
    if (!BcmBtWaitFor(Dev, -1, 2, BCMBT_CONTROL_TIMEOUT_MS))
    {
        DPRINT1("BCMBT: RTI did not reach state 2 (%ld)\n", Dev->RtiStatus);
        return STATUS_IO_TIMEOUT;
    }
    KeAcquireSpinLock(&Dev->RingLock, &OldIrql);
    Dev->CompletionRings[BCMBT_COMPLETION_CONTROL_ACK].Enabled = TRUE;
    Dev->TransferRings[BCMBT_XFER_CONTROL].Enabled = TRUE;
    KeReleaseSpinLock(&Dev->RingLock, OldIrql);
    return STATUS_SUCCESS;
}

static
NTSTATUS
BcmBtHciCommand(
    _In_ PBCMBT_DEVICE Dev,
    _In_ USHORT Opcode,
    _In_reads_bytes_opt_(ParameterLength) const UCHAR *Parameters,
    _In_ UCHAR ParameterLength,
    _Out_writes_bytes_opt_(ResultSize) PUCHAR Result,
    _In_ ULONG ResultSize)
{
    UCHAR Command[3 + 255];
    ULONGLONG Start = KeQueryInterruptTime();
    LARGE_INTEGER Step;
    NTSTATUS Status;

    Command[0] = (UCHAR)Opcode;
    Command[1] = (UCHAR)(Opcode >> 8);
    Command[2] = ParameterLength;
    if (ParameterLength)
        RtlCopyMemory(Command + 3, Parameters, ParameterLength);
    Status = BcmBtEnqueue(Dev, BCMBT_XFER_HCI_H2D, Command, 3 + ParameterLength, FALSE);
    if (!NT_SUCCESS(Status))
        return Status;

    Step.QuadPart = -10000LL * 10;
    while (!Dev->StopThread && BcmBtElapsedMs(Start) < BCMBT_COMMAND_TIMEOUT_MS)
    {
        PBCMBT_PACKET Packet;

        BcmBtPollCompletions(Dev);
        while ((Packet = BcmBtTakePacket(Dev)) != NULL)
        {
            const UCHAR *Event = Packet->Data;
            BOOLEAN Done = FALSE;

            if (Packet->Type == HciPacketEvent && Packet->Length >= 6 && Event[0] == 0x0e &&
                (Event[3] | (Event[4] << 8)) == Opcode)
            {
                if (Result && ResultSize)
                    RtlCopyMemory(Result, Event + 5, min(ResultSize, Packet->Length - 5));
                Status = Event[5] ? STATUS_UNSUCCESSFUL : STATUS_SUCCESS;
                if (Event[5])
                    DPRINT1("BCMBT: command %04x failed with HCI status %02x\n", Opcode, Event[5]);
                Done = TRUE;
            }
            else if (Packet->Type == HciPacketEvent && Packet->Length >= 6 && Event[0] == 0x0f &&
                     (Event[4] | (Event[5] << 8)) == Opcode && Event[2] != 0)
            {
                DPRINT1("BCMBT: command %04x refused with HCI status %02x\n", Opcode, Event[2]);
                Status = STATUS_UNSUCCESSFUL;
                Done = TRUE;
            }
            ExFreePoolWithTag(Packet, BCMBT_TAG);
            if (Done)
                return Status;
        }
        KeWaitForSingleObject(&Dev->RxEvent, Executive, KernelMode, FALSE, &Step);
    }
    DPRINT1("BCMBT: command %04x timed out\n", Opcode);
    return Dev->StopThread ? STATUS_CANCELLED : STATUS_IO_TIMEOUT;
}

static
NTSTATUS
BcmBtSendCalibration(
    _In_ PBCMBT_DEVICE Dev)
{
    PCSTR Property = BcmBtUsesBeamforming(Dev->Chip, Dev->Stepping) ? "brcm,taurus-bf-cal-blob" : "brcm,taurus-cal-blob";
    UCHAR Command[3 + BCMBT_CALIBRATION_CHUNK];
    const UCHAR *Blob;
    ULONG Size, Chunks, Index;
    NTSTATUS Status = STATUS_SUCCESS;

    Blob = FdtGetProperty(&Dev->Tree.Fdt, Dev->Tree.Node, Property, &Size);
    if (!Blob || !Size)
    {
        DPRINT1("BCMBT: the firmware tree carries no %s; the controller cannot be calibrated\n", Property);
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    Chunks = BcmBtChunkCount(Size, BCMBT_CALIBRATION_CHUNK);
    for (Index = 0; NT_SUCCESS(Status) && Index < Chunks; Index++)
    {
        ULONG Offset = Index * BCMBT_CALIBRATION_CHUNK;

        RtlZeroMemory(Command, sizeof(Command));
        Command[0] = 0x03;
        Command[1] = (UCHAR)(Chunks - Index - 1);
        Command[2] = (UCHAR)((Chunks - Index - 1) >> 8);
        RtlCopyMemory(Command + 3, Blob + Offset, min(Size - Offset, BCMBT_CALIBRATION_CHUNK));
        Status = BcmBtHciCommand(Dev, BCMBT_HCI_CALIBRATION, Command, sizeof(Command), NULL, 0);
    }
    DPRINT1("BCMBT: %lu bytes of %s sent in %lu chunks: %lx\n", Size, Property, Chunks, Status);
    return Status;
}

static
NTSTATUS
BcmBtSendPtb(
    _In_ PBCMBT_DEVICE Dev)
{
    UCHAR Command[2 + BCMBT_PTB_CHUNK];
    PUCHAR Blob;
    ULONG Size, Chunks, Index;
    NTSTATUS Status;

    Status = BcmBtLoadBlob(Dev, "ptb", &Blob, &Size);
    if (!NT_SUCCESS(Status))
        return Status;
    Chunks = BcmBtChunkCount(Size, BCMBT_PTB_CHUNK);
    for (Index = 0; NT_SUCCESS(Status) && Index < Chunks; Index++)
    {
        ULONG Offset = Index * BCMBT_PTB_CHUNK;

        RtlZeroMemory(Command, sizeof(Command));
        Command[0] = (UCHAR)(Chunks - Index - 1);
        Command[1] = (UCHAR)((Chunks - Index - 1) >> 8);
        RtlCopyMemory(Command + 2, Blob + Offset, min(Size - Offset, BCMBT_PTB_CHUNK));
        Status = BcmBtHciCommand(Dev, BCMBT_HCI_PTB, Command, sizeof(Command), NULL, 0);
    }
    ExFreePoolWithTag(Blob, BCMBT_TAG);
    DPRINT1("BCMBT: PTB sent in %lu chunks: %lx\n", Chunks, Status);
    return Status;
}

static
NTSTATUS
BcmBtCheckAddress(
    _In_ PBCMBT_DEVICE Dev)
{
    UCHAR Result[1 + 6];
    NTSTATUS Status;

    RtlZeroMemory(Result, sizeof(Result));
    Status = BcmBtHciCommand(Dev, BCMBT_HCI_READ_BD_ADDR, NULL, 0, Result, sizeof(Result));
    if (!NT_SUCCESS(Status))
        return Status;
    if (!BcmBtIsDefaultAddress(Dev->Chip, Result + 1))
        return STATUS_SUCCESS;
    if (!Dev->HavePlatformAddress)
    {
        DPRINT1("BCMBT: the controller has its default address and the platform gives none\n");
        return STATUS_SUCCESS;
    }
    Status = BcmBtHciCommand(Dev, BCMBT_HCI_WRITE_BD_ADDR, Dev->PlatformAddress, sizeof(Dev->PlatformAddress), NULL, 0);
    DPRINT1("BCMBT: platform address %02x:%02x:%02x:%02x:%02x:%02x programmed: %lx\n", Dev->PlatformAddress[5],
            Dev->PlatformAddress[4], Dev->PlatformAddress[3], Dev->PlatformAddress[2], Dev->PlatformAddress[1],
            Dev->PlatformAddress[0], Status);
    return Status;
}

static
NTSTATUS
BcmBtSetup(
    _In_ PBCMBT_DEVICE Dev)
{
    NTSTATUS Status;

    Status = BcmBtCreateRings(Dev);
    if (NT_SUCCESS(Status))
        Status = BcmBtSendCalibration(Dev);
    if (NT_SUCCESS(Status))
        Status = BcmBtSendPtb(Dev);
    if (NT_SUCCESS(Status))
        Status = BcmBtCheckAddress(Dev);
    Dev->SetupDone = NT_SUCCESS(Status);
    return Status;
}

NTSTATUS
BcmBtBringUp(
    _In_ PBCMBT_DEVICE Dev)
{
    NTSTATUS Status;

    BcmBtUpdateState(Dev);
    if (Dev->Booted && Dev->Bootstage == 2 && Dev->RtiStatus == 2)
    {
        BcmBtWrite0(Dev, BCMBT_BAR0_SLEEP_CONTROL, BCMBT_SLEEP_UNQUIESCE);
        if (Dev->SetupDone)
            return STATUS_SUCCESS;
        BcmBtDestroyRings(Dev);
        return BcmBtSetup(Dev);
    }
    if (Dev->Booted)
    {
        DPRINT1("BCMBT: the controller lost its firmware (bootstage %ld, RTI %ld)\n", Dev->Bootstage, Dev->RtiStatus);
        Dev->Booted = FALSE;
        Dev->SetupDone = FALSE;
    }
    if (Dev->Bootstage != 0 || Dev->RtiStatus != 0)
    {
        DPRINT1("BCMBT: the controller is already started (bootstage %ld, RTI %ld)\n", Dev->Bootstage, Dev->RtiStatus);
        Status = BcmBtResetFunction(Dev);
        if (!NT_SUCCESS(Status))
            return Status;
    }

    Status = BcmBtResetShared(Dev);
    if (NT_SUCCESS(Status))
        Status = BcmBtBootFirmware(Dev);
    if (NT_SUCCESS(Status))
        Status = BcmBtStartRti(Dev);
    if (!NT_SUCCESS(Status))
        return Status;
    Dev->Booted = TRUE;
    DPRINT1("BCMBT: firmware running, control ring ready\n");
    return BcmBtSetup(Dev);
}
