/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test miniport that records what NdisMRegisterScatterGatherDma reports
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arifing193@gmail.com>
 */

#define NDIS_MINIPORT_DRIVER 1
#define NDIS60_MINIPORT 1
#include <ntddk.h>
#include <ndis.h>
#include "NdisSgDma.h"

static NDIS_HANDLE NdisSgDriverHandle;
static NDIS_HANDLE NdisSgAdapterHandle;
static ULONG NdisSgAdapterContext;
static UNICODE_STRING NdisSgParameters;

static
VOID
NdisSgWrite(
    _In_ PCWSTR Name,
    _In_ ULONG Value)
{
    OBJECT_ATTRIBUTES Attributes;
    UNICODE_STRING ValueName;
    HANDLE Key;

    InitializeObjectAttributes(&Attributes, &NdisSgParameters, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    if (!NT_SUCCESS(ZwCreateKey(&Key, KEY_SET_VALUE, &Attributes, 0, NULL, REG_OPTION_NON_VOLATILE, NULL)))
        return;
    RtlInitUnicodeString(&ValueName, Name);
    ZwSetValueKey(Key, &ValueName, 0, REG_DWORD, &Value, sizeof(Value));
    ZwClose(Key);
}

static
VOID
NTAPI
NdisSgProcessList(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PVOID Reserved,
    _In_ PSCATTER_GATHER_LIST List,
    _In_ PVOID Context)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(Reserved);
    UNREFERENCED_PARAMETER(List);
    UNREFERENCED_PARAMETER(Context);
}

static
NDIS_STATUS
NTAPI
NdisSgInitialize(
    _In_ NDIS_HANDLE MiniportAdapterHandle,
    _In_ NDIS_HANDLE MiniportDriverContext,
    _In_ PNDIS_MINIPORT_INIT_PARAMETERS InitParameters)
{
    NDIS_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES Registration;
    NDIS_SG_DMA_DESCRIPTION Description;
    NDIS_HANDLE DmaHandle;
    NDIS_STATUS Status;
    ULONG i;

    UNREFERENCED_PARAMETER(MiniportDriverContext);
    UNREFERENCED_PARAMETER(InitParameters);

    NdisSgAdapterHandle = MiniportAdapterHandle;
    RtlZeroMemory(&Registration, sizeof(Registration));
    Registration.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES;
    Registration.Header.Revision = NDIS_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES_REVISION_1;
    Registration.Header.Size = NDIS_SIZEOF_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES_REVISION_1;
    Registration.MiniportAdapterContext = &NdisSgAdapterContext;
    Registration.AttributeFlags = NDIS_MINIPORT_ATTRIBUTES_HARDWARE_DEVICE | NDIS_MINIPORT_ATTRIBUTES_BUS_MASTER;
    Registration.InterfaceType = NdisInterfacePci;
    Status = NdisMSetMiniportAttributes(MiniportAdapterHandle, (PNDIS_MINIPORT_ADAPTER_ATTRIBUTES)&Registration);
    NdisSgWrite(L"Registration", Status);
    if (Status != NDIS_STATUS_SUCCESS)
        return Status;

    for (i = 0; i < NDISSG_MAPPING_COUNT; ++i)
    {
        RtlZeroMemory(&Description, sizeof(Description));
        Description.Header.Type = NDIS_OBJECT_TYPE_SG_DMA_DESCRIPTION;
        Description.Header.Revision = NDIS_SG_DMA_DESCRIPTION_REVISION_1;
        Description.Header.Size = NDIS_SIZEOF_SG_DMA_DESCRIPTION_REVISION_1;
        Description.Flags = NDIS_SG_DMA_64_BIT_ADDRESS;
        Description.MaximumPhysicalMapping = NdisSgMappings[i];
        Description.ProcessSGListHandler = NdisSgProcessList;
        Description.ScatterGatherListSize = NDISSG_UNSET;
        DmaHandle = NULL;
        Status = NdisMRegisterScatterGatherDma(MiniportAdapterHandle, &Description, &DmaHandle);
        NdisSgWrite(NdisSgStatusNames[i], Status);
        NdisSgWrite(NdisSgSizeNames[i], Description.ScatterGatherListSize);
        NdisSgWrite(NdisSgRequiredNames[i], FIELD_OFFSET(SCATTER_GATHER_LIST, Elements) +
                    (BYTES_TO_PAGES(NdisSgMappings[i]) + 1) * sizeof(SCATTER_GATHER_ELEMENT));
        if (Status == NDIS_STATUS_SUCCESS)
            NdisMDeregisterScatterGatherDma(DmaHandle);
    }
    NdisSgWrite(L"Done", 1);
    return NDIS_STATUS_FAILURE;
}

static
VOID
NTAPI
NdisSgHalt(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ NDIS_HALT_ACTION HaltAction)
{
    UNREFERENCED_PARAMETER(MiniportAdapterContext);
    UNREFERENCED_PARAMETER(HaltAction);
}

static
VOID
NTAPI
NdisSgUnload(
    _In_ PDRIVER_OBJECT DriverObject)
{
    UNREFERENCED_PARAMETER(DriverObject);
    NdisMDeregisterMiniportDriver(NdisSgDriverHandle);
    ExFreePoolWithTag(NdisSgParameters.Buffer, 'gSdN');
}

static
NDIS_STATUS
NTAPI
NdisSgPause(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNDIS_MINIPORT_PAUSE_PARAMETERS PauseParameters)
{
    UNREFERENCED_PARAMETER(MiniportAdapterContext);
    UNREFERENCED_PARAMETER(PauseParameters);
    return NDIS_STATUS_SUCCESS;
}

static
NDIS_STATUS
NTAPI
NdisSgRestart(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNDIS_MINIPORT_RESTART_PARAMETERS RestartParameters)
{
    UNREFERENCED_PARAMETER(MiniportAdapterContext);
    UNREFERENCED_PARAMETER(RestartParameters);
    return NDIS_STATUS_SUCCESS;
}

static
NDIS_STATUS
NTAPI
NdisSgOidRequest(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNDIS_OID_REQUEST OidRequest)
{
    UNREFERENCED_PARAMETER(MiniportAdapterContext);
    UNREFERENCED_PARAMETER(OidRequest);
    return NDIS_STATUS_NOT_SUPPORTED;
}

static
VOID
NTAPI
NdisSgCancelOidRequest(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PVOID RequestId)
{
    UNREFERENCED_PARAMETER(MiniportAdapterContext);
    UNREFERENCED_PARAMETER(RequestId);
}

static
VOID
NTAPI
NdisSgSend(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNET_BUFFER_LIST NetBufferLists,
    _In_ NDIS_PORT_NUMBER PortNumber,
    _In_ ULONG SendFlags)
{
    PNET_BUFFER_LIST List;

    UNREFERENCED_PARAMETER(MiniportAdapterContext);
    UNREFERENCED_PARAMETER(PortNumber);
    for (List = NetBufferLists; List; List = NET_BUFFER_LIST_NEXT_NBL(List))
        NET_BUFFER_LIST_STATUS(List) = NDIS_STATUS_FAILURE;
    NdisMSendNetBufferListsComplete(NdisSgAdapterHandle, NetBufferLists,
                                    (SendFlags & NDIS_SEND_FLAGS_DISPATCH_LEVEL) ? NDIS_SEND_COMPLETE_FLAGS_DISPATCH_LEVEL : 0);
}

static
VOID
NTAPI
NdisSgReturn(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNET_BUFFER_LIST NetBufferLists,
    _In_ ULONG ReturnFlags)
{
    UNREFERENCED_PARAMETER(MiniportAdapterContext);
    UNREFERENCED_PARAMETER(NetBufferLists);
    UNREFERENCED_PARAMETER(ReturnFlags);
}

static
VOID
NTAPI
NdisSgCancelSend(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PVOID CancelId)
{
    UNREFERENCED_PARAMETER(MiniportAdapterContext);
    UNREFERENCED_PARAMETER(CancelId);
}

static
VOID
NTAPI
NdisSgPnpEvent(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNET_DEVICE_PNP_EVENT NetDevicePnPEvent)
{
    UNREFERENCED_PARAMETER(MiniportAdapterContext);
    UNREFERENCED_PARAMETER(NetDevicePnPEvent);
}

static
VOID
NTAPI
NdisSgShutdown(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ NDIS_SHUTDOWN_ACTION ShutdownAction)
{
    UNREFERENCED_PARAMETER(MiniportAdapterContext);
    UNREFERENCED_PARAMETER(ShutdownAction);
}

NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    NDIS_MINIPORT_DRIVER_CHARACTERISTICS Characteristics;
    static const WCHAR Suffix[] = L"\\Parameters";
    NDIS_STATUS Status;

    NdisSgParameters.MaximumLength = RegistryPath->Length + sizeof(Suffix);
    NdisSgParameters.Buffer = ExAllocatePoolWithTag(NonPagedPool, NdisSgParameters.MaximumLength, 'gSdN');
    if (!NdisSgParameters.Buffer)
        return STATUS_INSUFFICIENT_RESOURCES;
    NdisSgParameters.Length = 0;
    RtlAppendUnicodeStringToString(&NdisSgParameters, RegistryPath);
    RtlAppendUnicodeToString(&NdisSgParameters, Suffix);

    RtlZeroMemory(&Characteristics, sizeof(Characteristics));
    Characteristics.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_DRIVER_CHARACTERISTICS;
    Characteristics.Header.Revision = NDIS_MINIPORT_DRIVER_CHARACTERISTICS_REVISION_1;
    Characteristics.Header.Size = NDIS_SIZEOF_MINIPORT_DRIVER_CHARACTERISTICS_REVISION_1;
    Characteristics.MajorNdisVersion = 6;
    Characteristics.MinorNdisVersion = 0;
    Characteristics.MajorDriverVersion = 1;
    Characteristics.InitializeHandlerEx = NdisSgInitialize;
    Characteristics.HaltHandlerEx = NdisSgHalt;
    Characteristics.UnloadHandler = NdisSgUnload;
    Characteristics.PauseHandler = NdisSgPause;
    Characteristics.RestartHandler = NdisSgRestart;
    Characteristics.OidRequestHandler = NdisSgOidRequest;
    Characteristics.CancelOidRequestHandler = NdisSgCancelOidRequest;
    Characteristics.SendNetBufferListsHandler = NdisSgSend;
    Characteristics.ReturnNetBufferListsHandler = NdisSgReturn;
    Characteristics.CancelSendHandler = NdisSgCancelSend;
    Characteristics.DevicePnPEventNotifyHandler = NdisSgPnpEvent;
    Characteristics.ShutdownHandlerEx = NdisSgShutdown;
    Status = NdisMRegisterMiniportDriver(DriverObject, RegistryPath, NULL, &Characteristics, &NdisSgDriverHandle);
    if (Status != NDIS_STATUS_SUCCESS)
        ExFreePoolWithTag(NdisSgParameters.Buffer, 'gSdN');
    return Status;
}
