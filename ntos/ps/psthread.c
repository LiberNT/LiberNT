/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Making a thread, starting it, opening one, and what a thread
 *              answers about itself
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "pspriv.h"
#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

/* PRIVATE FUNCTIONS **********************************************************/

/*
 * Puts the thread on its process's list and gives it an identifier. A process
 * that is already exiting takes no new thread, which is what keeps a thread
 * from being created into a process on its way out.
 */
static
NTSTATUS
PspInsertThread(
    _In_ PETHREAD Thread,
    _In_ PEPROCESS Process,
    _In_ BOOLEAN FirstThread)
{

    PAGED_CODE();

    KeEnterCriticalRegion();
    ExAcquirePushLockExclusive(&Process->ProcessLock);

    if (Process->ProcessDelete ||
        (!FirstThread && Process->ProcessExiting))
    {
        ExReleasePushLockExclusive(&Process->ProcessLock);
        KeLeaveCriticalRegion();
        PspRemoveCid(Thread->Cid.UniqueThread);
        Thread->Cid.UniqueThread = NULL;
        return STATUS_PROCESS_IS_TERMINATING;
    }

    InsertTailList(&Process->ThreadListHead, &Thread->ThreadListEntry);
    Thread->ThreadInserted = TRUE;

    if (InterlockedIncrement((PLONG)&Process->ActiveThreads) >
        (LONG)Process->ActiveThreadsHighWatermark)
    {
        Process->ActiveThreadsHighWatermark = Process->ActiveThreads;
    }

    ExReleasePushLockExclusive(&Process->ProcessLock);
    KeLeaveCriticalRegion();

    return STATUS_SUCCESS;
}

/* PUBLIC FUNCTIONS ***********************************************************/

/*
 * A system thread that lets an exception reach the top of its stack has
 * nothing left to run, and the fault is in the kernel, so the system stops
 * here rather than carrying on with a thread in an unknown state.
 */
LONG
NTAPI
PspUnhandledExceptionInSystemThread(
    _In_ PEXCEPTION_POINTERS Pointers)
{
    KeBugCheckEx(KMODE_EXCEPTION_NOT_HANDLED,
                 Pointers->ExceptionRecord->ExceptionCode,
                 (ULONG_PTR)Pointers->ExceptionRecord->ExceptionAddress,
                 (ULONG_PTR)Pointers->ExceptionRecord,
                 (ULONG_PTR)Pointers->ContextRecord);

    return EXCEPTION_EXECUTE_HANDLER;
}

/*
 * Where a kernel thread begins. The routine it was given is called, and a
 * routine that returns instead of ending the thread ends it here, because
 * there is nowhere for a kernel thread to return to.
 */
VOID
NTAPI
PspSystemThreadStartup(
    _In_ PKSTART_ROUTINE StartRoutine,
    _In_ PVOID StartContext)
{
    PETHREAD Thread = PsGetCurrentThread();

    /*
     * The dispatcher hands a thread its first turn with the processor still
     * raised; what the thread runs expects to own it, so it is lowered here.
     */
    KeLowerIrql(PASSIVE_LEVEL);

    /* Every thread is announced, whatever it was made for, and on itself */
    PspRunCreateThreadNotifyRoutines(Thread, TRUE);

    /* A thread killed before it ever ran must not call anything */
    if (!Thread->Terminated)
    {
        _SEH2_TRY
        {
            StartRoutine(StartContext);
        }
        _SEH2_EXCEPT(PspUnhandledExceptionInSystemThread(
                         _SEH2_GetExceptionInformation()))
        {
            /* The filter stops the system, so nothing runs here */
        }
        _SEH2_END;
    }

    PspTerminateThreadByPointer(Thread, STATUS_SUCCESS, TRUE);
}

/*
 * Where a user thread begins, on the kernel side. The create notifications
 * and the debugger are told the thread exists, and the trap frame the thread
 * returns through is pointed at user mode.
 */
VOID
NTAPI
PspUserThreadStartup(
    _In_ PKSTART_ROUTINE StartRoutine,
    _In_ PVOID StartContext)
{
    PETHREAD Thread = PsGetCurrentThread();
    PEPROCESS Process = THREAD_TO_PROCESS(Thread);

    /* As for a system thread: the processor is the new thread's from here */
    KeLowerIrql(PASSIVE_LEVEL);

    UNREFERENCED_PARAMETER(StartRoutine);
    UNREFERENCED_PARAMETER(StartContext);

    /*
     * The notifications run on the new thread, in its own process, which is
     * what lets a driver touch the new thread's user address space.
     */
    PspRunCreateThreadNotifyRoutines(Thread, TRUE);

    if (!Process->CreateReported)
    {
        Process->CreateReported = TRUE;
        PspRunCreateProcessNotifyRoutines(Process, TRUE);
    }

    if (!Thread->HideFromDebugger)
        DbgkCreateThread(Thread, Thread->Win32StartAddress);

    if (Thread->Terminated)
    {
        PspTerminateThreadByPointer(Thread, STATUS_SUCCESS, TRUE);
        return;
    }

    /* A new thread's first return to user mode goes through the frames made for it below its stack */
    PspArchInitializeUserThreadStartup(KeGetExceptionFrame(&Thread->Tcb),
                                       KeGetTrapFrame(&Thread->Tcb));
}

/*
 * The whole of making a thread. A user thread needs a stack and a block of
 * its own in user memory; a kernel thread needs neither.
 */
NTSTATUS
NTAPI
PspCreateThread(
    _Out_ PHANDLE ThreadHandle,
    _In_ ACCESS_MASK DesiredAccess,
    _In_opt_ POBJECT_ATTRIBUTES ObjectAttributes,
    _In_opt_ HANDLE ProcessHandle,
    _In_opt_ PEPROCESS TargetProcess,
    _Out_opt_ PCLIENT_ID ClientId,
    _In_opt_ PCONTEXT ThreadContext,
    _In_opt_ PINITIAL_TEB InitialTeb,
    _In_opt_ PINITIAL_TEB Wow64InitialTeb,
    _In_ ULONG CreateFlags,
    _In_opt_ PKSTART_ROUTINE StartRoutine,
    _In_opt_ PVOID StartContext,
    _In_opt_ PVOID Win32StartAddress)
{
    /* A thread given a routine is the kernel's own, whoever the calling thread last served */
    KPROCESSOR_MODE PreviousMode = (StartRoutine != NULL) ? KernelMode
                                                          : ExGetPreviousMode();
    BOOLEAN FirstThread = FALSE;
    PEPROCESS Process = NULL;
    PETHREAD Thread = NULL;
    PVOID KernelStack = NULL;
    PTEB Teb = NULL;
    HANDLE Handle;
    NTSTATUS Status;

    PAGED_CODE();

    /*
     * A kernel thread is given a routine to run; a user thread is given the
     * state to start in. Neither one may be asked for both or for neither.
     */
    if ((StartRoutine == NULL) == (ThreadContext == NULL))
        return STATUS_INVALID_PARAMETER_MIX;

    if (TargetProcess != NULL)
    {
        Process = TargetProcess;
        ObReferenceObject(Process);
    }
    else
    {
        Status = ObReferenceObjectByHandle(ProcessHandle,
                                           PROCESS_CREATE_THREAD,
                                           PsProcessType,
                                           PreviousMode,
                                           (PVOID*)&Process,
                                           NULL);
        if (!NT_SUCCESS(Status)) return Status;
    }

    Status = ObCreateObject(PreviousMode,
                            PsThreadType,
                            ObjectAttributes,
                            PreviousMode,
                            NULL,
                            sizeof(ETHREAD),
                            0,
                            0,
                            (PVOID*)&Thread);
    if (!NT_SUCCESS(Status))
    {
        ObDereferenceObject(Process);
        return Status;
    }

    RtlZeroMemory(Thread, sizeof(ETHREAD));

    InitializeListHead(&Thread->IrpList);
    InitializeListHead(&Thread->ActiveTimerListHead);
    InitializeListHead(&Thread->BoostList);
    InitializeListHead(&Thread->DeboostList);
    InitializeListHead(&Thread->AlpcWaitListEntry);
    KeInitializeSemaphore(&Thread->AlpcWaitSemaphore, 0, 1);
    KeInitializeSpinLock(&Thread->ActiveTimerListLock);
    ExInitializePushLock(&Thread->ThreadLock);
    ExInitializeRundownProtection(&Thread->RundownProtect);
    KeQuerySystemTime(&Thread->CreateTime);
    Thread->Cid.UniqueProcess = Process->UniqueProcessId;
    Thread->ReadClusterSize = MmReadClusterSize;
    Thread->ExitStatus = STATUS_PENDING;

    /* The identifier is part of the thread's user block, so it exists first */
    Status = PspInsertCid(Thread, &Thread->Cid.UniqueThread);
    if (!NT_SUCCESS(Status))
    {
        ObDereferenceObject(Thread);
        ObDereferenceObject(Process);
        return Status;
    }

    if (StartRoutine != NULL) Thread->SystemThread = TRUE;
    if ((CreateFlags & THREAD_CREATE_FLAGS_HIDE_FROM_DEBUGGER) != 0)
        Thread->HideFromDebugger = TRUE;

    KernelStack = MmCreateKernelStack(FALSE, 0);
    if (KernelStack == NULL)
    {
        ObDereferenceObject(Thread);
        ObDereferenceObject(Process);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /*
     * A user thread needs its block in user memory before it is initialized,
     * because the block is part of the state the thread starts in.
     */
    if (ThreadContext != NULL)
    {
        Status = MmCreateTeb(Process,
                             &Thread->Cid,
                             InitialTeb,
                             Wow64InitialTeb,
                             &Teb);
        if (!NT_SUCCESS(Status))
        {
            MmDeleteKernelStack(KernelStack, FALSE);
            ObDereferenceObject(Thread);
            ObDereferenceObject(Process);
            return Status;
        }

        Thread->Win32StartAddress = Win32StartAddress;
    }
    else
    {
        Thread->StartAddress = (PVOID)StartRoutine;
    }

    Status = KeInitThread(&Thread->Tcb,
                          KernelStack,
                          (ThreadContext != NULL) ? PspUserThreadStartup
                                                  : PspSystemThreadStartup,
                          StartRoutine,
                          StartContext,
                          ThreadContext,
                          Teb,
                          &Process->Pcb);
    if (!NT_SUCCESS(Status))
    {
        if (Teb != NULL) MmDeleteTeb(Process, Teb);
        MmDeleteKernelStack(KernelStack, FALSE);
        ObDereferenceObject(Thread);
        ObDereferenceObject(Process);
        return Status;
    }

    /*
     * From here the thread names its process, so it keeps a reference of its
     * own on it: a process never goes away under a thread of its own. That
     * reference is given back when the thread object is deleted.
     */
    ObReferenceObject(Process);

    /*
     * Whoever brings the active thread count up from nothing is the first
     * thread, and only the first thread may enter a process that is new.
     */
    FirstThread = ((CreateFlags & THREAD_CREATE_FLAGS_INITIAL_THREAD) != 0 ||
                   Process->ActiveThreads == 0);

    Status = PspInsertThread(Thread, Process, FirstThread);
    if (!NT_SUCCESS(Status))
    {
        if (Teb != NULL) MmDeleteTeb(Process, Teb);
        ObDereferenceObject(Thread);
        ObDereferenceObject(Process);
        return Status;
    }

    /*
     * A thread the caller wants suspended is suspended before it can run, so
     * it never executes an instruction the caller did not allow.
     */
    if ((CreateFlags & THREAD_CREATE_FLAGS_CREATE_SUSPENDED) != 0)
        KeSuspendThread(&Thread->Tcb);

    /*
     * This reference is what keeps the thread object alive while the thread
     * runs. It is handed to whoever reaps the thread, so it is never given
     * back here, on either outcome.
     */
    ObReferenceObject(Thread);

    Status = ObInsertObject(Thread,
                            NULL,
                            DesiredAccess,
                            0,
                            NULL,
                            &Handle);
    if (!NT_SUCCESS(Status))
    {
        /*
         * The thread is already on its process's list and an identifier for
         * it is already handed out, so it has to be killed rather than
         * dropped: it is started only to run its own termination.
         */
        PspSetCrossThreadFlag(Thread, CT_TERMINATED_BIT);
        KeStartThread(&Thread->Tcb);
        KeReadyThread(&Thread->Tcb);
        ObDereferenceObject(Process);
        return Status;
    }

    if (ClientId != NULL)
    {
        if (PreviousMode != KernelMode)
        {
            _SEH2_TRY
            {
                *ClientId = Thread->Cid;
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                Status = _SEH2_GetExceptionCode();
            }
            _SEH2_END;
        }
        else
        {
            *ClientId = Thread->Cid;
        }
    }

    if (ThreadHandle != NULL)
    {
        if (PreviousMode != KernelMode)
        {
            _SEH2_TRY
            {
                *ThreadHandle = Handle;
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                Status = _SEH2_GetExceptionCode();
            }
            _SEH2_END;
        }
        else
        {
            *ThreadHandle = Handle;
        }
    }

    /* From here the thread runs, so nothing of it may be touched again */
    KeStartThread(&Thread->Tcb);
    KeReadyThread(&Thread->Tcb);

    ObDereferenceObject(Process);
    return Status;
}

/*
 * Makes a thread that runs in the kernel. A caller that names no process
 * gets a thread of the system process.
 */
NTSTATUS
NTAPI
PsCreateSystemThread(
    _Out_ PHANDLE ThreadHandle,
    _In_ ACCESS_MASK DesiredAccess,
    _In_opt_ POBJECT_ATTRIBUTES ObjectAttributes,
    _In_opt_ HANDLE ProcessHandle,
    _Out_opt_ PCLIENT_ID ClientId,
    _In_ PKSTART_ROUTINE StartRoutine,
    _In_opt_ PVOID StartContext)
{
    PEPROCESS Target = NULL;

    PAGED_CODE();

    if (ProcessHandle == NULL) Target = PsInitialSystemProcess;

    return PspCreateThread(ThreadHandle,
                           DesiredAccess,
                           ObjectAttributes,
                           ProcessHandle,
                           Target,
                           ClientId,
                           NULL,
                           NULL,
                           NULL,
                           0,
                           StartRoutine,
                           StartContext,
                           NULL);
}

/* SYSTEM CALLS ***************************************************************/

NTSTATUS
NTAPI
NtCreateThread(
    _Out_ PHANDLE ThreadHandle,
    _In_ ACCESS_MASK DesiredAccess,
    _In_opt_ POBJECT_ATTRIBUTES ObjectAttributes,
    _In_ HANDLE ProcessHandle,
    _Out_ PCLIENT_ID ClientId,
    _In_ PCONTEXT ThreadContext,
    _In_ PINITIAL_TEB InitialTeb,
    _In_ BOOLEAN CreateSuspended)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    INITIAL_TEB SafeInitialTeb;
    CONTEXT SafeContext;
    ULONG CreateFlags = 0;

    PAGED_CODE();

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForWriteHandle(ThreadHandle);
            ProbeForWrite(ClientId, sizeof(CLIENT_ID), sizeof(ULONG));
            ProbeForRead(ThreadContext, sizeof(CONTEXT), sizeof(ULONG));
            ProbeForRead(InitialTeb, sizeof(INITIAL_TEB), sizeof(ULONG));
            SafeContext = *ThreadContext;
            SafeInitialTeb = *InitialTeb;
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }
    else
    {
        SafeContext = *ThreadContext;
        SafeInitialTeb = *InitialTeb;
    }

    if (CreateSuspended) CreateFlags |= THREAD_CREATE_FLAGS_CREATE_SUSPENDED;

    return PspCreateThread(ThreadHandle,
                           DesiredAccess,
                           ObjectAttributes,
                           ProcessHandle,
                           NULL,
                           ClientId,
                           &SafeContext,
                           &SafeInitialTeb,
                           NULL,
                           CreateFlags,
                           NULL,
                           NULL,
                           NULL);
}

/*
 * The newer form, which builds the state the thread starts in from the entry
 * point the caller names and allocates the stack itself.
 */
NTSTATUS
NTAPI
NtCreateThreadEx(
    _Out_ PHANDLE ThreadHandle,
    _In_ ACCESS_MASK DesiredAccess,
    _In_opt_ POBJECT_ATTRIBUTES ObjectAttributes,
    _In_ HANDLE ProcessHandle,
    _In_ PVOID StartRoutine,
    _In_opt_ PVOID Argument,
    _In_ ULONG CreateFlags,
    _In_ SIZE_T ZeroBits,
    _In_ SIZE_T StackSize,
    _In_ SIZE_T MaximumStackSize,
    _In_opt_ PPS_ATTRIBUTE_LIST AttributeList)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    PSP_CREATE_ATTRIBUTES Attributes;
    INITIAL_TEB InitialTeb;
    HANDLE Handle = NULL;
    PETHREAD Thread;
    CONTEXT Context;
    SIZE_T Reserve;
    SIZE_T Commit;
    NTSTATUS Status;

    PAGED_CODE();

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForWriteHandle(ThreadHandle);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }

    Status = PspCaptureAttributes(AttributeList, PreviousMode, &Attributes);
    if (!NT_SUCCESS(Status)) return Status;

    /*
     * The two sizes are what the caller wants committed and reserved. Either
     * one left at nothing falls back to what the image asked for.
     */
    Commit = StackSize;
    Reserve = MaximumStackSize;
    if (Reserve == 0) Reserve = Commit;
    if (Reserve == 0) Reserve = PSP_DEFAULT_STACK_RESERVE;
    if (Commit == 0) Commit = PAGE_SIZE;

    Status = PspAllocateUserStack(ProcessHandle,
                                  ZeroBits,
                                  Reserve,
                                  Commit,
                                  0,
                                  &InitialTeb);
    if (!NT_SUCCESS(Status)) return Status;

    RtlZeroMemory(&Context, sizeof(Context));
    PsArchInitializeUserThreadContext(&Context,
                                      PspRtlUserThreadStart,
                                      StartRoutine,
                                      Argument,
                                      InitialTeb.StackBase);

    Status = PspCreateThread(&Handle,
                             DesiredAccess,
                             ObjectAttributes,
                             ProcessHandle,
                             NULL,
                             Attributes.ClientId,
                             &Context,
                             &InitialTeb,
                             NULL,
                             CreateFlags,
                             NULL,
                             NULL,
                             StartRoutine);
    if (!NT_SUCCESS(Status))
    {
        SIZE_T Size = 0;

        ZwFreeVirtualMemory(ProcessHandle,
                            &InitialTeb.AllocatedStackBase,
                            &Size,
                            MEM_RELEASE);
        return Status;
    }

    /*
     * Where the thread runs, and where its own block landed, are settled
     * once the thread exists: a suspended thread has not moved yet, and one
     * that is already running simply picks the change up.
     */
    Status = ObReferenceObjectByHandle(Handle,
                                       0,
                                       PsThreadType,
                                       KernelMode,
                                       (PVOID*)&Thread,
                                       NULL);
    if (NT_SUCCESS(Status))
    {
        PVOID Teb = Thread->Tcb.Teb;

        Status = PspApplyThreadAttributes(Thread, &Attributes);
        ObDereferenceObject(Thread);

        if (NT_SUCCESS(Status) && Attributes.TebAddress != NULL)
        {
            if (PreviousMode != KernelMode)
            {
                _SEH2_TRY
                {
                    *Attributes.TebAddress = Teb;
                }
                _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
                {
                    Status = _SEH2_GetExceptionCode();
                }
                _SEH2_END;
            }
            else
            {
                *Attributes.TebAddress = Teb;
            }
        }
    }

    if (!NT_SUCCESS(Status))
    {
        ZwClose(Handle);
        return Status;
    }

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            *ThreadHandle = Handle;
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;
    }
    else
    {
        *ThreadHandle = Handle;
    }

    return Status;
}

NTSTATUS
NTAPI
NtOpenThread(
    _Out_ PHANDLE ThreadHandle,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ POBJECT_ATTRIBUTES ObjectAttributes,
    _In_opt_ PCLIENT_ID ClientId)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    CLIENT_ID SafeCid = { NULL, NULL };
    BOOLEAN HasCid = FALSE;
    BOOLEAN HasName;
    ACCESS_STATE AccessState;
    AUX_ACCESS_DATA AuxData;
    PETHREAD Thread;
    HANDLE Handle;
    NTSTATUS Status;

    PAGED_CODE();

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForWriteHandle(ThreadHandle);
            if (ClientId != NULL)
            {
                ProbeForRead(ClientId, sizeof(CLIENT_ID), sizeof(ULONG));
                SafeCid = *ClientId;
                HasCid = TRUE;
            }
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }
    else if (ClientId != NULL)
    {
        SafeCid = *ClientId;
        HasCid = TRUE;
    }

    HasName = (ObjectAttributes != NULL &&
               ObjectAttributes->ObjectName != NULL &&
               ObjectAttributes->ObjectName->Length != 0);

    if (HasCid == HasName) return STATUS_INVALID_PARAMETER_MIX;

    Status = PspCreateOpenAccessState(&AccessState,
                                      &AuxData,
                                      DesiredAccess,
                                      PsThreadType,
                                      THREAD_ALL_ACCESS,
                                      PreviousMode);
    if (!NT_SUCCESS(Status)) return Status;

    if (HasCid)
    {
        if (SafeCid.UniqueProcess != NULL)
        {
            Status = PsLookupProcessThreadByCid(&SafeCid, NULL, &Thread);
        }
        else
        {
            Status = PsLookupThreadByThreadId(SafeCid.UniqueThread, &Thread);
        }

        if (!NT_SUCCESS(Status))
        {
            SeDeleteAccessState(&AccessState);
            return Status;
        }

        Status = ObOpenObjectByPointer(Thread,
                                       (ObjectAttributes != NULL) ?
                                           ObjectAttributes->Attributes : 0,
                                       &AccessState,
                                       0,
                                       PsThreadType,
                                       PreviousMode,
                                       &Handle);
        ObDereferenceObject(Thread);
    }
    else
    {
        Status = ObOpenObjectByName(ObjectAttributes,
                                    PsThreadType,
                                    PreviousMode,
                                    &AccessState,
                                    0,
                                    NULL,
                                    &Handle);
    }

    SeDeleteAccessState(&AccessState);

    if (!NT_SUCCESS(Status)) return Status;

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            *ThreadHandle = Handle;
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;
    }
    else
    {
        *ThreadHandle = Handle;
    }

    return Status;
}

/*
 * Walks the threads of a process, handing back the next one after the handle
 * the caller passes. A caller that passes none starts at the first thread.
 */
NTSTATUS
NTAPI
NtGetNextThread(
    _In_ HANDLE ProcessHandle,
    _In_opt_ HANDLE ThreadHandle,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ ULONG HandleAttributes,
    _In_ ULONG Flags,
    _Out_ PHANDLE NewThreadHandle)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    PETHREAD Previous = NULL;
    PEPROCESS Process;
    PETHREAD Thread;
    HANDLE Handle;
    NTSTATUS Status;

    PAGED_CODE();

    if (Flags != 0) return STATUS_INVALID_PARAMETER;

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForWriteHandle(NewThreadHandle);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }

    Status = ObReferenceObjectByHandle(ProcessHandle,
                                       PROCESS_QUERY_INFORMATION,
                                       PsProcessType,
                                       PreviousMode,
                                       (PVOID*)&Process,
                                       NULL);
    if (!NT_SUCCESS(Status)) return Status;

    if (ThreadHandle != NULL)
    {
        Status = ObReferenceObjectByHandle(ThreadHandle,
                                           0,
                                           PsThreadType,
                                           PreviousMode,
                                           (PVOID*)&Previous,
                                           NULL);
        if (!NT_SUCCESS(Status))
        {
            ObDereferenceObject(Process);
            return Status;
        }

        if (THREAD_TO_PROCESS(Previous) != Process)
        {
            ObDereferenceObject(Previous);
            ObDereferenceObject(Process);
            return STATUS_INVALID_PARAMETER;
        }
    }

    /* The walk consumes the reference it is given and gives one back */
    Thread = PsGetNextProcessThread(Process, Previous);
    ObDereferenceObject(Process);

    if (Thread == NULL) return STATUS_NO_MORE_ENTRIES;

    Status = ObOpenObjectByPointer(Thread,
                                   HandleAttributes,
                                   NULL,
                                   DesiredAccess,
                                   PsThreadType,
                                   PreviousMode,
                                   &Handle);
    ObDereferenceObject(Thread);

    if (!NT_SUCCESS(Status)) return Status;

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            *NewThreadHandle = Handle;
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;
    }
    else
    {
        *NewThreadHandle = Handle;
    }

    return Status;
}

#ifdef _WIN64

/*
 * Turns a request to make a 32-bit thread into the native one that actually
 * carries it. The 32-bit side keeps the stack and the state the caller gave,
 * written into a block of its own that the thread finds through its own
 * storage; the native side gets a stack and a state of its own, because that
 * is what the kernel returns through.
 */
NTSTATUS
NTAPI
PspPrepareWow64Thread(
    _In_ HANDLE ProcessHandle,
    _In_ PWOW64_CONTEXT Wow64Context,
    _Out_ PCONTEXT ThreadContext,
    _In_ PINITIAL_TEB Wow64InitialTeb,
    _Out_ PINITIAL_TEB InitialTeb)
{
    WOW64_CPU_INIT CpuInit;
    SIZE_T Size = sizeof(CpuInit);
    PVOID CpuArea = NULL;
    NTSTATUS Status;

    PAGED_CODE();

    UNREFERENCED_PARAMETER(Wow64InitialTeb);

    Status = PspAllocateUserStack(ProcessHandle,
                                  0,
                                  PSP_DEFAULT_STACK_RESERVE,
                                  PAGE_SIZE,
                                  0,
                                  InitialTeb);
    if (!NT_SUCCESS(Status)) return Status;

    /*
     * The 32-bit state is put where the thread can reach it, rather than
     * being carried in the native state, because it does not fit there.
     */
    Status = ZwAllocateVirtualMemory(ProcessHandle,
                                     &CpuArea,
                                     0,
                                     &Size,
                                     MEM_COMMIT | MEM_RESERVE,
                                     PAGE_READWRITE);
    if (!NT_SUCCESS(Status))
    {
        SIZE_T Release = 0;

        ZwFreeVirtualMemory(ProcessHandle,
                            &InitialTeb->AllocatedStackBase,
                            &Release,
                            MEM_RELEASE);
        return Status;
    }

    RtlZeroMemory(&CpuInit, sizeof(CpuInit));
    CpuInit.Cpu.Machine = IMAGE_FILE_MACHINE_I386;
    CpuInit.Context = *Wow64Context;

    Status = ZwWriteVirtualMemory(ProcessHandle,
                                  CpuArea,
                                  &CpuInit,
                                  sizeof(CpuInit),
                                  NULL);
    if (!NT_SUCCESS(Status))
    {
        SIZE_T Release = 0;

        ZwFreeVirtualMemory(ProcessHandle, &CpuArea, &Release, MEM_RELEASE);
        ZwFreeVirtualMemory(ProcessHandle,
                            &InitialTeb->AllocatedStackBase,
                            &Release,
                            MEM_RELEASE);
        return Status;
    }

    RtlZeroMemory(ThreadContext, sizeof(*ThreadContext));
    PsArchInitializeUserThreadContext(ThreadContext,
                                      PspRtlUserThreadStart,
                                      NULL,
                                      NULL,
                                      InitialTeb->StackBase);

    KiInitializeWow64ThreadContext(ThreadContext,
                                   Wow64Context->Eip,
                                   Wow64Context->Eax,
                                   (ULONG_PTR)CpuArea);

    return STATUS_SUCCESS;
}

#endif

/*
 * Marks an APC as belonging to a thread that runs 32-bit code, so the
 * dispatcher that delivers it uses the 32-bit shape of the call.
 */
NTSTATUS
NTAPI
PsWrapApcWow64Thread(
    _Inout_ PVOID *ApcContext,
    _Inout_ PVOID *ApcRoutine)
{
    UNREFERENCED_PARAMETER(ApcContext);
    UNREFERENCED_PARAMETER(ApcRoutine);

    /* TODO: wrap the routine in the 32-bit APC thunk once one exists */
    return STATUS_SUCCESS;
}
