/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Power manager internals
 * COPYRIGHT:   Copyright Alex Ionescu <alex.ionescu@reactos.org>
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <guiddef.h>

#include <poclass.h>

typedef struct _POP_POWER_ACTION
{
    POWER_ACTION Action;
    SYSTEM_POWER_STATE SystemState;
    UCHAR IrpMinor;
} POP_POWER_ACTION, *PPOP_POWER_ACTION;

typedef struct _DEVICE_OBJECT_POWER_EXTENSION
{
    PDEVICE_OBJECT DeviceObject;
    DEVICE_POWER_STATE State;
    ULONG IdleCount;
    ULONG ConservationIdleTime;
    ULONG PerformanceIdleTime;
    LIST_ENTRY Volume;
} DEVICE_OBJECT_POWER_EXTENSION, *PDEVICE_OBJECT_POWER_EXTENSION;

extern BOOLEAN PopAoAc;

extern volatile LONG64 PopLastSleepTime;

extern volatile LONG64 PopLastWakeTime;

#define POP_MAX_CACHED_POWER_SETTINGS 64

#define POP_MAX_POWER_SETTING_VALUE_LENGTH 1024

typedef struct _POP_TTM_PROVIDER
{
    NTSTATUS
    (NTAPI *NotifyDeviceArrival)(
        _In_ ULONG DeviceType,
        _In_opt_ PVOID DeviceId,
        _In_ PVOID DeviceInformation,
        _In_ ULONG Flags,
        _In_opt_ PVOID ArrivalInformation);

    VOID
    (NTAPI *NotifyDeviceDeparture)(
        _In_ ULONG DeviceType,
        _In_opt_ PVOID DeviceId);

    VOID
    (NTAPI *NotifyDeviceInput)(
        _In_ ULONG DeviceType,
        _In_opt_ PVOID DeviceId,
        _In_ ULONG Flags);
} POP_TTM_PROVIDER, *PPOP_TTM_PROVIDER;

extern PDEVICE_NODE PopSystemPowerDeviceNode;

extern KGUARDED_MUTEX PopVolumeLock;

extern LIST_ENTRY PopVolumeDevices;

extern KSPIN_LOCK PopDopeGlobalLock;

extern POP_POWER_ACTION PopAction;

extern SYSTEM_POWER_CAPABILITIES PopCapabilities;

VOID
NTAPI
PopHiberCaptureLoaderBlock(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock);

VOID
NTAPI
PopHiberInitialize(VOID);

VOID
NTAPI
PopHiberInvalidate(VOID);

BOOLEAN
NTAPI
PopHiberIsReady(VOID);

VOID
NTAPI
PopHiberPagingFileCreated(VOID);

VOID
NTAPI
PopHiberPlatformSupported(VOID);

NTSTATUS
NTAPI
PopHiberPrepare(
    _Out_ PVOID *SystemContext);

VOID
NTAPI
PopHiberRelease(VOID);

NTSTATUS
NTAPI
PopHiberReserve(
    _In_ BOOLEAN Enable);

NTSTATUS
NTAPI
PopHiberSystemHandler(
    _In_ PVOID SystemContext);

NTSTATUS
NTAPI
PopAddRemoveSysCapsCallback(
    _In_ PVOID NotificationStructure,
    _In_ PVOID Context);

VOID
NTAPI
PopInitializeButtons(VOID);

VOID
NTAPI
PoInitializeDeviceObject(
    _Inout_ PDEVOBJ_EXTENSION DeviceObjectExtension);

VOID
NTAPI
PoNotifySystemTimeSet(VOID);

PDEVICE_OBJECT_POWER_EXTENSION
NTAPI
PopGetDope(
    _In_ PDEVICE_OBJECT DeviceObject);

CODE_SEG("INIT")
VOID
NTAPI
PopInitializeDevicePower(VOID);

BOOLEAN
NTAPI
PopIsDisplayRequired(VOID);

EXECUTION_STATE
NTAPI
PopQueryKeptOnState(VOID);

EXECUTION_STATE
NTAPI
PopQuerySystemState(
    _In_ PVOID StateHandle);

VOID
NTAPI
PopReleaseThreadPowerObject(
    _In_ PVOID PowerObject);

VOID
NTAPI
PopFxNotifyDeviceStarted(
    _In_ PDEVICE_OBJECT Pdo);

BOOLEAN
NTAPI
PopInitializePowerRequests(VOID);

NTSTATUS
NTAPI
PoRequestShutdownWait(
    _In_ PETHREAD Thread);

VOID
NTAPI
PopGracefulShutdown(
    _In_ PVOID Context);

VOID
NTAPI
PopInitShutdownList(VOID);

VOID
NTAPI
PopReadShutdownPolicy(VOID);

NTSTATUS
NTAPI
PopShutdownSystem(
    _In_ POWER_ACTION SystemAction);

CODE_SEG("INIT")
BOOLEAN
NTAPI
PoInitSystem(
    _In_ ULONG BootPhase);

CODE_SEG("INIT")
VOID
NTAPI
PoInitializePrcb(
    _In_ PKPRCB Prcb);

VOID
FASTCALL
PopIdle0(
    _In_ PPROCESSOR_POWER_STATE PowerState);

VOID
FASTCALL
PopPpmIdle(
    _In_ PPROCESSOR_POWER_STATE PowerState);

CODE_SEG("INIT")
VOID
NTAPI
PopPpmInitialize(VOID);

NTSTATUS
NTAPI
PopPpmProcessorRequest(
    _In_reads_bytes_(InputBufferLength) PVOID InputBuffer,
    _In_ ULONG InputBufferLength);

VOID
NTAPI
PopPpmResumeFromSystemState(
    _In_ SYSTEM_POWER_STATE SystemState);

VOID
NTAPI
PopPpmQueryProcessorInformation(
    _In_ ULONG Index,
    _Out_ PULONG MaxIdleState,
    _Out_ PULONG CurrentIdleState,
    _Out_ PULONG LimitPercent);

ULONG
NTAPI
PopGetVideoTimeout(VOID);

VOID
NTAPI
PopInitializeEventSupport(VOID);

BOOLEAN
NTAPI
PopLockConsoleOnWake(VOID);

NTSTATUS
PopSendQuerySystemPowerState(
    PDEVICE_OBJECT DeviceObject,
    SYSTEM_POWER_STATE SystemState,
    POWER_ACTION PowerAction);

NTSTATUS
PopSendSetSystemPowerState(
    PDEVICE_OBJECT DeviceObject,
    SYSTEM_POWER_STATE SystemState,
    POWER_ACTION PowerAction);

NTSTATUS
NTAPI
PopSetSystemPowerState(
    SYSTEM_POWER_STATE PowerState,
    POWER_ACTION PowerAction);

VOID
NTAPI
PoRemoveVolumeDevice(
    _In_ PDEVICE_OBJECT DeviceObject);

VOID
NTAPI
PoVolumeDevice(
    _In_ PDEVICE_OBJECT DeviceObject);

VOID
NTAPI
PopFlushVolumes(
    _In_ BOOLEAN ShuttingDown);

CODE_SEG("INIT")
VOID
NTAPI
PopInitializeVolumes(VOID);

BOOLEAN
NTAPI
PopEnterConsoleSession(
    _Out_ PVOID *Session,
    _Out_ PKAPC_STATE ApcState);

VOID
NTAPI
PopInitializeSleepSupport(VOID);

VOID
NTAPI
PopLeaveConsoleSession(
    _In_opt_ PVOID Session,
    _In_ PKAPC_STATE ApcState);

NTSTATUS
NTAPI
PopRegisterPowerStateHandler(
    _In_ PPOWER_STATE_HANDLER PowerStateHandler);

NTSTATUS
NTAPI
PopShutdownSendDevices(
    _In_ POWER_ACTION SystemAction);

VOID
PopSleepCallWin32(
    _In_ POWERSTATETASK Task,
    _In_ POWER_ACTION SystemAction,
    _In_ SYSTEM_POWER_STATE SystemState,
    _In_ ULONG Flags);

NTSTATUS
NTAPI
PopSleepSystem(
    _In_ POWER_ACTION SystemAction,
    _In_ SYSTEM_POWER_STATE LightestSystemState,
    _In_ ULONG Flags);

BOOLEAN
NTAPI
PopDetectAoAc(VOID);

MONITOR_DISPLAY_STATE
NTAPI
PopGetDisplayState(VOID);

VOID
NTAPI
PopInitializeStandby(VOID);

VOID
NTAPI
PopNoteDisplayActivity(VOID);

VOID
NTAPI
PopRequestDisplayState(
    _In_ MONITOR_DISPLAY_STATE State);

NTSTATUS
NTAPI
PopRegisterTtmProvider(
    _In_ const POP_TTM_PROVIDER *Provider);

NTSTATUS
NTAPI
PopActOnPowerRequest(
    _In_reads_bytes_(InputBufferLength) PVOID InputBuffer,
    _In_ ULONG InputBufferLength);

NTSTATUS
NTAPI
PopCreatePowerRequest(
    _In_reads_bytes_(InputBufferLength) PVOID InputBuffer,
    _In_ ULONG InputBufferLength,
    _Out_writes_bytes_(OutputBufferLength) PVOID OutputBuffer,
    _In_ ULONG OutputBufferLength);

VOID
NTAPI
PopInitializeThermalRequests(VOID);

NTSTATUS
NTAPI
PopQueryPlatformInformation(
    _In_opt_ PVOID InputBuffer,
    _In_ ULONG InputBufferLength,
    _Out_writes_bytes_opt_(OutputBufferLength) PVOID OutputBuffer,
    _In_ ULONG OutputBufferLength);

NTSTATUS
NTAPI
PopQuerySleepDisableReasons(
    _Out_writes_bytes_(OutputBufferLength) PVOID OutputBuffer,
    _In_ ULONG OutputBufferLength);

NTSTATUS
NTAPI
PopQuerySleepLogging(
    _In_opt_ PVOID InputBuffer,
    _Out_writes_bytes_opt_(OutputBufferLength) PVOID OutputBuffer,
    _In_ ULONG OutputBufferLength);

NTSTATUS
NTAPI
PopQuerySleepTime(
    _In_ POWER_INFORMATION_LEVEL Level,
    _In_opt_ PVOID InputBuffer,
    _Out_writes_bytes_opt_(OutputBufferLength) PVOID OutputBuffer,
    _In_ ULONG OutputBufferLength);

UCHAR
NTAPI
PopQuerySystemCoolingMode(VOID);

BOOLEAN
NTAPI
PopReadPowerSetting(
    _In_ LPCGUID Setting,
    _Out_writes_bytes_(Length) PVOID Value,
    _In_ ULONG Length);

NTSTATUS
NTAPI
PopReserveHiberFileFromCaller(
    _In_ KPROCESSOR_MODE PreviousMode,
    _In_reads_bytes_opt_(InputBufferLength) PVOID InputBuffer,
    _In_ ULONG InputBufferLength,
    _In_opt_ PVOID OutputBuffer,
    _In_ ULONG OutputBufferLength);

NTSTATUS
NTAPI
PopSetPowerSettingValue(
    _In_ LPCGUID SettingGuid,
    _In_ SYSTEM_POWER_CONDITION PowerCondition,
    _In_reads_bytes_(ValueLength) PVOID Value,
    _In_ ULONG ValueLength);

NTSTATUS
NTAPI
PopSetPowerStateHandler(
    _In_ KPROCESSOR_MODE PreviousMode,
    _In_reads_bytes_opt_(InputBufferLength) PVOID InputBuffer,
    _In_ ULONG InputBufferLength,
    _In_opt_ PVOID OutputBuffer,
    _In_ ULONG OutputBufferLength);

NTSTATUS
NTAPI
PopSetSessionDisplayState(
    _In_ KPROCESSOR_MODE PreviousMode,
    _In_reads_bytes_opt_(InputBufferLength) PVOID InputBuffer,
    _In_ ULONG InputBufferLength);
