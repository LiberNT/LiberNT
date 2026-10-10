/*
 * PROJECT:     LiberNT AMD64 emulation host
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Run the threads of an AMD64 process under the CPU core
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "amd64emu.h"

#define EMU_PROCESS_UNINITIALIZED 0
#define EMU_PROCESS_INITIALIZING  1
#define EMU_PROCESS_READY         2

EMU_PROCESS EmuProcess;

static LONG EmuProcessState;

DECLSPEC_NORETURN
VOID
EmuFatal(
    _In_ NTSTATUS Status,
    _In_ PCSTR Message)
{
    DbgPrint("AMD64EMU: %s: %08lx\n", Message, Status);
    NtTerminateProcess(NtCurrentProcess(), Status);
    for (;;) NtYieldExecution();
}

static
NTSTATUS
EmuGetGuestExport(
    _In_ PCSTR Name,
    _Out_ PULONG64 Address)
{
    *Address = (ULONG64)RtlFindExportedRoutineByName(EmuProcess.GuestNtdll, Name);
    return *Address ? STATUS_SUCCESS : STATUS_PROCEDURE_NOT_FOUND;
}

static
NTSTATUS
EmuBuildDirectory(
    _Out_ PUNICODE_STRING Directory,
    _Out_writes_bytes_(BufferSize) PWCHAR Buffer,
    _In_ USHORT BufferSize,
    _In_ PCWSTR Name)
{
    NTSTATUS Status;

    RtlInitEmptyUnicodeString(Directory, Buffer, BufferSize);
    Status = RtlAppendUnicodeToString(Directory, L"\\??\\");
    if (NT_SUCCESS(Status)) Status = RtlAppendUnicodeToString(Directory, SharedUserData->NtSystemRoot);
    if (NT_SUCCESS(Status)) Status = RtlAppendUnicodeToString(Directory, L"\\");
    if (NT_SUCCESS(Status)) Status = RtlAppendUnicodeToString(Directory, Name);
    if (NT_SUCCESS(Status)) Status = RtlAppendUnicodeToString(Directory, L"\\");
    return Status;
}

static
NTSTATUS
EmuInitializeProcess(
    _In_ PEMULATION_THREAD Host)
{
    static const UNICODE_STRING NativeName = RTL_CONSTANT_STRING(L"ntdll.dll");
    WCHAR NtdllBuffer[MAX_PATH];
    UNICODE_STRING NtdllPath;
    PVOID NativeNtdll;
    NTSTATUS Status;

    EmuProcess.GuestPeb = Host->GuestPeb;
    RtlInitUnicodeString(&EmuProcess.SystemRootDirectory, L"\\SystemRoot\\System32\\");
    Status = EmuBuildDirectory(&EmuProcess.SystemDirectory,
                               EmuProcess.SystemDirectoryBuffer,
                               sizeof(EmuProcess.SystemDirectoryBuffer),
                               L"System32");
    if (!NT_SUCCESS(Status)) return Status;
    Status = EmuBuildDirectory(&EmuProcess.GuestDirectory,
                               EmuProcess.GuestDirectoryBuffer,
                               sizeof(EmuProcess.GuestDirectoryBuffer),
                               EMULATION_SYSTEM_DIRECTORY_AMD64);
    if (!NT_SUCCESS(Status)) return Status;

    Status = LdrGetDllHandle(NULL, NULL, (PUNICODE_STRING)&NativeName, &NativeNtdll);
    if (!NT_SUCCESS(Status)) return Status;
    EmuProcess.NativeRtlUserThreadStart = (ULONG_PTR)RtlFindExportedRoutineByName(NativeNtdll, "RtlUserThreadStart");
    if (!EmuProcess.NativeRtlUserThreadStart) return STATUS_PROCEDURE_NOT_FOUND;

    Status = EmuCoreInitialize();
    if (!NT_SUCCESS(Status)) return Status;

    RtlInitEmptyUnicodeString(&NtdllPath, NtdllBuffer, sizeof(NtdllBuffer));
    Status = RtlAppendUnicodeStringToString(&NtdllPath, &EmuProcess.GuestDirectory);
    if (NT_SUCCESS(Status)) Status = RtlAppendUnicodeToString(&NtdllPath, L"ntdll.dll");
    if (!NT_SUCCESS(Status)) return Status;

    Status = EmuMapGuestImage(&NtdllPath, &EmuProcess.GuestNtdll);
    if (!NT_SUCCESS(Status)) return Status;

    Status = EmuGetGuestExport("LdrInitializeThunk", &EmuProcess.LdrInitializeThunk);
    if (NT_SUCCESS(Status)) Status = EmuGetGuestExport("KiUserApcDispatcher", &EmuProcess.KiUserApcDispatcher);
    if (NT_SUCCESS(Status)) Status = EmuGetGuestExport("KiUserExceptionDispatcher", &EmuProcess.KiUserExceptionDispatcher);
    if (NT_SUCCESS(Status)) Status = EmuGetGuestExport("RtlUserThreadStart", &EmuProcess.RtlUserThreadStart);
    if (!NT_SUCCESS(Status)) return Status;

    if (!RtlAddVectoredExceptionHandler(1, EmuVectoredHandler)) return STATUS_NO_MEMORY;
    return STATUS_SUCCESS;
}

static
NTSTATUS
EmuEnsureProcess(
    _In_ PEMULATION_THREAD Host)
{
    LARGE_INTEGER Timeout;
    NTSTATUS Status;
    LONG State;

    for (;;)
    {
        State = _InterlockedCompareExchange(&EmuProcessState,
                                            EMU_PROCESS_INITIALIZING,
                                            EMU_PROCESS_UNINITIALIZED);
        if (State == EMU_PROCESS_READY) return STATUS_SUCCESS;
        if (State == EMU_PROCESS_UNINITIALIZED) break;
        Timeout.QuadPart = Int32x32To64(10, -10000);
        NtDelayExecution(FALSE, &Timeout);
    }

    Status = EmuInitializeProcess(Host);
    _InterlockedExchange(&EmuProcessState, NT_SUCCESS(Status) ? EMU_PROCESS_READY : EMU_PROCESS_UNINITIALIZED);
    return Status;
}

static
VOID
EmuBuildStartContext(
    _In_ PEMULATION_THREAD Host,
    _Out_ AMD64_CONTEXT *Start)
{
    PCONTEXT Native = &Host->StartContext;
    PEMU_THREAD_START Record;

    if (Native->Pc == (ULONG_PTR)EmuGuestThreadEntry)
    {
        Record = (PEMU_THREAD_START)Native->A0;
        *Start = Record->Context;
        RtlFreeHeap(RtlGetProcessHeap(), 0, Record);
    }
    else
    {
        RtlZeroMemory(Start, sizeof(*Start));
        Start->Rcx = Native->A0;
        Start->Rdx = Native->A1;
        Start->Rsp = (Native->Sp & ~(ULONG64)15) - 5 * sizeof(ULONG64);
        Start->Rip = Native->Pc == EmuProcess.NativeRtlUserThreadStart ? EmuProcess.RtlUserThreadStart : Native->Pc;
        Start->MxCsr = 0x1F80;
        Start->FltSave.ControlWord = 0x27F;
        Start->FltSave.MxCsr = 0x1F80;
        Start->EFlags = 0x202;
    }

    Start->ContextFlags = CONTEXT_AMD64_FULL | CONTEXT_AMD64_SEGMENTS;
    Start->SegCs = EMU_SEG_CODE;
    Start->SegDs = EMU_SEG_DATA;
    Start->SegEs = EMU_SEG_DATA;
    Start->SegSs = EMU_SEG_DATA;
    Start->SegGs = EMU_SEG_DATA;
    Start->SegFs = EMU_SEG_TEB;
}

VOID
NTAPI
EmuThreadStart(
    _In_ PEMULATION_THREAD Host)
{
    AMD64_CONTEXT Start;
    PEMU_THREAD Thread;
    NTSTATUS Status;

    if (Host != EmulationCurrentThread()) EmuFatal(STATUS_INVALID_IMAGE_FORMAT, "not started by the loader");

    Status = EmuEnsureProcess(Host);
    if (!NT_SUCCESS(Status)) EmuFatal(Status, "process initialization failed");

    Thread = RtlAllocateHeap(RtlGetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*Thread));
    if (!Thread) EmuFatal(STATUS_NO_MEMORY, "thread allocation failed");
    Thread->Host = Host;
    Thread->GuestTeb = Host->GuestTeb;

    Status = EmuCoreCreateCpu(Thread, Host->GuestTeb, &Thread->Cpu);
    if (!NT_SUCCESS(Status)) EmuFatal(Status, "CPU creation failed");
    Host->HostData = Thread;

    EmuBuildStartContext(Host, &Start);
    Status = EmuStartGuest(Thread, &Start);
    if (!NT_SUCCESS(Status)) EmuFatal(Status, "guest thread start failed");

    EmuCoreRun(Thread->Cpu);
}
