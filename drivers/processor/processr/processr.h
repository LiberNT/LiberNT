/*
 * PROJECT:     LiberNT Processor Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     ACPI processor idle and performance states for the kernel processor power manager
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <ntddk.h>
#include <ntpoapi.h>
#include <poclass.h>
#include <wdmguid.h>
#include <acpiioct.h>
#include <pepfx.h>
#include <drivers/ppmproc.h>

NTHALAPI VOID NTAPI HalProcessorIdle(VOID);

#define PROC_TAG                    'corP'
#define PROC_MAX_ACPI_OUTPUT        (64 * 1024)
#define PROC_MAX_IDLE_STATES        8
#define PROC_MAX_PERF_STATES        64

#define PROC_SPACE_SYSTEM_MEMORY    0
#define PROC_SPACE_SYSTEM_IO        1
#define PROC_SPACE_PCC              0x0A
#define PROC_SPACE_FIXED_HARDWARE   0x7F

#define PROC_NOTIFY_PERFORMANCE     0x80
#define PROC_NOTIFY_IDLE            0x81
#define PROC_NOTIFY_HIGHEST         0x85

#define PROC_METHOD(a, b, c, d) \
    ((ULONG)(UCHAR)(a) | ((ULONG)(UCHAR)(b) << 8) | ((ULONG)(UCHAR)(c) << 16) | ((ULONG)(UCHAR)(d) << 24))

typedef struct _PROC_REGISTER
{
    BOOLEAN Valid;
    UCHAR SpaceId;
    UCHAR BitWidth;
    UCHAR BitOffset;
    UCHAR AccessSize;
    ULONGLONG Address;
    PVOID MappingBase;
    PVOID Mapped;
    SIZE_T MappingLength;
} PROC_REGISTER, *PPROC_REGISTER;

typedef enum _PROC_IDLE_METHOD
{
    ProcIdleHalt,
    ProcIdleFfh,
    ProcIdleIo,
    ProcIdleMemory
} PROC_IDLE_METHOD, *PPROC_IDLE_METHOD;

typedef struct _PROC_IDLE_STATE
{
    PROC_IDLE_METHOD Method;
    BOOLEAN FlushCaches;
    PROC_REGISTER Register;
    ULONG Hint;
    ULONG Type;
    ULONG Latency;
    ULONG Power;
} PROC_IDLE_STATE, *PPROC_IDLE_STATE;

typedef enum _PROC_PERF_METHOD
{
    ProcPerfNone,
    ProcPerfPss,
    ProcPerfCppc,
    ProcPerfHwp
} PROC_PERF_METHOD;

typedef struct _PROC_PSS_STATE
{
    ULONG Frequency;
    ULONG Power;
    ULONG Latency;
    ULONG BusMasterLatency;
    ULONG Control;
    ULONG Status;
} PROC_PSS_STATE, *PPROC_PSS_STATE;

typedef struct _PROC_CPPC
{
    PROC_REGISTER Desired;
    PROC_REGISTER Minimum;
    PROC_REGISTER Maximum;
    PROC_REGISTER Enable;
    PROC_REGISTER Autonomous;
    PROC_REGISTER EnergyPreference;
    BOOLEAN AutonomousFixed;
} PROC_CPPC, *PPROC_CPPC;

typedef struct _PROC_FEATURES
{
    BOOLEAN Intel;
    BOOLEAN Amd;
    BOOLEAN Mwait;
    BOOLEAN MwaitBreakOnInterrupt;
    ULONG MwaitSubstates;
    BOOLEAN Est;
    BOOLEAN Hwp;
    BOOLEAN HwpEpp;
    BOOLEAN Feedback;
} PROC_FEATURES, *PPROC_FEATURES;

typedef struct _PROC_HWP
{
    UCHAR Highest;
    UCHAR Guaranteed;
    UCHAR Efficient;
    UCHAR Lowest;
    BOOLEAN Saved;
    ULONGLONG SavedRequest;
} PROC_HWP, *PPROC_HWP;

typedef struct _PROC_DEVICE
{
    PDEVICE_OBJECT Self;
    PDEVICE_OBJECT Pdo;
    PDEVICE_OBJECT Lower;
    IO_REMOVE_LOCK RemoveLock;
    KMUTEX Lock;
    PIO_WORKITEM WorkItem;
    volatile LONG WorkQueued;
    volatile LONG PendingNotify;

    ACPI_INTERFACE_STANDARD AcpiInterface;
    BOOLEAN AcpiInterfaceHeld;
    BOOLEAN NotificationsRegistered;
    BOOLEAN Started;
    BOOLEAN Registered;

    ULONG Uid;
    ULONGLONG HardwareId;
    BOOLEAN HardwareIdValid;
    BOOLEAN NumberValid;
    ULONG Index;
    PROCESSOR_NUMBER Number;
    PROC_FEATURES Features;
    ULONG Capabilities;

    ULONG IdleCount;
    PROC_IDLE_STATE Idle[PROC_MAX_IDLE_STATES];
    volatile LONG Monitor;

    PROC_PERF_METHOD PerfMethod;
    ULONG Highest;
    ULONG Nominal;
    ULONG LowestNonlinear;
    ULONG Lowest;
    ULONG Guaranteed;
    ULONG TransitionLatency;
    ULONG PssCount;
    PROC_PSS_STATE Pss[PROC_MAX_PERF_STATES];
    PROC_REGISTER PerfControl;
    PROC_REGISTER PerfStatus;
    ULONG Ppc;
    PROC_CPPC Cppc;
    PROC_HWP Hwp;
    BOOLEAN EppSupported;
    BOOLEAN AutonomousSupported;
    BOOLEAN StatusMismatchLogged;

    ULONG DomainId;
    ULONG DomainMembers;
    UCHAR Coordination;

    volatile LONG ThermalLimit;
} PROC_DEVICE, *PPROC_DEVICE;

/* acpi.c */

NTSTATUS
ProcEvaluate(
    _In_ PPROC_DEVICE Device,
    _In_ ULONG Method,
    _Outptr_ PACPI_EVAL_OUTPUT_BUFFER *Output);

NTSTATUS
ProcEvaluateComplex(
    _In_ PPROC_DEVICE Device,
    _In_ PACPI_EVAL_INPUT_BUFFER_COMPLEX Input,
    _In_ ULONG InputLength,
    _Outptr_ PACPI_EVAL_OUTPUT_BUFFER *Output);

PACPI_METHOD_ARGUMENT
ProcArgument(
    _In_ PACPI_EVAL_OUTPUT_BUFFER Output,
    _In_ ULONG Index);

PACPI_METHOD_ARGUMENT
ProcPackageElement(
    _In_opt_ PACPI_METHOD_ARGUMENT Package,
    _In_ ULONG Index);

ULONG
ProcPackageCount(
    _In_opt_ PACPI_METHOD_ARGUMENT Package);

BOOLEAN
ProcInteger(
    _In_opt_ PACPI_METHOD_ARGUMENT Argument,
    _Out_ PULONG Value);

BOOLEAN
ProcQueryInteger(
    _In_ PPROC_DEVICE Device,
    _In_ ULONG Method,
    _Out_ PULONG Value);

BOOLEAN
ProcParseRegister(
    _In_opt_ PACPI_METHOD_ARGUMENT Argument,
    _Out_ PPROC_REGISTER Register);

BOOLEAN
ProcPrepareRegister(
    _Inout_ PPROC_REGISTER Register);

VOID
ProcReleaseRegister(
    _Inout_ PPROC_REGISTER Register);

BOOLEAN
ProcReadRegister(
    _In_ PPROC_REGISTER Register,
    _Out_ PULONGLONG Value);

BOOLEAN
ProcWriteRegister(
    _In_ PPROC_REGISTER Register,
    _In_ ULONGLONG Value);

VOID
ProcTouchRegister(
    _In_ PPROC_REGISTER Register);

/* pcc.c */

VOID
ProcPccInitialize(VOID);

BOOLEAN
ProcPccPrepare(
    _Inout_ PPROC_REGISTER Register);

VOID
ProcPccRelease(
    _Inout_ PPROC_REGISTER Register);

BOOLEAN
ProcPccRead(
    _In_ PPROC_REGISTER Register,
    _Out_ PULONGLONG Value);

BOOLEAN
ProcPccWrite(
    _In_ ULONG Count,
    _In_reads_(Count) PPROC_REGISTER *Registers,
    _In_reads_(Count) const ULONGLONG *Values);

/* tables.c */

VOID
ProcEnterProcessor(
    _In_ PPROC_DEVICE Device,
    _Out_ PGROUP_AFFINITY Previous);

VOID
ProcLeaveProcessor(
    _In_ PGROUP_AFFINITY Previous);

VOID
ProcReadFixedTable(VOID);

VOID
ProcIdleExitWait(VOID);

VOID
ProcDeclareCapabilities(
    _Inout_ PPROC_DEVICE Device);

BOOLEAN
ProcIdentify(
    _Inout_ PPROC_DEVICE Device);

VOID
ProcParseIdleStates(
    _Inout_ PPROC_DEVICE Device);

VOID
ProcParsePerformance(
    _Inout_ PPROC_DEVICE Device);

BOOLEAN
ProcRefreshPpc(
    _Inout_ PPROC_DEVICE Device);

VOID
ProcReleaseTables(
    _Inout_ PPROC_DEVICE Device);

/* ppm.c */

PEPCALLBACKNOTIFYPPM ProcAcceptProcessorNotification;

VOID
ProcStartPerformance(
    _Inout_ PPROC_DEVICE Device);

VOID
ProcStopPerformance(
    _Inout_ PPROC_DEVICE Device);

/* Architecture layer */

VOID
ProcArchQueryFeatures(
    _Out_ PPROC_FEATURES Features);

ULONG
ProcArchCapabilities(
    _In_ PPROC_FEATURES Features);

BOOLEAN
ProcArchParseMat(
    _In_reads_bytes_(Length) const UCHAR *Entry,
    _In_ ULONG Length,
    _Out_ PULONGLONG HardwareId);

ULONGLONG
ProcArchCurrentHardwareId(VOID);

BOOLEAN
ProcArchIdleRegister(
    _In_ PPROC_FEATURES Features,
    _In_ PPROC_REGISTER Register,
    _Out_ PPROC_IDLE_METHOD Method,
    _Out_ PULONG Hint);

VOID
ProcArchIdleFfh(
    _In_ ULONG Hint,
    _In_ volatile LONG *Monitor);

BOOLEAN
ProcArchFlushCachesSupported(VOID);

VOID
ProcArchFlushCaches(VOID);

BOOLEAN
ProcArchReadFeedback(
    _Out_ PULONGLONG Nominal,
    _Out_ PULONGLONG Actual);

BOOLEAN
ProcArchPerfRegisters(
    _In_ PPROC_FEATURES Features,
    _Inout_ PPROC_REGISTER Control,
    _Inout_ PPROC_REGISTER Status);

BOOLEAN
ProcArchReadFfh(
    _In_ ULONGLONG Address,
    _Out_ PULONGLONG Value);

BOOLEAN
ProcArchWriteFfh(
    _In_ ULONGLONG Address,
    _In_ ULONGLONG Value);

BOOLEAN
ProcArchHwpEnable(
    _In_ PPROC_FEATURES Features,
    _Out_ PPROC_HWP Hwp);

VOID
ProcArchHwpRequest(
    _In_ PPROC_HWP Hwp,
    _In_ UCHAR Minimum,
    _In_ UCHAR Maximum,
    _In_ UCHAR Desired,
    _In_ UCHAR EnergyPreference,
    _In_ BOOLEAN SetEnergyPreference,
    _In_ ULONG ActivityWindow);

VOID
ProcArchHwpRestore(
    _Inout_ PPROC_HWP Hwp);
