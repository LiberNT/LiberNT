/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The driver context of a create IRP belongs to the driver that handles it
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>

#define NDEBUG
#include <debug.h>

#define OWNED_SIZE 64
#define OWNED_FILL 0x5A
#define OWNED_TAG  'xCDK'

static PDEVICE_OBJECT ContextDevice;
static PDRIVER_DISPATCH SavedDispatch[IRP_MJ_MAXIMUM_FUNCTION + 1];
static const UCHAR HookedFunctions[] = { IRP_MJ_CREATE, IRP_MJ_CLEANUP, IRP_MJ_CLOSE };
static PVOID SeenContext[4];
static PUCHAR OwnedBlock;
static ULONG CreatesSeen;

static
NTSTATUS
NTAPI
TestDispatch(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    UCHAR MajorFunction = IoGetCurrentIrpStackLocation(Irp)->MajorFunction;
    ULONG Index;

    if (DeviceObject != ContextDevice)
        return SavedDispatch[MajorFunction](DeviceObject, Irp);

    if (MajorFunction == IRP_MJ_CREATE)
    {
        for (Index = 0; Index < RTL_NUMBER_OF(SeenContext); Index++)
            SeenContext[Index] = Irp->Tail.Overlay.DriverContext[Index];
        Irp->Tail.Overlay.DriverContext[0] = OwnedBlock;
        CreatesSeen++;
    }

    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

static
VOID
HookDispatch(
    _In_ BOOLEAN Hook)
{
    ULONG i;

    for (i = 0; i < RTL_NUMBER_OF(HookedFunctions); i++)
    {
        PVOID *Slot = (PVOID *)&KmtDriverObject->MajorFunction[HookedFunctions[i]];

        if (Hook)
            SavedDispatch[HookedFunctions[i]] = InterlockedExchangePointer(Slot, (PVOID)TestDispatch);
        else
            InterlockedExchangePointer(Slot, (PVOID)SavedDispatch[HookedFunctions[i]]);
    }
}

static
BOOLEAN
OwnedBlockIntact(VOID)
{
    ULONG Index;

    for (Index = 0; Index < OWNED_SIZE; Index++)
    {
        if (OwnedBlock[Index] != OWNED_FILL)
            return FALSE;
    }

    return TRUE;
}

START_TEST(IoCreateDriverContext)
{
    UNICODE_STRING Name = RTL_CONSTANT_STRING(L"\\Device\\KmtestCreateDriverContext");
    OBJECT_ATTRIBUTES Attributes;
    IO_STATUS_BLOCK IoStatus;
    BOOLEAN Intact;
    HANDLE Handle;
    NTSTATUS Status;
    ULONG Index;

    OwnedBlock = ExAllocatePoolWithTag(NonPagedPool, OWNED_SIZE, OWNED_TAG);
    if (skip(OwnedBlock != NULL, "no memory for the driver's block\n"))
        return;
    RtlFillMemory(OwnedBlock, OWNED_SIZE, OWNED_FILL);

    Status = IoCreateDevice(KmtDriverObject, 0, &Name, FILE_DEVICE_UNKNOWN, 0, FALSE, &ContextDevice);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(OwnedBlock, OWNED_TAG);
        return;
    }
    ContextDevice->Flags &= ~DO_DEVICE_INITIALIZING;

    for (Index = 0; Index < RTL_NUMBER_OF(SeenContext); Index++)
        SeenContext[Index] = (PVOID)(ULONG_PTR)-1;
    CreatesSeen = 0;
    HookDispatch(TRUE);

    InitializeObjectAttributes(&Attributes, &Name, OBJ_KERNEL_HANDLE, NULL, NULL);
    Status = ZwCreateFile(&Handle, FILE_READ_ATTRIBUTES, &Attributes, &IoStatus, NULL, 0,
                          FILE_SHARE_READ | FILE_SHARE_WRITE, FILE_OPEN, 0, NULL, 0);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
        ZwClose(Handle);

    HookDispatch(FALSE);
    IoDeleteDevice(ContextDevice);
    ContextDevice = NULL;

    ok_eq_ulong(CreatesSeen, 1UL);
    for (Index = 0; Index < RTL_NUMBER_OF(SeenContext); Index++)
        ok(SeenContext[Index] == NULL, "DriverContext[%lu] of the create IRP is %p, expected NULL\n", Index, SeenContext[Index]);

    Intact = OwnedBlockIntact();
    ok(Intact, "the block the driver left in DriverContext[0] was changed after the create\n");

    /* A block the I/O manager freed must not be freed twice */
    if (Intact)
        ExFreePoolWithTag(OwnedBlock, OWNED_TAG);
    OwnedBlock = NULL;
}
