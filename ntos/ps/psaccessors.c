/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     What a driver may ask about a process or a thread, and the few
 *              things it may set
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "pspriv.h"
#define NDEBUG
#include <debug.h>

/* What a graphics driver reports about a process that stopped answering */
#define PSP_FAULT_CRASHED           0x1
#define PSP_FAULT_HANG              0x2
#define PSP_FAULT_GHOST             0x4
#define PSP_FAULT_PREFILTER         0x8

/* The counters a process keeps of those reports do not go past this */
#define PSP_FAULT_COUNT_MAX         7

/* GLOBALS ********************************************************************/

BOOLEAN PspDiskCountersEnabled;

static KGUARDED_MUTEX PspMitigationLock;
static BOOLEAN PspMitigationLockReady;

/* PROCESS IDENTITY ***********************************************************/

HANDLE
NTAPI
PsGetCurrentProcessId(VOID)
{
    return PsGetCurrentThread()->Cid.UniqueProcess;
}

HANDLE
NTAPI
PsGetProcessId(
    _In_ PEPROCESS Process)
{
    return Process->UniqueProcessId;
}

HANDLE
NTAPI
PsGetProcessInheritedFromUniqueProcessId(
    _In_ PEPROCESS Process)
{
    return Process->InheritedFromUniqueProcessId;
}

PCHAR
NTAPI
PsGetProcessImageFileName(
    _In_ PEPROCESS Process)
{
    return (PCHAR)Process->ImageFileName;
}

ULONGLONG
NTAPI
PsGetProcessSequenceNumber(
    _In_ PEPROCESS Process)
{
    return Process->SequenceNumber;
}

/*
 * A value that tells one run of a process from every other, across reboots.
 * The sequence number names the process within this boot, and the boot
 * identifier is folded in above it so that two processes from two boots never
 * share a key.
 */
ULONGLONG
NTAPI
PsGetProcessStartKey(
    _In_ PEPROCESS Process)
{
    return Process->SequenceNumber |
           ((ULONGLONG)SharedUserData->BootId << 48);
}

LARGE_INTEGER
NTAPI
PsGetProcessExitTime(VOID)
{
    return PsGetCurrentProcess()->ExitTime;
}

LONGLONG
NTAPI
PsGetProcessCreateTimeQuadPart(
    _In_ PEPROCESS Process)
{
    return Process->CreateTime.QuadPart;
}

NTSTATUS
NTAPI
PsGetProcessExitStatus(
    _In_ PEPROCESS Process)
{
    return Process->ExitStatus;
}

BOOLEAN
NTAPI
PsGetProcessExitProcessCalled(
    _In_ PEPROCESS Process)
{
    return (BOOLEAN)(Process->ProcessExiting != 0);
}

USHORT
NTAPI
PsGetProcessMachine(
    _In_ PEPROCESS Process)
{
    return Process->Machine;
}

/*
 * Which machine the process's 32-bit code is written for. A process that runs
 * no 32-bit code answers with its own machine.
 */
USHORT
NTAPI
PsWow64GetProcessMachine(
    _In_ PEPROCESS Process)
{
    if (Process->WoW64Process == NULL) return Process->Machine;

    return IMAGE_FILE_MACHINE_I386;
}

ULONG
NTAPI
PsGetCurrentProcessSessionId(VOID)
{
    return MmGetSessionId(PsGetCurrentProcess());
}

ULONG
NTAPI
PsGetProcessSessionId(
    _In_ PEPROCESS Process)
{
    return MmGetSessionId(Process);
}

/*
 * The same, except that a process which belongs to no session says so rather
 * than naming session zero.
 */
ULONG
NTAPI
PsGetProcessSessionIdEx(
    _In_ PEPROCESS Process)
{
    return MmGetSessionIdEx(Process);
}

PPEB
NTAPI
PsGetProcessPeb(
    _In_ PEPROCESS Process)
{
    return Process->Peb;
}

struct _PEB32 *
NTAPI
PsGetProcessPeb32(
    _In_ PEPROCESS Process)
{
    if (Process->WoW64Process == NULL) return NULL;

    return (struct _PEB32 *)Process->WoW64Process->Peb;
}

PVOID
NTAPI
PsGetProcessWow64Process(
    _In_ PEPROCESS Process)
{
    return Process->WoW64Process;
}

PVOID
NTAPI
PsGetCurrentProcessWow64Process(VOID)
{
    return PsGetCurrentProcess()->WoW64Process;
}

PVOID
NTAPI
PsGetProcessSectionBaseAddress(
    _In_ PEPROCESS Process)
{
    return Process->SectionBaseAddress;
}

PVOID
NTAPI
PsGetProcessDebugPort(
    _In_ PEPROCESS Process)
{
    return Process->DebugPort;
}

BOOLEAN
NTAPI
PsIsProcessBeingDebugged(
    _In_ PEPROCESS Process)
{
    return (BOOLEAN)(Process->DebugPort != NULL);
}

PVOID
NTAPI
PsGetProcessSecurityPort(
    _In_ PEPROCESS Process)
{
    return Process->SecurityPort;
}

NTSTATUS
NTAPI
PsSetProcessSecurityPort(
    _Inout_ PEPROCESS Process,
    _In_ PVOID SecurityPort)
{
    Process->SecurityPort = SecurityPort;
    return STATUS_SUCCESS;
}

ULONG
NTAPI
PsGetProcessPriorityClass(
    _In_ PEPROCESS Process)
{
    return Process->PriorityClass;
}

BOOLEAN
NTAPI
PsIsSystemProcess(
    _In_ PEPROCESS Process)
{
    return (BOOLEAN)(Process->SystemProcess != 0);
}

/*
 * Whether the process is one the system will not let another process read or
 * change. The two answers are not the same: a process protected outright is
 * closed to everything, while one protected lightly still allows a few of
 * the things a debugger needs.
 */
LOGICAL
NTAPI
PsIsProtectedProcess(
    _In_ PEPROCESS Process)
{
    return (Process->Protection.Type != PsProtectedTypeNone);
}

LOGICAL
NTAPI
PsIsProtectedProcessLight(
    _In_ PEPROCESS Process)
{
    return (Process->Protection.Type == PsProtectedTypeProtectedLight);
}

BOOLEAN
NTAPI
PsIsProcessCommitRelinquished(
    _In_ PEPROCESS Process)
{
    return (BOOLEAN)(Process->RelinquishedCommit != 0);
}

/* WINDOWING STATE ************************************************************/

PVOID
NTAPI
PsGetProcessWin32Process(
    _In_ PEPROCESS Process)
{
    return Process->Win32Process;
}

PVOID
NTAPI
PsGetCurrentProcessWin32Process(VOID)
{
    return PsGetCurrentProcess()->Win32Process;
}

/*
 * Attaches the windowing kernel's own state to the process, or takes it off.
 * The old value the caller passes is what it expects to find, so two callers
 * cannot each believe they attached theirs.
 */
NTSTATUS
NTAPI
PsSetProcessWin32Process(
    _Inout_ PEPROCESS Process,
    _In_opt_ PVOID Win32Process,
    _In_opt_ PVOID OldWin32Process)
{
    PVOID Found;

    Found = InterlockedCompareExchangePointer(&Process->Win32Process,
                                              Win32Process,
                                              OldWin32Process);

    return (Found == OldWin32Process) ? STATUS_SUCCESS
                                      : STATUS_UNSUCCESSFUL;
}

PVOID
NTAPI
PsGetProcessWin32WindowStation(
    _In_ PEPROCESS Process)
{
    return Process->Win32WindowStation;
}

VOID
NTAPI
PsSetProcessWindowStation(
    _Inout_ PEPROCESS Process,
    _In_opt_ PVOID WindowStation)
{
    Process->Win32WindowStation = WindowStation;
}

PVOID
NTAPI
PsGetThreadWin32Thread(
    _In_ PETHREAD Thread)
{
    return Thread->Tcb.Win32Thread;
}

PVOID
NTAPI
PsGetCurrentThreadWin32Thread(VOID)
{
    return PsGetCurrentThread()->Tcb.Win32Thread;
}

/*
 * The windowing kernel reads its per-thread state and holds off APCs in one
 * step, because every call it makes with that state in hand has to run to
 * the end without an APC in between.
 */
PVOID
NTAPI
PsGetCurrentThreadWin32ThreadAndEnterCriticalRegion(
    _Out_ PHANDLE ProcessId)
{
    PETHREAD Thread = PsGetCurrentThread();

    *ProcessId = Thread->Cid.UniqueProcess;
    KeEnterCriticalRegion();

    return Thread->Tcb.Win32Thread;
}

PVOID
NTAPI
PsSetThreadWin32Thread(
    _Inout_ PETHREAD Thread,
    _In_opt_ PVOID Win32Thread,
    _In_opt_ PVOID OldWin32Thread)
{
    return InterlockedCompareExchangePointer(&Thread->Tcb.Win32Thread,
                                             Win32Thread,
                                             OldWin32Thread);
}

PVOID
NTAPI
PsGetProcessDxgProcess(
    _In_ PEPROCESS Process)
{
    return Process->DxgProcess;
}

VOID
NTAPI
PsSetProcessDxgProcess(
    _Inout_ PEPROCESS Process,
    _In_opt_ PVOID DxgProcess)
{
    Process->DxgProcess = DxgProcess;
}

/*
 * A graphics driver tells the process manager that a process stopped
 * answering, or came back wrong. The counts saturate rather than wrap,
 * because what matters is that it happened more than a few times.
 */
VOID
NTAPI
PsSetProcessFaultInformation(
    _Inout_ PEPROCESS Process,
    _In_ PVOID FaultInformation)
{
    ULONG Flags = *(PULONG)FaultInformation;

    if ((Flags & PSP_FAULT_CRASHED) != 0) Process->Crashed = TRUE;

    if ((Flags & PSP_FAULT_HANG) != 0 &&
        Process->HangCount != PSP_FAULT_COUNT_MAX)
    {
        Process->HangCount++;
    }

    if ((Flags & PSP_FAULT_GHOST) != 0 &&
        Process->GhostCount != PSP_FAULT_COUNT_MAX)
    {
        Process->GhostCount++;
    }

    if ((Flags & PSP_FAULT_PREFILTER) != 0) Process->PrefilterException = TRUE;
}

/*
 * Asks the windowing kernel to put every process's windows into a state, as
 * the shell does when it shows or hides the desktop.
 */
NTSTATUS
NTAPI
PsSetProcessesWindowState(
    _In_ ULONG WindowState,
    _In_opt_ PVOID Context)
{
    UNREFERENCED_PARAMETER(WindowState);
    UNREFERENCED_PARAMETER(Context);

    /* TODO: hand the request to the windowing kernel once it registers a
     * window-state provider; with none, there are no windows to change */
    return STATUS_SUCCESS;
}

/* THREAD IDENTITY ************************************************************/

HANDLE
NTAPI
PsGetCurrentThreadId(VOID)
{
    return PsGetCurrentThread()->Cid.UniqueThread;
}

HANDLE
NTAPI
PsGetThreadId(
    _In_ PETHREAD Thread)
{
    return Thread->Cid.UniqueThread;
}

HANDLE
NTAPI
PsGetThreadProcessId(
    _In_ PETHREAD Thread)
{
    return Thread->Cid.UniqueProcess;
}

HANDLE
NTAPI
PsGetCurrentThreadProcessId(VOID)
{
    return PsGetCurrentThread()->Cid.UniqueProcess;
}

PEPROCESS
NTAPI
PsGetThreadProcess(
    _In_ PETHREAD Thread)
{
    return THREAD_TO_PROCESS(Thread);
}

PEPROCESS
NTAPI
PsGetCurrentThreadProcess(VOID)
{
    return PsGetCurrentProcess();
}

ULONG
NTAPI
PsGetThreadSessionId(
    _In_ PETHREAD Thread)
{
    return MmGetSessionId(THREAD_TO_PROCESS(Thread));
}

PTEB
NTAPI
PsGetThreadTeb(
    _In_ PETHREAD Thread)
{
    return Thread->Tcb.Teb;
}

PVOID
NTAPI
PsGetCurrentThreadTeb(VOID)
{
    return PsGetCurrentThread()->Tcb.Teb;
}

PVOID
NTAPI
PsGetCurrentThreadStackBase(VOID)
{
    return PsGetCurrentThread()->Tcb.StackBase;
}

PVOID
NTAPI
PsGetCurrentThreadStackLimit(VOID)
{
    return (PVOID)PsGetCurrentThread()->Tcb.StackLimit;
}

KPROCESSOR_MODE
NTAPI
PsGetCurrentThreadPreviousMode(VOID)
{
    return PsGetCurrentThread()->Tcb.PreviousMode;
}

BOOLEAN
NTAPI
PsIsSystemThread(
    _In_ PETHREAD Thread)
{
    return (BOOLEAN)(Thread->SystemThread != 0);
}

BOOLEAN
NTAPI
PsIsThreadImpersonating(
    _In_ PETHREAD Thread)
{
    return (BOOLEAN)(Thread->ActiveImpersonationInfo != 0);
}

BOOLEAN
NTAPI
PsIsThreadTerminating(
    _In_ PETHREAD Thread)
{
    return (BOOLEAN)(Thread->Terminated != 0);
}

ULONG
NTAPI
PsGetThreadFreezeCount(
    _In_ PETHREAD Thread)
{
    return (ULONG)(UCHAR)Thread->Tcb.FreezeCount;
}

BOOLEAN
NTAPI
PsGetThreadHardErrorsAreDisabled(
    _In_ PETHREAD Thread)
{
    return (BOOLEAN)(Thread->HardErrorsAreDisabled != 0);
}

VOID
NTAPI
PsSetThreadHardErrorsAreDisabled(
    _Inout_ PETHREAD Thread,
    _In_ BOOLEAN HardErrorsAreDisabled)
{
    if (HardErrorsAreDisabled)
        PspSetCrossThreadFlag(Thread, CT_HARD_ERRORS_ARE_DISABLED_BIT);
    else
        PspClearCrossThreadFlag(Thread, CT_HARD_ERRORS_ARE_DISABLED_BIT);
}

/* SCHEDULING AND STACKS ******************************************************/

/*
 * A region a caller runs in while it holds something the scheduler must not
 * take it off for. It holds off ordinary APCs, so nothing runs on the thread
 * in between, and leaves the special ones alone.
 */
VOID
NTAPI
PsEnterPriorityRegion(VOID)
{
    KeEnterCriticalRegion();
}

VOID
NTAPI
PsLeavePriorityRegion(VOID)
{
    KeLeaveCriticalRegion();
}

/*
 * The windowing kernel keeps a thread from falling below a priority while it
 * holds a lock everything else is waiting on. The floor that was in force is
 * handed back, so the caller can put it back when it is done.
 */
CHAR
NTAPI
PsAdjustWin32kPriorityFloor(
    _Inout_ PETHREAD Thread,
    _In_ LONG PriorityFloor)
{
    CHAR Previous = Thread->Win32kPriorityFloor;

    Thread->Win32kPriorityFloor = (CHAR)PriorityFloor;
    return Previous;
}

/*
 * Keeps a thread's kernel stack in memory. A caller that is about to walk
 * another thread's stack holds it this way, because a stack that was swapped
 * out while being read would fault where no fault may be taken.
 */
VOID
NTAPI
PsReferenceKernelStack(
    _Inout_ PETHREAD Thread)
{
    InterlockedIncrement((PLONG)&Thread->KernelStackReference);
}

VOID
NTAPI
PsDereferenceKernelStack(
    _Inout_ PETHREAD Thread)
{
    InterlockedDecrement((PLONG)&Thread->KernelStackReference);
}

/*
 * Says whose work the calling thread is doing, so that the cycles it burns
 * and the power it costs are charged where the work came from rather than
 * to whichever thread happened to pick the work up. A ticket that names no
 * thread clears the attribution.
 */
NTSTATUS
NTAPI
PspSetWorkOnBehalfTicket(
    _In_ PETHREAD Thread,
    _In_ PRTL_WORK_ON_BEHALF_TICKET_EX Ticket)
{
    PETHREAD Behalf = NULL;
    PETHREAD Previous;
    NTSTATUS Status;

    PAGED_CODE();

    if (Ticket->Ticket.ThreadId != 0)
    {
        Status = PsLookupThreadByThreadId(
                     (HANDLE)(ULONG_PTR)Ticket->Ticket.ThreadId,
                     &Behalf);
        if (!NT_SUCCESS(Status)) return Status;

        /*
         * The ticket also carries when the named thread was made, so a
         * thread identifier that has since been given to another thread
         * does not pass for the one the ticket was written for.
         */
        if ((ULONG)Behalf->CreateTime.LowPart !=
            Ticket->Ticket.ThreadCreationTimeLow)
        {
            ObDereferenceObject(Behalf);
            return STATUS_INVALID_PARAMETER;
        }
    }

    Previous = InterlockedExchangePointer((PVOID*)&Thread->WorkOnBehalfThread,
                                          Behalf);

    if (Previous != NULL) ObDereferenceObject(Previous);

    return STATUS_SUCCESS;
}

/* CONTAINERS *****************************************************************/

/*
 * A silo is the container a process runs in. This system runs everything in
 * the host silo, which is named by nothing at all, so every question about a
 * silo is answered from that.
 */
PESILO
NTAPI
PsGetHostSilo(VOID)
{
    return NULL;
}

BOOLEAN
NTAPI
PsIsHostSilo(
    _In_ PESILO Silo)
{
    return (BOOLEAN)(Silo == NULL);
}

PESILO
NTAPI
PsGetCurrentServerSilo(VOID)
{
    return NULL;
}

PESILO
NTAPI
PsGetProcessServerSilo(
    _In_ PEPROCESS Process)
{
    return (PESILO)Process->ServerSilo;
}

BOOLEAN
NTAPI
PsIsCurrentThreadInServerSilo(VOID)
{
    return FALSE;
}

ULONG
NTAPI
PsGetServerSiloServiceSessionId(
    _In_opt_ PESILO Silo)
{
    UNREFERENCED_PARAMETER(Silo);

    /* The host silo's services run in the session the system runs in */
    return 0;
}

PESILO
NTAPI
PsAttachSiloToCurrentThread(
    _In_opt_ PESILO Silo)
{
    PETHREAD Thread = PsGetCurrentThread();
    PESILO Previous = (PESILO)Thread->Silo;

    Thread->Silo = (struct _EJOB *)Silo;
    return Previous;
}

VOID
NTAPI
PsDetachSiloFromCurrentThread(
    _In_opt_ PESILO PreviousSilo)
{
    PsGetCurrentThread()->Silo = (struct _EJOB *)PreviousSilo;
}

/* SYSTEM-CALL FILTERING ******************************************************/

/*
 * A process may be told that it may not call the windowing kernel, or that
 * only part of it is open to the process. Nothing sets that today, so every
 * process is allowed everything and nothing is audited.
 */
ULONG
NTAPI
PsGetWin32KFilterSet(VOID)
{
    return 0;
}

BOOLEAN
NTAPI
PsIsWin32KFilterEnabled(VOID)
{
    return FALSE;
}

BOOLEAN
NTAPI
PsIsWin32KFilterAuditEnabled(VOID)
{
    return FALSE;
}

BOOLEAN
NTAPI
PsIsWin32KFilterEnabledForProcess(
    _In_ PEPROCESS Process)
{
    return (BOOLEAN)(Process->MitigationFlagsValues.EnableFilteredWin32kAPIs != 0);
}

BOOLEAN
NTAPI
PsIsWin32KFilterAuditEnabledForProcess(
    _In_ PEPROCESS Process)
{
    return (BOOLEAN)(Process->MitigationFlagsValues.AuditFilteredWin32kAPIs != 0);
}

/* MITIGATION POLICY **********************************************************/

/*
 * The mitigation policies of a process are read and written as whole words,
 * and a caller that asks for one policy must not see half of an update to
 * another, so one lock covers all of them.
 */
VOID
NTAPI
PspLockMitigationPolicy(VOID)
{
    if (!PspMitigationLockReady)
    {
        KeInitializeGuardedMutex(&PspMitigationLock);
        PspMitigationLockReady = TRUE;
    }

    KeAcquireGuardedMutex(&PspMitigationLock);
}

VOID
NTAPI
PspUnlockMitigationPolicy(VOID)
{
    KeReleaseGuardedMutex(&PspMitigationLock);
}

/*
 * Writes one policy's flags into the words the process keeps them in. The
 * three words are laid out so that policy numbers below sixteen live in the
 * first two, each taking four bits.
 */
VOID
NTAPI
PspStoreMitigationPolicy(
    _In_ PEPROCESS Process,
    _In_ ULONG Policy,
    _In_ ULONG Flags)
{
    ULONG Shift = (Policy % 8) * 4;
    PULONG Word;

    if (Policy < 8) Word = &Process->MitigationFlags;
    else if (Policy < 16) Word = &Process->MitigationFlags2;
    else Word = &Process->MitigationFlags3;

    *Word = (*Word & ~(0xFUL << Shift)) | ((Flags & 0xFUL) << Shift);
}

ULONG
NTAPI
PsGetProcessMitigationPolicyFlags(
    _In_ PEPROCESS Process,
    _In_ ULONG Policy)
{
    ULONG Shift = (Policy % 8) * 4;
    ULONG Word;

    if (Policy < 8) Word = Process->MitigationFlags;
    else if (Policy < 16) Word = Process->MitigationFlags2;
    else Word = Process->MitigationFlags3;

    return (Word >> Shift) & 0xFUL;
}

/*
 * Sets one of the policies a process may change after it has started. A
 * policy that is already on cannot be turned off, because a process that
 * could lift its own mitigation would gain nothing from having one.
 */
NTSTATUS
NTAPI
PspSetExtendedMitigationPolicy(
    _In_ PEPROCESS Process,
    _In_ ULONG Policy,
    _In_ ULONG Flags)
{
    ULONG Current;

    PAGED_CODE();

    if (!PspIsExtendedMitigationPolicy(Policy))
        return STATUS_INVALID_PARAMETER;

    PspLockMitigationPolicy();

    Current = PsGetProcessMitigationPolicyFlags(Process, Policy);
    if ((Current & ~Flags) != 0)
    {
        PspUnlockMitigationPolicy();
        return STATUS_ACCESS_DENIED;
    }

    PspStoreMitigationPolicy(Process, Policy, Current | Flags);
    PspUnlockMitigationPolicy();

    return STATUS_SUCCESS;
}

/* TOKEN ATTRIBUTES ***********************************************************/

/*
 * Two things a caller wants to know about a token without reading it: whether
 * it belongs to a part of the system itself, and whether it belongs to an
 * application that was installed as a package.
 */
VOID
NTAPI
PsQueryProcessAttributesByToken(
    _In_ PACCESS_TOKEN Token,
    _Out_opt_ PBOOLEAN SystemAppIdentifier,
    _Out_opt_ PBOOLEAN PackagedApplication)
{
    PAGED_CODE();

    UNREFERENCED_PARAMETER(Token);

    /* TODO: answer from the token's package claim and its app-container
     * number once a token carries them */
    if (SystemAppIdentifier != NULL) *SystemAppIdentifier = FALSE;
    if (PackagedApplication != NULL) *PackagedApplication = FALSE;
}

/* ACCOUNTING *****************************************************************/

BOOLEAN
NTAPI
PsIsDiskCountersEnabled(VOID)
{
    return PspDiskCountersEnabled;
}

/*
 * Adds what a disk did for a process to the process's own account. A process
 * that has nowhere to keep the account is passed over rather than given one,
 * because the counters are only kept when somebody asked for them.
 */
VOID
NTAPI
PsUpdateDiskCounters(
    _Inout_ PEPROCESS Process,
    _In_ ULONG64 BytesRead,
    _In_ ULONG64 BytesWritten,
    _In_ ULONG ReadOperationCount,
    _In_ ULONG WriteOperationCount,
    _In_ ULONG FlushOperationCount)
{
    PPROCESS_DISK_COUNTERS Counters = Process->DiskCounters;

    if (Counters == NULL) return;

    InterlockedExchangeAdd64((PLONG64)&Counters->BytesRead, BytesRead);
    InterlockedExchangeAdd64((PLONG64)&Counters->BytesWritten, BytesWritten);
    InterlockedExchangeAdd64((PLONG64)&Counters->ReadOperationCount,
                             ReadOperationCount);
    InterlockedExchangeAdd64((PLONG64)&Counters->WriteOperationCount,
                             WriteOperationCount);
    InterlockedExchangeAdd64((PLONG64)&Counters->FlushOperationCount,
                             FlushOperationCount);
}

/* IMAGES *********************************************************************/

/*
 * Whether a process is allowed to map this image. A process told to accept
 * only images that come from somewhere trusted refuses the rest.
 */
NTSTATUS
NTAPI
PsCheckImageLoadPolicy(
    _In_ PFILE_OBJECT FileObject)
{
    PEPROCESS Process = PsGetCurrentProcess();

    PAGED_CODE();

    UNREFERENCED_PARAMETER(FileObject);

    UNREFERENCED_PARAMETER(Process);

    /* TODO: refuse a remote or a low-integrity image when the process asked
     * for ProhibitRemoteImageMap or ProhibitLowILImageMap */
    return STATUS_SUCCESS;
}

/*
 * Hands back the file the process was started from, with a reference the
 * caller gives back. A process started from no image has none.
 */
NTSTATUS
NTAPI
PsReferenceProcessFilePointer(
    _In_ PEPROCESS Process,
    _Outptr_ PFILE_OBJECT *FileObject)
{
    PAGED_CODE();

    *FileObject = NULL;

    PspLockProcessSecurityShared(Process);

    if (Process->ImageFilePointer != NULL)
    {
        ObReferenceObject(Process->ImageFilePointer);
        *FileObject = Process->ImageFilePointer;
    }

    PspUnlockProcessSecurityShared(Process);

    return (*FileObject != NULL) ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
}

/* WORKING-SET WATCHING *******************************************************/

/*
 * Records a fault for a process that asked to be told where its faults come
 * from. The ring is written under a lock at the fault's own level, because
 * this runs inside the fault.
 */
VOID
NTAPI
PsWatchWorkingSet(
    _In_ PEPROCESS Process,
    _In_ PVOID FaultingPc,
    _In_ PVOID FaultingVa)
{
    PPAGEFAULT_HISTORY History = Process->WorkingSetWatch;
    KIRQL OldIrql;

    if (History == NULL) return;

    KeAcquireSpinLock(&History->SpinLock, &OldIrql);

    if (History->CurrentIndex < PSP_WS_WATCH_RECORDS)
    {
        History->WatchInfo[History->CurrentIndex].FaultingPc = FaultingPc;
        History->WatchInfo[History->CurrentIndex].FaultingVa = FaultingVa;
        History->CurrentIndex++;
    }

    KeReleaseSpinLock(&History->SpinLock, OldIrql);
}

/*
 * Hands back what has been recorded. A caller that asks with no room is told
 * how much it needs; the records are left where they are either way, so two
 * callers see the same thing.
 */
NTSTATUS
NTAPI
PspQueryWorkingSetWatch(
    _In_ PEPROCESS Process,
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length,
    _Out_ PULONG ReturnLength,
    _In_ BOOLEAN Extended)
{
    PPAGEFAULT_HISTORY History = Process->WorkingSetWatch;
    ULONG Needed;
    KIRQL OldIrql;

    PAGED_CODE();

    UNREFERENCED_PARAMETER(Extended);

    *ReturnLength = 0;

    if (History == NULL) return STATUS_UNSUCCESSFUL;

    KeAcquireSpinLock(&History->SpinLock, &OldIrql);

    Needed = (ULONG)FIELD_OFFSET(PAGEFAULT_HISTORY,
                                 WatchInfo[History->CurrentIndex]);

    if (Length < Needed)
    {
        KeReleaseSpinLock(&History->SpinLock, OldIrql);
        *ReturnLength = Needed;
        return STATUS_BUFFER_TOO_SMALL;
    }

    RtlCopyMemory(Buffer, History, Needed);

    KeReleaseSpinLock(&History->SpinLock, OldIrql);

    *ReturnLength = Needed;
    return STATUS_SUCCESS;
}

/*
 * Turns the recording on or off. Turning it on twice is refused, because the
 * second caller would silently take over the first one's records.
 */
NTSTATUS
NTAPI
PspSetWorkingSetWatch(
    _In_ PEPROCESS Process,
    _In_ BOOLEAN Enable)
{
    PPAGEFAULT_HISTORY History;

    PAGED_CODE();

    if (!Enable)
    {
        History = InterlockedExchangePointer((PVOID*)&Process->WorkingSetWatch,
                                             NULL);
        if (History == NULL) return STATUS_UNSUCCESSFUL;

        Process->WriteWatch = FALSE;
        ExFreePoolWithTag(History, TAG_PS_WS_WATCH);
        return STATUS_SUCCESS;
    }

    if (Process->WorkingSetWatch != NULL) return STATUS_PORT_ALREADY_SET;

    History = ExAllocatePoolZero(NonPagedPool,
                                 PSP_WS_WATCH_SIZE,
                                 TAG_PS_WS_WATCH);
    if (History == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    KeInitializeSpinLock(&History->SpinLock);
    History->CurrentIndex = 0;
    History->MapIndex = PSP_WS_WATCH_RECORDS;

    if (InterlockedCompareExchangePointer((PVOID*)&Process->WorkingSetWatch,
                                          History,
                                          NULL) != NULL)
    {
        ExFreePoolWithTag(History, TAG_PS_WS_WATCH);
        return STATUS_PORT_ALREADY_SET;
    }

    Process->WriteWatch = TRUE;
    return STATUS_SUCCESS;
}
