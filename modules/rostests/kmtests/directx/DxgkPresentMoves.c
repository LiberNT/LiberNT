/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Display device IOCTLs: move hints of a dirty-rectangle present are validated
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include <kmt_test.h>
#include <windef.h>
#include <libernt/dwmframe.h>

#define SOURCE_PITCH (64 * sizeof(ULONG))
#define SOURCE_ROWS  64

typedef struct _PRESENT_COMPLETION
{
    NTSTATUS Status;
    KEVENT Event;
} PRESENT_COMPLETION, *PPRESENT_COMPLETION;

static
IO_COMPLETION_ROUTINE
PresentCompleted;

static
NTSTATUS
NTAPI
PresentCompleted(PDEVICE_OBJECT Device, PIRP Irp, PVOID Context)
{
    PPRESENT_COMPLETION Completion = Context;

    UNREFERENCED_PARAMETER(Device);
    Completion->Status = Irp->IoStatus.Status;
    KeSetEvent(&Completion->Event, IO_NO_INCREMENT, FALSE);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

static NTSTATUS
PresentMoves(PDEVICE_OBJECT Device, PVOID Source, ULONG Flags, ULONG MoveCount)
{
    PRESENT_COMPLETION Completion;
    DXGK_PRESENT_DIRTY_RECTS_INPUT Input;
    PIO_STACK_LOCATION Stack;
    NTSTATUS Returned;
    ULONG Index;
    PIRP Irp;

    RtlZeroMemory(&Input, sizeof(Input));
    Input.StructSize = sizeof(Input);
    Input.Flags = Flags;
    Input.RectCount = 1;
    Input.SourcePitch = SOURCE_PITCH;
    Input.SourceSize = SOURCE_PITCH * SOURCE_ROWS;
    Input.Source = (ULONG_PTR)Source;
    Input.MoveCount = MoveCount;
    for (Index = 0; Index < MoveCount && Index < DWM_PRESENT_MAX_MOVES; Index++)
    {
        Input.Moves[Index].SourcePoint.x = 0;
        Input.Moves[Index].SourcePoint.y = 0;
        Input.Moves[Index].DestRect.left = 8;
        Input.Moves[Index].DestRect.top = 8;
        Input.Moves[Index].DestRect.right = 16;
        Input.Moves[Index].DestRect.bottom = 16;
    }
    Input.Rects[0].right = 16;
    Input.Rects[0].bottom = 16;

    Irp = IoAllocateIrp(Device->StackSize, FALSE);
    if (Irp == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;
    Irp->RequestorMode = KernelMode;
    Irp->AssociatedIrp.SystemBuffer = &Input;
    Stack = IoGetNextIrpStackLocation(Irp);
    Stack->MajorFunction = IRP_MJ_DEVICE_CONTROL;
    Stack->Parameters.DeviceIoControl.IoControlCode = IOCTL_VIDEO_DXGK_PRESENT_DIRTY_RECT;
    Stack->Parameters.DeviceIoControl.InputBufferLength = sizeof(Input);
    Completion.Status = STATUS_PENDING;
    KeInitializeEvent(&Completion.Event, NotificationEvent, FALSE);
    IoSetCompletionRoutine(Irp, PresentCompleted, &Completion, TRUE, TRUE, TRUE);
    Returned = IoCallDriver(Device, Irp);
    KeWaitForSingleObject(&Completion.Event, Executive, KernelMode, FALSE, NULL);
    IoFreeIrp(Irp);
    if (Returned != STATUS_PENDING)
        ok_eq_hex(Returned, Completion.Status);
    return Completion.Status;
}

START_TEST(DxgkPresentMoves)
{
    UNICODE_STRING Name = RTL_CONSTANT_STRING(L"\\Device\\Video0");
    PFILE_OBJECT FileObject;
    PDEVICE_OBJECT Device;
    PVOID Source;
    NTSTATUS Status;

    Status = IoGetDeviceObjectPointer(&Name, FILE_READ_DATA, &FileObject, &Device);
    if (skip(NT_SUCCESS(Status), "No display device (0x%lx)\n", Status))
        return;
    Source = ExAllocatePoolZero(NonPagedPool, SOURCE_PITCH * SOURCE_ROWS, 'tsmK');
    ok(Source != NULL, "No source buffer\n");
    if (Source != NULL)
    {
        Status = PresentMoves(Device, Source, DXGK_PRESENT_DIRTY_FLUSH, DWM_PRESENT_MAX_MOVES + 1);
        if (!skip(Status != STATUS_DEVICE_NOT_READY, "Display not ready\n"))
        {
            ok_eq_hex(Status, STATUS_INVALID_PARAMETER);
            Status = PresentMoves(Device, Source, 0, 1);
            ok_eq_hex(Status, STATUS_INVALID_PARAMETER);
        }
        ExFreePoolWithTag(Source, 'tsmK');
    }
    ObDereferenceObject(FileObject);
}
