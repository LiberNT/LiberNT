/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     What the WMI files say to one another
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <stdarg.h>
#include <wmistr.h>
#include <wmiguid.h>
#include <wmidata.h>
#include <ntwmi.h>
#include "wmip.h"

/* The two devices the user-mode half of WMI talks to */
extern PDEVICE_OBJECT WmipServiceDeviceObject;
extern PDEVICE_OBJECT WmipAdminDeviceObject;

extern FAST_IO_DISPATCH WmipFastIoDispatch;

/* Finishes off everything a caller left waiting on one identifier */
VOID
NTAPI
WmipClearIrpObjectList(
    _In_ PWMIP_GUID_OBJECT GuidObject);

/* What the object manager calls as a handle, and then an object, goes away */
_Function_class_(OB_CLOSE_METHOD)
VOID
NTAPI
WmipCloseMethod(
    _In_opt_ PEPROCESS Process,
    _In_ PVOID Object,
    _In_ ULONG_PTR ProcessHandleCount,
    _In_ ULONG_PTR SystemHandleCount);

_Function_class_(OB_DELETE_METHOD)
VOID
NTAPI
WmipDeleteMethod(
    _In_ PVOID Object);

_Function_class_(OB_SECURITY_METHOD)
NTSTATUS
NTAPI
WmipSecurityMethod(
    _In_ PVOID Object,
    _In_ SECURITY_OPERATION_CODE OperationType,
    _In_ PSECURITY_INFORMATION SecurityInformation,
    _In_ PSECURITY_DESCRIPTOR SecurityDescriptor,
    _Inout_ PULONG CapturedLength,
    _Inout_ PSECURITY_DESCRIPTOR *ObjectSecurityDescriptor,
    _In_ POOL_TYPE PoolType,
    _In_ PGENERIC_MAPPING GenericMapping,
    _In_ KPROCESSOR_MODE AccessMode);

_Function_class_(DRIVER_CANCEL)
VOID
NTAPI
WmipNotificationIrpCancel(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp);

_Function_class_(DRIVER_DISPATCH)
NTSTATUS
NTAPI
WmipOpenCloseCleanup(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp);

_Function_class_(DRIVER_DISPATCH)
NTSTATUS
NTAPI
WmipIoControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp);

_Function_class_(DRIVER_DISPATCH)
NTSTATUS
NTAPI
WmipSystemControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp);

_Function_class_(DRIVER_DISPATCH)
NTSTATUS
NTAPI
WmipShutdown(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp);

BOOLEAN
NTAPI
WmipFastIoDeviceControl(
    _In_ PFILE_OBJECT FileObject,
    _In_ BOOLEAN Wait,
    _In_opt_ PVOID InputBuffer,
    _In_ ULONG InputBufferLength,
    _Out_opt_ PVOID OutputBuffer,
    _In_ ULONG OutputBufferLength,
    _In_ ULONG IoControlCode,
    _Out_ PIO_STATUS_BLOCK IoStatus,
    _In_ PDEVICE_OBJECT DeviceObject);

VOID
NTAPI
WmipDockUndockEventCallback(
    _In_opt_ PVOID CallbackContext,
    _In_opt_ PVOID Argument1,
    _In_opt_ PVOID Argument2);

/* Hands a notification to everybody holding the identifier it names */
VOID
NTAPI
WmipDeliverNotification(
    _In_ PWNODE_HEADER Wnode);

/* The kernel's own data blocks, answered without going out to a provider */
NTSTATUS
NTAPI
WmipQueryKernelDataBlock(
    _In_ LPCGUID Guid,
    _Inout_ PULONG InOutBufferSize,
    _Out_writes_bytes_opt_(*InOutBufferSize) PVOID OutBuffer);

/* Whether the kernel answers for this identifier itself */
BOOLEAN
NTAPI
WmipIsKernelDataBlock(
    _In_ LPCGUID Guid);

/* The firmware structure table as the kernel kept it */
PUCHAR
NTAPI
WmipGetRawSMBiosTableData(
    _Out_ PULONG Length);

NTSTATUS
NTAPI
WmiStartTrace(
    _Inout_ PWMI_LOGGER_INFORMATION LoggerInformation);

NTSTATUS
NTAPI
WmiStopTrace(
    _Inout_ PWMI_LOGGER_INFORMATION LoggerInformation);

NTSTATUS
NTAPI
WmiQueryTrace(
    _Inout_ PWMI_LOGGER_INFORMATION LoggerInformation);

NTSTATUS
NTAPI
WmiUpdateTrace(
    _Inout_ PWMI_LOGGER_INFORMATION LoggerInformation);

NTSTATUS
NTAPI
WmiFlushTrace(
    _Inout_ PWMI_LOGGER_INFORMATION LoggerInformation);

LONGLONG
FASTCALL
WmiGetClock(
    _In_ ULONG ClockType,
    _In_opt_ PVOID Context);

NTSTATUS
FASTCALL
WmiTraceFastEvent(
    _In_ PWNODE_HEADER Wnode);

NTSTATUS
NTAPI
WmiTraceMessageVa(
    _In_ TRACEHANDLE LoggerHandle,
    _In_ ULONG MessageFlags,
    _In_ LPGUID MessageGuid,
    _In_ USHORT MessageNumber,
    _In_ va_list MessageArgList);

NTSTATUS
__cdecl
WmiTraceMessage(
    _In_ TRACEHANDLE LoggerHandle,
    _In_ ULONG MessageFlags,
    _In_ LPGUID MessageGuid,
    _In_ USHORT MessageNumber,
    ...);

ULONG
NTAPI
EtwpDisableStackWalkApc(VOID);

VOID
NTAPI
EtwpReenableStackWalkApc(
    _In_ ULONG PreviousState);

BOOLEAN
NTAPI
WmiInitialize(VOID);

/* The provider side of event tracing */

VOID
NTAPI
EtwpInitializeProviders(VOID);

VOID
NTAPI
EtwpEnableProvider(
    _In_ LPCGUID ProviderId,
    _In_opt_ LPCGUID SourceId,
    _In_ ULONG ControlCode,
    _In_ ULONG LoggerId,
    _In_ UCHAR Level,
    _In_ ULONGLONG MatchAnyKeyword,
    _In_ ULONGLONG MatchAllKeyword,
    _In_opt_ PEVENT_FILTER_DESCRIPTOR FilterData);

NTSTATUS
NTAPI
EtwRegisterClassicProvider(
    _In_ LPCGUID ProviderId,
    _In_ ULONG Type,
    _In_opt_ PETWENABLECALLBACK EnableCallback,
    _In_opt_ PVOID CallbackContext,
    _Out_ PREGHANDLE RegHandle);

NTSTATUS
NTAPI
EtwSetInformation(
    _In_ REGHANDLE RegHandle,
    _In_ EVENT_INFO_CLASS InformationClass,
    _In_opt_ PVOID EventInformation,
    _In_ ULONG InformationLength);

NTSTATUS
NTAPI
EtwTelemetryCoverageReport(
    _In_opt_ PVOID CoverageReport);

/* EOF */
