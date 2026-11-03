/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Cancelling a TDI receive while tcpip.sys is queuing it
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>
#include <tdikrnl.h>
#include <ndk/rtlfuncs.h>

#include "tcpip.h"

#define TAG_TEST 'tseT'
#define CANCEL_ROUNDS 50
#define RECEIVE_SIZE 64

typedef struct _CANCEL_CONTEXT
{
    PIRP Irp;
    KEVENT Go;
} CANCEL_CONTEXT, *PCANCEL_CONTEXT;

static
NTSTATUS
NTAPI
CancelTestCompletion(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_ PVOID Context)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(Irp);

    KeSetEvent((PKEVENT)Context, IO_NETWORK_INCREMENT, FALSE);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

static
NTSTATUS
CancelTestOpen(
    _In_ PCSTR EaName,
    _In_ USHORT EaNameLength,
    _In_ PVOID EaValue,
    _In_ USHORT EaValueLength,
    _Out_ PHANDLE Handle)
{
    UNICODE_STRING TcpDeviceName = RTL_CONSTANT_STRING(L"\\Device\\Tcp");
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK StatusBlock;
    PFILE_FULL_EA_INFORMATION Ea;
    ULONG EaSize;
    NTSTATUS Status;

    EaSize = FIELD_OFFSET(FILE_FULL_EA_INFORMATION, EaName[EaNameLength]) + 1 + EaValueLength;
    Ea = ExAllocatePoolZero(NonPagedPool, EaSize, TAG_TEST);
    if (!Ea)
        return STATUS_INSUFFICIENT_RESOURCES;

    Ea->EaNameLength = (UCHAR)EaNameLength;
    Ea->EaValueLength = EaValueLength;
    RtlCopyMemory(Ea->EaName, EaName, EaNameLength);
    RtlCopyMemory(&Ea->EaName[EaNameLength + 1], EaValue, EaValueLength);

    InitializeObjectAttributes(&ObjectAttributes,
                               &TcpDeviceName,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               NULL,
                               NULL);
    Status = ZwCreateFile(Handle,
                          GENERIC_READ | GENERIC_WRITE,
                          &ObjectAttributes,
                          &StatusBlock,
                          NULL,
                          FILE_ATTRIBUTE_NORMAL,
                          FILE_SHARE_READ | FILE_SHARE_WRITE,
                          FILE_OPEN_IF,
                          0,
                          Ea,
                          EaSize);
    ExFreePoolWithTag(Ea, TAG_TEST);
    return Status;
}

static
NTSTATUS
CancelTestCall(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_ PKEVENT Event)
{
    NTSTATUS Status;

    KeClearEvent(Event);
    IoSetCompletionRoutine(Irp, CancelTestCompletion, Event, TRUE, TRUE, TRUE);
    Status = IoCallDriver(DeviceObject, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(Event, Executive, KernelMode, FALSE, NULL);
        Status = Irp->IoStatus.Status;
    }
    return Status;
}

static KSTART_ROUTINE CancelTestThread;
static
VOID
NTAPI
CancelTestThread(
    _In_ PVOID Parameter)
{
    PCANCEL_CONTEXT Context = Parameter;

    KeWaitForSingleObject(&Context->Go, Executive, KernelMode, FALSE, NULL);
    KeStallExecutionProcessor(1000);
    IoCancelIrp(Context->Irp);
}

static
VOID
TestCancelReceive(VOID)
{
    TA_IP_ADDRESS LocalAddress, RemoteAddress;
    TDI_CONNECTION_INFORMATION RequestInfo;
    CONNECTION_CONTEXT ConnectionContext = (CONNECTION_CONTEXT)(ULONG_PTR)0xCA11CE1DULL;
    HANDLE AddressHandle = NULL, ConnectionHandle = NULL;
    PFILE_OBJECT ConnectionFileObject = NULL;
    PDEVICE_OBJECT DeviceObject;
    LPCWSTR Terminator;
    IN_ADDR Loopback;
    CANCEL_CONTEXT Context;
    LARGE_INTEGER Timeout;
    PVOID Buffer;
    KEVENT Event;
    PIRP Irp;
    PMDL Mdl;
    NTSTATUS Status;
    ULONG Round;

    Status = RtlIpv4StringToAddressW(L"127.0.0.1", TRUE, &Terminator, &Loopback);
    ok_eq_hex(Status, STATUS_SUCCESS);

    RtlZeroMemory(&LocalAddress, sizeof(LocalAddress));
    LocalAddress.TAAddressCount = 1;
    LocalAddress.Address[0].AddressType = TDI_ADDRESS_TYPE_IP;
    LocalAddress.Address[0].AddressLength = TDI_ADDRESS_LENGTH_IP;
    LocalAddress.Address[0].Address[0].sin_port = RtlUshortByteSwap(TEST_CANCEL_CLIENT_PORT);
    LocalAddress.Address[0].Address[0].in_addr = Loopback.S_un.S_addr;
    Status = CancelTestOpen(TdiTransportAddress, TDI_TRANSPORT_ADDRESS_LENGTH,
                            &LocalAddress, sizeof(LocalAddress), &AddressHandle);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;

    Status = CancelTestOpen(TdiConnectionContext, TDI_CONNECTION_CONTEXT_LENGTH,
                            &ConnectionContext, sizeof(ConnectionContext), &ConnectionHandle);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    Status = ObReferenceObjectByHandle(ConnectionHandle, GENERIC_READ, *IoFileObjectType,
                                       KernelMode, (PVOID *)&ConnectionFileObject, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        goto Cleanup;
    DeviceObject = IoGetRelatedDeviceObject(ConnectionFileObject);
    KeInitializeEvent(&Event, NotificationEvent, FALSE);

    Irp = IoAllocateIrp(DeviceObject->StackSize, FALSE);
    ok(Irp != NULL, "IoAllocateIrp failed\n");
    if (!Irp)
        goto Cleanup;
    TdiBuildAssociateAddress(Irp, DeviceObject, ConnectionFileObject, NULL, NULL, AddressHandle);
    Status = CancelTestCall(DeviceObject, Irp, &Event);
    ok_eq_hex(Status, STATUS_SUCCESS);
    IoFreeIrp(Irp);

    RtlZeroMemory(&RemoteAddress, sizeof(RemoteAddress));
    RemoteAddress.TAAddressCount = 1;
    RemoteAddress.Address[0].AddressType = TDI_ADDRESS_TYPE_IP;
    RemoteAddress.Address[0].AddressLength = TDI_ADDRESS_LENGTH_IP;
    RemoteAddress.Address[0].Address[0].sin_port = RtlUshortByteSwap(TEST_CANCEL_SERVER_PORT);
    RemoteAddress.Address[0].Address[0].in_addr = Loopback.S_un.S_addr;
    RtlZeroMemory(&RequestInfo, sizeof(RequestInfo));
    RequestInfo.RemoteAddressLength = sizeof(RemoteAddress);
    RequestInfo.RemoteAddress = &RemoteAddress;

    Irp = IoAllocateIrp(DeviceObject->StackSize, FALSE);
    ok(Irp != NULL, "IoAllocateIrp failed\n");
    if (!Irp)
        goto Cleanup;
    TdiBuildConnect(Irp, DeviceObject, ConnectionFileObject, NULL, NULL, NULL, &RequestInfo, NULL);
    Status = CancelTestCall(DeviceObject, Irp, &Event);
    ok_eq_hex(Status, STATUS_SUCCESS);
    IoFreeIrp(Irp);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    Buffer = ExAllocatePoolZero(NonPagedPool, RECEIVE_SIZE, TAG_TEST);
    ok(Buffer != NULL, "Buffer allocation failed\n");
    if (!Buffer)
        goto Cleanup;

    for (Round = 0; Round < CANCEL_ROUNDS; Round++)
    {
        PKTHREAD Thread;

        Irp = IoAllocateIrp(DeviceObject->StackSize, FALSE);
        Mdl = IoAllocateMdl(Buffer, RECEIVE_SIZE, FALSE, FALSE, NULL);
        if (!Irp || !Mdl)
        {
            ok(0, "Round %lu: allocation failed\n", Round);
            if (Irp)
                IoFreeIrp(Irp);
            if (Mdl)
                IoFreeMdl(Mdl);
            break;
        }
        MmBuildMdlForNonPagedPool(Mdl);
        TdiBuildReceive(Irp, DeviceObject, ConnectionFileObject, NULL, NULL,
                        Mdl, TDI_RECEIVE_NORMAL, RECEIVE_SIZE);
        KeClearEvent(&Event);
        IoSetCompletionRoutine(Irp, CancelTestCompletion, &Event, TRUE, TRUE, TRUE);

        Context.Irp = Irp;
        KeInitializeEvent(&Context.Go, NotificationEvent, FALSE);
        Thread = KmtStartThread(CancelTestThread, &Context);
        KeSetEvent(&Context.Go, IO_NO_INCREMENT, FALSE);
        IoCallDriver(DeviceObject, Irp);

        Timeout.QuadPart = -30000000LL;
        Status = KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, &Timeout);
        KmtFinishThread(Thread, NULL);
        if (Status == STATUS_TIMEOUT)
        {
            ok(0, "Round %lu: the cancelled receive did not complete\n", Round);
            ObDereferenceObject(ConnectionFileObject);
            ConnectionFileObject = NULL;
            ZwClose(ConnectionHandle);
            ConnectionHandle = NULL;
            KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
            IoFreeMdl(Mdl);
            IoFreeIrp(Irp);
            break;
        }

        ok_eq_hex(Irp->IoStatus.Status, STATUS_CANCELLED);
        IoFreeMdl(Mdl);
        IoFreeIrp(Irp);
    }

    ExFreePoolWithTag(Buffer, TAG_TEST);

Cleanup:
    if (ConnectionFileObject)
        ObDereferenceObject(ConnectionFileObject);
    if (ConnectionHandle)
        ZwClose(ConnectionHandle);
    if (AddressHandle)
        ZwClose(AddressHandle);
}

static KSTART_ROUTINE RunCancelTest;
static
VOID
NTAPI
RunCancelTest(
    _In_ PVOID Context)
{
    UNREFERENCED_PARAMETER(Context);

    TestCancelReceive();
}

KMT_MESSAGE_HANDLER TestCancelReceiveHandler;
NTSTATUS
TestCancelReceiveHandler(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ ULONG ControlCode,
    _In_opt_ PVOID Buffer,
    _In_ SIZE_T InLength,
    _Inout_ PSIZE_T OutLength)
{
    PKTHREAD Thread;

    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(ControlCode);
    UNREFERENCED_PARAMETER(Buffer);
    UNREFERENCED_PARAMETER(InLength);
    UNREFERENCED_PARAMETER(OutLength);

    Thread = KmtStartThread(RunCancelTest, NULL);
    KmtFinishThread(Thread, NULL);

    return STATUS_SUCCESS;
}
