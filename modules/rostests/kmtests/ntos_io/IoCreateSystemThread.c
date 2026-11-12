/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     IoCreateSystemThread holds a reference on its I/O object while the thread runs
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>

typedef struct _THREAD_CONTEXT
{
    KEVENT Started;
    KEVENT Go;
} THREAD_CONTEXT, *PTHREAD_CONTEXT;

static KSTART_ROUTINE ThreadRoutine;

static
VOID
NTAPI
ThreadRoutine(
    _In_ PVOID Context)
{
    PTHREAD_CONTEXT ThreadContext = Context;
    LARGE_INTEGER Timeout;

    Timeout.QuadPart = -5LL * 1000 * 1000 * 10;

    KeSetEvent(&ThreadContext->Started, IO_NO_INCREMENT, FALSE);
    KeWaitForSingleObject(&ThreadContext->Go, Executive, KernelMode, FALSE, &Timeout);
}

START_TEST(IoCreateSystemThread)
{
    OBJECT_ATTRIBUTES Attributes;
    THREAD_CONTEXT Context;
    LARGE_INTEGER Timeout;
    PDEVICE_OBJECT Device;
    PETHREAD Thread;
    HANDLE Handle;
    LONG_PTR Before;
    NTSTATUS Status;

    Status = IoCreateDevice(KmtDriverObject, 0, NULL, FILE_DEVICE_UNKNOWN, 0, FALSE, &Device);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) return;

    KeInitializeEvent(&Context.Started, NotificationEvent, FALSE);
    KeInitializeEvent(&Context.Go, NotificationEvent, FALSE);
    Timeout.QuadPart = -5LL * 1000 * 1000 * 10;

    Before = OBJECT_TO_OBJECT_HEADER(Device)->PointerCount;

    InitializeObjectAttributes(&Attributes, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);
    Status = IoCreateSystemThread(Device,
                                  &Handle,
                                  THREAD_ALL_ACCESS,
                                  &Attributes,
                                  NULL,
                                  NULL,
                                  ThreadRoutine,
                                  &Context);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
    {
        IoDeleteDevice(Device);
        return;
    }

    Status = KeWaitForSingleObject(&Context.Started, Executive, KernelMode, FALSE, &Timeout);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_longptr(OBJECT_TO_OBJECT_HEADER(Device)->PointerCount, Before + 1);

    Status = ObReferenceObjectByHandle(Handle, SYNCHRONIZE, *PsThreadType, KernelMode, (PVOID *)&Thread, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);

    KeSetEvent(&Context.Go, IO_NO_INCREMENT, FALSE);

    if (NT_SUCCESS(Status))
    {
        Status = KeWaitForSingleObject(Thread, Executive, KernelMode, FALSE, &Timeout);
        ok_eq_hex(Status, STATUS_SUCCESS);
        ok_eq_longptr(OBJECT_TO_OBJECT_HEADER(Device)->PointerCount, Before);
        ObDereferenceObject(Thread);
    }

    ZwClose(Handle);
    IoDeleteDevice(Device);
}
