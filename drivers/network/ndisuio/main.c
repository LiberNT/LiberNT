/*
 * COPYRIGHT:   See COPYING in the top level directory
 * PROJECT:     ReactOS NDIS User I/O driver
 * FILE:        main.c
 * PURPOSE:     Driver entry point and protocol initialization
 * PROGRAMMERS: Cameron Gutman (cameron.gutman@reactos.org)
 */

#include "ndisuio.h"

#define NDEBUG
#include <debug.h>

PDEVICE_OBJECT GlobalDeviceObject;
NDIS_HANDLE GlobalProtocolHandle;
KSPIN_LOCK GlobalAdapterListLock;
LIST_ENTRY GlobalAdapterList;

NDIS_STRING ProtocolName = RTL_CONSTANT_STRING(L"NDISUIO");

VOID NTAPI NduUnload(PDRIVER_OBJECT DriverObject)
{
    DPRINT("NDISUIO: Unloaded\n");
}

NTSTATUS
NTAPI
DriverEntry(PDRIVER_OBJECT DriverObject,
            PUNICODE_STRING RegistryPath)
{
    NDIS_STATUS Status;
    NDIS_PROTOCOL_DRIVER_CHARACTERISTICS Chars;
    UNICODE_STRING NtDeviceName = RTL_CONSTANT_STRING(NDISUIO_DEVICE_NAME_NT);
    UNICODE_STRING DosDeviceName = RTL_CONSTANT_STRING(NDISUIO_DEVICE_NAME_DOS);

    /* Setup dispatch functions */
    DriverObject->MajorFunction[IRP_MJ_CREATE] = NduDispatchCreate;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = NduDispatchClose;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = NduDispatchDeviceControl;
    DriverObject->MajorFunction[IRP_MJ_READ] = NduDispatchRead;
    DriverObject->MajorFunction[IRP_MJ_WRITE] = NduDispatchWrite;
    DriverObject->DriverUnload = NduUnload;

    /* Setup global state */
    InitializeListHead(&GlobalAdapterList);
    KeInitializeSpinLock(&GlobalAdapterListLock);

    /* Create the NDISUIO device object */
    Status = IoCreateDevice(DriverObject,
                            0,
                            &NtDeviceName,
                            FILE_DEVICE_SECURE_OPEN,
                            0,
                            FALSE,
                            &GlobalDeviceObject);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Failed to create device object with status 0x%x\n", Status);
        return Status;
    }

    GlobalDeviceObject->Flags |= DO_BUFFERED_IO;

    /* Create a symbolic link into the DOS devices namespace */
    Status = IoCreateSymbolicLink(&DosDeviceName, &NtDeviceName);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Failed to create symbolic link with status 0x%x\n", Status);
        IoDeleteDevice(GlobalDeviceObject);
        return Status;
    }

    /* Register the protocol with NDIS */
    RtlZeroMemory(&Chars, sizeof(Chars));
    Chars.Header.Type = NDIS_OBJECT_TYPE_PROTOCOL_DRIVER_CHARACTERISTICS;
    Chars.Header.Revision = NDIS_PROTOCOL_DRIVER_CHARACTERISTICS_REVISION_1;
    Chars.Header.Size = NDIS_SIZEOF_PROTOCOL_DRIVER_CHARACTERISTICS_REVISION_1;
    Chars.MajorNdisVersion = NDIS_MAJOR_VERSION;
    Chars.MinorNdisVersion = NDIS_MINOR_VERSION;
    Chars.Name = ProtocolName;
    Chars.BindAdapterHandlerEx = NduBindAdapter;
    Chars.UnbindAdapterHandlerEx = NduUnbindAdapter;
    Chars.OpenAdapterCompleteHandlerEx = NduOpenAdapterComplete;
    Chars.CloseAdapterCompleteHandlerEx = NduCloseAdapterComplete;
    Chars.NetPnPEventHandler = NduNetPnPEvent;
    Chars.OidRequestCompleteHandler = NduRequestComplete;
    Chars.StatusHandlerEx = NduStatus;
    Chars.ReceiveNetBufferListsHandler = NduReceive;
    Chars.SendNetBufferListsCompleteHandler = NduSendComplete;

    Status = NdisRegisterProtocolDriver(NULL,
                                        &Chars,
                                        &GlobalProtocolHandle);
    if (Status != NDIS_STATUS_SUCCESS)
    {
        DPRINT1("Failed to register protocol with status 0x%x\n", Status);
        IoDeleteSymbolicLink(&DosDeviceName);
        IoDeleteDevice(GlobalDeviceObject);
        return Status;
    }

    DPRINT("NDISUIO: Loaded\n");

    return STATUS_SUCCESS;
}
