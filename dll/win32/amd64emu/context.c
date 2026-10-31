/*
 * PROJECT:     LiberNT AMD64 emulation host
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Suspend guest threads at a consistent state and access their contexts
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "amd64emu.h"

#define EMU_PARK_TIMEOUT (-10 * 1000 * 1000)

NTSTATUS
EmuRegisterThread(
    _In_ PEMU_THREAD Thread)
{
    NTSTATUS Status;

    Status = NtCreateEvent(&Thread->ParkEvent, EVENT_ALL_ACCESS, NULL, NotificationEvent, FALSE);
    if (!NT_SUCCESS(Status)) return Status;
    Status = NtCreateEvent(&Thread->ReleaseEvent, EVENT_ALL_ACCESS, NULL, SynchronizationEvent, FALSE);
    if (!NT_SUCCESS(Status))
    {
        NtClose(Thread->ParkEvent);
        return Status;
    }

    RtlEnterCriticalSection(&EmuProcess.ThreadLock);
    InsertTailList(&EmuProcess.Threads, &Thread->Link);
    RtlLeaveCriticalSection(&EmuProcess.ThreadLock);
    return STATUS_SUCCESS;
}

VOID
EmuUnregisterThread(
    _In_ PEMU_THREAD Thread)
{
    RtlEnterCriticalSection(&EmuProcess.ThreadLock);
    RemoveEntryList(&Thread->Link);
    RtlLeaveCriticalSection(&EmuProcess.ThreadLock);
    NtClose(Thread->ParkEvent);
    NtClose(Thread->ReleaseEvent);
}

static
NTSTATUS
EmuCheckAccess(
    _In_ HANDLE ThreadHandle,
    _In_ ACCESS_MASK Access)
{
    OBJECT_BASIC_INFORMATION Information;
    NTSTATUS Status;

    if (ThreadHandle == NtCurrentThread()) return STATUS_SUCCESS;
    Status = NtQueryObject(ThreadHandle, ObjectBasicInformation, &Information, sizeof(Information), NULL);
    if (!NT_SUCCESS(Status)) return Status;
    return (Information.GrantedAccess & Access) == Access ? STATUS_SUCCESS : STATUS_ACCESS_DENIED;
}

static
NTSTATUS
EmuFindThread(
    _In_ HANDLE ThreadHandle,
    _Out_ PEMU_THREAD *Thread)
{
    THREAD_BASIC_INFORMATION Information;
    PLIST_ENTRY Entry;
    PEMU_THREAD Candidate;
    NTSTATUS Status;

    *Thread = NULL;
    if (ThreadHandle == NtCurrentThread())
    {
        *Thread = EmuCurrentThread();
        return *Thread ? STATUS_SUCCESS : STATUS_INVALID_HANDLE;
    }

    Status = NtQueryInformationThread(ThreadHandle, ThreadBasicInformation, &Information, sizeof(Information), NULL);
    if (!NT_SUCCESS(Status)) return Status;

    /* TODO: AMD64 contexts of threads in other processes */
    if (Information.ClientId.UniqueProcess != NtCurrentTeb()->ClientId.UniqueProcess) return STATUS_NOT_SUPPORTED;

    for (Entry = EmuProcess.Threads.Flink; Entry != &EmuProcess.Threads; Entry = Entry->Flink)
    {
        Candidate = CONTAINING_RECORD(Entry, EMU_THREAD, Link);
        if (Candidate->GuestTeb == Information.TebBaseAddress)
        {
            *Thread = Candidate;
            return STATUS_SUCCESS;
        }
    }

    return STATUS_NOT_SUPPORTED;
}

static
BOOLEAN
EmuIsCurrentThread(
    _In_ HANDLE ThreadHandle)
{
    THREAD_BASIC_INFORMATION Information;

    if (ThreadHandle == NtCurrentThread()) return TRUE;
    if (!NT_SUCCESS(NtQueryInformationThread(ThreadHandle, ThreadBasicInformation, &Information,
                                             sizeof(Information), NULL)))
    {
        return FALSE;
    }
    return Information.ClientId.UniqueThread == NtCurrentTeb()->ClientId.UniqueThread;
}

VOID
EmuParkThread(
    _In_ PEMU_THREAD Thread)
{
    NtSetEvent(Thread->ParkEvent, NULL);
    NtWaitForSingleObject(Thread->ReleaseEvent, FALSE, NULL);
}

NTSTATUS
EmuSuspendThread(
    _In_ HANDLE ThreadHandle,
    _Out_opt_ PULONG PreviousSuspendCount)
{
    LARGE_INTEGER Timeout;
    PEMU_THREAD Target;
    BOOLEAN Parked;
    ULONG Previous;
    NTSTATUS Status;

    if (EmuIsCurrentThread(ThreadHandle)) return NtSuspendThread(ThreadHandle, PreviousSuspendCount);

    RtlEnterCriticalSection(&EmuProcess.ThreadLock);
    Status = NtSuspendThread(ThreadHandle, &Previous);
    if (NT_SUCCESS(Status) && !Previous &&
        NT_SUCCESS(EmuFindThread(ThreadHandle, &Target)) && !Target->HostDepth)
    {
        NtClearEvent(Target->ParkEvent);
        EmuCoreRequestPark(Target->Cpu);
        NtResumeThread(ThreadHandle, NULL);
        Timeout.QuadPart = EMU_PARK_TIMEOUT;
        Parked = NtWaitForSingleObject(Target->ParkEvent, FALSE, &Timeout) == STATUS_SUCCESS;
        if (!Parked && !EmuCoreCancelPark(Target->Cpu))
        {
            NtWaitForSingleObject(Target->ParkEvent, FALSE, NULL);
            Parked = TRUE;
        }
        if (!Parked) DbgPrint("AMD64EMU: thread %p did not reach a safepoint\n", Target);
        NtSuspendThread(ThreadHandle, NULL);
        if (Parked) NtSetEvent(Target->ReleaseEvent, NULL);
    }
    RtlLeaveCriticalSection(&EmuProcess.ThreadLock);

    if (NT_SUCCESS(Status) && PreviousSuspendCount) *PreviousSuspendCount = Previous;
    return Status;
}

static
NTSTATUS
EmuGetStartContextThread(
    _In_ HANDLE ThreadHandle,
    _Inout_ AMD64_CONTEXT *Context)
{
    THREAD_BASIC_INFORMATION Thread;
    PROCESS_BASIC_INFORMATION Process;
    OBJECT_ATTRIBUTES ObjectAttributes;
    ULONG Flags = Context->ContextFlags;
    HANDLE ProcessHandle;
    PVOID Ldr = NULL;
    CONTEXT Native;
    NTSTATUS Status;

    Native.ContextFlags = CONTEXT_FULL;
    Status = NtGetContextThread(ThreadHandle, &Native);
    if (!NT_SUCCESS(Status)) return Status;

    if (Native.Pc != EmuProcess.NativeRtlUserThreadStart)
    {
        Status = NtQueryInformationThread(ThreadHandle, ThreadBasicInformation, &Thread, sizeof(Thread), NULL);
        if (!NT_SUCCESS(Status)) return Status;
        InitializeObjectAttributes(&ObjectAttributes, NULL, 0, NULL, NULL);
        Status = NtOpenProcess(&ProcessHandle, PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ,
                               &ObjectAttributes, &Thread.ClientId);
        if (!NT_SUCCESS(Status)) return STATUS_NOT_SUPPORTED;
        Status = NtQueryInformationProcess(ProcessHandle, ProcessBasicInformation, &Process, sizeof(Process), NULL);
        if (NT_SUCCESS(Status))
            Status = NtReadVirtualMemory(ProcessHandle, &Process.PebBaseAddress->Ldr, &Ldr, sizeof(Ldr), NULL);
        NtClose(ProcessHandle);
        /* TODO: AMD64 contexts of threads that already run in other processes */
        if (!NT_SUCCESS(Status) || Ldr) return STATUS_NOT_SUPPORTED;
    }

    EmuBuildNativeStartContext(&Native, Context);
    Context->ContextFlags = Flags;
    return STATUS_SUCCESS;
}

static
NTSTATUS
EmuGetNativeViewContextThread(
    _In_ HANDLE ThreadHandle,
    _Inout_ AMD64_CONTEXT *Context)
{
    THREAD_BASIC_INFORMATION Information;
    ULONG Flags = Context->ContextFlags;
    CONTEXT Native;
    NTSTATUS Status;

    Status = NtQueryInformationThread(ThreadHandle, ThreadBasicInformation, &Information, sizeof(Information), NULL);
    if (!NT_SUCCESS(Status)) return Status;
    if (Information.ClientId.UniqueProcess != NtCurrentTeb()->ClientId.UniqueProcess)
        return EmuGetStartContextThread(ThreadHandle, Context);

    Native.ContextFlags = CONTEXT_FULL;
    Status = NtGetContextThread(ThreadHandle, &Native);
    if (!NT_SUCCESS(Status)) return Status;
    if (Native.Pc == EmuProcess.NativeRtlUserThreadStart)
    {
        EmuBuildNativeStartContext(&Native, Context);
        Context->ContextFlags = Flags;
        return STATUS_SUCCESS;
    }
    if ((Flags & CONTEXT_AMD64_CONTROL) == CONTEXT_AMD64_CONTROL)
    {
        Context->Rip = Native.Pc;
        Context->Rsp = Native.Sp;
    }
    return STATUS_SUCCESS;
}

NTSTATUS
EmuGetContextThread(
    _In_ HANDLE ThreadHandle,
    _Inout_ AMD64_CONTEXT *Context)
{
    AMD64_CONTEXT Captured = *Context;
    PEMU_THREAD Target;
    BOOLEAN InHost = FALSE;
    NTSTATUS Status;

    if ((Captured.ContextFlags & CONTEXT_AMD64) != CONTEXT_AMD64) return STATUS_INVALID_PARAMETER;
    Status = EmuCheckAccess(ThreadHandle, THREAD_GET_CONTEXT);
    if (!NT_SUCCESS(Status)) return Status;

    RtlEnterCriticalSection(&EmuProcess.ThreadLock);
    Status = EmuFindThread(ThreadHandle, &Target);
    if (NT_SUCCESS(Status))
    {
        EmuCoreGetContext(Target->Cpu, &Captured);
        InHost = Target->HostDepth != 0;
    }
    RtlLeaveCriticalSection(&EmuProcess.ThreadLock);
    if (EmuProcess.Attached && (Status == STATUS_NOT_SUPPORTED || InHost) && !EmuIsCurrentThread(ThreadHandle))
        Status = EmuGetNativeViewContextThread(ThreadHandle, &Captured);
    else if (Status == STATUS_NOT_SUPPORTED)
        Status = EmuGetStartContextThread(ThreadHandle, &Captured);

    if (NT_SUCCESS(Status)) *Context = Captured;
    return Status;
}

NTSTATUS
EmuSetContextThread(
    _In_ PEMU_THREAD Thread,
    _In_ HANDLE ThreadHandle,
    _In_ const AMD64_CONTEXT *Context,
    _Out_ PBOOLEAN Resumed)
{
    AMD64_CONTEXT Captured = *Context;
    PEMU_THREAD Target;
    NTSTATUS Status;

    *Resumed = FALSE;
    if ((Captured.ContextFlags & CONTEXT_AMD64) != CONTEXT_AMD64) return STATUS_INVALID_PARAMETER;
    Status = EmuCheckAccess(ThreadHandle, THREAD_SET_CONTEXT);
    if (!NT_SUCCESS(Status)) return Status;

    RtlEnterCriticalSection(&EmuProcess.ThreadLock);
    Status = EmuFindThread(ThreadHandle, &Target);
    if (NT_SUCCESS(Status))
    {
        EmuCoreSetContext(Target->Cpu, &Captured);
        *Resumed = Target == Thread &&
                   (Captured.ContextFlags & CONTEXT_AMD64_INTEGER) == CONTEXT_AMD64_INTEGER;
    }
    RtlLeaveCriticalSection(&EmuProcess.ThreadLock);
    return Status;
}
