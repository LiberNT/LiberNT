/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     An HD Audio function device answers the standard bus interface query
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>
#include <wdmguid.h>

#define NDEBUG
#include <debug.h>

static const GUID AudioCategory =
    { 0x6994AD04, 0x93EF, 0x11D0, { 0xA3, 0xCC, 0x00, 0xA0, 0xC9, 0x22, 0x31, 0x96 } };

static
BOOLEAN
IsHdAudioDevice(
    _In_ PDEVICE_OBJECT Pdo)
{
    WCHAR Enumerator[16];
    ULONG Length = 0;
    NTSTATUS Status;

    Status = IoGetDeviceProperty(Pdo, DevicePropertyEnumeratorName, sizeof(Enumerator), Enumerator, &Length);
    return NT_SUCCESS(Status) && _wcsicmp(Enumerator, L"HDAUDIO") == 0;
}

static
NTSTATUS
QueryBusInterface(
    _In_ PDEVICE_OBJECT Pdo,
    _Out_ PBUS_INTERFACE_STANDARD Interface)
{
    PDEVICE_OBJECT Top;
    PIO_STACK_LOCATION Stack;
    IO_STATUS_BLOCK Iosb;
    KEVENT Event;
    NTSTATUS Status;
    PIRP Irp;

    RtlZeroMemory(Interface, sizeof(*Interface));
    Top = IoGetAttachedDeviceReference(Pdo);
    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    Irp = IoBuildSynchronousFsdRequest(IRP_MJ_PNP, Top, NULL, 0, NULL, &Event, &Iosb);
    if (Irp == NULL)
    {
        ObDereferenceObject(Top);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    Irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
    Stack = IoGetNextIrpStackLocation(Irp);
    Stack->MinorFunction = IRP_MN_QUERY_INTERFACE;
    Stack->Parameters.QueryInterface.InterfaceType = &GUID_BUS_INTERFACE_STANDARD;
    Stack->Parameters.QueryInterface.Size = sizeof(*Interface);
    Stack->Parameters.QueryInterface.Version = 1;
    Stack->Parameters.QueryInterface.Interface = (PINTERFACE)Interface;
    Stack->Parameters.QueryInterface.InterfaceSpecificData = NULL;
    Status = IoCallDriver(Top, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = Iosb.Status;
    }
    ObDereferenceObject(Top);
    return Status;
}

START_TEST(HdAudBusInterface)
{
    BUS_INTERFACE_STANDARD Interface;
    PFILE_OBJECT FileObject;
    PDEVICE_OBJECT DeviceObject;
    PDEVICE_OBJECT Pdo;
    UNICODE_STRING Name;
    PZZWSTR List = NULL;
    PWSTR Link;
    BOOLEAN Tested = FALSE;
    ULONG Id;
    ULONG Read;
    NTSTATUS Status;

    Status = IoGetDeviceInterfaces(&AudioCategory, NULL, 0, &List);
    if (!NT_SUCCESS(Status) || List == NULL || *List == UNICODE_NULL)
    {
        skip(FALSE, "No audio device interface (0x%08lx)\n", Status);
        if (List != NULL)
            ExFreePool(List);
        return;
    }

    for (Link = List; *Link != UNICODE_NULL && !Tested; Link += wcslen(Link) + 1)
    {
        RtlInitUnicodeString(&Name, Link);
        Status = IoGetDeviceObjectPointer(&Name, FILE_READ_ATTRIBUTES, &FileObject, &DeviceObject);
        if (!NT_SUCCESS(Status))
            continue;
        Pdo = IoGetDeviceAttachmentBaseRef(DeviceObject);
        if (Pdo != NULL && IsHdAudioDevice(Pdo))
        {
            Tested = TRUE;
            Status = QueryBusInterface(Pdo, &Interface);
            ok_eq_hex(Status, STATUS_SUCCESS);
            if (NT_SUCCESS(Status))
            {
                ok(Interface.Size >= FIELD_OFFSET(BUS_INTERFACE_STANDARD, GetBusData) + sizeof(Interface.GetBusData),
                   "Interface size %u\n", Interface.Size);
                ok(Interface.GetBusData != NULL, "No GetBusData\n");
                ok(Interface.GetDmaAdapter != NULL, "No GetDmaAdapter\n");
                if (Interface.GetBusData != NULL)
                {
                    Id = MAXULONG;
                    Read = Interface.GetBusData(Interface.Context, PCI_WHICHSPACE_CONFIG, &Id, 0, sizeof(Id));
                    ok_eq_ulong(Read, (ULONG)sizeof(Id));
                    ok((Id & 0xFFFF) != PCI_INVALID_VENDORID && (Id & 0xFFFF) != 0,
                       "Controller ID 0x%08lx through %wZ\n", Id, &Name);
                }
                if (Interface.InterfaceDereference != NULL)
                    Interface.InterfaceDereference(Interface.Context);
            }
        }
        if (Pdo != NULL)
            ObDereferenceObject(Pdo);
        ObDereferenceObject(FileObject);
    }
    if (!Tested)
        skip(FALSE, "No HD Audio function device\n");
    ExFreePool(List);
}
