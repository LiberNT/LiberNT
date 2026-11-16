/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     A device header create item slot emptied after it was opened takes a new create item
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>
#include <ks.h>

static KSOBJECT_CREATE_ITEM SlotItems[2];
static PVOID SlotCreateContext;
static ULONG SlotCreateCalls;

static
NTSTATUS
NTAPI
SlotCreate(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    PKSOBJECT_CREATE_ITEM CreateItem = KSCREATE_ITEM_IRP_STORAGE(Irp);

    UNREFERENCED_PARAMETER(DeviceObject);

    SlotCreateContext = (CreateItem != NULL) ? CreateItem->Context : NULL;
    SlotCreateCalls++;

    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
SlotKeepIrp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_opt_ PVOID Context)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(Irp);
    UNREFERENCED_PARAMETER(Context);

    return STATUS_MORE_PROCESSING_REQUIRED;
}

static
NTSTATUS
SlotOpen(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PCWSTR Name)
{
    FILE_OBJECT FileObject;
    PIO_STACK_LOCATION IoStack;
    NTSTATUS Status;
    PIRP Irp;

    RtlZeroMemory(&FileObject, sizeof(FileObject));
    FileObject.Type = IO_TYPE_FILE;
    FileObject.Size = sizeof(FileObject);
    FileObject.DeviceObject = DeviceObject;
    RtlInitUnicodeString(&FileObject.FileName, Name);

    Irp = IoAllocateIrp(2, FALSE);
    if (Irp == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    IoSetNextIrpStackLocation(Irp);
    IoSetCompletionRoutine(Irp, SlotKeepIrp, NULL, TRUE, TRUE, TRUE);

    IoStack = IoGetNextIrpStackLocation(Irp);
    IoStack->MajorFunction = IRP_MJ_CREATE;
    IoStack->DeviceObject = DeviceObject;
    IoStack->FileObject = &FileObject;
    IoSetNextIrpStackLocation(Irp);

    Status = KsDispatchIrp(DeviceObject, Irp);

    IoFreeIrp(Irp);
    return Status;
}

START_TEST(KsCreateItemSlot)
{
    PVOID FirstContext = (PVOID)(ULONG_PTR)0x1001;
    PVOID SecondContext = (PVOID)(ULONG_PTR)0x1002;
    PVOID ThirdContext = (PVOID)(ULONG_PTR)0x1003;
    PDEVICE_OBJECT DeviceObject;
    KSDEVICE_HEADER Header;
    NTSTATUS Status;

    RtlZeroMemory(SlotItems, sizeof(SlotItems));
    SlotCreateContext = NULL;
    SlotCreateCalls = 0;

    Status = IoCreateDevice(KmtDriverObject,
                            sizeof(KSDEVICE_HEADER),
                            NULL,
                            FILE_DEVICE_KS,
                            0,
                            FALSE,
                            &DeviceObject);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (skip(NT_SUCCESS(Status), "No device object\n"))
        return;

    Status = KsAllocateDeviceHeader(&Header, RTL_NUMBER_OF(SlotItems), SlotItems);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (skip(NT_SUCCESS(Status), "No device header\n"))
    {
        IoDeleteDevice(DeviceObject);
        return;
    }

    *(KSDEVICE_HEADER *)DeviceObject->DeviceExtension = Header;

    Status = KsAddObjectCreateItemToDeviceHeader(Header, SlotCreate, FirstContext, L"KmtSlotFirst", NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_pointer(SlotItems[0].Context, FirstContext);

    Status = KsAddObjectCreateItemToDeviceHeader(Header, SlotCreate, SecondContext, L"KmtSlotSecond", NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_pointer(SlotItems[1].Context, SecondContext);

    Status = KsAddObjectCreateItemToDeviceHeader(Header, SlotCreate, ThirdContext, L"KmtSlotThird", NULL);
    ok_eq_hex(Status, STATUS_ALLOTTED_SPACE_EXCEEDED);

    Status = SlotOpen(DeviceObject, L"\\KmtSlotFirst");
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulong(SlotCreateCalls, 1UL);
    ok_eq_pointer(SlotCreateContext, FirstContext);

    /* An empty item is one with no create dispatch function */
    SlotItems[0].Create = NULL;

    Status = KsAddObjectCreateItemToDeviceHeader(Header, SlotCreate, ThirdContext, L"KmtSlotThird", NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_pointer(SlotItems[0].Context, ThirdContext);

    Status = SlotOpen(DeviceObject, L"\\KmtSlotThird");
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulong(SlotCreateCalls, 2UL);
    ok_eq_pointer(SlotCreateContext, ThirdContext);

    KsFreeDeviceHeader(Header);
    IoDeleteDevice(DeviceObject);
}

/* EOF */
