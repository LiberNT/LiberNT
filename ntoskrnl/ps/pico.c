/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Pico process and thread support for subsystem providers
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include <reactos/pico.h>

#define NDEBUG
#include <debug.h>

NTSTATUS
NTAPI
ZwCreateProcessEx(
    _Out_ PHANDLE ProcessHandle,
    _In_ ACCESS_MASK DesiredAccess,
    _In_opt_ POBJECT_ATTRIBUTES ObjectAttributes,
    _In_ HANDLE ParentProcess,
    _In_ ULONG Flags,
    _In_opt_ HANDLE SectionHandle,
    _In_opt_ HANDLE DebugPort,
    _In_opt_ HANDLE ExceptionPort,
    _In_ BOOLEAN InJob);

#define PSP_PICO_PROVIDER_NONE        0
#define PSP_PICO_PROVIDER_REGISTERING 1
#define PSP_PICO_PROVIDER_REGISTERED  2

static PICO_PROVIDER_ROUTINES PspPicoRoutines;
static volatile LONG PspPicoProviderState = PSP_PICO_PROVIDER_NONE;

static
BOOLEAN
PspPicoProviderRegistered(VOID)
{
    return InterlockedCompareExchange(&PspPicoProviderState,
                                      PSP_PICO_PROVIDER_REGISTERED,
                                      PSP_PICO_PROVIDER_REGISTERED) == PSP_PICO_PROVIDER_REGISTERED;
}

NTSTATUS
NTAPI
PsPicoRegisterProvider(
    _In_ const PICO_PROVIDER_ROUTINES *Routines)
{
    PAGED_CODE();

    if ((Routines == NULL) ||
        (Routines->Size != sizeof(PICO_PROVIDER_ROUTINES)) ||
        (Routines->SystemCallDispatch == NULL) ||
        (Routines->DispatchException == NULL) ||
        (Routines->ThreadStart == NULL) ||
        (Routines->ThreadExit == NULL) ||
        (Routines->ProcessExit == NULL))
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (InterlockedCompareExchange(&PspPicoProviderState,
                                   PSP_PICO_PROVIDER_REGISTERING,
                                   PSP_PICO_PROVIDER_NONE) != PSP_PICO_PROVIDER_NONE)
    {
        return STATUS_ALREADY_REGISTERED;
    }

    PspPicoRoutines = *Routines;
    InterlockedExchange(&PspPicoProviderState, PSP_PICO_PROVIDER_REGISTERED);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
PsPicoCreateProcess(
    _In_ BOOLEAN CloneCurrent,
    _In_opt_ PVOID PicoContext,
    _Out_ PHANDLE ProcessHandle)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    PEPROCESS Process;
    HANDLE Handle;
    NTSTATUS Status;

    PAGED_CODE();

    if (!PspPicoProviderRegistered())
        return STATUS_NOT_SUPPORTED;

    if (CloneCurrent && !PsGetCurrentProcess()->PicoCreated)
        return STATUS_INVALID_PARAMETER;

    InitializeObjectAttributes(&ObjectAttributes, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);
    Status = ZwCreateProcessEx(&Handle,
                               PROCESS_ALL_ACCESS,
                               &ObjectAttributes,
                               NtCurrentProcess(),
                               CloneCurrent ? PSP_PROCESS_CREATE_PICO_CLONE : PSP_PROCESS_CREATE_PICO,
                               NULL,
                               NULL,
                               NULL,
                               FALSE);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = ObReferenceObjectByHandle(Handle,
                                       PROCESS_ALL_ACCESS,
                                       PsProcessType,
                                       KernelMode,
                                       (PVOID*)&Process,
                                       NULL);
    if (!NT_SUCCESS(Status))
    {
        ObCloseHandle(Handle, KernelMode);
        return Status;
    }

    Process->PicoContext = PicoContext;
    ObDereferenceObject(Process);

    *ProcessHandle = Handle;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
PsPicoCreateThread(
    _In_ HANDLE ProcessHandle,
    _In_ PCONTEXT Context,
    _In_opt_ PVOID PicoContext,
    _Out_ PHANDLE ThreadHandle,
    _Out_opt_ PCLIENT_ID ClientId)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    INITIAL_TEB InitialTeb;
    CLIENT_ID Cid;
    PEPROCESS Process;
    PETHREAD Thread;
    HANDLE Handle;
    BOOLEAN Pico;
    NTSTATUS Status;

    PAGED_CODE();

    Status = ObReferenceObjectByHandle(ProcessHandle,
                                       PROCESS_CREATE_THREAD,
                                       PsProcessType,
                                       KernelMode,
                                       (PVOID*)&Process,
                                       NULL);
    if (!NT_SUCCESS(Status))
        return Status;

    Pico = (BOOLEAN)Process->PicoCreated;
    ObDereferenceObject(Process);
    if (!Pico)
        return STATUS_INVALID_PARAMETER;

    RtlZeroMemory(&InitialTeb, sizeof(InitialTeb));
    InitializeObjectAttributes(&ObjectAttributes, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);
    Status = ZwCreateThread(&Handle,
                            THREAD_ALL_ACCESS,
                            &ObjectAttributes,
                            ProcessHandle,
                            &Cid,
                            Context,
                            &InitialTeb,
                            TRUE);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = ObReferenceObjectByHandle(Handle,
                                       THREAD_ALL_ACCESS,
                                       PsThreadType,
                                       KernelMode,
                                       (PVOID*)&Thread,
                                       NULL);
    if (!NT_SUCCESS(Status))
    {
        ZwTerminateThread(Handle, Status);
        ObCloseHandle(Handle, KernelMode);
        return Status;
    }

    Thread->PicoContext = PicoContext;
    KeResumeThread(&Thread->Tcb);
    ObDereferenceObject(Thread);

    *ThreadHandle = Handle;
    if (ClientId != NULL)
        *ClientId = Cid;
    return STATUS_SUCCESS;
}

VOID
NTAPI
PsPicoTerminateCurrentThread(
    _In_ NTSTATUS ExitStatus)
{
    PETHREAD Thread = PsGetCurrentThread();

    PAGED_CODE();

    ASSERT(Thread->Tcb.Header.Minimal);
    PspTerminateThreadByPointer(Thread, ExitStatus, TRUE);
    UNREACHABLE;
}

PVOID
NTAPI
PsPicoGetProcessContext(
    _In_ PEPROCESS Process)
{
    return Process->PicoContext;
}

PVOID
NTAPI
PsPicoGetThreadContext(
    _In_ PETHREAD Thread)
{
    return Thread->PicoContext;
}

VOID
NTAPI
PspPicoSystemCallDispatch(
    _Inout_ PKTRAP_FRAME TrapFrame,
    _Inout_opt_ PKEXCEPTION_FRAME ExceptionFrame)
{
    PspPicoRoutines.SystemCallDispatch(TrapFrame, ExceptionFrame);
}

BOOLEAN
NTAPI
PspPicoDispatchException(
    _Inout_ PEXCEPTION_RECORD ExceptionRecord,
    _Inout_opt_ PKEXCEPTION_FRAME ExceptionFrame,
    _Inout_ PKTRAP_FRAME TrapFrame)
{
    if (!KeGetCurrentThread()->Header.Minimal)
        return FALSE;

    if (!PspPicoRoutines.DispatchException(ExceptionRecord, ExceptionFrame, TrapFrame))
        ZwTerminateProcess(NtCurrentProcess(), ExceptionRecord->ExceptionCode);

    return TRUE;
}

VOID
NTAPI
PspPicoThreadStartup(
    _Inout_ PKTRAP_FRAME TrapFrame,
    _Inout_opt_ PKEXCEPTION_FRAME ExceptionFrame)
{
    PspPicoRoutines.ThreadStart(TrapFrame, ExceptionFrame);
}

VOID
NTAPI
PspPicoThreadExit(
    _In_ PETHREAD Thread)
{
    PspPicoRoutines.ThreadExit(Thread);
    Thread->PicoContext = NULL;
}

VOID
NTAPI
PspPicoProcessExit(
    _In_ PEPROCESS Process)
{
    PspPicoRoutines.ProcessExit(Process);
    Process->PicoContext = NULL;
}
