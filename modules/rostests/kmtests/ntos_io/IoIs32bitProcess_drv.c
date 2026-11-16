/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Reports IoIs32bitProcess in the context of the process that sends the request
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>
#include "IoIs32bitProcess.h"

typedef BOOLEAN (NTAPI *PFN_IO_IS_32BIT_PROCESS)(_In_opt_ PIRP Irp);

static KMT_IRP_HANDLER TestProbeIrpHandler;
static KSTART_ROUTINE SystemThreadProbe;
static PFN_IO_IS_32BIT_PROCESS pIoIs32bitProcess;
static PDEVICE_OBJECT ProbeDevice;

static
VOID
NTAPI
SystemThreadProbe(
    _In_ PVOID Context)
{
    *(PBOOLEAN)Context = pIoIs32bitProcess(NULL);
    PsTerminateSystemThread(STATUS_SUCCESS);
}

static
VOID
CheckSystemThread(VOID)
{
    BOOLEAN Result = 2;
    HANDLE ThreadHandle;
    PVOID Thread;
    NTSTATUS Status;

    Status = PsCreateSystemThread(&ThreadHandle, THREAD_ALL_ACCESS, NULL, NULL, NULL, SystemThreadProbe, &Result);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;

    Status = ObReferenceObjectByHandle(ThreadHandle, SYNCHRONIZE, *PsThreadType, KernelMode, &Thread, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ZwClose(ThreadHandle);
    if (!NT_SUCCESS(Status))
        return;

    Status = KeWaitForSingleObject(Thread, Executive, KernelMode, FALSE, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ObDereferenceObject(Thread);

    ok_eq_uint(Result, FALSE);
}

NTSTATUS
TestEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PCUNICODE_STRING RegistryPath,
    _Out_ PCWSTR *DeviceName,
    _Inout_ INT *Flags)
{
    UNICODE_STRING RoutineName = RTL_CONSTANT_STRING(L"IoIs32bitProcess");
    UNICODE_STRING ProbeName = RTL_CONSTANT_STRING(L"\\Device\\" IO_IS32BIT_PROBE_DEVICE);
    NTSTATUS Status;

    PAGED_CODE();

    UNREFERENCED_PARAMETER(RegistryPath);
    UNREFERENCED_PARAMETER(Flags);

    *DeviceName = L"IoIs32bitProcess";
    pIoIs32bitProcess = (PFN_IO_IS_32BIT_PROCESS)MmGetSystemRoutineAddress(&RoutineName);

    Status = IoCreateDevice(DriverObject, 0, &ProbeName, FILE_DEVICE_UNKNOWN, FILE_DEVICE_SECURE_OPEN, FALSE, &ProbeDevice);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return Status;
    ProbeDevice->Flags &= ~DO_DEVICE_INITIALIZING;

    Status = KmtRegisterIrpHandler(IRP_MJ_DEVICE_CONTROL, ProbeDevice, TestProbeIrpHandler);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
    {
        IoDeleteDevice(ProbeDevice);
        ProbeDevice = NULL;
        return Status;
    }

    if (pIoIs32bitProcess != NULL)
        CheckSystemThread();

    return STATUS_SUCCESS;
}

VOID
TestUnload(
    _In_ PDRIVER_OBJECT DriverObject)
{
    PAGED_CODE();

    UNREFERENCED_PARAMETER(DriverObject);

    if (ProbeDevice != NULL)
    {
        KmtUnregisterIrpHandler(IRP_MJ_DEVICE_CONTROL, ProbeDevice, TestProbeIrpHandler);
        IoDeleteDevice(ProbeDevice);
        ProbeDevice = NULL;
    }
}

static
NTSTATUS
TestProbeIrpHandler(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_ PIO_STACK_LOCATION IoStackLocation)
{
    PIO_IS32BIT_PROBE_RESULT Result = Irp->AssociatedIrp.SystemBuffer;
    ULONG_PTR Wow64Information = 0;
    ULONG_PTR Information = 0;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(DeviceObject);

    ok_irql(PASSIVE_LEVEL);

    if (IoStackLocation->Parameters.DeviceIoControl.IoControlCode != IOCTL_IO_IS32BIT_PROBE)
    {
        Status = STATUS_INVALID_DEVICE_REQUEST;
    }
    else if (IoStackLocation->Parameters.DeviceIoControl.OutputBufferLength < sizeof(*Result))
    {
        Status = STATUS_BUFFER_TOO_SMALL;
    }
    else if (pIoIs32bitProcess == NULL)
    {
        Status = STATUS_NOT_SUPPORTED;
    }
    else
    {
        Status = ZwQueryInformationProcess(ZwCurrentProcess(), ProcessWow64Information,
                                           &Wow64Information, sizeof(Wow64Information), NULL);
        ok_eq_hex(Status, STATUS_SUCCESS);

        Result->NullForm = pIoIs32bitProcess(NULL);
        Result->IrpForm = pIoIs32bitProcess(Irp);
        Result->ProcessIsWow64 = Wow64Information != 0;
        Information = sizeof(*Result);
        Status = STATUS_SUCCESS;
    }

    Irp->IoStatus.Status = Status;
    Irp->IoStatus.Information = Information;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}
