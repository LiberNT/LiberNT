/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Process and thread manager internals
 * COPYRIGHT:   Copyright Alex Ionescu <alex.ionescu@reactos.org>
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#define PSP_MAX_CREATE_THREAD_NOTIFY            8

#define PSP_MAX_LOAD_IMAGE_NOTIFY               8

#define PSP_MAX_CREATE_PROCESS_NOTIFY           8

#define PSP_JOB_SCHEDULING_CLASS_DEFAULT        5

#define PSP_JOB_SCHEDULING_CLASSES              10

#define PSP_JOB_BASIC_LIMIT_VALID_FLAGS     \
    (JOB_OBJECT_LIMIT_WORKINGSET |          \
     JOB_OBJECT_LIMIT_PROCESS_TIME |        \
     JOB_OBJECT_LIMIT_JOB_TIME |            \
     JOB_OBJECT_LIMIT_ACTIVE_PROCESS |      \
     JOB_OBJECT_LIMIT_AFFINITY |            \
     JOB_OBJECT_LIMIT_PRIORITY_CLASS |      \
     JOB_OBJECT_LIMIT_PRESERVE_JOB_TIME |   \
     JOB_OBJECT_LIMIT_SCHEDULING_CLASS)

#define PSP_JOB_EXTENDED_LIMIT_VALID_FLAGS          \
    (PSP_JOB_BASIC_LIMIT_VALID_FLAGS |              \
     JOB_OBJECT_LIMIT_PROCESS_MEMORY |              \
     JOB_OBJECT_LIMIT_JOB_MEMORY |                  \
     JOB_OBJECT_LIMIT_DIE_ON_UNHANDLED_EXCEPTION |  \
     JOB_OBJECT_LIMIT_BREAKAWAY_OK |                \
     JOB_OBJECT_LIMIT_SILENT_BREAKAWAY_OK |         \
     JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE |           \
     JOB_OBJECT_LIMIT_SUBSET_AFFINITY)

#define PSP_JOB_ACCOUNTING_FOLDED   0x00000002

#define PSP_COMPONENT_FILTER_KTM         0x00000001

#define PSP_COMPONENT_FILTER_VALID_FLAGS PSP_COMPONENT_FILTER_KTM

#if (NTDDI_VERSION < NTDDI_WIN10_RS2)
ULONGLONG
NTAPI
PsGetProcessStartKey(
    _In_ PEPROCESS Process);
#endif

typedef struct _GET_SET_CTX_CONTEXT
{
    KAPC Apc;
    KEVENT Event;
    KPROCESSOR_MODE Mode;
    NTSTATUS Status;
    PVOID XState;
    CONTEXT Context;
} GET_SET_CTX_CONTEXT, *PGET_SET_CTX_CONTEXT;

extern volatile LONG64 PspProcessSequenceNumber;

#if (NTDDI_VERSION >= NTDDI_LONGHORN)

#define THREAD_TO_PROCESS(Thread)   ((PEPROCESS)(Thread)->Tcb.Process)
#else
#define THREAD_TO_PROCESS(Thread)   ((Thread)->ThreadsProcess)
#endif

#define PSP_WS_WATCH_RECORDS 1024

#if defined(_M_ARM64)
NTSTATUS
NTAPI
PspMapChpeBridge(IN PEPROCESS Process);
#endif

#ifdef _WIN64
NTSTATUS
NTAPI
PspPrepareWow64Thread(
    _In_ HANDLE ProcessHandle,
    _In_ PWOW64_CONTEXT Wow64Context,
    _Out_ PCONTEXT ThreadContext,
    _In_ PINITIAL_TEB Wow64InitialTeb,
    _Out_ PINITIAL_TEB InitialTeb
);
#endif

#if defined(_X86_)

VOID
NTAPI
PspDeleteLdt(
    IN PEPROCESS Process
);

VOID
NTAPI
PspDeleteVdmObjects(
    IN PEPROCESS Process
);

NTSTATUS
NTAPI
PspQueryDescriptorThread(
    IN PETHREAD Thread,
    IN PVOID ThreadInformation,
    IN ULONG ThreadInformationLength,
    OUT PULONG ReturnLength OPTIONAL
);
#endif

typedef NTSTATUS
(NTAPI *PJOB_ENUMERATOR_CALLBACK)(
    _In_ PEPROCESS Process,
    _In_opt_ PVOID Context
);

#define PSP_PROCESS_CREATE_PICO       0x40000000

#define PSP_PROCESS_CREATE_PICO_CLONE 0x80000000

#define PSP_PROCESS_CREATE_PICO_MASK  (PSP_PROCESS_CREATE_PICO | PSP_PROCESS_CREATE_PICO_CLONE)

extern ULONG PspTraceLevel;

extern LCID PsDefaultThreadLocaleId;

extern LCID PsDefaultSystemLocaleId;

extern LIST_ENTRY PspReaperListHead;

extern WORK_QUEUE_ITEM PspReaperWorkItem;

extern BOOLEAN PspReaping;

extern PEPROCESS PsIdleProcess;

extern LIST_ENTRY PsActiveProcessHead;

extern EX_PUSH_LOCK PspActiveProcessLock;

extern LARGE_INTEGER ShortPsLockDelay;

extern EPROCESS_QUOTA_BLOCK PspDefaultQuotaBlock;

extern PHANDLE_TABLE PspCidTable;

extern EX_CALLBACK PspThreadNotifyRoutine[PSP_MAX_CREATE_THREAD_NOTIFY];

extern EX_CALLBACK PspProcessNotifyRoutine[PSP_MAX_CREATE_PROCESS_NOTIFY];

extern EX_CALLBACK PspLoadImageNotifyRoutine[PSP_MAX_LOAD_IMAGE_NOTIFY];

extern PLEGO_NOTIFY_ROUTINE PspLegoNotifyRoutine;

extern ULONG PspThreadNotifyRoutineCount, PspProcessNotifyRoutineCount;

extern BOOLEAN PsImageNotifyEnabled;

extern PKWIN32_PROCESS_CALLOUT PspW32ProcessCallout;

extern PKWIN32_THREAD_CALLOUT PspW32ThreadCallout;

extern PKWIN32_JOB_CALLOUT PspW32JobCallout;

#define CT_DISABLE_DYNAMIC_CODE_OPT_OUT_BIT 0x40000

#define PSP_DEP_POLICY 0

#define PSP_ASLR_POLICY 1

#define PSP_DYNAMIC_CODE_POLICY 2

#define PSP_STRICT_HANDLE_CHECK_POLICY 3

#define PSP_SYSTEM_CALL_DISABLE_POLICY 4

#define PSP_EXTENSION_POINT_DISABLE_POLICY 6

#define PSP_CONTROL_FLOW_GUARD_POLICY 7

#define PSP_SIGNATURE_POLICY 8

#define PSP_FONT_DISABLE_POLICY 9

#define PSP_IMAGE_LOAD_POLICY 10

#define PSP_SYSTEM_CALL_FILTER_POLICY 11

#define PSP_PAYLOAD_RESTRICTION_POLICY 12

#define PSP_CHILD_PROCESS_POLICY 13

#define PSP_SIDE_CHANNEL_ISOLATION_POLICY 14

#define PSP_EXTENDED_POLICY_COUNT 16

#define PSP_PROCESS_POWER_THROTTLING_VALID_FLAGS 0x7

typedef struct _PO_PROCESS_ENERGY_CONTEXT
{
    PROCESS_ENERGY_VALUES Values;
    volatile LONG PowerThrottlingControlMask;
    volatile LONG PowerThrottlingStateMask;
} PO_PROCESS_ENERGY_CONTEXT, *PPO_PROCESS_ENERGY_CONTEXT;

extern PVOID PspSystemDllEntryPoint;

extern PVOID PspRtlUserThreadStart;

extern PVOID PspSystemDllInitBlock;

extern PVOID PspSystemDllBase;

extern PVOID PspSystemDllSection;

extern PVOID PspWow64SystemDllSection;

extern BOOLEAN PspUseJobSchedulingClasses;

extern CHAR PspJobSchedulingClasses[PSP_JOB_SCHEDULING_CLASSES];

extern ULONG PsRawPrioritySeparation;

extern ULONG PsPrioritySeparation;

extern PTOKEN PspBootAccessToken;

extern GENERIC_MAPPING PspJobMapping;

extern POBJECT_TYPE PsJobType;

extern LARGE_INTEGER ShortPsLockDelay;

extern UNICODE_STRING PsNtDllPathName;

extern LIST_ENTRY PsLoadedModuleList;

extern KSPIN_LOCK PsLoadedModuleSpinLock;

extern ERESOURCE PsLoadedModuleResource;

extern ULONG_PTR PsNtosImageBase;

BOOLEAN
NTAPI
PsArchGetWow64StartContext(
    _In_ PCONTEXT Context,
    _Out_ PULONG Eip,
    _Out_ PULONG Eax,
    _Out_ PULONG Ebx,
    _Out_ PULONG Esp);

VOID
NTAPI
PsArchInitializeUserThreadContext(
    _Out_ PCONTEXT Context,
    _In_ PVOID ThreadStart,
    _In_ PVOID StartRoutine,
    _In_opt_ PVOID Argument,
    _In_ PVOID StackBase);

#ifndef PspArchCaptureXStateContext
#define PspArchCaptureXStateContext PspCaptureNoXStateContext
#define PspArchCompleteXStateContext PspCompleteNoXStateContext

FORCEINLINE
NTSTATUS
PspCaptureNoXStateContext(
    _In_ PCONTEXT Context,
    _In_ ULONG ContextFlags,
    _In_ KPROCESSOR_MODE PreviousMode,
    _In_ BOOLEAN SetContext,
    _Out_ PVOID *XState)
{
    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(ContextFlags);
    UNREFERENCED_PARAMETER(PreviousMode);
    UNREFERENCED_PARAMETER(SetContext);

    /* TODO: capture the extended processor state (CONTEXT_XSTATE) of the context */
    *XState = NULL;
    return STATUS_SUCCESS;
}

FORCEINLINE
NTSTATUS
PspCompleteNoXStateContext(
    _In_opt_ PVOID XState,
    _In_ BOOLEAN CopyOut)
{
    UNREFERENCED_PARAMETER(XState);
    UNREFERENCED_PARAMETER(CopyOut);
    return STATUS_SUCCESS;
}
#endif

NTSTATUS
NTAPI
PspArchCopyLiveWow64Context(
    _In_ PETHREAD Thread,
    _Inout_ PWOW64_CONTEXT Context,
    _In_ BOOLEAN SetContext);

#ifndef PspArchCopyWow64DebugRegisters
#define PspArchCopyWow64DebugRegisters PspCopyNoWow64DebugRegisters

FORCEINLINE
NTSTATUS
PspCopyNoWow64DebugRegisters(
    _In_ PETHREAD Thread,
    _Inout_ PWOW64_CONTEXT Context,
    _In_ BOOLEAN SetContext)
{
    UNREFERENCED_PARAMETER(Thread);
    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(SetContext);
    return STATUS_SUCCESS;
}
#endif

#ifndef PspArchInitializeUserThreadStartup
#define PspArchInitializeUserThreadStartup PspStartUserThreadAtSystemDll

FORCEINLINE
VOID
PspStartUserThreadAtSystemDll(
    _In_ PKEXCEPTION_FRAME ExceptionFrame,
    _Inout_ PKTRAP_FRAME TrapFrame)
{
    KiInitializeUserApc(ExceptionFrame,
                        TrapFrame,
                        PspSystemDllEntryPoint,
                        NULL,
                        PspSystemDllBase,
                        NULL);
}
#endif

BOOLEAN
NTAPI
PspArchManagesExecutableWrites(VOID);

VOID
NTAPI
PspGetOrSetContextKernelRoutine(
    _In_ PKAPC Apc,
    _Inout_ PKNORMAL_ROUTINE* NormalRoutine,
    _Inout_ PVOID* NormalContext,
    _Inout_ PVOID* SystemArgument1,
    _Inout_ PVOID* SystemArgument2);

BOOLEAN
NTAPI
PspInitializeApiSetSchema(VOID);

BOOLEAN
NTAPI
PspInitializeApiSetSection(VOID);

NTSTATUS
NTAPI
PspMapApiSetSchema(
    _In_ PEPROCESS Process);

PPO_PROCESS_ENERGY_CONTEXT
PspGetEnergyContext(
    _In_ PEPROCESS Process);

BOOLEAN
NTAPI
PspPicoDispatchException(
    _Inout_ PEXCEPTION_RECORD ExceptionRecord,
    _Inout_opt_ PKEXCEPTION_FRAME ExceptionFrame,
    _Inout_ PKTRAP_FRAME TrapFrame);

VOID
NTAPI
PspPicoProcessExit(
    _In_ PEPROCESS Process);

VOID
NTAPI
PspPicoSystemCallDispatch(
    _Inout_ PKTRAP_FRAME TrapFrame,
    _Inout_opt_ PKEXCEPTION_FRAME ExceptionFrame);

VOID
NTAPI
PspPicoThreadExit(
    _In_ PETHREAD Thread);

VOID
NTAPI
PspPicoThreadStartup(
    _Inout_ PKTRAP_FRAME TrapFrame,
    _Inout_opt_ PKEXCEPTION_FRAME ExceptionFrame);

VOID
NTAPI
PspRundownPropertySet(
    _Inout_ PPS_PROPERTY_SET PropertySet);

NTSTATUS
NTAPI
PsCheckImageLoadPolicy(
    _In_ PFILE_OBJECT FileObject);

NTSTATUS
NTAPI
PsReferenceProcessFilePointer(
    _In_ PEPROCESS Process,
    _Outptr_ PFILE_OBJECT *FileObject);

VOID
NTAPI
PsWatchWorkingSet(
    _In_ PEPROCESS Process,
    _In_ PVOID FaultingPc,
    _In_ PVOID FaultingVa);

VOID
NTAPI
PspLockMitigationPolicy(VOID);

NTSTATUS
NTAPI
PspSetExtendedMitigationPolicy(
    _In_ PEPROCESS Process,
    _In_ ULONG Policy,
    _In_ ULONG Flags);

VOID
NTAPI
PspStoreMitigationPolicy(
    _In_ PEPROCESS Process,
    _In_ ULONG Policy,
    _In_ ULONG Flags);

VOID
NTAPI
PspUnlockMitigationPolicy(VOID);

PEPROCESS
NTAPI
PsGetNextProcess(
    _In_opt_ PEPROCESS OldProcess);

PETHREAD
NTAPI
PsGetNextProcessThread(
    _In_ PEPROCESS Process,
    _In_opt_ PETHREAD Thread);

VOID
NTAPI
PsExitSpecialApc(
    PKAPC Apc,
    PKNORMAL_ROUTINE *NormalRoutine,
    PVOID *NormalContext,
    PVOID *SystemArgument1,
    PVOID *SystemArgument2);

NTSTATUS
NTAPI
PsTerminateProcess(
    _In_ PEPROCESS Process,
    _In_ NTSTATUS ExitStatus);

VOID
NTAPI
PspDeleteProcess(
    _In_ PVOID ObjectBody);

VOID
NTAPI
PspDeleteThread(
    _In_ PVOID ObjectBody);

VOID
NTAPI
PspExitProcess(
    _In_ BOOLEAN LastThread,
    _In_ PEPROCESS Process);

VOID
NTAPI
PspExitThread(
    _In_ NTSTATUS ExitStatus);

BOOLEAN
NTAPI
PspIsProcessExiting(
    _In_ PEPROCESS Process);

VOID
NTAPI
PspReapRoutine(
    _In_ PVOID Context);

VOID
NTAPI
PspShutdownProcessManager(VOID);

NTSTATUS
NTAPI
PspTerminateThreadByPointer(
    _In_ PETHREAD Thread,
    _In_ NTSTATUS ExitStatus,
    _In_ BOOLEAN bSelf);

NTSTATUS
NTAPI
PsChargeJobCommitment(
    _In_ PEPROCESS Process,
    _In_ SIZE_T PageCount);

VOID
NTAPI
PsReturnJobCommitment(
    _In_ PEPROCESS Process,
    _In_ SIZE_T PageCount);

NTSTATUS
NTAPI
PspAssignProcessToJob(
    _In_ PEPROCESS Process,
    _In_ PEJOB Job);

VOID
NTAPI
PspCloseJob(
    _In_ PEPROCESS Process,
    _In_ PVOID ObjectBody,
    _In_ ULONG_PTR ProcessHandleCount,
    _In_ ULONG_PTR SystemHandleCount);

VOID
NTAPI
PspDeleteJob(
    _In_ PVOID ObjectBody);

NTSTATUS
NTAPI
PspEnumerateProcessesInJob(
    _In_ PEJOB Job,
    _In_ PJOB_ENUMERATOR_CALLBACK Callback,
    _In_opt_ PVOID Context);

VOID
NTAPI
PspExitProcessFromJob(
    _In_ PEPROCESS Process);

NTSTATUS
NTAPI
PspInheritParentJob(
    _In_ PEPROCESS Process,
    _In_opt_ PEPROCESS Parent,
    _In_ ULONG Flags);

CODE_SEG("INIT")
VOID
NTAPI
PspInitializeJobStructures(VOID);

NTSTATUS
NTAPI
PspInvokeW32JobCallout(
    _In_ PEJOB Job,
    _In_ PSW32JOBCALLOUTTYPE CalloutType,
    _In_opt_ PVOID Data);

VOID
NTAPI
PspRemoveProcessFromJob(
    _In_ PEPROCESS Process);

NTSTATUS
NTAPI
PspSendJobMessageLocked(
    _In_ PEJOB Job,
    _In_ ULONG Message,
    _In_opt_ PVOID CompletionValue,
    _In_ BOOLEAN Quota);

VOID
NTAPI
PsChangeQuantumTable(
    _In_ BOOLEAN Immediate,
    _In_ ULONG PrioritySeparation);

NTSTATUS
PspAllocateUserStack(
    _In_ HANDLE ProcessHandle,
    _In_ ULONG_PTR ZeroBits,
    _In_ SIZE_T StackReserve,
    _In_ SIZE_T StackCommit,
    _In_ SIZE_T GuardSize,
    _Out_ PINITIAL_TEB InitialTeb);

KPRIORITY
NTAPI
PspComputeQuantumAndPriority(
    _In_ PEPROCESS Process,
    _In_ PSPROCESSPRIORITYMODE Mode,
    _Out_ PUCHAR Quantum);

NTSTATUS
NTAPI
PspCreateProcess(
    _Out_ PHANDLE ProcessHandle,
    _In_ ACCESS_MASK DesiredAccess,
    _In_opt_ POBJECT_ATTRIBUTES ObjectAttributes,
    _In_opt_ HANDLE ParentProcess,
    _In_ ULONG Flags,
    _In_opt_ HANDLE SectionHandle,
    _In_opt_ HANDLE DebugPort,
    _In_opt_ HANDLE ExceptionPort,
    _In_ BOOLEAN InJob,
    _In_opt_ const HANDLE *HandleList,
    _In_ SIZE_T HandleCount);

NTSTATUS
NTAPI
PsChargeProcessPageFileQuota(
    _In_ PEPROCESS Process,
    _In_ SIZE_T Amount);

PEPROCESS_QUOTA_BLOCK
NTAPI
PsChargeSharedPoolQuota(
    _In_ PEPROCESS Process,
    _In_ SIZE_T AmountToChargePaged,
    _In_ SIZE_T AmountToChargeNonPaged);

NTSTATUS
NTAPI
PsReturnProcessPageFileQuota(
    _In_ PEPROCESS Process,
    _In_ SIZE_T Amount);

VOID
NTAPI
PsReturnSharedPoolQuota(
    _In_ PEPROCESS_QUOTA_BLOCK QuotaBlock,
    _In_ SIZE_T AmountToReturnPaged,
    _In_ SIZE_T AmountToReturnNonPaged);

VOID
NTAPI
PspDereferenceQuotaBlock(
    _In_opt_ PEPROCESS Process,
    _In_ PEPROCESS_QUOTA_BLOCK QuotaBlock);

VOID
NTAPI
PspInheritQuota(
    _In_ PEPROCESS Process,
    _In_ PEPROCESS ParentProcess);

NTSTATUS
NTAPI
PspSetQuotaLimits(
    _In_ PEPROCESS Process,
    _In_ ULONG Unused,
    _In_ PVOID QuotaLimits,
    _In_ ULONG QuotaLimitsLength,
    _In_ KPROCESSOR_MODE PreviousMode);

NTSTATUS
NTAPI
PsOpenTokenOfProcess(
    _In_ HANDLE ProcessHandle,
    _Out_ PACCESS_TOKEN* Token);

PACCESS_TOKEN
NTAPI
PsReferenceEffectiveToken(
    _In_ PETHREAD Thread,
    OUT _In_ PTOKEN_TYPE TokenType,
    _Out_ PBOOLEAN EffectiveOnly,
    _Out_ PSECURITY_IMPERSONATION_LEVEL ImpersonationLevel);

VOID
NTAPI
PspDeleteProcessSecurity(
    _In_ PEPROCESS Process);

VOID
NTAPI
PspDeleteThreadSecurity(
    _In_ PETHREAD Thread);

NTSTATUS
NTAPI
PspInitializeProcessSecurity(
    _In_ PEPROCESS Process,
    _In_opt_ PEPROCESS Parent);

VOID
NTAPI
PspLabelWithPrimaryToken(
    _In_ PVOID Object,
    _In_ PEPROCESS Process);

NTSTATUS
NTAPI
PspSetPrimaryToken(
    _In_ PEPROCESS Process,
    _In_opt_ HANDLE TokenHandle,
    _In_opt_ PACCESS_TOKEN Token,
    _In_ BOOLEAN Duplicate);

CODE_SEG("INIT")
NTSTATUS
NTAPI
ApphelpCacheInitialize(VOID);

VOID
NTAPI
ApphelpCacheShutdown(VOID);

NTSTATUS
NTAPI
PsGetWow64SystemDll(
    _In_ PEPROCESS Process,
    _Out_opt_ PVOID *DllBase,
    _Out_opt_ PULONG RtlUserThreadStart);

CODE_SEG("INIT")
BOOLEAN
NTAPI
PsInitSystem(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock);

CODE_SEG("INIT")
NTSTATUS
NTAPI
PsLocateSystemDll(VOID);

BOOLEAN
NTAPI
PspGetLegacyXpdmVersion(
    _In_ PVOID CallerAddress,
    _Out_ PULONG MajorVersion,
    _Out_ PULONG MinorVersion,
    _Out_ PULONG BuildNumber);

NTSTATUS
NTAPI
PspMapSystemDll(
    _In_ PEPROCESS Process,
    _Out_ PVOID *DllBase,
    _In_ BOOLEAN UseLargePages);

NTSTATUS
NTAPI
PsResumeProcess(
    _In_ PEPROCESS Process);

NTSTATUS
NTAPI
PsResumeThread(
    _In_ PETHREAD Thread,
    _Out_opt_ PULONG PreviousCount);

NTSTATUS
NTAPI
PsSuspendProcess(
    _In_ PEPROCESS Process);

NTSTATUS
NTAPI
PsSuspendThread(
    _In_ PETHREAD Thread,
    _Out_opt_ PULONG PreviousCount);

NTSTATUS
NTAPI
PspCreateThread(
    _Out_ PHANDLE ThreadHandle,
    _In_ ACCESS_MASK DesiredAccess,
    _In_opt_ POBJECT_ATTRIBUTES ObjectAttributes,
    _In_ HANDLE ProcessHandle,
    _In_ PEPROCESS TargetProcess,
    _Out_ PCLIENT_ID ClientId,
    _In_ PCONTEXT ThreadContext,
    _In_ PINITIAL_TEB InitialTeb,
    _In_opt_ PINITIAL_TEB Wow64InitialTeb,
    _In_ ULONG CreateFlags,
    _In_opt_ PKSTART_ROUTINE StartRoutine,
    _In_opt_ PVOID StartContext,
    _In_opt_ PVOID Win32StartAddress);

VOID
NTAPI
PspSystemThreadStartup(
    PKSTART_ROUTINE StartRoutine,
    PVOID StartContext);

NTSTATUS
NTAPI
PsGetContextThread(
    _In_ PETHREAD Thread,
    _Inout_ PCONTEXT ThreadContext,
    _In_ KPROCESSOR_MODE PreviousMode);

NTSTATUS
NTAPI
PsSetContextThread(
    _In_ PETHREAD Thread,
    _In_ PCONTEXT ThreadContext,
    _In_ KPROCESSOR_MODE PreviousMode);

NTSTATUS
NTAPI
PspCopyWow64CpuAreaContext(
    _In_ PETHREAD Thread,
    _Inout_ PWOW64_CONTEXT Context,
    _In_ BOOLEAN SetContext);

NTSTATUS
NTAPI
PspGetOrSetUserContext(
    _In_ PETHREAD Thread,
    _Inout_ PCONTEXT Context,
    _In_ BOOLEAN SetContext);

VOID
NTAPI
PspInitializeTls(VOID);

VOID
NTAPI
PspTlsThreadCleanup(
    _In_ PETHREAD Thread);

VOID
NTAPI
PspApplyCreationMitigations(
    _In_ PEPROCESS Process,
    _In_reads_(2) const ULONGLONG *Options);

NTSTATUS
NTAPI
PspArchSetInstrumentationCallback(
    _In_ PEPROCESS Process,
    _In_reads_bytes_(Length) PVOID Information,
    _In_ ULONG Length,
    _In_ KPROCESSOR_MODE PreviousMode);

#include "ps_x.h"
