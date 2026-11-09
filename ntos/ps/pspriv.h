/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     What the files of the process manager share with each other
 *              and with nothing else
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

/*
 * What a thread's stack is reserved at when neither the caller nor the image
 * says. One megabyte is what the linker writes into an image that does not
 * ask for another size.
 */
#define PSP_DEFAULT_STACK_RESERVE 0x100000

/* How many attributes one creation may carry */
#define PSP_MAX_ATTRIBUTES 32

/*
 * An attribute's low half names which attribute it is; the high half carries
 * the flags that say whether it belongs to the thread, is read rather than
 * written, and may appear more than once.
 */
#define PSP_ATTRIBUTE_NUMBER_MASK 0x0000FFFF

/* How many jobs a creation may name */
#define PSP_MAX_CREATE_JOBS 16

/* What the attribute list told us, once it has been read and checked */
typedef struct _PSP_CREATE_ATTRIBUTES
{
    PUNICODE_STRING ImageName;
    HANDLE ParentProcess;
    HANDLE DebugPort;
    HANDLE Token;
    PCLIENT_ID ClientId;
    PVOID *TebAddress;
    PSECTION_IMAGE_INFORMATION ImageInfo;
    SIZE_T MemoryReserve;
    ULONG PriorityClass;
    ULONG ErrorMode;
    ULONG ComponentFilter;
    ULONG Win32kFilter;
    ULONG ChildProcessPolicy;
    ULONG AllApplicationPackagesPolicy;
    ULONG DesktopAppPolicy;
    ULONGLONG MitigationOptions;
    ULONGLONG MitigationAuditOptions;
    ULONGLONG OptionalXStateFeatures;
    USHORT MachineType;
    USHORT PreferredNode;
    PROCESSOR_NUMBER IdealProcessor;
    GROUP_AFFINITY GroupAffinity;
    PS_PROTECTION Protection;
    const HANDLE *HandleList;
    SIZE_T HandleCount;
    const HANDLE *JobList;
    ULONG JobCount;
    BOOLEAN HasProtection;
    BOOLEAN HasGroupAffinity;
    BOOLEAN HasIdealProcessor;
    BOOLEAN HasPreferredNode;
    BOOLEAN Chpe;
    UNICODE_STRING CapturedImageName;
} PSP_CREATE_ATTRIBUTES, *PPSP_CREATE_ATTRIBUTES;

NTSTATUS
NTAPI
PspCaptureAttributes(
    _In_opt_ PPS_ATTRIBUTE_LIST AttributeList,
    _In_ KPROCESSOR_MODE PreviousMode,
    _Out_ PPSP_CREATE_ATTRIBUTES Attributes);

NTSTATUS
NTAPI
PspApplyThreadAttributes(
    _In_ PETHREAD Thread,
    _In_ PPSP_CREATE_ATTRIBUTES Attributes);

/* The identifier table: one table, both kinds of object */

NTSTATUS
NTAPI
PspInsertCid(
    _In_ PVOID Object,
    _Out_ PHANDLE Cid);

VOID
NTAPI
PspRemoveCid(
    _In_ HANDLE Cid);

/* Object types */

extern GENERIC_MAPPING PspProcessMapping;
extern GENERIC_MAPPING PspThreadMapping;

NTSTATUS
NTAPI
PspProcessOpen(
    _In_ OB_OPEN_REASON Reason,
    _In_ KPROCESSOR_MODE AccessMode,
    _In_opt_ PEPROCESS Process,
    _In_ PVOID ObjectBody,
    _Inout_ PACCESS_MASK GrantedAccess,
    _In_ ULONG HandleCount);

VOID
NTAPI
PspProcessClose(
    _In_opt_ PEPROCESS Process,
    _In_ PVOID ObjectBody,
    _In_ ULONG_PTR ProcessHandleCount,
    _In_ ULONG_PTR SystemHandleCount);

NTSTATUS
NTAPI
PspThreadOpen(
    _In_ OB_OPEN_REASON Reason,
    _In_ KPROCESSOR_MODE AccessMode,
    _In_opt_ PEPROCESS Process,
    _In_ PVOID ObjectBody,
    _Inout_ PACCESS_MASK GrantedAccess,
    _In_ ULONG HandleCount);

/* Initialization */

CODE_SEG("INIT")
BOOLEAN
NTAPI
PspInitPhase0(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock);

CODE_SEG("INIT")
BOOLEAN
NTAPI
PspInitPhase1(VOID);

CODE_SEG("INIT")
NTSTATUS
NTAPI
PspInitializeSystemDll(VOID);

CODE_SEG("INIT")
VOID
NTAPI
PsInitializeQuotaSystem(VOID);

extern HANDLE PspInitialSystemProcessHandle;

VOID
NTAPI
PsSetProcessFaultInformation(
    _Inout_ PEPROCESS Process,
    _In_ PVOID FaultInformation);

/* Scheduling parameters a process is given at creation */

extern UCHAR PspForegroundQuantum[3];
extern const UCHAR PspFixedQuantums[6];
extern const UCHAR PspVariableQuantums[6];
extern const KPRIORITY PspPriorityTable[PROCESS_PRIORITY_CLASS_ABOVE_NORMAL + 1];

NTSTATUS
NTAPI
PspSetProcessPriorityClass(
    _In_ PEPROCESS Process,
    _In_ UCHAR PriorityClass);

/* Creation */

NTSTATUS
NTAPI
PspAllocateProcess(
    _In_opt_ PEPROCESS Parent,
    _In_opt_ PVOID Section,
    _In_opt_ PACCESS_TOKEN Token,
    _In_ ULONG Flags,
    _Outptr_ PEPROCESS *NewProcess);

NTSTATUS
NTAPI
PspInsertProcess(
    _In_ PEPROCESS Process);

/* Termination */

NTSTATUS
NTAPI
PspTerminateProcess(
    _In_ PEPROCESS Process,
    _In_ NTSTATUS ExitStatus);

VOID
NTAPI
PspExitNormalApc(
    _In_ PVOID NormalContext,
    _In_ PVOID SystemArgument1,
    _In_ PVOID SystemArgument2);

VOID
NTAPI
PspExitApcRundown(
    _In_ PKAPC Apc);

VOID
NTAPI
PspCatchCriticalBreak(
    _In_ PCSTR Message,
    _In_ PVOID Object,
    _In_ PCSTR ImageName);

VOID
NTAPI
PspUserThreadStartup(
    _In_ PKSTART_ROUTINE StartRoutine,
    _In_ PVOID StartContext);

LONG
NTAPI
PspUnhandledExceptionInSystemThread(
    _In_ PEXCEPTION_POINTERS Pointers);

/* State control */

VOID
NTAPI
PspQueueApcSpecialApc(
    _In_ PKAPC Apc,
    _Inout_ PKNORMAL_ROUTINE *NormalRoutine,
    _Inout_ PVOID *NormalContext,
    _Inout_ PVOID *SystemArgument1,
    _Inout_ PVOID *SystemArgument2);

/* Security */

NTSTATUS
NTAPI
PspAssignPrimaryToken(
    _In_ PEPROCESS Process,
    _In_opt_ HANDLE TokenHandle,
    _In_opt_ PACCESS_TOKEN Token);

VOID
NTAPI
PspWriteTebImpersonationInfo(
    _In_ PETHREAD Thread,
    _In_ PEPROCESS Process);

/* Quotas */

NTSTATUS
NTAPI
PspChargeProcessQuotaSpecifiedPool(
    _In_ PEPROCESS Process,
    _In_ ULONG PoolIndex,
    _In_ SIZE_T Amount);

VOID
NTAPI
PspReturnProcessQuotaSpecifiedPool(
    _In_ PEPROCESS Process,
    _In_ ULONG PoolIndex,
    _In_ SIZE_T Amount);

VOID
NTAPI
PspInsertQuotaBlock(
    _In_ PEPROCESS_QUOTA_BLOCK QuotaBlock);

VOID
NTAPI
PspReturnExcessQuotas(
    _In_ PEPROCESS Process);

VOID
NTAPI
PspReturnQuotasOnDestroy(
    _In_ PEPROCESS Process);

NTSTATUS
NTAPI
PspQueryQuotaLimits(
    _In_ PEPROCESS Process,
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length,
    _Out_ PULONG ReturnLength);

/* Information classes, which the query and the set side both describe */

NTSTATUS
NTAPI
PspQueryWorkingSetWatch(
    _In_ PEPROCESS Process,
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length,
    _Out_ PULONG ReturnLength,
    _In_ BOOLEAN Extended);

NTSTATUS
NTAPI
PspSetWorkingSetWatch(
    _In_ PEPROCESS Process,
    _In_ BOOLEAN Enable);

NTSTATUS
NTAPI
PspQueryHandleTracing(
    _In_ PEPROCESS Process,
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length,
    _Out_ PULONG ReturnLength);

NTSTATUS
NTAPI
PspSetHandleTracing(
    _In_ PEPROCESS Process,
    _In_reads_bytes_(Length) PVOID Information,
    _In_ ULONG Length,
    _In_ KPROCESSOR_MODE PreviousMode);

NTSTATUS
NTAPI
PspQueryProcessCpuSets(
    _In_ PEPROCESS Process,
    _In_ BOOLEAN Allowed,
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length,
    _Out_ PULONG ReturnLength);

NTSTATUS
NTAPI
PspSetProcessCpuSets(
    _In_ PEPROCESS Process,
    _In_ BOOLEAN Allowed,
    _In_reads_bytes_(Length) PVOID Information,
    _In_ ULONG Length,
    _In_ KPROCESSOR_MODE PreviousMode);

NTSTATUS
NTAPI
PspQueryThreadCpuSets(
    _In_ PETHREAD Thread,
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length,
    _Out_ PULONG ReturnLength);

NTSTATUS
NTAPI
PspSetThreadCpuSets(
    _In_ PETHREAD Thread,
    _In_reads_bytes_(Length) PVOID Information,
    _In_ ULONG Length,
    _In_ KPROCESSOR_MODE PreviousMode);

NTSTATUS
NTAPI
PspSetTokenVirtualization(
    _In_ PEPROCESS Process,
    _In_ BOOLEAN Enable);

NTSTATUS
NTAPI
PspSetWorkOnBehalfTicket(
    _In_ PETHREAD Thread,
    _In_ PRTL_WORK_ON_BEHALF_TICKET_EX Ticket);

/* Disk accounting */

extern BOOLEAN PspDiskCountersEnabled;

/* Jobs */

NTSTATUS
NTAPI
PspAddProcessToJobLimits(
    _In_ PEPROCESS Process,
    _In_ PEJOB Job);

NTSTATUS
NTAPI
PspTerminateJobObject(
    _In_ PEJOB Job,
    _In_ KPROCESSOR_MODE AccessMode,
    _In_ NTSTATUS ExitStatus);
