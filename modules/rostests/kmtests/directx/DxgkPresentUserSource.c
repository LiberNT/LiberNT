/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Dirty-rectangle presents refuse a user-mode source
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

#include <kmt_test.h>
#include <windef.h>
#include <reactos/dwmframe.h>

#define SOURCE_PITCH  (4096 * sizeof(ULONG))
#define SOURCE_ROWS   2304
#define COMMIT_ROWS   32

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
PresentDirtyRect(PDEVICE_OBJECT Device, PVOID Source, LONG Top)
{
    PRESENT_COMPLETION Completion;
    DXGK_PRESENT_DIRTY_RECTS_INPUT Input;
    PIO_STACK_LOCATION Stack;
    PIRP Irp;

    RtlZeroMemory(&Input, sizeof(Input));
    Input.StructSize = sizeof(Input);
    Input.Flags = DXGK_PRESENT_DIRTY_FLUSH;
    Input.RectCount = 1;
    Input.SourcePitch = SOURCE_PITCH;
    Input.SourceSize = SOURCE_PITCH * SOURCE_ROWS;
    Input.Source = (ULONG_PTR)Source;
    Input.Rects[0].left = 0;
    Input.Rects[0].top = Top;
    Input.Rects[0].right = 16;
    Input.Rects[0].bottom = Top + 16;

    /* A non-threaded IRP: its status comes from the completion, not from a thread APC. */
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
    (VOID)IoCallDriver(Device, Irp);
    KeWaitForSingleObject(&Completion.Event, Executive, KernelMode, FALSE, NULL);
    IoFreeIrp(Irp);
    return Completion.Status;
}

START_TEST(DxgkPresentUserSource)
{
    UNICODE_STRING Name = RTL_CONSTANT_STRING(L"\\Device\\Video0");
    PFILE_OBJECT FileObject;
    PDEVICE_OBJECT Device;
    PVOID Base = NULL;
    SIZE_T Size = SOURCE_PITCH * SOURCE_ROWS;
    SIZE_T CommitSize = SOURCE_PITCH * COMMIT_ROWS;
    NTSTATUS Status;

    Status = IoGetDeviceObjectPointer(&Name, FILE_READ_DATA, &FileObject, &Device);
    if (skip(NT_SUCCESS(Status), "No display device (0x%lx)\n", Status))
        return;

    /* The source lives in this process like DWM's composition bitmap; only its first rows are committed. */
    Status = ZwAllocateVirtualMemory(ZwCurrentProcess(), &Base, 0, &Size, MEM_RESERVE, PAGE_READWRITE);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        Status = ZwAllocateVirtualMemory(ZwCurrentProcess(), &Base, 0, &CommitSize, MEM_COMMIT, PAGE_READWRITE);
        ok_eq_hex(Status, STATUS_SUCCESS);
    }
    if (NT_SUCCESS(Status))
    {
        RtlFillMemory(Base, CommitSize, 0x40);

        /* Miniports read pSource as kernel memory, so a user bitmap never reaches them. */
        Status = PresentDirtyRect(Device, Base, 0);
        if (!skip(Status != STATUS_DEVICE_NOT_READY, "Display not ready\n"))
        {
            ok_eq_hex(Status, STATUS_INVALID_PARAMETER);
            Status = PresentDirtyRect(Device, Base, COMMIT_ROWS + 16);
            ok_eq_hex(Status, STATUS_INVALID_PARAMETER);
        }

        Size = 0;
        ZwFreeVirtualMemory(ZwCurrentProcess(), &Base, &Size, MEM_RELEASE);
    }
    ObDereferenceObject(FileObject);
}
