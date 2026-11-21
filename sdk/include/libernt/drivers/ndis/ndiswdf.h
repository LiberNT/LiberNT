/*
 * PROJECT:     LiberNT NDIS library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Private interface between NDIS and the NetAdapterCx class extension
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193.001@gmail.com>
 */

#pragma once

#include <ndis.h>

#ifdef __cplusplus
extern "C" {
#endif

DECLARE_HANDLE(NDIS_WDF_CX_DRIVER);
DECLARE_HANDLE(NDIS_WDF_CX_DRIVER_CONTEXT);

typedef NDIS_WDF_CX_DRIVER NDIS_WDF_CX_HANDLE;

typedef enum _NET_DEVICE_RESET_TYPE
{
    FunctionLevelReset = 0,
    PlatformLevelReset = 1,
} NET_DEVICE_RESET_TYPE;

typedef enum _NDIS_IDLE_CONDITION
{
    NdisIdleConditionAnyLowLatency = 0,
    NdisIdleConditionAny = 1,
    NdisIdleConditionUnicastOnly = 2,
    NdisIdleConditionL2ConnectedOnly = 3,
} NDIS_IDLE_CONDITION;

typedef enum NdisWdfPnpPowerAction
{
    NdisWdfActionPnpStart = 0,
    NdisWdfActionPnpQueryStop = 1,
    NdisWdfActionPnpCancelStop = 2,
    NdisWdfActionPnpStop = 3,
    NdisWdfActionPnpQueryRemove = 4,
    NdisWdfActionPnpCancelRemove = 5,
    NdisWdfActionPnpSurpriseRemove = 6,
    NdisWdfActionPnpRemove = 7,
    NdisWdfActionPowerD0Implicit = 8,
    NdisWdfActionPowerD0 = 9,
    NdisWdfActionPowerDx = 10,
    NdisWdfActionStartPowerManagement = 11,
    NdisWdfActionStopPowerManagement = 12,
    NdisWdfActionPowerDxFinal = 13,
    NdisWdfActionPowerDxOnSystemSx = 14,
    NdisWdfActionPowerDxOnSystemShutdown = 15,
    NdisWdfActionPowerNone = 16,
    NdisWdfActionPreReleaseHardware = 17,
    NdisWdfActionPostReleaseHardware = 18,
    NdisWdfActionPnpRebalance = 19,
    NdisWdfActionDeviceObjectCleanup = 20,
} NdisWdfPnpPowerAction;

typedef struct _EXECUTION_CONTEXT_RUNTIME_KNOBS EXECUTION_CONTEXT_RUNTIME_KNOBS;

typedef struct _NDIS_WDF_COMPLETE_ADD_PARAMS
{
    GUID InterfaceGuid;
    NET_LUID NetLuid;
    NDIS_MEDIUM MediaType;
    UNICODE_STRING BaseName;
    UNICODE_STRING AdapterInstanceName;
    UNICODE_STRING DriverImageName;
    EXECUTION_CONTEXT_RUNTIME_KNOBS * ExecutionContextKnobs;
} NDIS_WDF_COMPLETE_ADD_PARAMS;

typedef struct _NDIS_WDF_ADD_DEVICE_INFO
{
    PDRIVER_OBJECT DriverObject;
    PDEVICE_OBJECT PhysicalDeviceObject;
    NDIS_HANDLE MiniportAdapterContext;
} NDIS_WDF_ADD_DEVICE_INFO;

typedef NTSTATUS EVT_NDIS_WDF_MINIPORT_POWER_REFERENCE(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ BOOLEAN WaitForD0,
    _In_ BOOLEAN InvokeCompletionCallback);
typedef void EVT_NDIS_WDF_MINIPORT_POWER_DEREFERENCE(
    _In_ NDIS_HANDLE MiniportAdapterContext);
typedef void EVT_NDIS_WDF_MINIPORT_UPDATE_IDLE_CONDITION(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ NDIS_IDLE_CONDITION IdleCondition);
typedef PDEVICE_OBJECT EVT_NDIS_WDF_MINIPORT_GET_DEVICE_OBJECT(
    _In_ NDIS_HANDLE MiniportAdapterContext);
typedef PDEVICE_OBJECT EVT_NDIS_WDF_MINIPORT_GET_NEXT_DEVICE_OBJECT(
    _In_ NDIS_HANDLE MiniportAdapterContext);
typedef NTSTATUS EVT_NDIS_WDF_MINIPORT_GET_ASSIGNED_FDO_NAME(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _Inout_ PUNICODE_STRING FdoName);
typedef NDIS_HANDLE EVT_NDIS_WDF_MINIPORT_GET_NDIS_HANDLE_FROM_DEVICE_OBJECT(
    _In_ PDEVICE_OBJECT DeviceObject);
typedef void EVT_NDIS_WDF_MINIPORT_UPDATE_PM_PARAMETERS(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNDIS_PM_PARAMETERS PmParameters);
typedef NTSTATUS EVT_NDIS_WDF_MINIPORT_ALLOCATE_BLOCK(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ ULONG Size,
    _Out_ PVOID * MiniportBlock);
typedef void EVT_NDIS_WDF_MINIPORT_COMPLETE_ADD(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ NDIS_WDF_COMPLETE_ADD_PARAMS * Parameters);
typedef void EVT_NDIS_WDF_DEVICE_START_COMPLETE(
    _In_ NDIS_HANDLE MiniportAdapterContext);
typedef NTSTATUS EVT_NDIS_WDF_MINIPORT_DEVICE_RESET(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ NET_DEVICE_RESET_TYPE ResetType);
typedef NTSTATUS EVT_NDIS_WDF_MINIPORT_QUERY_DEVICE_RESET_SUPPORT(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _Out_ PULONG SupportedResetTypes);
typedef NTSTATUS EVT_NDIS_WDF_GET_WMI_EVENT_GUID(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ NTSTATUS GuidStatus,
    _Out_ PNDIS_GUID * Guid);

typedef void EVT_NDIS_WDF_MINIPORT_AOAC_ENGAGE(
    _In_ NDIS_HANDLE MiniportAdapterContext);
typedef void EVT_NDIS_WDF_MINIPORT_AOAC_DISENGAGE(
    _In_ NDIS_HANDLE MiniportAdapterContext);

typedef struct _NDIS_WDF_CX_CHARACTERISTICS
{
    NDIS_OBJECT_HEADER Header;
    EVT_NDIS_WDF_MINIPORT_POWER_REFERENCE * EvtCxPowerReference;
    EVT_NDIS_WDF_MINIPORT_POWER_DEREFERENCE * EvtCxPowerDereference;
    EVT_NDIS_WDF_MINIPORT_UPDATE_IDLE_CONDITION * EvtCxUpdateIdleCondition;
    EVT_NDIS_WDF_MINIPORT_GET_DEVICE_OBJECT * EvtCxGetDeviceObject;
    EVT_NDIS_WDF_MINIPORT_GET_NEXT_DEVICE_OBJECT * EvtCxGetNextDeviceObject;
    EVT_NDIS_WDF_MINIPORT_GET_ASSIGNED_FDO_NAME * EvtCxGetAssignedFdoName;
    EVT_NDIS_WDF_MINIPORT_GET_NDIS_HANDLE_FROM_DEVICE_OBJECT * EvtCxGetNdisHandleFromDeviceObject;
    EVT_NDIS_WDF_MINIPORT_UPDATE_PM_PARAMETERS * EvtCxUpdatePMParameters;
    EVT_NDIS_WDF_MINIPORT_ALLOCATE_BLOCK * EvtCxAllocateMiniportBlock;
    EVT_NDIS_WDF_MINIPORT_COMPLETE_ADD * EvtCxMiniportCompleteAdd;
    EVT_NDIS_WDF_DEVICE_START_COMPLETE * EvtCxDeviceStartComplete;
    EVT_NDIS_WDF_MINIPORT_DEVICE_RESET * EvtCxMiniportDeviceReset;
    EVT_NDIS_WDF_MINIPORT_QUERY_DEVICE_RESET_SUPPORT * EvtCxMiniportQueryDeviceResetSupport;
    EVT_NDIS_WDF_GET_WMI_EVENT_GUID * EvtCxGetWmiEventGuid;
} NDIS_WDF_CX_CHARACTERISTICS;

FORCEINLINE
VOID
NDIS_WDF_CX_CHARACTERISTICS_INIT(
    _Out_ NDIS_WDF_CX_CHARACTERISTICS * Characteristics)
{
    RtlZeroMemory(Characteristics, sizeof(*Characteristics));
    Characteristics->Header.Size = sizeof(*Characteristics);
}

#define NDIS_OBJECT_TYPE_LWM_DRIVER_CHARACTERISTICS 0xC7
#define NDIS_LWM_DRIVER_CHARACTERISTICS_REVISION_1 1
#define NDIS_SIZEOF_LWM_DRIVER_CHARACTERISTICS_REVISION_1 \
    RTL_SIZEOF_THROUGH_FIELD(NDIS_LWM_DRIVER_CHARACTERISTICS, SynchronousOidRequestHandler)

#define NDIS_DRIVER_POWERMGMT_PROXY 0x00000008
#define NDIS_NETCX_DRIVER 0x00000010

typedef struct _NDIS_LWM_DRIVER_CHARACTERISTICS
{
    NDIS_OBJECT_HEADER Header;
    UCHAR MajorNdisVersion;
    UCHAR MinorNdisVersion;
    UCHAR MajorDriverVersion;
    UCHAR MinorDriverVersion;
    ULONG Flags;
    UNICODE_STRING ServiceName;
    MINIPORT_SET_OPTIONS_HANDLER SetOptionsHandler;
    MINIPORT_PAUSE_HANDLER PauseHandler;
    MINIPORT_RESTART_HANDLER RestartHandler;
    MINIPORT_OID_REQUEST_HANDLER OidRequestHandler;
    MINIPORT_SEND_NET_BUFFER_LISTS_HANDLER SendNetBufferListsHandler;
    MINIPORT_RETURN_NET_BUFFER_LISTS_HANDLER ReturnNetBufferListsHandler;
    MINIPORT_CANCEL_SEND_HANDLER CancelSendHandler;
    MINIPORT_CANCEL_OID_REQUEST_HANDLER CancelOidRequestHandler;
    MINIPORT_DIRECT_OID_REQUEST_HANDLER DirectOidRequestHandler;
    MINIPORT_CANCEL_DIRECT_OID_REQUEST_HANDLER CancelDirectOidRequestHandler;
    MINIPORT_SYNCHRONOUS_OID_REQUEST_HANDLER SynchronousOidRequestHandler;
} NDIS_LWM_DRIVER_CHARACTERISTICS;

NDIS_STATUS
NTAPI
NdisConvertNtStatusToNdisStatus(
    _In_ NTSTATUS Status);

NTSTATUS
NTAPI
NdisConvertNdisStatusToNtStatus(
    _In_ NDIS_STATUS Status);

NTSTATUS
NTAPI
NdisWdfRegisterCx(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath,
    _In_ NDIS_WDF_CX_DRIVER_CONTEXT DriverContext,
    _In_ NDIS_WDF_CX_CHARACTERISTICS * Characteristics,
    _Out_ NDIS_WDF_CX_DRIVER * NdisWdfCxDriverHandle);

VOID
NTAPI
NdisWdfDeregisterCx(
    _In_ NDIS_WDF_CX_DRIVER NdisWdfCxDriverHandle);

NDIS_STATUS
NTAPI
NdisWdfRegisterMiniportDriver(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath,
    _In_ NDIS_WDF_CX_DRIVER NdisWdfCxDriverHandle,
    _In_opt_ NDIS_HANDLE MiniportDriverContext,
    _In_ PNDIS_MINIPORT_DRIVER_CHARACTERISTICS Characteristics,
    _Out_ PNDIS_HANDLE NdisMiniportDriverHandle);

NTSTATUS
NTAPI
NdisWdfPnPAddDevice(
    _In_ NDIS_WDF_ADD_DEVICE_INFO * AddDeviceInfo,
    _Out_ PNDIS_HANDLE NdisMiniportAdapterHandle);

NDIS_HANDLE
NTAPI
NdisWdfGetAdapterContextFromAdapterHandle(
    _In_ NDIS_HANDLE NdisMiniportAdapterHandle);

NTSTATUS
NTAPI
NdisWdfPnpPowerEventHandler(
    _In_ NDIS_HANDLE NdisMiniportAdapterHandle,
    _In_ NdisWdfPnpPowerAction PnpAction,
    _In_ NdisWdfPnpPowerAction PowerAction);

VOID
NTAPI
NdisWdfMiniportStarted(
    _In_ NDIS_HANDLE NdisMiniportAdapterHandle);

VOID
NTAPI
NdisWdfMiniportDataPathStart(
    _In_ NDIS_HANDLE NdisMiniportAdapterHandle);

VOID
NTAPI
NdisWdfMiniportDataPathPause(
    _In_ NDIS_HANDLE NdisMiniportAdapterHandle);

VOID
NTAPI
NdisWdfMiniportSetPower(
    _In_ NDIS_HANDLE NdisMiniportAdapterHandle,
    _In_ POWER_ACTION SystemPowerAction,
    _In_ DEVICE_POWER_STATE DevicePowerState);

BOOLEAN
NTAPI
NdisWdfMiniportTryReference(
    _In_ NDIS_HANDLE NdisMiniportAdapterHandle);

VOID
NTAPI
NdisWdfMiniportDereference(
    _In_ NDIS_HANDLE NdisMiniportAdapterHandle);

VOID
NTAPI
NdisWdfAsyncPowerReferenceCompleteNotification(
    _In_ NDIS_HANDLE NdisMiniportAdapterHandle,
    _In_ NTSTATUS Status);

VOID
NTAPI
NdisWdfNotifyWmiAdapterArrival(
    _In_ NDIS_HANDLE NdisMiniportAdapterHandle);

NTSTATUS
NTAPI
NdisWdfCreateIrpHandler(
    _In_ NDIS_HANDLE NdisMiniportAdapterHandle,
    _Inout_ PIRP Irp);

NTSTATUS
NTAPI
NdisWdfCloseIrpHandler(
    _Inout_ PIRP Irp);

NTSTATUS
NTAPI
NdisWdfDeviceControlIrpHandler(
    _Inout_ PIRP Irp);

NTSTATUS
NTAPI
NdisWdfDeviceInternalControlIrpHandler(
    _Inout_ PIRP Irp);

VOID
NTAPI
NdisWdfGetGuidToOidMap(
    _In_reads_(OidCount) PNDIS_OID OidList,
    _In_ USHORT OidCount,
    _Out_writes_opt_(*GuidCount) PNDIS_GUID GuidMap,
    _Inout_ PUSHORT GuidCount);

NTSTATUS
NTAPI
NdisWdfQueryAllData(
    _In_ NDIS_HANDLE NdisMiniportAdapterHandle,
    _In_ PNDIS_GUID NdisGuid,
    _In_ PGUID Guid,
    _Inout_updates_bytes_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG ReturnSize);

NTSTATUS
NTAPI
NdisWdfQuerySingleInstance(
    _In_ NDIS_HANDLE NdisMiniportAdapterHandle,
    _In_ PNDIS_GUID NdisGuid,
    _Inout_ PVOID Wnode,
    _In_ ULONG BufferSize,
    _Out_ PULONG ReturnSize);

NTSTATUS
NTAPI
NdisWdfChangeSingleInstance(
    _In_ NDIS_HANDLE NdisMiniportAdapterHandle,
    _In_ PNDIS_GUID NdisGuid,
    _Inout_ PVOID Wnode);

NTSTATUS
NTAPI
NdisWdfExecuteMethod(
    _In_ NDIS_HANDLE NdisMiniportAdapterHandle,
    _In_ PNDIS_GUID NdisGuid,
    _Inout_ PVOID Wnode,
    _In_ ULONG BufferSize,
    _Out_ PULONG ReturnSize);

VOID
NTAPI
NdisWdfReadConfiguration(
    _Out_ PNDIS_STATUS Status,
    _Out_ PNDIS_CONFIGURATION_PARAMETER * ParameterValue,
    _In_ NDIS_HANDLE ConfigurationHandle,
    _In_ PNDIS_STRING Keyword,
    _In_ NDIS_PARAMETER_TYPE ParameterType);

NDIS_STATUS
NTAPI
NdisLWMRegisterMiniportDriver(
    _In_ NDIS_HANDLE MiniportDriverContext,
    _In_ NDIS_LWM_DRIVER_CHARACTERISTICS * Characteristics,
    _Out_ PNDIS_HANDLE NdisLwmDriverHandle);

VOID
NTAPI
NdisLWMDeregisterMiniportDriver(
    _In_ NDIS_HANDLE NdisLwmDriverHandle);

NDIS_STATUS
NTAPI
NdisLWMInitializeNetworkInterface(
    _In_ NDIS_HANDLE NdisLwmDriverHandle,
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ GUID const * InterfaceGuid,
    _Out_ PNDIS_HANDLE NdisMiniportAdapterHandle);

NDIS_STATUS
NTAPI
NdisLWMStartNetworkInterface(
    _In_ NDIS_HANDLE NdisMiniportAdapterHandle);

VOID
NTAPI
NdisLWMUninitializeNetworkInterface(
    _In_ NDIS_HANDLE NdisMiniportAdapterHandle);

typedef struct _NDIS_WDI_ENGINE_TRANSPORT
{
    ULONG Size;
    ULONG WdiVersion;
    NDIS_HANDLE Context;
    MINIPORT_OID_REQUEST_HANDLER SendCommand;
} NDIS_WDI_ENGINE_TRANSPORT, *PNDIS_WDI_ENGINE_TRANSPORT;

NDIS_STATUS
NTAPI
NdisWdiEngineCreate(
    _In_ NDIS_HANDLE MiniportAdapterHandle,
    _In_ const NDIS_WDI_ENGINE_TRANSPORT * Transport,
    _In_ UINT16 PortId,
    _In_reads_bytes_(CapabilitiesLength) PUCHAR Capabilities,
    _In_ ULONG CapabilitiesLength,
    _Out_ PVOID * Engine);

VOID
NTAPI
NdisWdiEngineDestroy(
    _In_ PVOID Engine);

NDIS_STATUS
NTAPI
NdisWdiEngineOidRequest(
    _In_ PVOID Engine,
    _In_ PNDIS_OID_REQUEST OidRequest);

VOID
NTAPI
NdisWdiEngineCommandComplete(
    _In_ PVOID Engine,
    _In_ PNDIS_OID_REQUEST Command,
    _In_ NDIS_STATUS Status,
    _In_reads_bytes_opt_(ResponseLength) const VOID * Response,
    _In_ ULONG ResponseLength,
    _In_ ULONG BytesNeeded);

VOID
NTAPI
NdisWdiEngineIndicateStatus(
    _In_ PVOID Engine,
    _In_ PNDIS_STATUS_INDICATION StatusIndication);

#ifdef __cplusplus
}
#endif
