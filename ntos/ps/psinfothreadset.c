/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     What a caller may change about a thread
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "pspriv.h"
#define NDEBUG
#include <debug.h>

/* The longest name a thread may be given, in characters */
#define PSP_MAX_THREAD_NAME 512

/* What ThreadDynamicCodePolicyInfo carries */
#define PSP_DYNAMIC_CODE_DISABLE 0x1
#define PSP_DYNAMIC_CODE_OPT_OUT 0x2

/*
 * The lengths each class may be set with, checked before the thread is
 * looked up. A class that is only ever read is refused once the buffer has
 * been checked.
 */
static const PSP_INFORMATION_CLASS PspSetThreadClasses[MaxThreadInfoClass] =
{
    [ThreadBasicInformation] = PSP_INFO_WRONG_DIRECTION,
    [ThreadTimes] = PSP_INFO_WRONG_DIRECTION,
    [ThreadPriority] = PSP_INFO_EXACT(sizeof(KPRIORITY)),
    [ThreadBasePriority] = PSP_INFO_EXACT(sizeof(LONG)),
    [ThreadAffinityMask] = PSP_INFO_EXACT(sizeof(KAFFINITY)),
    [ThreadImpersonationToken] = PSP_INFO_EXACT(sizeof(HANDLE)),
    [ThreadDescriptorTableEntry] = PSP_INFO_WRONG_DIRECTION,
    [ThreadEnableAlignmentFaultFixup] = PSP_INFO_EXACT(sizeof(BOOLEAN)),
    [ThreadEventPair_Reusable] = PSP_INFO_WRONG_DIRECTION,
    [ThreadQuerySetWin32StartAddress] = PSP_INFO_EXACT(sizeof(PVOID)),
    [ThreadZeroTlsCell] = PSP_INFO_EXACT(sizeof(ULONG)),
    [ThreadPerformanceCount] = PSP_INFO_WRONG_DIRECTION,
    [ThreadAmILastThread] = PSP_INFO_WRONG_DIRECTION,
    [ThreadIdealProcessor] = PSP_INFO_EXACT(sizeof(ULONG)),
    [ThreadPriorityBoost] = PSP_INFO_EXACT(sizeof(ULONG)),
    [ThreadSetTlsArrayAddress] = PSP_INFO_EXACT(sizeof(PVOID)),
    [ThreadIsIoPending] = PSP_INFO_WRONG_DIRECTION,
    [ThreadHideFromDebugger] = PSP_INFO_EXACT(0),
    [ThreadBreakOnTermination] = PSP_INFO_EXACT(sizeof(ULONG)),
    /* TODO: x86 switches a thread's legacy floating point state through this class */
    [ThreadSwitchLegacyState] = PSP_INFO_NOT_IMPLEMENTED,
    [ThreadIsTerminated] = PSP_INFO_WRONG_DIRECTION,
    [ThreadLastSystemCall] = PSP_INFO_WRONG_DIRECTION,
    [ThreadIoPriority] = PSP_INFO_EXACT(sizeof(ULONG)),
    [ThreadCycleTime] = PSP_INFO_WRONG_DIRECTION,
    [ThreadPagePriority] = PSP_INFO_EXACT(sizeof(PAGE_PRIORITY_INFORMATION)),
    [ThreadActualBasePriority] = PSP_INFO_WRONG_DIRECTION,
    [ThreadTebInformation] = PSP_INFO_WRONG_DIRECTION,
    [ThreadCSwitchMon] = PSP_INFO_WRONG_DIRECTION,
    [ThreadCSwitchPmu] = PSP_INFO_WRONG_DIRECTION,
    [ThreadWow64Context] = PSP_INFO_EXACT(sizeof(WOW64_CONTEXT)),
    [ThreadGroupInformation] = PSP_INFO_EXACT(sizeof(GROUP_AFFINITY)),
    [ThreadUmsInformation] = PSP_INFO_WRONG_DIRECTION,
    [ThreadCounterProfiling] = PSP_INFO_ALIGNED(sizeof(UCHAR)),
    [ThreadIdealProcessorEx] = PSP_INFO_EXACT(sizeof(PROCESSOR_NUMBER)),
    [ThreadCpuAccountingInformation] = PSP_INFO_EXACT(sizeof(HANDLE)),
    [ThreadSuspendCount] = PSP_INFO_WRONG_DIRECTION,
    [ThreadContainerId] = PSP_INFO_WRONG_DIRECTION,
    [ThreadNameInformation] = PSP_INFO_EXACT(sizeof(THREAD_NAME_INFORMATION)),
    [ThreadSelectedCpuSets] = PSP_INFO_MULTIPLE(sizeof(ULONG)),
    [ThreadSystemThreadInformation] = PSP_INFO_WRONG_DIRECTION,
    [ThreadActualGroupAffinity] = PSP_INFO_WRONG_DIRECTION,
    [ThreadDynamicCodePolicyInfo] = PSP_INFO_EXACT(sizeof(ULONG)),
    [ThreadExplicitCaseSensitivity] = PSP_INFO_EXACT(sizeof(ULONG)),
    [ThreadWorkOnBehalfTicket] = PSP_INFO_EXACT_OR(sizeof(ALPC_WORK_ON_BEHALF_TICKET),
                                                   sizeof(RTL_WORK_ON_BEHALF_TICKET_EX)),
    [ThreadSubsystemInformation] = PSP_INFO_WRONG_DIRECTION,
    [ThreadDbgkWerReportActive] = PSP_INFO_EXACT(sizeof(ULONG)),
    [ThreadAttachContainer] = PSP_INFO_EXACT(sizeof(HANDLE)),
    [ThreadManageWritesToExecutableMemory] = PSP_INFO_EXACT(sizeof(MANAGE_WRITES_TO_EXECUTABLE_MEMORY)),
    [ThreadPowerThrottlingState] = PSP_INFO_EXACT(sizeof(THREAD_POWER_THROTTLING_STATE)),
    [ThreadWorkloadClass] = PSP_INFO_EXACT(sizeof(ULONG)),
    [ThreadCreateStateChange] = PSP_INFO_WRONG_DIRECTION,
    [ThreadApplyStateChange] = PSP_INFO_WRONG_DIRECTION,
    [ThreadStrongerBadHandleChecks] = PSP_INFO_EXACT(0),
    [ThreadEffectiveIoPriority] = PSP_INFO_WRONG_DIRECTION,
    [ThreadEffectivePagePriority] = PSP_INFO_WRONG_DIRECTION,
};

/* PRIVATE FUNCTIONS **********************************************************/

static
NTSTATUS
PspCaptureThreadSetInformation(
    _In_reads_bytes_(Length) PVOID Information,
    _In_ ULONG Length,
    _In_ KPROCESSOR_MODE PreviousMode,
    _Out_writes_bytes_(Expected) PVOID Captured,
    _In_ ULONG Expected)
{
    if (Length != Expected) return STATUS_INFO_LENGTH_MISMATCH;

    if (PreviousMode == KernelMode)
    {
        RtlCopyMemory(Captured, Information, Expected);
        return STATUS_SUCCESS;
    }

    _SEH2_TRY
    {
        ProbeForRead(Information, Expected, sizeof(ULONG));
        RtlCopyMemory(Captured, Information, Expected);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        _SEH2_YIELD(return _SEH2_GetExceptionCode());
    }
    _SEH2_END;

    return STATUS_SUCCESS;
}

/*
 * Names a thread, or takes its name away. The name is copied into the
 * kernel's own memory, because what a caller passes may change or go away
 * while the name is still being read by somebody else.
 */
static
NTSTATUS
PspSetThreadName(
    _In_ PETHREAD Thread,
    _In_reads_bytes_(Length) PVOID Information,
    _In_ ULONG Length,
    _In_ KPROCESSOR_MODE PreviousMode)
{
    THREAD_NAME_INFORMATION Safe;
    PUNICODE_STRING New = NULL;
    PUNICODE_STRING Old;
    NTSTATUS Status;

    PAGED_CODE();

    Status = PspCaptureThreadSetInformation(Information,
                                            Length,
                                            PreviousMode,
                                            &Safe,
                                            sizeof(Safe));
    if (!NT_SUCCESS(Status)) return Status;

    if ((Safe.ThreadName.Length % sizeof(WCHAR)) != 0 ||
        Safe.ThreadName.Length > PSP_MAX_THREAD_NAME * sizeof(WCHAR))
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (Safe.ThreadName.Length != 0)
    {
        /* A user caller's missing characters fault when read, as on Windows */
        if ((Safe.ThreadName.Buffer == NULL) && (PreviousMode == KernelMode))
            return STATUS_INVALID_PARAMETER;

        New = ExAllocatePoolWithTag(PagedPool,
                                    sizeof(UNICODE_STRING) +
                                        Safe.ThreadName.Length,
                                    TAG_THREAD_NAME);
        if (New == NULL) return STATUS_INSUFFICIENT_RESOURCES;

        New->Buffer = (PWCHAR)((PUCHAR)New + sizeof(UNICODE_STRING));
        New->Length = Safe.ThreadName.Length;
        New->MaximumLength = Safe.ThreadName.Length;

        if (PreviousMode != KernelMode)
        {
            _SEH2_TRY
            {
                ProbeForRead(Safe.ThreadName.Buffer,
                             Safe.ThreadName.Length,
                             sizeof(WCHAR));
                RtlCopyMemory(New->Buffer,
                              Safe.ThreadName.Buffer,
                              Safe.ThreadName.Length);
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                ExFreePoolWithTag(New, TAG_THREAD_NAME);
                _SEH2_YIELD(return _SEH2_GetExceptionCode());
            }
            _SEH2_END;
        }
        else
        {
            RtlCopyMemory(New->Buffer,
                          Safe.ThreadName.Buffer,
                          Safe.ThreadName.Length);
        }
    }

    PspLockThreadSecurityExclusive(Thread);
    Old = Thread->ThreadName;
    Thread->ThreadName = New;
    PspUnlockThreadSecurityExclusive(Thread);

    if (Old != NULL) ExFreePoolWithTag(Old, TAG_THREAD_NAME);

    return STATUS_SUCCESS;
}

/*
 * Clears one storage slot of the thread, which is what a module that owned
 * the slot asks for on its way out.
 */
static
NTSTATUS
PspZeroThreadTlsCell(
    _In_ PETHREAD Thread,
    _In_ ULONG Index)
{
    PEPROCESS Process = THREAD_TO_PROCESS(Thread);
    PTEB Teb = Thread->Tcb.Teb;
    KAPC_STATE ApcState;
    NTSTATUS Status = STATUS_SUCCESS;

    PAGED_CODE();

    if (Index >= TLS_MINIMUM_AVAILABLE) return STATUS_INVALID_PARAMETER;
    if (Teb == NULL) return STATUS_INVALID_PARAMETER;

    if (!ExAcquireRundownProtection(&Process->RundownProtect))
        return STATUS_PROCESS_IS_TERMINATING;

    KeStackAttachProcess(&Process->Pcb, &ApcState);

    _SEH2_TRY
    {
        Teb->TlsSlots[Index] = NULL;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    KeUnstackDetachProcess(&ApcState);
    ExReleaseRundownProtection(&Process->RundownProtect);

    return Status;
}

/*
 * Points the thread's storage array somewhere else, which is what a runtime
 * does when it grows the array past what the thread's own block holds.
 */
static
NTSTATUS
PspSetTlsArrayAddress(
    _In_ PETHREAD Thread,
    _In_ PVOID Address)
{
    PEPROCESS Process = THREAD_TO_PROCESS(Thread);
    PTEB Teb = Thread->Tcb.Teb;
    KAPC_STATE ApcState;
    NTSTATUS Status = STATUS_SUCCESS;

    PAGED_CODE();

    if (Teb == NULL) return STATUS_INVALID_PARAMETER;

    /* The array lives in the thread's own memory, so it must be there */
    if ((ULONG_PTR)Address >= MmUserProbeAddress)
        return STATUS_INVALID_PARAMETER;

    if (!ExAcquireRundownProtection(&Process->RundownProtect))
        return STATUS_PROCESS_IS_TERMINATING;

    KeStackAttachProcess(&Process->Pcb, &ApcState);

    _SEH2_TRY
    {
        Teb->ThreadLocalStoragePointer = Address;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    KeUnstackDetachProcess(&ApcState);
    ExReleaseRundownProtection(&Process->RundownProtect);

    return Status;
}

/* SYSTEM CALLS ***************************************************************/

NTSTATUS
NTAPI
NtSetInformationThread(
    _In_ HANDLE ThreadHandle,
    _In_ THREADINFOCLASS ThreadInformationClass,
    _In_reads_bytes_(ThreadInformationLength) PVOID ThreadInformation,
    _In_ ULONG ThreadInformationLength)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    ACCESS_MASK Access = THREAD_SET_INFORMATION;
    THREAD_POWER_THROTTLING_STATE Throttling;
    RTL_WORK_ON_BEHALF_TICKET_EX Ticket;
    PROCESSOR_NUMBER Processor;
    GROUP_AFFINITY Affinity;
    WOW64_CONTEXT Wow64;
    KPRIORITY Priority;
    PETHREAD Thread;
    NTSTATUS Status;
    ULONG_PTR Pointer;
    BOOLEAN Boolean;
    ULONG Value;
    PVOID Address;
    HANDLE Handle;

    PAGED_CODE();

    if (ThreadInformationClass >= MaxThreadInfoClass)
        return STATUS_INVALID_INFO_CLASS;

    Status = PspCheckInformationClass(&PspSetThreadClasses[ThreadInformationClass],
                                      ThreadInformation,
                                      ThreadInformationLength,
                                      PreviousMode);
    if (!NT_SUCCESS(Status)) return Status;

    /*
     * Which right the change needs depends on what is being changed: wearing
     * a token is its own right, naming a thread needs less than steering it,
     * and writing its registers is the right to set its state.
     */
    if (ThreadInformationClass == ThreadImpersonationToken)
        Access = THREAD_SET_THREAD_TOKEN;
    else if (ThreadInformationClass == ThreadNameInformation ||
             ThreadInformationClass == ThreadPowerThrottlingState ||
             ThreadInformationClass == ThreadSelectedCpuSets)
    {
        Access = THREAD_SET_LIMITED_INFORMATION;
    }
    else if (ThreadInformationClass == ThreadWow64Context)
    {
        Access = THREAD_SET_CONTEXT;
    }

    /* The alignment fixup switch is read before the handle is looked at */
    if (ThreadInformationClass == ThreadEnableAlignmentFaultFixup)
    {
        Status = PspCaptureThreadSetInformation(ThreadInformation,
                                                ThreadInformationLength,
                                                PreviousMode,
                                                &Boolean,
                                                sizeof(Boolean));
        if (!NT_SUCCESS(Status)) return Status;
    }

    Status = ObReferenceObjectByHandle(ThreadHandle,
                                       Access,
                                       PsThreadType,
                                       PreviousMode,
                                       (PVOID*)&Thread,
                                       NULL);
    if (!NT_SUCCESS(Status)) return Status;

    switch (ThreadInformationClass)
    {
        case ThreadPriority:
            Status = PspCaptureThreadSetInformation(ThreadInformation,
                                                    ThreadInformationLength,
                                                    PreviousMode,
                                                    &Priority,
                                                    sizeof(Priority));
            if (!NT_SUCCESS(Status)) break;

            if (Priority > HIGH_PRIORITY || Priority <= LOW_PRIORITY)
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }

            if (Priority >= LOW_REALTIME_PRIORITY &&
                !SeSinglePrivilegeCheck(SeIncreaseBasePriorityPrivilege,
                                        PreviousMode))
            {
                Status = STATUS_PRIVILEGE_NOT_HELD;
                break;
            }

            KeSetPriorityThread((PKTHREAD)&Thread->Tcb, Priority);
            Status = STATUS_SUCCESS;
            break;

        case ThreadBasePriority:
            Status = PspCaptureThreadSetInformation(ThreadInformation,
                                                    ThreadInformationLength,
                                                    PreviousMode,
                                                    &Priority,
                                                    sizeof(Priority));
            if (!NT_SUCCESS(Status)) break;

            /*
             * A thread steps at most two levels from its class's base, or
             * saturates at either edge of the class, which is what the
             * time-critical and idle levels ask for and needs no privilege.
             * Only a real-time process may step further inside its band.
             */
            if (Priority != THREAD_BASE_PRIORITY_LOWRT + 1 &&
                Priority != THREAD_BASE_PRIORITY_IDLE - 1 &&
                (Priority > THREAD_BASE_PRIORITY_MAX ||
                 Priority < THREAD_BASE_PRIORITY_MIN))
            {
                /* FIXME: SetThreadPriority documents -7..6 for real-time; the whole band is accepted and clamped */
                if (THREAD_TO_PROCESS(Thread)->PriorityClass !=
                        PROCESS_PRIORITY_CLASS_REALTIME ||
                    Priority > THREAD_BASE_PRIORITY_LOWRT ||
                    Priority < THREAD_BASE_PRIORITY_IDLE)
                {
                    Status = STATUS_INVALID_PARAMETER;
                    break;
                }
            }

            KeSetBasePriorityThread((PKTHREAD)&Thread->Tcb, Priority);
            Status = STATUS_SUCCESS;
            break;

        case ThreadAffinityMask:
            Status = PspCaptureThreadSetInformation(ThreadInformation,
                                                    ThreadInformationLength,
                                                    PreviousMode,
                                                    &Pointer,
                                                    sizeof(Pointer));
            if (!NT_SUCCESS(Status)) break;

            /* Bits naming processors the machine lacks are dropped: an all-ones mask means every processor */
            Pointer &= (ULONG_PTR)KeActiveProcessors;

            /*
             * A thread may only be narrowed to processors its process is
             * already allowed to run on.
             */
            if (Pointer == 0 ||
                (Pointer & ~THREAD_TO_PROCESS(Thread)->Pcb.Affinity) != 0)
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }

            KeSetAffinityThread(&Thread->Tcb, (KAFFINITY)Pointer);
            Status = STATUS_SUCCESS;
            break;

        case ThreadGroupInformation:
            Status = PspCaptureThreadSetInformation(ThreadInformation,
                                                    ThreadInformationLength,
                                                    PreviousMode,
                                                    &Affinity,
                                                    sizeof(Affinity));
            if (!NT_SUCCESS(Status)) break;

            /* This machine has one group, so no other one may be named */
            if (Affinity.Group != 0)
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }

            if (Affinity.Mask == 0 ||
                (Affinity.Mask &
                 ~THREAD_TO_PROCESS(Thread)->Pcb.Affinity) != 0)
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }

            KeSetAffinityThread(&Thread->Tcb, Affinity.Mask);
            Status = STATUS_SUCCESS;
            break;

        case ThreadIdealProcessor:
            Status = PspCaptureThreadSetInformation(ThreadInformation,
                                                    ThreadInformationLength,
                                                    PreviousMode,
                                                    &Value,
                                                    sizeof(Value));
            if (!NT_SUCCESS(Status)) break;

            if (Value >= MAXIMUM_PROCESSORS)
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }

            KeSetIdealProcessorThread((PKTHREAD)&Thread->Tcb, (UCHAR)Value);
            Status = STATUS_SUCCESS;
            break;

        case ThreadIdealProcessorEx:
            Status = PspCaptureThreadSetInformation(ThreadInformation,
                                                    ThreadInformationLength,
                                                    PreviousMode,
                                                    &Processor,
                                                    sizeof(Processor));
            if (!NT_SUCCESS(Status)) break;

            if (Processor.Group != 0 ||
                Processor.Reserved != 0 ||
                Processor.Number >= (UCHAR)KeNumberProcessors)
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }

            Processor.Number = KeSetIdealProcessorThread((PKTHREAD)&Thread->Tcb,
                                                         Processor.Number);
            Status = STATUS_SUCCESS;

            /* The caller's record is given back holding the previous choice */
            _SEH2_TRY
            {
                *(PPROCESSOR_NUMBER)ThreadInformation = Processor;
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                Status = _SEH2_GetExceptionCode();
            }
            _SEH2_END;
            break;

        case ThreadPriorityBoost:
            Status = PspCaptureThreadSetInformation(ThreadInformation,
                                                    ThreadInformationLength,
                                                    PreviousMode,
                                                    &Value,
                                                    sizeof(Value));
            if (!NT_SUCCESS(Status)) break;

            KeSetDisableBoostThread(&Thread->Tcb, (BOOLEAN)(Value == 0));
            Status = STATUS_SUCCESS;
            break;

        case ThreadImpersonationToken:
            Status = PspCaptureThreadSetInformation(ThreadInformation,
                                                    ThreadInformationLength,
                                                    PreviousMode,
                                                    &Handle,
                                                    sizeof(Handle));
            if (!NT_SUCCESS(Status)) break;

            Status = PsAssignImpersonationToken(Thread, Handle);
            break;

        case ThreadQuerySetWin32StartAddress:
            Status = PspCaptureThreadSetInformation(ThreadInformation,
                                                    ThreadInformationLength,
                                                    PreviousMode,
                                                    &Address,
                                                    sizeof(Address));
            if (!NT_SUCCESS(Status)) break;

            Thread->Win32StartAddress = Address;
            Status = STATUS_SUCCESS;
            break;

        case ThreadHideFromDebugger:
            /*
             * This one carries nothing: asking for it at all is the request,
             * and it cannot be undone, because a thread that could show
             * itself again would defeat the point of hiding it.
             */
            if (ThreadInformationLength != 0)
            {
                Status = STATUS_INFO_LENGTH_MISMATCH;
                break;
            }

            PspSetCrossThreadFlag(Thread, CT_HIDE_FROM_DEBUGGER_BIT);
            Status = STATUS_SUCCESS;
            break;

        case ThreadBreakOnTermination:
            Status = PspCaptureThreadSetInformation(ThreadInformation,
                                                    ThreadInformationLength,
                                                    PreviousMode,
                                                    &Value,
                                                    sizeof(Value));
            if (!NT_SUCCESS(Status)) break;

            if (!SeSinglePrivilegeCheck(SeDebugPrivilege, PreviousMode))
            {
                Status = STATUS_PRIVILEGE_NOT_HELD;
                break;
            }

            if (Value != 0)
                PspSetCrossThreadFlag(Thread, CT_BREAK_ON_TERMINATION_BIT);
            else
                PspClearCrossThreadFlag(Thread, CT_BREAK_ON_TERMINATION_BIT);

            Status = STATUS_SUCCESS;
            break;

        case ThreadEnableAlignmentFaultFixup:
            KeSetAutoAlignmentThread(&Thread->Tcb, Boolean);
            Status = STATUS_SUCCESS;
            break;

        case ThreadNameInformation:
            Status = PspSetThreadName(Thread,
                                      ThreadInformation,
                                      ThreadInformationLength,
                                      PreviousMode);
            break;

        case ThreadIoPriority:
            Status = PspCaptureThreadSetInformation(ThreadInformation,
                                                    ThreadInformationLength,
                                                    PreviousMode,
                                                    &Value,
                                                    sizeof(Value));
            if (!NT_SUCCESS(Status)) break;

            if (Value >= MaxIoPriorityTypes)
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }

            /* Asking for more than normal needs the privilege to raise it */
            if (Value > IoPriorityNormal &&
                !SeSinglePrivilegeCheck(SeIncreaseBasePriorityPrivilege,
                                        PreviousMode))
            {
                Status = STATUS_PRIVILEGE_NOT_HELD;
                break;
            }

            Thread->ThreadIoPriority = Value;
            Status = STATUS_SUCCESS;
            break;

        case ThreadPagePriority:
            Status = PspCaptureThreadSetInformation(ThreadInformation,
                                                    ThreadInformationLength,
                                                    PreviousMode,
                                                    &Value,
                                                    sizeof(Value));
            if (!NT_SUCCESS(Status)) break;

            if (Value > MEMORY_PRIORITY_FOREGROUND + 3)
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }

            Thread->ThreadPagePriority = Value;
            Status = STATUS_SUCCESS;
            break;

        case ThreadZeroTlsCell:
            Status = PspCaptureThreadSetInformation(ThreadInformation,
                                                    ThreadInformationLength,
                                                    PreviousMode,
                                                    &Value,
                                                    sizeof(Value));
            if (!NT_SUCCESS(Status)) break;

            Status = PspZeroThreadTlsCell(Thread, Value);
            break;

        case ThreadSetTlsArrayAddress:
            Status = PspCaptureThreadSetInformation(ThreadInformation,
                                                    ThreadInformationLength,
                                                    PreviousMode,
                                                    &Address,
                                                    sizeof(Address));
            if (!NT_SUCCESS(Status)) break;

            Status = PspSetTlsArrayAddress(Thread, Address);
            break;

        case ThreadSelectedCpuSets:
            Status = PspSetThreadCpuSets(Thread,
                                         ThreadInformation,
                                         ThreadInformationLength,
                                         PreviousMode);
            break;

        case ThreadExplicitCaseSensitivity:
            Status = PspCaptureThreadSetInformation(ThreadInformation,
                                                    ThreadInformationLength,
                                                    PreviousMode,
                                                    &Value,
                                                    sizeof(Value));
            if (!NT_SUCCESS(Status)) break;

            if (Value > 1)
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }

            /*
             * Telling the file system to match names exactly is a system
             * decision: a thread that could ask for it would see files its
             * process was never meant to reach by another spelling.
             */
            if (Value != 0 &&
                !SeSinglePrivilegeCheck(SeDebugPrivilege, PreviousMode))
            {
                Status = STATUS_PRIVILEGE_NOT_HELD;
                break;
            }

            if (Value != 0)
                PspSetCrossThreadFlag(Thread, CT_EXPLICIT_CASE_SENSITIVITY_BIT);
            else
                PspClearCrossThreadFlag(Thread,
                                        CT_EXPLICIT_CASE_SENSITIVITY_BIT);

            Status = STATUS_SUCCESS;
            break;

        case ThreadDynamicCodePolicyInfo:
            Status = PspCaptureThreadSetInformation(ThreadInformation,
                                                    ThreadInformationLength,
                                                    PreviousMode,
                                                    &Value,
                                                    sizeof(Value));
            if (!NT_SUCCESS(Status)) break;

            if ((Value & ~(PSP_DYNAMIC_CODE_DISABLE |
                           PSP_DYNAMIC_CODE_OPT_OUT)) != 0)
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }

            /*
             * A thread may step out of its process's ban on making code at
             * run time only if the process allowed that when it set the ban.
             */
            if ((Value & PSP_DYNAMIC_CODE_OPT_OUT) != 0 &&
                !THREAD_TO_PROCESS(Thread)->
                     MitigationFlagsValues.DisableDynamicCodeAllowOptOut)
            {
                Status = STATUS_ACCESS_DENIED;
                break;
            }

            InterlockedExchange(&Thread->DynamicCodeOptOut,
                                ((Value & PSP_DYNAMIC_CODE_OPT_OUT) != 0)
                                    ? 1 : 0);
            Status = STATUS_SUCCESS;
            break;

        case ThreadWorkOnBehalfTicket:
            /* The ticket alone, or the ticket with the flags that follow it */
            RtlZeroMemory(&Ticket, sizeof(Ticket));
            Status = PspCaptureThreadSetInformation(ThreadInformation,
                                                    ThreadInformationLength,
                                                    PreviousMode,
                                                    &Ticket,
                                                    ThreadInformationLength);
            if (!NT_SUCCESS(Status)) break;

            /* Only the thread itself may say whose work it is doing */
            if (Thread != PsGetCurrentThread())
            {
                Status = STATUS_ACCESS_DENIED;
                break;
            }

            Status = PspSetWorkOnBehalfTicket(Thread, &Ticket);
            break;

        case ThreadPowerThrottlingState:
            Status = PspCaptureThreadSetInformation(ThreadInformation,
                                                    ThreadInformationLength,
                                                    PreviousMode,
                                                    &Throttling,
                                                    sizeof(Throttling));
            if (!NT_SUCCESS(Status)) break;

            if (Throttling.Version != 1 ||
                (Throttling.ControlMask &
                 ~(ULONG)THREAD_POWER_THROTTLING_VALID_FLAGS) != 0 ||
                (Throttling.StateMask & ~Throttling.ControlMask) != 0)
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }

            Thread->PowerThrottlingControlMask = Throttling.ControlMask;
            Thread->PowerThrottlingStateMask = Throttling.StateMask;
            Status = STATUS_SUCCESS;
            break;

        case ThreadDbgkWerReportActive:
            Status = PspCaptureThreadSetInformation(ThreadInformation,
                                                    ThreadInformationLength,
                                                    PreviousMode,
                                                    &Value,
                                                    sizeof(Value));
            if (!NT_SUCCESS(Status)) break;

            if (Value != 0)
                PspSetCrossThreadFlag(Thread,
                                      CT_DBG_WER_USER_REPORT_ACTIVE_BIT);
            else
                PspClearCrossThreadFlag(Thread,
                                        CT_DBG_WER_USER_REPORT_ACTIVE_BIT);

            Status = STATUS_SUCCESS;
            break;

        case ThreadManageWritesToExecutableMemory:
        {
            MANAGE_WRITES_TO_EXECUTABLE_MEMORY Writes;

            Status = PspCaptureThreadSetInformation(ThreadInformation,
                                                    ThreadInformationLength,
                                                    PreviousMode,
                                                    &Writes,
                                                    sizeof(Writes));
            if (!NT_SUCCESS(Status)) break;

            if (Writes.Version != 1)
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }

            /*
             * Only a thread of a process that asked to be told about writes
             * to its own code may let those writes through.
             */
            if (!THREAD_TO_PROCESS(Thread)->ManageExecutableMemoryWrites)
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }

            InterlockedExchange(&Thread->ExecutableWriteAllowed,
                                Writes.ThreadAllowWrites ? 1 : 0);
            Status = STATUS_SUCCESS;
            break;
        }

        case ThreadWow64Context:
            if (THREAD_TO_PROCESS(Thread)->WoW64Process == NULL)
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }

            Status = PspCaptureThreadSetInformation(ThreadInformation,
                                                    ThreadInformationLength,
                                                    PreviousMode,
                                                    &Wow64,
                                                    sizeof(Wow64));
            if (!NT_SUCCESS(Status)) break;

            Status = PspArchCopyLiveWow64Context(Thread, &Wow64, TRUE);
            if (Status == STATUS_NOT_FOUND)
            {
                Status = PspCopyWow64CpuAreaContext(Thread, &Wow64, TRUE);
                if (NT_SUCCESS(Status))
                    Status = PspArchCopyWow64DebugRegisters(Thread, &Wow64, TRUE);
            }
            break;

        case ThreadCpuAccountingInformation:
            Status = PspCaptureThreadSetInformation(ThreadInformation,
                                                    ThreadInformationLength,
                                                    PreviousMode,
                                                    &Handle,
                                                    sizeof(Handle));
            if (!NT_SUCCESS(Status)) break;

            /* TODO: charge the thread's time to the named session */
            Status = STATUS_NOT_SUPPORTED;
            break;

        case ThreadWorkloadClass:
            Status = PspCaptureThreadSetInformation(ThreadInformation,
                                                    ThreadInformationLength,
                                                    PreviousMode,
                                                    &Value,
                                                    sizeof(Value));
            if (!NT_SUCCESS(Status)) break;

            /* TODO: tell the scheduler what kind of work this thread does */
            Status = STATUS_NOT_SUPPORTED;
            break;

        case ThreadStrongerBadHandleChecks:
            if (ThreadInformationLength != 0)
            {
                Status = STATUS_INFO_LENGTH_MISMATCH;
                break;
            }

            /* TODO: raise an exception on a bad handle for this thread */
            Status = STATUS_NOT_SUPPORTED;
            break;

        case ThreadAttachContainer:
            Status = PspCaptureThreadSetInformation(ThreadInformation,
                                                    ThreadInformationLength,
                                                    PreviousMode,
                                                    &Handle,
                                                    sizeof(Handle));
            if (!NT_SUCCESS(Status)) break;

            /*
             * Only the host container exists here, and a thread is already
             * in it, so there is nothing to attach it to.
             */
            Status = (Handle == NULL) ? STATUS_SUCCESS
                                      : STATUS_INVALID_PARAMETER;
            break;

        case ThreadCounterProfiling:
            /* FIXME: counters are not kept per thread; every request is refused by length, as Windows 11 refuses the shapes tested */
            Status = STATUS_INFO_LENGTH_MISMATCH;
            break;

        case ThreadHeterogeneousCpuPolicy:
            /* TODO: choose the kind of core a thread runs on where the machine has more than one */
            Status = STATUS_NOT_SUPPORTED;
            break;

        /*
         * The classes below are read and never written.
         */
        case ThreadBasicInformation:
        case ThreadTimes:
        case ThreadPerformanceCount:
        case ThreadAmILastThread:
        case ThreadIsIoPending:
        case ThreadIsTerminated:
        case ThreadLastSystemCall:
        case ThreadCycleTime:
        case ThreadActualBasePriority:
        case ThreadTebInformation:
        case ThreadSuspendCount:
        case ThreadContainerId:
        case ThreadSystemThreadInformation:
        case ThreadActualGroupAffinity:
        case ThreadSubsystemInformation:
        case ThreadEffectiveIoPriority:
        case ThreadEffectivePagePriority:
            Status = STATUS_INVALID_INFO_CLASS;
            break;

        default:
            Status = STATUS_INVALID_INFO_CLASS;
            break;
    }

    ObDereferenceObject(Thread);
    return Status;
}
