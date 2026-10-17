/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     WMI data provider path through the ACPI WMI mapping driver
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>
#include <wmistr.h>

static const GUID WmiAcpiDataGuid =
    { 0x8a9e1e6a, 0x9d2f, 0x4c7e, { 0x9b, 0x0b, 0x1c, 0x6b, 0x7e, 0x4d, 0x2a, 0x01 } };
static const GUID WmiAcpiMethodGuid =
    { 0x8a9e1e6a, 0x9d2f, 0x4c7e, { 0x9b, 0x0b, 0x1c, 0x6b, 0x7e, 0x4d, 0x2a, 0x02 } };
static const GUID WmiAcpiEventGuid =
    { 0x8a9e1e6a, 0x9d2f, 0x4c7e, { 0x9b, 0x0b, 0x1c, 0x6b, 0x7e, 0x4d, 0x2a, 0x03 } };
static const GUID WmiAcpiStringGuid =
    { 0x8a9e1e6a, 0x9d2f, 0x4c7e, { 0x9b, 0x0b, 0x1c, 0x6b, 0x7e, 0x4d, 0x2a, 0x04 } };
static const GUID WmiAcpiMissingGuid =
    { 0x8a9e1e6a, 0x9d2f, 0x4c7e, { 0x9b, 0x0b, 0x1c, 0x6b, 0x7e, 0x4d, 0x2a, 0xff } };

#define WMIACPI_INSTANCE_0 L"ACPI\\PNP0C14\\WMITEST_0"
#define WMIACPI_INSTANCE_1 L"ACPI\\PNP0C14\\WMITEST_1"

typedef struct _WMIACPI_EVENT_CONTEXT
{
    KEVENT Event;
    ULONG Flags;
    ULONG DataSize;
    UCHAR Data[8];
    WCHAR Name[64];
    USHORT NameLength;
} WMIACPI_EVENT_CONTEXT, *PWMIACPI_EVENT_CONTEXT;

static
VOID
NTAPI
WmiAcpiEventCallback(
    _In_ PVOID Wnode,
    _In_ PVOID Context)
{
    PWMIACPI_EVENT_CONTEXT EventContext = Context;
    PWNODE_SINGLE_INSTANCE Instance = Wnode;
    USHORT Length;

    EventContext->Flags = Instance->WnodeHeader.Flags;
    EventContext->DataSize = Instance->SizeDataBlock;
    if (Instance->SizeDataBlock <= sizeof(EventContext->Data))
    {
        RtlCopyMemory(EventContext->Data,
                      (PUCHAR)Instance + Instance->DataBlockOffset,
                      Instance->SizeDataBlock);
    }
    if (Instance->OffsetInstanceName)
    {
        Length = *(PUSHORT)((PUCHAR)Instance + Instance->OffsetInstanceName);
        if (Length <= sizeof(EventContext->Name))
        {
            RtlCopyMemory(EventContext->Name,
                          (PUCHAR)Instance + Instance->OffsetInstanceName + sizeof(USHORT),
                          Length);
            EventContext->NameLength = Length;
        }
    }

    KeSetEvent(&EventContext->Event, IO_NO_INCREMENT, FALSE);
}

static
NTSTATUS
WmiAcpiWaitForProvider(
    _In_ PVOID DataBlock)
{
    LARGE_INTEGER Interval;
    ULONG Size, Attempt;
    NTSTATUS Status = STATUS_WMI_GUID_NOT_FOUND;

    Interval.QuadPart = -100 * 10000LL;
    for (Attempt = 0; Attempt < 50; Attempt++)
    {
        Size = 0;
        Status = IoWMIQueryAllData(DataBlock, &Size, NULL);
        if (Status != STATUS_WMI_GUID_NOT_FOUND)
            break;
        KeDelayExecutionThread(KernelMode, FALSE, &Interval);
    }

    return Status;
}

static
ULONG
WmiAcpiMethod(
    _In_ PVOID MethodBlock,
    _In_ ULONG MethodId,
    _In_ ULONG InSize,
    _Inout_updates_bytes_(16) PUCHAR Buffer,
    _Out_ PULONG OutSize)
{
    UNICODE_STRING Instance = RTL_CONSTANT_STRING(WMIACPI_INSTANCE_0);

    *OutSize = 16;
    return IoWMIExecuteMethod(MethodBlock, &Instance, MethodId, InSize, OutSize, Buffer);
}

START_TEST(IoWmiAcpi)
{
    UNICODE_STRING Instance0 = RTL_CONSTANT_STRING(WMIACPI_INSTANCE_0);
    UNICODE_STRING Instance1 = RTL_CONSTANT_STRING(WMIACPI_INSTANCE_1);
    UNICODE_STRING Missing = RTL_CONSTANT_STRING(L"ACPI\\PNP0C14\\WMITEST_7");
    UNICODE_STRING Name;
    PWMIACPI_EVENT_CONTEXT EventContext;
    PVOID DataBlock, MethodBlock, EventBlock, StringBlock, MissingBlock;
    PWNODE_ALL_DATA AllData;
    PWNODE_SINGLE_INSTANCE Single;
    UCHAR Buffer[16], NewData[8] = { 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88 };
    PULONG NameOffsets;
    LARGE_INTEGER Timeout;
    ULONG Size, OutSize, Value;
    NTSTATUS Status;

    Status = IoWMIOpenBlock(&WmiAcpiMissingGuid, WMIGUID_QUERY, &MissingBlock);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        Size = 0;
        Status = IoWMIQueryAllData(MissingBlock, &Size, NULL);
        ok_eq_hex(Status, STATUS_WMI_GUID_NOT_FOUND);
        ObDereferenceObject(MissingBlock);
    }

    Status = IoWMIOpenBlock(&WmiAcpiDataGuid, WMIGUID_QUERY | WMIGUID_SET, &DataBlock);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;

    Status = WmiAcpiWaitForProvider(DataBlock);
    if (skip(Status != STATUS_WMI_GUID_NOT_FOUND, "the WMITEST ACPI WMI device is not present\n"))
    {
        ObDereferenceObject(DataBlock);
        return;
    }

    Size = 0;
    Status = IoWMIQueryAllData(DataBlock, &Size, NULL);
    ok_eq_hex(Status, STATUS_BUFFER_TOO_SMALL);
    ok(Size > sizeof(WNODE_ALL_DATA), "Size = %lu\n", Size);

    AllData = ExAllocatePoolZero(NonPagedPoolNx, Size, 'tmWK');
    if (!skip(AllData != NULL, "out of memory\n"))
    {
        Status = IoWMIQueryAllData(DataBlock, &Size, AllData);
        ok_eq_hex(Status, STATUS_SUCCESS);
        if (NT_SUCCESS(Status))
        {
            ok_eq_ulong(AllData->WnodeHeader.Linkage, 0UL);
            ok(IsEqualGUID(&AllData->WnodeHeader.Guid, &WmiAcpiDataGuid), "Wrong GUID\n");
            ok_eq_ulong(AllData->InstanceCount, 2UL);
            ok_eq_ulong(AllData->WnodeHeader.Flags & WNODE_FLAG_FIXED_INSTANCE_SIZE, 0UL);
            ok_eq_ulong(AllData->OffsetInstanceDataAndLength[0].LengthInstanceData, 8UL);
            ok_eq_ulong(AllData->OffsetInstanceDataAndLength[1].LengthInstanceData, 4UL);
            ok_eq_ulong(AllData->OffsetInstanceDataAndLength[0].OffsetInstanceData % 8, 0UL);
            ok_eq_ulong(AllData->OffsetInstanceDataAndLength[1].OffsetInstanceData % 8, 0UL);
            ok_eq_uint(((PUCHAR)AllData)[AllData->OffsetInstanceDataAndLength[1].OffsetInstanceData], 9);
            ok(AllData->OffsetInstanceNameOffsets != 0, "No instance names\n");
            if (AllData->OffsetInstanceNameOffsets)
            {
                NameOffsets = (PULONG)((PUCHAR)AllData + AllData->OffsetInstanceNameOffsets);
                Name.Length = Name.MaximumLength = *(PUSHORT)((PUCHAR)AllData + NameOffsets[1]);
                Name.Buffer = (PWCHAR)((PUCHAR)AllData + NameOffsets[1] + sizeof(USHORT));
                ok(RtlEqualUnicodeString(&Name, &Instance1, TRUE), "Instance name %wZ\n", &Name);
            }
        }
        ExFreePoolWithTag(AllData, 'tmWK');
    }

    Size = sizeof(Buffer) + 0x100;
    Single = ExAllocatePoolZero(NonPagedPoolNx, Size, 'tmWK');
    if (!skip(Single != NULL, "out of memory\n"))
    {
        Status = IoWMIQuerySingleInstance(DataBlock, &Instance1, &Size, Single);
        ok_eq_hex(Status, STATUS_SUCCESS);
        if (NT_SUCCESS(Status))
        {
            ok_eq_ulong(Single->SizeDataBlock, 4UL);
            ok_eq_uint(((PUCHAR)Single)[Single->DataBlockOffset + 3], 12);
            ok(Single->OffsetInstanceName != 0, "No instance name\n");
        }

        Size = sizeof(Buffer) + 0x100;
        Status = IoWMIQuerySingleInstance(DataBlock, &Missing, &Size, Single);
        ok_eq_hex(Status, STATUS_WMI_INSTANCE_NOT_FOUND);

        Status = IoWMISetSingleInstance(DataBlock, &Instance0, 0, sizeof(NewData), NewData);
        ok_eq_hex(Status, STATUS_SUCCESS);

        Size = sizeof(Buffer) + 0x100;
        Status = IoWMIQuerySingleInstance(DataBlock, &Instance0, &Size, Single);
        ok_eq_hex(Status, STATUS_SUCCESS);
        if (NT_SUCCESS(Status))
        {
            ok_eq_ulong(Single->SizeDataBlock, 8UL);
            ok_eq_uint(((PUCHAR)Single)[Single->DataBlockOffset + 7], 0x88);
        }

        ExFreePoolWithTag(Single, 'tmWK');
    }

    ObDereferenceObject(DataBlock);

    Status = IoWMIOpenBlock(&WmiAcpiStringGuid, WMIGUID_QUERY, &StringBlock);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        Size = sizeof(Buffer) + 0x100;
        Single = ExAllocatePoolZero(NonPagedPoolNx, Size, 'tmWK');
        if (!skip(Single != NULL, "out of memory\n"))
        {
            UNICODE_STRING StringInstance = RTL_CONSTANT_STRING(L"ACPI\\PNP0C14\\WMITEST_0");

            Status = IoWMIQuerySingleInstance(StringBlock, &StringInstance, &Size, Single);
            ok_eq_hex(Status, STATUS_SUCCESS);
            if (NT_SUCCESS(Status))
            {
                ok_eq_ulong(Single->SizeDataBlock, (ULONG)(sizeof(USHORT) + 7 * sizeof(WCHAR)));
                ok_eq_uint(*(PUSHORT)((PUCHAR)Single + Single->DataBlockOffset), 7 * sizeof(WCHAR));
                ok_eq_uint(*(PWCHAR)((PUCHAR)Single + Single->DataBlockOffset + sizeof(USHORT)), L'L');
            }
            ExFreePoolWithTag(Single, 'tmWK');
        }
        ObDereferenceObject(StringBlock);
    }

    Status = IoWMIOpenBlock(&WmiAcpiMethodGuid, WMIGUID_QUERY | WMIGUID_EXECUTE, &MethodBlock);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;

    RtlZeroMemory(Buffer, sizeof(Buffer));
    Buffer[0] = 0xA1;
    Buffer[1] = 0xB2;
    Buffer[2] = 0xC3;
    Status = WmiAcpiMethod(MethodBlock, 1, 3, Buffer, &OutSize);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulong(OutSize, 3UL);
    ok_eq_uint(Buffer[2], 0xC3);

    OutSize = 1;
    Status = IoWMIExecuteMethod(MethodBlock, &Instance0, 2, 0, &OutSize, Buffer);
    ok_eq_hex(Status, STATUS_BUFFER_TOO_SMALL);
    ok_eq_ulong(OutSize, 4UL);

    Status = IoWMIExecuteMethod(MethodBlock, &Missing, 1, 0, &OutSize, Buffer);
    ok_eq_hex(Status, STATUS_WMI_INSTANCE_NOT_FOUND);

    EventContext = ExAllocatePoolZero(NonPagedPoolNx, sizeof(*EventContext), 'tmWK');
    if (skip(EventContext != NULL, "out of memory\n"))
    {
        ObDereferenceObject(MethodBlock);
        return;
    }
    KeInitializeEvent(&EventContext->Event, NotificationEvent, FALSE);

    Status = IoWMIOpenBlock(&WmiAcpiEventGuid, WMIGUID_NOTIFICATION | SYNCHRONIZE, &EventBlock);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        Status = WmiAcpiMethod(MethodBlock, 3, 0, Buffer, &OutSize);
        ok_eq_hex(Status, STATUS_SUCCESS);
        RtlCopyMemory(&Value, Buffer, sizeof(Value));
        ok_eq_ulong(Value, 0UL);

        Status = IoWMISetNotificationCallback(EventBlock, WmiAcpiEventCallback, EventContext);
        ok_eq_hex(Status, STATUS_SUCCESS);

        Status = WmiAcpiMethod(MethodBlock, 3, 0, Buffer, &OutSize);
        ok_eq_hex(Status, STATUS_SUCCESS);
        RtlCopyMemory(&Value, Buffer, sizeof(Value));
        ok_eq_ulong(Value, 1UL);

        Status = WmiAcpiMethod(MethodBlock, 2, 0, Buffer, &OutSize);
        ok_eq_hex(Status, STATUS_SUCCESS);
        RtlCopyMemory(&Value, Buffer, sizeof(Value));
        ok_eq_ulong(Value, 0x1234UL);

        Timeout.QuadPart = -5000 * 10000LL;
        Status = KeWaitForSingleObject(&EventContext->Event, Executive, KernelMode, FALSE, &Timeout);
        ok_eq_hex(Status, STATUS_SUCCESS);
        if (Status == STATUS_SUCCESS)
        {
            ok_eq_ulong(EventContext->Flags & (WNODE_FLAG_EVENT_ITEM | WNODE_FLAG_SINGLE_INSTANCE),
                        (ULONG)(WNODE_FLAG_EVENT_ITEM | WNODE_FLAG_SINGLE_INSTANCE));
            ok_eq_ulong(EventContext->DataSize, 4UL);
            ok_eq_uint(EventContext->Data[0], 0xEF);
            ok_eq_uint(EventContext->Data[3], 0xDE);
            Name.Buffer = EventContext->Name;
            Name.Length = Name.MaximumLength = EventContext->NameLength;
            ok(RtlEqualUnicodeString(&Name, &Instance0, TRUE), "Event instance %wZ\n", &Name);
        }

        Status = IoWMISetNotificationCallback(EventBlock, NULL, NULL);
        ok_eq_hex(Status, STATUS_SUCCESS);

        Status = WmiAcpiMethod(MethodBlock, 3, 0, Buffer, &OutSize);
        ok_eq_hex(Status, STATUS_SUCCESS);
        RtlCopyMemory(&Value, Buffer, sizeof(Value));
        ok_eq_ulong(Value, 0UL);

        ObDereferenceObject(EventBlock);
    }

    if (KeReadStateEvent(&EventContext->Event))
        ExFreePoolWithTag(EventContext, 'tmWK');
    ObDereferenceObject(MethodBlock);
}
