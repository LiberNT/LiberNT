/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     What a caller may ask about a thread
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "pspriv.h"
#define NDEBUG
#include <debug.h>

/* PRIVATE FUNCTIONS **********************************************************/

static
NTSTATUS
PspReturnThreadInformation(
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length,
    _In_reads_bytes_(DataLength) const VOID *Data,
    _In_ ULONG DataLength,
    _Out_ PULONG ReturnLength)
{
    *ReturnLength = DataLength;

    if (Length < DataLength) return STATUS_INFO_LENGTH_MISMATCH;

    RtlCopyMemory(Buffer, Data, DataLength);
    return STATUS_SUCCESS;
}

/*
 * The name a caller gave the thread, as a counted string followed by its own
 * characters. A thread with no name answers with an empty one, which is not
 * the same as failing: having no name is a valid answer.
 */
static
NTSTATUS
PspQueryThreadName(
    _In_ PETHREAD Thread,
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length,
    _Out_ PULONG ReturnLength)
{
    PTHREAD_NAME_INFORMATION Out = Buffer;
    PUNICODE_STRING Name;
    ULONG Needed;

    PAGED_CODE();

    PspLockThreadSecurityShared(Thread);

    Name = Thread->ThreadName;
    Needed = sizeof(THREAD_NAME_INFORMATION) +
             ((Name != NULL) ? Name->Length : 0);

    if (Length < Needed)
    {
        PspUnlockThreadSecurityShared(Thread);
        *ReturnLength = Needed;
        return STATUS_INFO_LENGTH_MISMATCH;
    }

    Out->ThreadName.Buffer = (PWCHAR)((PUCHAR)Buffer +
                                      sizeof(THREAD_NAME_INFORMATION));
    Out->ThreadName.Length = (Name != NULL) ? Name->Length : 0;
    Out->ThreadName.MaximumLength = Out->ThreadName.Length;

    if (Out->ThreadName.Length != 0)
    {
        RtlCopyMemory(Out->ThreadName.Buffer,
                      Name->Buffer,
                      Out->ThreadName.Length);
    }

    PspUnlockThreadSecurityShared(Thread);

    *ReturnLength = Needed;
    return STATUS_SUCCESS;
}

/*
 * Everything the system knows about one thread in the shape the system-wide
 * enumeration uses, which is what a caller that already parses that shape
 * asks for when it wants one thread rather than all of them.
 */
static
NTSTATUS
PspQuerySystemThreadInformation(
    _In_ PETHREAD Thread,
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length,
    _Out_ PULONG ReturnLength)
{
    SYSTEM_THREAD_INFORMATION Information;

    PAGED_CODE();

    RtlZeroMemory(&Information, sizeof(Information));

    Information.KernelTime.QuadPart =
        (LONGLONG)Thread->Tcb.KernelTime * KeMaximumIncrement;
    Information.UserTime.QuadPart =
        (LONGLONG)Thread->Tcb.UserTime * KeMaximumIncrement;
    Information.CreateTime = Thread->CreateTime;
    Information.WaitTime = Thread->Tcb.WaitTime;
    Information.StartAddress = Thread->Win32StartAddress;
    Information.ClientId = Thread->Cid;
    Information.Priority = Thread->Tcb.Priority;
    Information.BasePriority = KeQueryBasePriorityThread(&Thread->Tcb);
    Information.ContextSwitches = Thread->Tcb.ContextSwitches;
    Information.ThreadState = Thread->Tcb.State;

    /* TODO: the reason a thread is waiting has no published field on this
     * architecture's thread block, so it is reported as none */

    return PspReturnThreadInformation(Buffer,
                                      Length,
                                      &Information,
                                      sizeof(Information),
                                      ReturnLength);
}

/* SYSTEM CALLS ***************************************************************/

NTSTATUS
NTAPI
NtQueryInformationThread(
    _In_ HANDLE ThreadHandle,
    _In_ THREADINFOCLASS ThreadInformationClass,
    _Out_writes_bytes_(ThreadInformationLength) PVOID ThreadInformation,
    _In_ ULONG ThreadInformationLength,
    _Out_opt_ PULONG ReturnLength)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    ACCESS_MASK Access = THREAD_QUERY_INFORMATION;
    THREAD_POWER_THROTTLING_STATE Throttling;
    THREAD_CYCLE_TIME_INFORMATION Cycles;
    RTL_WORK_ON_BEHALF_TICKET_EX Ticket;
    THREAD_BASIC_INFORMATION Basic;
    PROCESSOR_NUMBER Processor;
    GROUP_AFFINITY Affinity;
    KERNEL_USER_TIMES Times;
    LARGE_INTEGER Counter;
    WOW64_CONTEXT Wow64;
    ULONG Length = 0;
    PETHREAD Thread;
    NTSTATUS Status;
    PVOID Pointer;
    ULONG Value;
    GUID Container;

    PAGED_CODE();

    if (ThreadInformationClass >= MaxThreadInfoClass)
        return STATUS_INVALID_INFO_CLASS;

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            if (ThreadInformationLength != 0)
            {
                ProbeForWrite(ThreadInformation,
                              ThreadInformationLength,
                              sizeof(ULONG));
            }

            if (ReturnLength != NULL) ProbeForWriteUlong(ReturnLength);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }

    /* What a thread is, rather than what it is doing, is open more widely */
    if (ThreadInformationClass == ThreadBasicInformation ||
        ThreadInformationClass == ThreadTimes ||
        ThreadInformationClass == ThreadNameInformation ||
        ThreadInformationClass == ThreadIsTerminated ||
        ThreadInformationClass == ThreadSuspendCount ||
        ThreadInformationClass == ThreadCycleTime ||
        ThreadInformationClass == ThreadSubsystemInformation)
    {
        Access = THREAD_QUERY_LIMITED_INFORMATION;
    }

    /* Reading a thread's registers is reading its state, not its identity */
    if (ThreadInformationClass == ThreadWow64Context)
        Access = THREAD_GET_CONTEXT;

    Status = ObReferenceObjectByHandle(ThreadHandle,
                                       Access,
                                       PsThreadType,
                                       PreviousMode,
                                       (PVOID*)&Thread,
                                       NULL);
    if (!NT_SUCCESS(Status)) return Status;

    switch (ThreadInformationClass)
    {
        case ThreadBasicInformation:
            Basic.ExitStatus = Thread->ExitStatus;
            Basic.TebBaseAddress = Thread->Tcb.Teb;
            Basic.ClientId = Thread->Cid;
            Basic.AffinityMask = Thread->Tcb.Affinity;
            Basic.Priority = Thread->Tcb.Priority;
            Basic.BasePriority = KeQueryBasePriorityThread(&Thread->Tcb);

            Status = PspReturnThreadInformation(ThreadInformation,
                                                ThreadInformationLength,
                                                &Basic,
                                                sizeof(Basic),
                                                &Length);
            break;

        case ThreadTimes:
            RtlZeroMemory(&Times, sizeof(Times));
            Times.CreateTime = Thread->CreateTime;
            if (Thread->Terminated) Times.ExitTime = Thread->ExitTime;
            Times.KernelTime.QuadPart =
                (LONGLONG)Thread->Tcb.KernelTime * KeMaximumIncrement;
            Times.UserTime.QuadPart =
                (LONGLONG)Thread->Tcb.UserTime * KeMaximumIncrement;

            Status = PspReturnThreadInformation(ThreadInformation,
                                                ThreadInformationLength,
                                                &Times,
                                                sizeof(Times),
                                                &Length);
            break;

        case ThreadQuerySetWin32StartAddress:
            Pointer = Thread->Win32StartAddress;

            Status = PspReturnThreadInformation(ThreadInformation,
                                                ThreadInformationLength,
                                                &Pointer,
                                                sizeof(Pointer),
                                                &Length);
            break;

        case ThreadPerformanceCount:
            /*
             * A thread keeps no counter of its own; the one the name refers
             * to was never carried past the earliest versions, and nothing
             * reads it, so the answer is nothing.
             */
            Counter.QuadPart = 0;

            Status = PspReturnThreadInformation(ThreadInformation,
                                                ThreadInformationLength,
                                                &Counter,
                                                sizeof(Counter),
                                                &Length);
            break;

        case ThreadAmILastThread:
            Value = (THREAD_TO_PROCESS(Thread)->ActiveThreads == 1) ? 1 : 0;

            Status = PspReturnThreadInformation(ThreadInformation,
                                                ThreadInformationLength,
                                                &Value,
                                                sizeof(Value),
                                                &Length);
            break;

        case ThreadIsIoPending:
            Value = IsListEmpty(&Thread->IrpList) ? 0 : 1;

            Status = PspReturnThreadInformation(ThreadInformation,
                                                ThreadInformationLength,
                                                &Value,
                                                sizeof(Value),
                                                &Length);
            break;

        case ThreadIsTerminated:
            Value = Thread->Terminated ? 1 : 0;

            Status = PspReturnThreadInformation(ThreadInformation,
                                                ThreadInformationLength,
                                                &Value,
                                                sizeof(Value),
                                                &Length);
            break;

        case ThreadHideFromDebugger:
            Value = Thread->HideFromDebugger ? 1 : 0;

            Status = PspReturnThreadInformation(ThreadInformation,
                                                ThreadInformationLength,
                                                &Value,
                                                sizeof(BOOLEAN),
                                                &Length);
            break;

        case ThreadBreakOnTermination:
            Value = Thread->BreakOnTermination ? 1 : 0;

            Status = PspReturnThreadInformation(ThreadInformation,
                                                ThreadInformationLength,
                                                &Value,
                                                sizeof(Value),
                                                &Length);
            break;

        case ThreadPriorityBoost:
            Value = Thread->Tcb.DisableBoost ? 0 : 1;

            Status = PspReturnThreadInformation(ThreadInformation,
                                                ThreadInformationLength,
                                                &Value,
                                                sizeof(Value),
                                                &Length);
            break;

        case ThreadSuspendCount:
            Value = (ULONG)(UCHAR)Thread->Tcb.SuspendCount;

            Status = PspReturnThreadInformation(ThreadInformation,
                                                ThreadInformationLength,
                                                &Value,
                                                sizeof(Value),
                                                &Length);
            break;

        case ThreadCycleTime:
            Cycles.AccumulatedCycles =
                KeQueryTotalCycleTimeThread(&Thread->Tcb,
                                            &Cycles.CurrentCycleCount);

            Status = PspReturnThreadInformation(ThreadInformation,
                                                ThreadInformationLength,
                                                &Cycles,
                                                sizeof(Cycles),
                                                &Length);
            break;

        case ThreadIoPriority:
            Value = Thread->ThreadIoPriority;

            Status = PspReturnThreadInformation(ThreadInformation,
                                                ThreadInformationLength,
                                                &Value,
                                                sizeof(Value),
                                                &Length);
            break;

        case ThreadPagePriority:
            Value = Thread->ThreadPagePriority;

            Status = PspReturnThreadInformation(ThreadInformation,
                                                ThreadInformationLength,
                                                &Value,
                                                sizeof(Value),
                                                &Length);
            break;

        case ThreadEffectiveIoPriority:
            /*
             * What the thread's requests actually run at: the lower of what
             * the thread asked for and what its process allows, because a
             * thread cannot outrank the process it belongs to.
             */
            Value = Thread->ThreadIoPriority;
            if (THREAD_TO_PROCESS(Thread)->DefaultIoPriority < Value)
                Value = THREAD_TO_PROCESS(Thread)->DefaultIoPriority;

            Status = PspReturnThreadInformation(ThreadInformation,
                                                ThreadInformationLength,
                                                &Value,
                                                sizeof(Value),
                                                &Length);
            break;

        case ThreadEffectivePagePriority:
            Value = Thread->ThreadPagePriority;
            if (Value == 0) Value = THREAD_TO_PROCESS(Thread)->DefaultPagePriority;

            Status = PspReturnThreadInformation(ThreadInformation,
                                                ThreadInformationLength,
                                                &Value,
                                                sizeof(Value),
                                                &Length);
            break;

        case ThreadActualBasePriority:
            Value = (ULONG)(LONG)Thread->Tcb.BasePriority;

            Status = PspReturnThreadInformation(ThreadInformation,
                                                ThreadInformationLength,
                                                &Value,
                                                sizeof(Value),
                                                &Length);
            break;

        case ThreadNameInformation:
            Status = PspQueryThreadName(Thread,
                                        ThreadInformation,
                                        ThreadInformationLength,
                                        &Length);
            break;

        case ThreadIdealProcessorEx:
            RtlZeroMemory(&Processor, sizeof(Processor));
            Processor.Number = (UCHAR)Thread->Tcb.IdealProcessor;

            Status = PspReturnThreadInformation(ThreadInformation,
                                                ThreadInformationLength,
                                                &Processor,
                                                sizeof(Processor),
                                                &Length);
            break;

        case ThreadGroupInformation:
        case ThreadActualGroupAffinity:
            /*
             * This machine has one processor group, so the affinity a thread
             * was given and the one it actually runs under are the same set
             * named two ways.
             */
            RtlZeroMemory(&Affinity, sizeof(Affinity));
            Affinity.Mask = Thread->Tcb.Affinity;

            Status = PspReturnThreadInformation(ThreadInformation,
                                                ThreadInformationLength,
                                                &Affinity,
                                                sizeof(Affinity),
                                                &Length);
            break;

        case ThreadHeterogeneousCpuPolicy:
            Value = (ULONG)KeQueryHeteroCpuPolicyThread(&Thread->Tcb, TRUE);

            Status = PspReturnThreadInformation(ThreadInformation,
                                                ThreadInformationLength,
                                                &Value,
                                                sizeof(Value),
                                                &Length);
            break;

        case ThreadContainerId:
            /*
             * The container the thread runs in. Everything here runs in the
             * host, which is named by no identifier at all.
             */
            RtlZeroMemory(&Container, sizeof(Container));

            Status = PspReturnThreadInformation(ThreadInformation,
                                                ThreadInformationLength,
                                                &Container,
                                                sizeof(Container),
                                                &Length);
            break;

        case ThreadSelectedCpuSets:
            Status = PspQueryThreadCpuSets(Thread,
                                           ThreadInformation,
                                           ThreadInformationLength,
                                           &Length);
            break;

        case ThreadSystemThreadInformation:
            Status = PspQuerySystemThreadInformation(Thread,
                                                     ThreadInformation,
                                                     ThreadInformationLength,
                                                     &Length);
            break;

        case ThreadSubsystemInformation:
            /* The native subsystem, which is what a thread runs under */
            Value = 0;

            Status = PspReturnThreadInformation(ThreadInformation,
                                                ThreadInformationLength,
                                                &Value,
                                                sizeof(Value),
                                                &Length);
            break;

        case ThreadDynamicCodePolicyInfo:
            Value = THREAD_TO_PROCESS(Thread)->
                        MitigationFlagsValues.DisableDynamicCode ? 1 : 0;
            if (Thread->DisableDynamicCodeOptOut) Value |= 2;

            Status = PspReturnThreadInformation(ThreadInformation,
                                                ThreadInformationLength,
                                                &Value,
                                                sizeof(Value),
                                                &Length);
            break;

        case ThreadExplicitCaseSensitivity:
            Value = Thread->ExplicitCaseSensitivity ? 1 : 0;

            Status = PspReturnThreadInformation(ThreadInformation,
                                                ThreadInformationLength,
                                                &Value,
                                                sizeof(Value),
                                                &Length);
            break;

        case ThreadWorkOnBehalfTicket:
            /*
             * Which other thread's work this one is doing, so that what it
             * costs is charged where the work came from.
             */
            RtlZeroMemory(&Ticket, sizeof(Ticket));

            if (Thread->WorkOnBehalfThread != NULL)
            {
                PETHREAD Behalf = Thread->WorkOnBehalfThread;

                Ticket.Ticket.ThreadId =
                    HandleToUlong(Behalf->Cid.UniqueThread);
                Ticket.Ticket.ThreadCreationTimeLow =
                    (ULONG)Behalf->CreateTime.LowPart;
                Ticket.CurrentThread = (Behalf == Thread) ? 1 : 0;
            }

            Status = PspReturnThreadInformation(ThreadInformation,
                                                ThreadInformationLength,
                                                &Ticket,
                                                sizeof(Ticket),
                                                &Length);
            break;

        case ThreadDbgkWerReportActive:
            Value = Thread->DbgWerUserReportActive ? 1 : 0;

            Status = PspReturnThreadInformation(ThreadInformation,
                                                ThreadInformationLength,
                                                &Value,
                                                sizeof(BOOLEAN),
                                                &Length);
            break;

        case ThreadPowerThrottlingState:
            RtlZeroMemory(&Throttling, sizeof(Throttling));
            Throttling.Version = 1;
            Throttling.ControlMask = Thread->PowerThrottlingControlMask;
            Throttling.StateMask = Thread->PowerThrottlingStateMask;

            Status = PspReturnThreadInformation(ThreadInformation,
                                                ThreadInformationLength,
                                                &Throttling,
                                                sizeof(Throttling),
                                                &Length);
            break;

        case ThreadWow64Context:
            /*
             * The 32-bit state of a thread that runs 32-bit code. A thread
             * that runs none has no such state to report.
             */
            if (THREAD_TO_PROCESS(Thread)->WoW64Process == NULL)
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }

            if (ThreadInformationLength != sizeof(WOW64_CONTEXT))
            {
                Length = sizeof(WOW64_CONTEXT);
                Status = STATUS_INFO_LENGTH_MISMATCH;
                break;
            }

            RtlZeroMemory(&Wow64, sizeof(Wow64));

            if (PreviousMode != KernelMode)
            {
                _SEH2_TRY
                {
                    Wow64.ContextFlags =
                        ((PWOW64_CONTEXT)ThreadInformation)->ContextFlags;
                }
                _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
                {
                    Status = _SEH2_GetExceptionCode();
                }
                _SEH2_END;

                if (!NT_SUCCESS(Status)) break;
            }
            else
            {
                Wow64.ContextFlags =
                    ((PWOW64_CONTEXT)ThreadInformation)->ContextFlags;
            }

            Status = PspArchCopyLiveWow64Context(Thread, &Wow64, FALSE);
            if (NT_SUCCESS(Status))
            {
                Status = PspReturnThreadInformation(ThreadInformation,
                                                    ThreadInformationLength,
                                                    &Wow64,
                                                    sizeof(Wow64),
                                                    &Length);
            }
            break;

        case ThreadDescriptorTableEntry:
            /* TODO: only x86 has a descriptor table, and reading an entry
             * from one needs an architecture hook this subsystem lacks */
            Status = STATUS_NOT_SUPPORTED;
            break;

        case ThreadEventPair_Reusable:
        case ThreadUmsInformation:
        case ThreadCounterProfiling:
        case ThreadCSwitchMon:
        case ThreadCSwitchPmu:
            /*
             * Each of these named a facility that was taken out of the
             * system: a caller is told it is not there rather than being
             * given an answer that no longer means anything.
             */
            Status = STATUS_NOT_SUPPORTED;
            break;

        case ThreadCreateStateChange:
        case ThreadApplyStateChange:
            /* TODO: the state-change object a caller freezes a thread with */
            Status = STATUS_NOT_SUPPORTED;
            break;

        case ThreadLastSystemCall:
            /* TODO: the last call the thread made; the published headers
             * give no shape for the answer. INFO_NEEDED */
            Status = STATUS_NOT_SUPPORTED;
            break;

        case ThreadTebInformation:
            /* TODO: read a span of the thread's own block; the published
             * headers give no shape for the request. INFO_NEEDED */
            Status = STATUS_NOT_SUPPORTED;
            break;

        /*
         * The classes below are written and never read.
         */
        case ThreadPriority:
        case ThreadBasePriority:
        case ThreadAffinityMask:
        case ThreadImpersonationToken:
        case ThreadEnableAlignmentFaultFixup:
        case ThreadZeroTlsCell:
        case ThreadIdealProcessor:
        case ThreadSetTlsArrayAddress:
        case ThreadSwitchLegacyState:
        case ThreadCpuAccountingInformation:
        case ThreadAttachContainer:
        case ThreadManageWritesToExecutableMemory:
        case ThreadWorkloadClass:
        case ThreadStrongerBadHandleChecks:
            Status = STATUS_INVALID_INFO_CLASS;
            break;

        default:
            Status = STATUS_INVALID_INFO_CLASS;
            break;
    }

    ObDereferenceObject(Thread);

    if (ReturnLength == NULL) return Status;

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            *ReturnLength = Length;
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            /* The answer was written; only the length could not be */
        }
        _SEH2_END;
    }
    else
    {
        *ReturnLength = Length;
    }

    return Status;
}
