/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Pico processes and threads made for a pico provider
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>
#include <ndk/ketypes.h>
#include <reactos/pico.h>

#define NDEBUG
#include <debug.h>

NTKERNELAPI
PVOID
NTAPI
PsGetProcessSectionBaseAddress(
    _In_ PEPROCESS Process);

#define PICO_PARENT_EXIT_STATUS ((NTSTATUS)0x0000BEEF)
#define PICO_CHILD_EXIT_STATUS  ((NTSTATUS)0x0000CAFE)
#define PICO_WAITER_EXIT_STATUS ((NTSTATUS)0x0000D00D)
#define PICO_THREADLESS_EXIT_STATUS ((NTSTATUS)0x0000F00D)
#define PICO_PAGE_BEFORE_CLONE  0x11111111UL
#define PICO_PAGE_AFTER_CLONE   0x22222222UL
#define PICO_TEST_SYSTEM_CALL   0x1234ULL

typedef struct _PICO_TEST_PROCESS
{
    volatile LONG ExitCalls;
    PEPROCESS ExitProcess;
    NTSTATUS ExitStatus;
} PICO_TEST_PROCESS, *PPICO_TEST_PROCESS;

typedef struct _PICO_TEST_THREAD
{
    BOOLEAN Child;
    BOOLEAN Waiter;
    PVOID Page;
    volatile LONG StartCalls;
    PETHREAD StartThread;
    PEPROCESS StartProcess;
    BOOLEAN StartMinimal;
    PVOID StartTeb;
    NTSTATUS StartBasicStatus;
    PVOID StartPeb;
    KIRQL StartIrql;
    KPROCESSOR_MODE StartPreviousMode;
    BOOLEAN StartTrapFrame;
    NTSTATUS AllocateStatus;
    NTSTATUS CloneStatus;
    NTSTATUS CloneThreadStatus;
    NTSTATUS CloneWaitStatus;
    NTSTATUS PageStatus;
    ULONG PageValue;
    NTSTATUS CodeStatus;
    PVOID Code;
    BOOLEAN SystemCallMinimal;
    ULONG64 SystemCallNumber;
    ULONG64 SystemCallPc;
    volatile LONG ExitCalls;
    PETHREAD ExitThread;
} PICO_TEST_THREAD, *PPICO_TEST_THREAD;

static PICO_TEST_PROCESS ParentProcess;
static PICO_TEST_PROCESS ChildProcess;
static PICO_TEST_THREAD ParentThread;
static PICO_TEST_THREAD ChildThread;
static PICO_TEST_PROCESS WaiterProcess;
static PICO_TEST_THREAD WaiterThread;
static PICO_TEST_PROCESS ThreadlessProcess;
static KEVENT WaiterStarted;
static KEVENT WaiterRelease;
static volatile LONG SystemCalls;
static volatile LONG Exceptions;

static
VOID
NTAPI
TestPicoSystemCall(
    _Inout_ PKTRAP_FRAME TrapFrame,
    _Inout_opt_ PKEXCEPTION_FRAME ExceptionFrame)
{
    PPICO_TEST_THREAD Record = PsPicoGetThreadContext(PsGetCurrentThread());

    UNREFERENCED_PARAMETER(TrapFrame);
    UNREFERENCED_PARAMETER(ExceptionFrame);

    InterlockedIncrement(&SystemCalls);
    if (Record != NULL)
    {
        Record->SystemCallMinimal = ((PDISPATCHER_HEADER)PsGetCurrentThread())->Minimal;
#if defined(_M_AMD64)
        Record->SystemCallNumber = TrapFrame->Rax;
        Record->SystemCallPc = TrapFrame->Rip;
#endif
    }

    PsPicoTerminateCurrentThread(PICO_CHILD_EXIT_STATUS);
}

static
BOOLEAN
NTAPI
TestPicoDispatchException(
    _Inout_ PEXCEPTION_RECORD ExceptionRecord,
    _Inout_opt_ PKEXCEPTION_FRAME ExceptionFrame,
    _Inout_ PKTRAP_FRAME TrapFrame)
{
    UNREFERENCED_PARAMETER(ExceptionRecord);
    UNREFERENCED_PARAMETER(ExceptionFrame);
    UNREFERENCED_PARAMETER(TrapFrame);

    InterlockedIncrement(&Exceptions);
    return FALSE;
}

static
VOID
TestPicoInitialContext(
    _Out_ PCONTEXT Context)
{
    RtlZeroMemory(Context, sizeof(*Context));
    Context->ContextFlags = CONTEXT_FULL;
}

static
BOOLEAN
TestPicoEnterUserMode(
    _Inout_ PPICO_TEST_THREAD Record,
    _Inout_ PKTRAP_FRAME TrapFrame)
{
#if defined(_M_AMD64)
    static const UCHAR Code[] = { 0x0F, 0x05, 0x0F, 0x0B };
    SIZE_T Size = PAGE_SIZE;
    PVOID Page = NULL;

    Record->CodeStatus = ZwAllocateVirtualMemory(NtCurrentProcess(),
                                                 &Page,
                                                 0,
                                                 &Size,
                                                 MEM_RESERVE | MEM_COMMIT,
                                                 PAGE_EXECUTE_READWRITE);
    if (!NT_SUCCESS(Record->CodeStatus))
        return FALSE;

    RtlCopyMemory(Page, Code, sizeof(Code));
    Record->Code = Page;
    TrapFrame->Rip = (ULONG64)(ULONG_PTR)Page;
    TrapFrame->Rsp = (ULONG64)(ULONG_PTR)Page + PAGE_SIZE - 0x80;
    TrapFrame->Rax = PICO_TEST_SYSTEM_CALL;
    return TRUE;
#else
    UNREFERENCED_PARAMETER(Record);
    UNREFERENCED_PARAMETER(TrapFrame);
    return FALSE;
#endif
}

static
VOID
TestPicoParentStart(
    _Inout_ PPICO_TEST_THREAD Record)
{
    LARGE_INTEGER Timeout;
    HANDLE CloneHandle = NULL;
    HANDLE CloneThreadHandle;
    CONTEXT Context;
    SIZE_T Size = PAGE_SIZE;
    PVOID Page = NULL;

    Record->AllocateStatus = ZwAllocateVirtualMemory(NtCurrentProcess(),
                                                     &Page,
                                                     0,
                                                     &Size,
                                                     MEM_RESERVE | MEM_COMMIT,
                                                     PAGE_READWRITE);
    if (!NT_SUCCESS(Record->AllocateStatus))
        return;

    *(volatile ULONG *)Page = PICO_PAGE_BEFORE_CLONE;

    Record->CloneStatus = PsPicoCreateProcess(TRUE, &ChildProcess, &CloneHandle);
    if (!NT_SUCCESS(Record->CloneStatus))
        return;

    *(volatile ULONG *)Page = PICO_PAGE_AFTER_CLONE;

    ChildThread.Child = TRUE;
    ChildThread.Page = Page;
    TestPicoInitialContext(&Context);
    Record->CloneThreadStatus = PsPicoCreateThread(CloneHandle,
                                                   &Context,
                                                   &ChildThread,
                                                   &CloneThreadHandle,
                                                   NULL);
    if (NT_SUCCESS(Record->CloneThreadStatus))
    {
        Timeout.QuadPart = -5LL * 1000 * 1000 * 10;
        Record->CloneWaitStatus = ZwWaitForSingleObject(CloneHandle, FALSE, &Timeout);
        ZwClose(CloneThreadHandle);
    }
    else
    {
        ZwTerminateProcess(CloneHandle, Record->CloneThreadStatus);
    }

    ZwClose(CloneHandle);
}

static
VOID
NTAPI
TestPicoThreadStart(
    _Inout_ PKTRAP_FRAME TrapFrame,
    _Inout_opt_ PKEXCEPTION_FRAME ExceptionFrame)
{
    PPICO_TEST_THREAD Record = PsPicoGetThreadContext(PsGetCurrentThread());
    PROCESS_BASIC_INFORMATION Basic;
    LARGE_INTEGER Timeout;

    UNREFERENCED_PARAMETER(ExceptionFrame);

    if (Record == NULL)
        PsPicoTerminateCurrentThread(STATUS_UNSUCCESSFUL);

    InterlockedIncrement(&Record->StartCalls);
    Record->StartThread = PsGetCurrentThread();
    Record->StartProcess = PsGetCurrentProcess();
    Record->StartMinimal = ((PDISPATCHER_HEADER)PsGetCurrentThread())->Minimal;
    Record->StartTeb = PsGetThreadTeb(PsGetCurrentThread());
    Record->StartBasicStatus = ZwQueryInformationProcess(NtCurrentProcess(),
                                                         ProcessBasicInformation,
                                                         &Basic,
                                                         sizeof(Basic),
                                                         NULL);
    if (NT_SUCCESS(Record->StartBasicStatus)) Record->StartPeb = Basic.PebBaseAddress;
    Record->StartIrql = KeGetCurrentIrql();
    Record->StartPreviousMode = ExGetPreviousMode();
    Record->StartTrapFrame = (TrapFrame != NULL);

    if (Record->Waiter)
    {
        Timeout.QuadPart = -5LL * 1000 * 1000 * 10;
        KeSetEvent(&WaiterStarted, IO_NO_INCREMENT, FALSE);
        /* A thread ends on its way back to user mode, so only an alertable user wait is cut short */
        if (KeWaitForSingleObject(&WaiterRelease, Executive, UserMode, TRUE, &Timeout) == STATUS_USER_APC)
            return;
        PsPicoTerminateCurrentThread(STATUS_TIMEOUT);
    }

    if (!Record->Child)
    {
        TestPicoParentStart(Record);
        PsPicoTerminateCurrentThread(PICO_PARENT_EXIT_STATUS);
    }

    _SEH2_TRY
    {
        Record->PageValue = *(volatile ULONG *)Record->Page;
        Record->PageStatus = STATUS_SUCCESS;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Record->PageStatus = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    if (TestPicoEnterUserMode(Record, TrapFrame))
        return;

    PsPicoTerminateCurrentThread(PICO_CHILD_EXIT_STATUS);
}

static
VOID
NTAPI
TestPicoThreadExit(
    _In_ PETHREAD Thread)
{
    PPICO_TEST_THREAD Record = PsPicoGetThreadContext(Thread);

    if (Record == NULL)
        return;

    InterlockedIncrement(&Record->ExitCalls);
    Record->ExitThread = Thread;
}

static
VOID
NTAPI
TestPicoProcessExit(
    _In_ PEPROCESS Process)
{
    PPICO_TEST_PROCESS Record = PsPicoGetProcessContext(Process);

    if (Record == NULL)
        return;

    InterlockedIncrement(&Record->ExitCalls);
    Record->ExitProcess = Process;
    Record->ExitStatus = PsGetProcessExitStatus(Process);
}

static const PICO_PROVIDER_ROUTINES TestPicoRoutines =
{
    sizeof(PICO_PROVIDER_ROUTINES),
    TestPicoSystemCall,
    TestPicoDispatchException,
    TestPicoThreadStart,
    TestPicoThreadExit,
    TestPicoProcessExit
};

static
VOID
TestPicoRegistration(
    _Out_ PBOOLEAN Registered)
{
    PICO_PROVIDER_ROUTINES Routines;
    NTSTATUS Status;

    *Registered = FALSE;

    Status = PsPicoRegisterProvider(NULL);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);

    Routines = TestPicoRoutines;
    Routines.Size--;
    Status = PsPicoRegisterProvider(&Routines);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);

    Routines = TestPicoRoutines;
    Routines.ThreadStart = NULL;
    Status = PsPicoRegisterProvider(&Routines);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);

    Status = PsPicoRegisterProvider(&TestPicoRoutines);
    if (Status == STATUS_ALREADY_REGISTERED)
    {
        skip(FALSE, "Another pico provider is registered\n");
        return;
    }
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;

    Status = PsPicoRegisterProvider(&TestPicoRoutines);
    ok_eq_hex(Status, STATUS_ALREADY_REGISTERED);

    *Registered = TRUE;
}

static
VOID
TestPicoProcessShape(
    _In_ HANDLE ProcessHandle,
    _In_ PEPROCESS Process)
{
    PROCESS_BASIC_INFORMATION Basic;
    MEMORY_BASIC_INFORMATION Memory;
    PVOID CallerImage;
    NTSTATUS Status;
    ULONG Length;

    ok_eq_pointer(PsPicoGetProcessContext(Process), &ParentProcess);
    ok_eq_pointer(PsGetProcessSectionBaseAddress(Process), NULL);

    Status = ZwQueryInformationProcess(ProcessHandle,
                                       ProcessBasicInformation,
                                       &Basic,
                                       sizeof(Basic),
                                       &Length);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        ok_eq_pointer(Basic.PebBaseAddress, NULL);
        ok_eq_pointer((PVOID)Basic.UniqueProcessId, PsGetProcessId(Process));
    }

    CallerImage = PsGetProcessSectionBaseAddress(PsGetCurrentProcess());
    if (CallerImage == NULL)
    {
        skip(FALSE, "The calling process has no image\n");
        return;
    }

    Status = ZwQueryVirtualMemory(ProcessHandle,
                                  CallerImage,
                                  MemoryBasicInformation,
                                  &Memory,
                                  sizeof(Memory),
                                  NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
        ok_eq_hex(Memory.State, MEM_FREE);
}

static
VOID
TestPicoSubsystem(
    _In_ HANDLE ProcessHandle,
    _In_ HANDLE ThreadHandle)
{
    ULONG Subsystem;
    ULONG Large[2];
    NTSTATUS Status;
    ULONG Length;

    Subsystem = MaxSubsystemInformationType;
    Status = ZwQueryInformationProcess(ProcessHandle,
                                       ProcessSubsystemInformation,
                                       &Subsystem,
                                       sizeof(Subsystem),
                                       &Length);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulong(Subsystem, (ULONG)SubsystemInformationTypeWSL);

    Status = ZwQueryInformationProcess(ProcessHandle,
                                       ProcessSubsystemInformation,
                                       Large,
                                       sizeof(Large),
                                       &Length);
    ok_eq_hex(Status, STATUS_INFO_LENGTH_MISMATCH);

    Status = ZwQueryInformationThread(ThreadHandle,
                                      ThreadSubsystemInformation,
                                      Large,
                                      sizeof(Large),
                                      &Length);
    ok_eq_hex(Status, STATUS_INFO_LENGTH_MISMATCH);

    Subsystem = MaxSubsystemInformationType;
    Status = ZwQueryInformationThread(ThreadHandle,
                                      ThreadSubsystemInformation,
                                      &Subsystem,
                                      sizeof(Subsystem),
                                      &Length);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulong(Subsystem, (ULONG)SubsystemInformationTypeWSL);

    Subsystem = MaxSubsystemInformationType;
    Status = ZwQueryInformationProcess(NtCurrentProcess(),
                                       ProcessSubsystemInformation,
                                       &Subsystem,
                                       sizeof(Subsystem),
                                       &Length);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulong(Subsystem, (ULONG)SubsystemInformationTypeWin32);

    Subsystem = MaxSubsystemInformationType;
    Status = ZwQueryInformationThread(NtCurrentThread(),
                                      ThreadSubsystemInformation,
                                      &Subsystem,
                                      sizeof(Subsystem),
                                      &Length);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulong(Subsystem, (ULONG)SubsystemInformationTypeWin32);
}

static
VOID
TestPicoStartRecord(
    _In_ PPICO_TEST_THREAD Record,
    _In_ PEPROCESS Process)
{
    ok_eq_long(Record->StartCalls, 1L);
    ok_eq_pointer(Record->StartProcess, Process);
    ok_eq_bool(Record->StartMinimal, TRUE);
    ok_eq_pointer(Record->StartTeb, NULL);
    ok_eq_hex(Record->StartBasicStatus, STATUS_SUCCESS);
    ok_eq_pointer(Record->StartPeb, NULL);
    ok_eq_uint(Record->StartIrql, PASSIVE_LEVEL);
    ok_eq_uint(Record->StartPreviousMode, UserMode);
    ok_eq_bool(Record->StartTrapFrame, TRUE);
    ok_eq_long(Record->ExitCalls, 1L);
    ok_eq_pointer(Record->ExitThread, Record->StartThread);
}

static
VOID
TestPicoThreadlessExit(VOID)
{
    HANDLE ProcessHandle;
    PEPROCESS Process;
    NTSTATUS Status;

    RtlZeroMemory(&ThreadlessProcess, sizeof(ThreadlessProcess));
    Status = PsPicoCreateProcess(FALSE, &ThreadlessProcess, &ProcessHandle);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;

    Status = ObReferenceObjectByHandle(ProcessHandle,
                                       PROCESS_ALL_ACCESS,
                                       *PsProcessType,
                                       KernelMode,
                                       (PVOID *)&Process,
                                       NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);

    ZwTerminateProcess(ProcessHandle, PICO_THREADLESS_EXIT_STATUS);
    ZwTerminateProcess(ProcessHandle, STATUS_UNSUCCESSFUL);

    ok_eq_long(ThreadlessProcess.ExitCalls, 1L);
    ok_eq_hex(ThreadlessProcess.ExitStatus, PICO_THREADLESS_EXIT_STATUS);
    if (NT_SUCCESS(Status))
    {
        ok_eq_pointer(ThreadlessProcess.ExitProcess, Process);
        ObDereferenceObject(Process);
    }

    ZwClose(ProcessHandle);
}

static
VOID
TestPicoTerminateThread(VOID)
{
    LARGE_INTEGER Timeout;
    HANDLE ProcessHandle;
    HANDLE ThreadHandle;
    CONTEXT Context;
    PETHREAD Thread;
    NTSTATUS Status;

    KeInitializeEvent(&WaiterStarted, NotificationEvent, FALSE);
    KeInitializeEvent(&WaiterRelease, NotificationEvent, FALSE);
    WaiterThread.Waiter = TRUE;

    Status = PsPicoCreateProcess(FALSE, &WaiterProcess, &ProcessHandle);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;

    TestPicoInitialContext(&Context);
    Status = PsPicoCreateThread(ProcessHandle, &Context, &WaiterThread, &ThreadHandle, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
    {
        ZwTerminateProcess(ProcessHandle, Status);
        ZwClose(ProcessHandle);
        return;
    }

    Timeout.QuadPart = -5LL * 1000 * 1000 * 10;
    Status = KeWaitForSingleObject(&WaiterStarted, Executive, KernelMode, FALSE, &Timeout);
    ok_eq_hex(Status, STATUS_SUCCESS);

    Status = ObReferenceObjectByHandle(ThreadHandle,
                                       THREAD_ALL_ACCESS,
                                       *PsThreadType,
                                       KernelMode,
                                       (PVOID *)&Thread,
                                       NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        Status = PsPicoTerminateThread(Thread, PICO_WAITER_EXIT_STATUS);
        ok_eq_hex(Status, STATUS_SUCCESS);
        ObDereferenceObject(Thread);
    }

    Timeout.QuadPart = -5LL * 1000 * 1000 * 10;
    Status = ZwWaitForSingleObject(ProcessHandle, FALSE, &Timeout);
    ok_eq_hex(Status, STATUS_SUCCESS);
    KeSetEvent(&WaiterRelease, IO_NO_INCREMENT, FALSE);
    if (Status != STATUS_SUCCESS)
        ZwWaitForSingleObject(ProcessHandle, FALSE, &Timeout);

    ok_eq_long(WaiterThread.StartCalls, 1L);
    ok_eq_long(WaiterThread.ExitCalls, 1L);
    ok_eq_long(WaiterProcess.ExitCalls, 1L);
    ok_eq_hex(WaiterProcess.ExitStatus, PICO_WAITER_EXIT_STATUS);

    ZwClose(ThreadHandle);
    ZwClose(ProcessHandle);
}

START_TEST(PsPico)
{
    THREAD_BASIC_INFORMATION ThreadBasic;
    PROCESS_BASIC_INFORMATION Basic;
    LARGE_INTEGER Timeout;
    HANDLE ProcessHandle;
    HANDLE ThreadHandle;
    HANDLE Unused;
    CLIENT_ID ClientId;
    CONTEXT Context;
    PEPROCESS Process;
    PETHREAD Thread;
    BOOLEAN Registered;
    NTSTATUS Status;
    ULONG Length;

    RtlZeroMemory(&ParentProcess, sizeof(ParentProcess));
    RtlZeroMemory(&ChildProcess, sizeof(ChildProcess));
    RtlZeroMemory(&ParentThread, sizeof(ParentThread));
    RtlZeroMemory(&ChildThread, sizeof(ChildThread));
    RtlZeroMemory(&WaiterProcess, sizeof(WaiterProcess));
    RtlZeroMemory(&WaiterThread, sizeof(WaiterThread));

    TestPicoRegistration(&Registered);
    if (!Registered)
        return;

    Status = PsPicoCreateProcess(TRUE, &ParentProcess, &Unused);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);

    Status = PsPicoCreateProcess(FALSE, &ParentProcess, &ProcessHandle);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;

    Status = ObReferenceObjectByHandle(ProcessHandle,
                                       PROCESS_ALL_ACCESS,
                                       *PsProcessType,
                                       KernelMode,
                                       (PVOID *)&Process,
                                       NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
    {
        ZwTerminateProcess(ProcessHandle, STATUS_UNSUCCESSFUL);
        ZwClose(ProcessHandle);
        return;
    }

    TestPicoProcessShape(ProcessHandle, Process);

    TestPicoInitialContext(&Context);
    RtlZeroMemory(&ClientId, sizeof(ClientId));
    Status = PsPicoCreateThread(ProcessHandle, &Context, &ParentThread, &ThreadHandle, &ClientId);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
    {
        ZwTerminateProcess(ProcessHandle, Status);
        ObDereferenceObject(Process);
        ZwClose(ProcessHandle);
        return;
    }
    ok_eq_pointer(ClientId.UniqueProcess, PsGetProcessId(Process));

    Timeout.QuadPart = -10LL * 1000 * 1000 * 10;
    Status = ZwWaitForSingleObject(ProcessHandle, FALSE, &Timeout);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (Status != STATUS_SUCCESS)
    {
        ZwTerminateProcess(ProcessHandle, STATUS_UNSUCCESSFUL);
        ZwWaitForSingleObject(ProcessHandle, FALSE, &Timeout);
    }

    TestPicoStartRecord(&ParentThread, Process);
    ok_eq_hex(ParentThread.AllocateStatus, STATUS_SUCCESS);
    ok_eq_hex(ParentThread.CloneStatus, STATUS_SUCCESS);
    ok_eq_hex(ParentThread.CloneThreadStatus, STATUS_SUCCESS);
    ok_eq_hex(ParentThread.CloneWaitStatus, STATUS_SUCCESS);

    ok_eq_long(ParentProcess.ExitCalls, 1L);
    ok_eq_pointer(ParentProcess.ExitProcess, Process);
    ok_eq_hex(ParentProcess.ExitStatus, PICO_PARENT_EXIT_STATUS);

    if (ChildThread.StartProcess != NULL)
    {
        TestPicoStartRecord(&ChildThread, ChildThread.StartProcess);
        ok(ChildThread.StartProcess != Process, "The clone ran in its parent\n");
        ok_eq_hex(ChildThread.PageStatus, STATUS_SUCCESS);
        ok_eq_hex(ChildThread.PageValue, PICO_PAGE_BEFORE_CLONE);
        ok_eq_long(ChildProcess.ExitCalls, 1L);
        ok_eq_pointer(ChildProcess.ExitProcess, ChildThread.StartProcess);
        ok_eq_hex(ChildProcess.ExitStatus, PICO_CHILD_EXIT_STATUS);
#if defined(_M_AMD64)
        ok_eq_hex(ChildThread.CodeStatus, STATUS_SUCCESS);
        ok_eq_bool(ChildThread.SystemCallMinimal, TRUE);
        ok_eq_hex64(ChildThread.SystemCallNumber, PICO_TEST_SYSTEM_CALL);
        ok_eq_hex64(ChildThread.SystemCallPc, (ULONG64)(ULONG_PTR)ChildThread.Code + 2);
#endif
    }
    else
    {
        ok(FALSE, "The clone's thread never started\n");
    }

    Status = ZwQueryInformationThread(ThreadHandle,
                                      ThreadBasicInformation,
                                      &ThreadBasic,
                                      sizeof(ThreadBasic),
                                      &Length);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
        ok_eq_hex(ThreadBasic.ExitStatus, PICO_PARENT_EXIT_STATUS);

    Status = ZwQueryInformationProcess(ProcessHandle,
                                       ProcessBasicInformation,
                                       &Basic,
                                       sizeof(Basic),
                                       &Length);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
        ok_eq_hex(Basic.ExitStatus, PICO_PARENT_EXIT_STATUS);

    Status = ObReferenceObjectByHandle(ThreadHandle,
                                       THREAD_ALL_ACCESS,
                                       *PsThreadType,
                                       KernelMode,
                                       (PVOID *)&Thread,
                                       NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        ok_eq_pointer(Thread, ParentThread.StartThread);
        ok_eq_pointer(PsPicoGetThreadContext(Thread), NULL);
        ObDereferenceObject(Thread);
    }
    ok_eq_pointer(PsPicoGetProcessContext(Process), NULL);

    TestPicoSubsystem(ProcessHandle, ThreadHandle);

#if defined(_M_AMD64)
    ok_eq_long(SystemCalls, 1L);
#else
    ok_eq_long(SystemCalls, 0L);
#endif
    ok_eq_long(Exceptions, 0L);

    ZwClose(ThreadHandle);
    ObDereferenceObject(Process);
    ZwClose(ProcessHandle);

    TestPicoTerminateThread();
    TestPicoThreadlessExit();
}
