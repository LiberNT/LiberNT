/*
 * PROJECT:     LiberNT Bluetooth stack
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     HCI command engine and controller initialization
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "bthmini.h"

#define NDEBUG
#include <debug.h>

static const UCHAR BthHciMaskedEvents[] =
{
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D,
    0x10, 0x11, 0x12, 0x14, 0x15, 0x16, 0x17, 0x18, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E,
    0x20, 0x21, 0x22, 0x23, 0x2C, 0x2D, 0x2E, 0x2F, 0x30, 0x31, 0x32, 0x33, 0x34,
    0x35, 0x36, 0x38, 0x39, 0x3B, 0x3C, 0x3D, 0x3E
};

static
VOID
BthHciKick(
    _In_ PBTHMINI_DEVICE Dev)
{
    while (Dev->CommandCredits && !IsListEmpty(&Dev->CommandsPending))
    {
        PBTHMINI_COMMAND Command = CONTAINING_RECORD(RemoveHeadList(&Dev->CommandsPending), BTHMINI_COMMAND, Link);
        UCHAR Packet[3 + 255];
        NTSTATUS Status;

        Packet[0] = (UCHAR)Command->Opcode;
        Packet[1] = (UCHAR)(Command->Opcode >> 8);
        Packet[2] = Command->Length;
        RtlCopyMemory(Packet + 3, Command->Parameters, Command->Length);
        Status = BthMiniSendPacket(Dev, HciPacketCommand, Packet, 3 + Command->Length);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("BTHMINI: command 0x%04x not sent 0x%08lx\n", Command->Opcode, Status);
            if (Command->Async)
            {
                BthRadioCommandFailed(Dev, Command->Opcode);
                ExFreePoolWithTag(Command, BTHMINI_TAG);
            }
            else
            {
                Command->Status = Status;
                Command->Done = TRUE;
            }
            continue;
        }
        Dev->CommandCredits--;
        InsertTailList(&Dev->CommandsSent, &Command->Link);
    }
}

static
VOID
BthHciFinishCommand(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ USHORT Opcode,
    _In_ UCHAR Status,
    _In_reads_bytes_opt_(Length) const UCHAR *Result,
    _In_ ULONG Length)
{
    PLIST_ENTRY Entry;

    for (Entry = Dev->CommandsSent.Flink; Entry != &Dev->CommandsSent; Entry = Entry->Flink)
    {
        PBTHMINI_COMMAND Command = CONTAINING_RECORD(Entry, BTHMINI_COMMAND, Link);

        if (Command->Opcode != Opcode)
            continue;
        RemoveEntryList(&Command->Link);
        if (Status)
            DPRINT1("BTHMINI: command 0x%04x failed with HCI status 0x%02x\n", Opcode, Status);
        if (Command->Async)
        {
            ExFreePoolWithTag(Command, BTHMINI_TAG);
            if (Status)
                BthRadioCommandFailed(Dev, Opcode);
            return;
        }
        Command->ResultLength = min(Length, sizeof(Command->Result));
        if (Result && Command->ResultLength)
            RtlCopyMemory(Command->Result, Result, Command->ResultLength);
        Command->Status = Status ? STATUS_UNSUCCESSFUL : STATUS_SUCCESS;
        Command->Done = TRUE;
        return;
    }
}

static
VOID
BthHciEvent(
    _In_ PBTHMINI_DEVICE Dev,
    _In_reads_bytes_(Length) const UCHAR *Event,
    _In_ ULONG Length)
{
    if (Length < 2 || Event[1] != Length - 2)
        return;

    switch (Event[0])
    {
        case HCI_EV_COMMAND_COMPLETE:
            if (Length < 5)
                return;
            Dev->CommandCredits = Event[2];
            BthHciFinishCommand(Dev, (USHORT)(Event[3] | (Event[4] << 8)), Length > 5 ? Event[5] : 0,
                                Event + 5, Length - 5);
            BthHciKick(Dev);
            return;

        case HCI_EV_COMMAND_STATUS:
            if (Length < 6)
                return;
            Dev->CommandCredits = Event[3];
            BthHciFinishCommand(Dev, (USHORT)(Event[4] | (Event[5] << 8)), Event[2], Event + 2, 1);
            BthHciKick(Dev);
            return;

        case HCI_EV_HARDWARE_ERROR:
            DPRINT1("BTHMINI: controller hardware error 0x%02x\n", Length > 2 ? Event[2] : 0);
            return;

        default:
            BthRadioEvent(Dev, Event, Length);
            return;
    }
}

VOID
BthHciProcessRx(
    _In_ PBTHMINI_DEVICE Dev)
{
    for (;;)
    {
        PBTHMINI_RX_PACKET Packet = NULL;
        KIRQL OldIrql;

        KeAcquireSpinLock(&Dev->RxLock, &OldIrql);
        if (!IsListEmpty(&Dev->RxPackets))
        {
            Packet = CONTAINING_RECORD(RemoveHeadList(&Dev->RxPackets), BTHMINI_RX_PACKET, Link);
            Dev->RxCount--;
        }
        KeReleaseSpinLock(&Dev->RxLock, OldIrql);
        if (!Packet)
            return;

        if (Packet->Type == HciPacketEvent)
            BthHciEvent(Dev, Packet->Data, Packet->Length);
        else if (Packet->Type == HciPacketAclData)
            BthAclReceive(Dev, Packet->Data, Packet->Length);
        ExFreePoolWithTag(Packet, BTHMINI_TAG);
    }
}

static
PBTHMINI_COMMAND
BthHciAllocateCommand(
    _In_ USHORT Opcode,
    _In_reads_bytes_opt_(Length) const UCHAR *Parameters,
    _In_ UCHAR Length,
    _In_ BOOLEAN Async)
{
    PBTHMINI_COMMAND Command = ExAllocatePoolZero(NonPagedPool, sizeof(*Command), BTHMINI_TAG);

    if (!Command)
        return NULL;
    Command->Opcode = Opcode;
    Command->Length = Length;
    Command->Async = Async;
    if (Length)
        RtlCopyMemory(Command->Parameters, Parameters, Length);
    return Command;
}

NTSTATUS
BthHciQueueCommand(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ USHORT Opcode,
    _In_reads_bytes_opt_(Length) const UCHAR *Parameters,
    _In_ UCHAR Length)
{
    PBTHMINI_COMMAND Command = BthHciAllocateCommand(Opcode, Parameters, Length, TRUE);

    if (!Command)
        return STATUS_INSUFFICIENT_RESOURCES;
    InsertTailList(&Dev->CommandsPending, &Command->Link);
    BthHciKick(Dev);
    return STATUS_SUCCESS;
}

NTSTATUS
BthHciCommand(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ USHORT Opcode,
    _In_reads_bytes_opt_(Length) const UCHAR *Parameters,
    _In_ UCHAR Length,
    _Out_writes_bytes_opt_(ResultSize) PUCHAR Result,
    _In_ ULONG ResultSize)
{
    PBTHMINI_COMMAND Command = BthHciAllocateCommand(Opcode, Parameters, Length, FALSE);
    LARGE_INTEGER Start, Now;
    NTSTATUS Status;

    if (!Command)
        return STATUS_INSUFFICIENT_RESOURCES;
    InsertTailList(&Dev->CommandsPending, &Command->Link);
    BthHciKick(Dev);

    KeQueryTickCount(&Start);
    for (;;)
    {
        BthHciProcessRx(Dev);
        if (Command->Done)
            break;
        KeQueryTickCount(&Now);
        if ((ULONGLONG)(Now.QuadPart - Start.QuadPart) * KeQueryTimeIncrement() >= BTHMINI_COMMAND_TIMEOUT_MS * 10000ULL ||
            !BthMiniWaitWork(Dev, 100))
        {
            RemoveEntryList(&Command->Link);
            ExFreePoolWithTag(Command, BTHMINI_TAG);
            DPRINT1("BTHMINI: command 0x%04x timed out\n", Opcode);
            return Dev->Stopping ? STATUS_CANCELLED : STATUS_IO_TIMEOUT;
        }
    }

    Status = Command->Status;
    if (Result && ResultSize)
    {
        RtlZeroMemory(Result, ResultSize);
        RtlCopyMemory(Result, Command->Result, min(ResultSize, Command->ResultLength));
    }
    ExFreePoolWithTag(Command, BTHMINI_TAG);
    return Status;
}

VOID
BthHciFlushCommands(
    _In_ PBTHMINI_DEVICE Dev)
{
    while (!IsListEmpty(&Dev->CommandsPending))
        ExFreePoolWithTag(CONTAINING_RECORD(RemoveHeadList(&Dev->CommandsPending), BTHMINI_COMMAND, Link), BTHMINI_TAG);
    while (!IsListEmpty(&Dev->CommandsSent))
        ExFreePoolWithTag(CONTAINING_RECORD(RemoveHeadList(&Dev->CommandsSent), BTHMINI_COMMAND, Link), BTHMINI_TAG);
}

static
VOID
BthHciReadComputerName(
    _Out_writes_(BTH_MAX_NAME_SIZE) PCHAR Name)
{
    static const WCHAR KeyName[] = L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Control\\ComputerName\\ComputerName";
    UCHAR Buffer[sizeof(KEY_VALUE_PARTIAL_INFORMATION) + 64 * sizeof(WCHAR)];
    PKEY_VALUE_PARTIAL_INFORMATION Value = (PKEY_VALUE_PARTIAL_INFORMATION)Buffer;
    UNICODE_STRING String, ValueName = RTL_CONSTANT_STRING(L"ComputerName");
    OBJECT_ATTRIBUTES Attributes;
    ANSI_STRING Ansi;
    HANDLE Key;
    ULONG Size;
    NTSTATUS Status;

    RtlStringCbCopyA(Name, BTH_MAX_NAME_SIZE, "LiberNT");
    RtlInitUnicodeString(&String, KeyName);
    InitializeObjectAttributes(&Attributes, &String, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NULL, NULL);
    if (!NT_SUCCESS(ZwOpenKey(&Key, KEY_READ, &Attributes)))
        return;
    Status = ZwQueryValueKey(Key, &ValueName, KeyValuePartialInformation, Value, sizeof(Buffer) - sizeof(WCHAR), &Size);
    ZwClose(Key);
    if (!NT_SUCCESS(Status) || Value->Type != REG_SZ || Value->DataLength < sizeof(WCHAR))
        return;

    String.Buffer = (PWCH)Value->Data;
    String.Length = (USHORT)(Value->DataLength - sizeof(WCHAR));
    String.MaximumLength = (USHORT)Value->DataLength;
    while (String.Length && !String.Buffer[String.Length / sizeof(WCHAR) - 1])
        String.Length -= sizeof(WCHAR);
    Ansi.Buffer = Name;
    Ansi.Length = 0;
    Ansi.MaximumLength = BTH_MAX_NAME_SIZE - 1;
    if (NT_SUCCESS(RtlUnicodeStringToAnsiString(&Ansi, &String, FALSE)))
        Name[Ansi.Length] = '\0';
}

NTSTATUS
BthHciWriteScanEnable(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ UCHAR ScanEnable)
{
    NTSTATUS Status = BthHciCommand(Dev, HCI_WRITE_SCAN_ENABLE, &ScanEnable, 1, NULL, 0);

    if (NT_SUCCESS(Status))
        Dev->ScanEnable = ScanEnable;
    return Status;
}

NTSTATUS
BthHciInitialize(
    _In_ PBTHMINI_DEVICE Dev)
{
    UCHAR Result[255], Parameters[248];
    ULONGLONG EventMask = 0;
    ULONG Index, NameLength;
    NTSTATUS Status;

    Status = BthHciCommand(Dev, HCI_RESET, NULL, 0, NULL, 0);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = BthHciCommand(Dev, HCI_READ_LOCAL_VERSION, NULL, 0, Result, 9);
    if (!NT_SUCCESS(Status))
        return Status;
    Dev->HciVersion = Result[1];
    Dev->HciRevision = (USHORT)(Result[2] | (Result[3] << 8));
    Dev->LmpVersion = Result[4];
    Dev->Manufacturer = (USHORT)(Result[5] | (Result[6] << 8));
    Dev->LmpSubversion = (USHORT)(Result[7] | (Result[8] << 8));

    Status = BthHciCommand(Dev, HCI_READ_LOCAL_COMMANDS, NULL, 0, Result, 65);
    if (NT_SUCCESS(Status))
        RtlCopyMemory(Dev->SupportedCommands, Result + 1, sizeof(Dev->SupportedCommands));

    Status = BthHciCommand(Dev, HCI_READ_LOCAL_FEATURES, NULL, 0, Result, 9);
    if (!NT_SUCCESS(Status))
        return Status;
    Dev->LmpFeatures = 0;
    for (Index = 0; Index < 8; Index++)
        Dev->LmpFeatures |= (ULONGLONG)Result[1 + Index] << (Index * 8);

    Status = BthHciCommand(Dev, HCI_READ_BD_ADDR, NULL, 0, Result, 7);
    if (!NT_SUCCESS(Status))
        return Status;
    Dev->Address = 0;
    for (Index = 0; Index < 6; Index++)
        Dev->Address |= (BTH_ADDR)Result[1 + Index] << (Index * 8);

    Status = BthHciCommand(Dev, HCI_READ_BUFFER_SIZE, NULL, 0, Result, 8);
    if (!NT_SUCCESS(Status))
        return Status;
    Dev->AclMtu = (USHORT)(Result[1] | (Result[2] << 8));
    Dev->AclPackets = (USHORT)(Result[4] | (Result[5] << 8));
    Dev->AclCredits = Dev->AclPackets;

    for (Index = 0; Index < RTL_NUMBER_OF(BthHciMaskedEvents); Index++)
        EventMask |= 1ULL << (BthHciMaskedEvents[Index] - 1);
    for (Index = 0; Index < 8; Index++)
        Parameters[Index] = (UCHAR)(EventMask >> (Index * 8));
    BthHciCommand(Dev, HCI_SET_EVENT_MASK, Parameters, 8, NULL, 0);

    if (LMP_FEATURE(Dev->LmpFeatures, 6, 3))
    {
        Parameters[0] = 1;
        BthHciCommand(Dev, HCI_WRITE_SIMPLE_PAIRING_MODE, Parameters, 1, NULL, 0);
    }

    Parameters[0] = LMP_FEATURE(Dev->LmpFeatures, 6, 0) ? 2 : (LMP_FEATURE(Dev->LmpFeatures, 3, 6) ? 1 : 0);
    BthHciCommand(Dev, HCI_WRITE_INQUIRY_MODE, Parameters, 1, NULL, 0);

    Parameters[0] = 1;
    Parameters[1] = 0x33;
    Parameters[2] = 0x8B;
    Parameters[3] = 0x9E;
    BthHciCommand(Dev, HCI_WRITE_CURRENT_IAC_LAP, Parameters, 4, NULL, 0);

    Dev->ClassOfDevice = BTHMINI_DEFAULT_COD;
    Parameters[0] = (UCHAR)Dev->ClassOfDevice;
    Parameters[1] = (UCHAR)(Dev->ClassOfDevice >> 8);
    Parameters[2] = (UCHAR)(Dev->ClassOfDevice >> 16);
    BthHciCommand(Dev, HCI_WRITE_CLASS_OF_DEVICE, Parameters, 3, NULL, 0);

    BthHciReadComputerName(Dev->Name);
    RtlZeroMemory(Parameters, sizeof(Parameters));
    RtlCopyMemory(Parameters, Dev->Name, strlen(Dev->Name));
    BthHciCommand(Dev, HCI_WRITE_LOCAL_NAME, Parameters, 248, NULL, 0);

    if (LMP_FEATURE(Dev->LmpFeatures, 6, 0))
    {
        RtlZeroMemory(Result, 241);
        NameLength = (ULONG)min(strlen(Dev->Name), 238);
        Result[1] = (UCHAR)(NameLength + 1);
        Result[2] = 0x09;
        RtlCopyMemory(Result + 3, Dev->Name, NameLength);
        BthHciCommand(Dev, HCI_WRITE_EIR, Result, 241, NULL, 0);
    }

    Parameters[0] = 0x05;
    Parameters[1] = 0x00;
    BthHciCommand(Dev, HCI_WRITE_DEFAULT_LINK_POLICY, Parameters, 2, NULL, 0);

    Status = BthHciWriteScanEnable(Dev, HCI_SCAN_PAGE);
    if (!NT_SUCCESS(Status))
        return Status;

    DPRINT1("BTHMINI: radio %02x:%02x:%02x:%02x:%02x:%02x \"%s\" HCI %u.%04x LMP %u.%04x manufacturer %u, ACL %u x %u\n",
            (UCHAR)(Dev->Address >> 40), (UCHAR)(Dev->Address >> 32), (UCHAR)(Dev->Address >> 24),
            (UCHAR)(Dev->Address >> 16), (UCHAR)(Dev->Address >> 8), (UCHAR)Dev->Address, Dev->Name,
            Dev->HciVersion, Dev->HciRevision, Dev->LmpVersion, Dev->LmpSubversion, Dev->Manufacturer,
            Dev->AclPackets, Dev->AclMtu);
    return STATUS_SUCCESS;
}
