/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test miniport that records what NdisMRegisterScatterGatherDma reports and the lists NDIS builds
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
static KEVENT NdisSgListEvent;
static PSCATTER_GATHER_LIST NdisSgList;
static PUCHAR NdisSgListBuffer;
static ULONG NdisSgListLength;
static ULONG NdisSgListReachable;
static ULONG NdisSgListMatches;
static NDIS_HANDLE NdisSgHangWorkItem;
static LONG NdisSgHangRecorded;
static LONG NdisSgHangWritten;
static ULONG NdisSgHangIrql;

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
ULONG
NdisSgRead(
    _In_ PCWSTR Name)
{
    UCHAR Buffer[sizeof(KEY_VALUE_PARTIAL_INFORMATION) + sizeof(ULONG)];
    PKEY_VALUE_PARTIAL_INFORMATION Info = (PKEY_VALUE_PARTIAL_INFORMATION)Buffer;
    OBJECT_ATTRIBUTES Attributes;
    UNICODE_STRING ValueName;
    ULONG Length, Value = 0;
    HANDLE Key;

    InitializeObjectAttributes(&Attributes, &NdisSgParameters, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    if (!NT_SUCCESS(ZwOpenKey(&Key, KEY_QUERY_VALUE, &Attributes)))
        return 0;
    RtlInitUnicodeString(&ValueName, Name);
    if (NT_SUCCESS(ZwQueryValueKey(Key, &ValueName, KeyValuePartialInformation, Info, sizeof(Buffer), &Length)) &&
        Info->Type == REG_DWORD && Info->DataLength == sizeof(ULONG))
    {
        Value = *(PULONG)Info->Data;
    }
    ZwClose(Key);
    return Value;
}

static
VOID
NTAPI
NdisSgHangWrite(
    _In_opt_ PVOID WorkItemContext,
    _In_ NDIS_HANDLE NdisIoWorkItemHandle)
{
    UNREFERENCED_PARAMETER(WorkItemContext);
    UNREFERENCED_PARAMETER(NdisIoWorkItemHandle);
    NdisSgWrite(L"HangIrql", NdisSgHangIrql);
    InterlockedExchange(&NdisSgHangWritten, 1);
}

static
BOOLEAN
NTAPI
NdisSgCheckForHang(
    _In_ NDIS_HANDLE MiniportAdapterContext)
{
    UNREFERENCED_PARAMETER(MiniportAdapterContext);
    if (InterlockedExchange(&NdisSgHangRecorded, 1) == 0)
    {
        NdisSgHangIrql = KeGetCurrentIrql();
        NdisQueueIoWorkItem(NdisSgHangWorkItem, NdisSgHangWrite, NULL);
    }
    return FALSE;
}

static
NDIS_STATUS
NdisSgStartForHangCheck(
    _In_ NDIS_HANDLE MiniportAdapterHandle)
{
    static const UCHAR Address[6] = { 0x02, 0x00, 0x4E, 0x44, 0x49, 0x53 };
    NDIS_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES General;

    NdisSgHangRecorded = 0;
    NdisSgHangWritten = 0;
    NdisSgHangWorkItem = NdisAllocateIoWorkItem(MiniportAdapterHandle);
    if (!NdisSgHangWorkItem)
        return NDIS_STATUS_RESOURCES;

    RtlZeroMemory(&General, sizeof(General));
    General.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES;
    General.Header.Revision = NDIS_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES_REVISION_1;
    General.Header.Size = NDIS_SIZEOF_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES_REVISION_1;
    General.MediaType = NdisMedium802_3;
    General.PhysicalMediumType = NdisPhysicalMediumUnspecified;
    General.MtuSize = 1500;
    General.MaxXmitLinkSpeed = General.XmitLinkSpeed = 1000000000ULL;
    General.MaxRcvLinkSpeed = General.RcvLinkSpeed = 1000000000ULL;
    General.MediaConnectState = MediaConnectStateDisconnected;
    General.MediaDuplexState = MediaDuplexStateFull;
    General.LookaheadSize = 1500;
    General.SupportedPacketFilters = NDIS_PACKET_TYPE_DIRECTED;
    General.MaxMulticastListSize = 0;
    General.MacAddressLength = sizeof(Address);
    RtlCopyMemory(General.PermanentMacAddress, Address, sizeof(Address));
    RtlCopyMemory(General.CurrentMacAddress, Address, sizeof(Address));
    General.AccessType = NET_IF_ACCESS_BROADCAST;
    General.DirectionType = NET_IF_DIRECTION_SENDRECEIVE;
    General.ConnectionType = NET_IF_CONNECTION_DEDICATED;
    General.IfType = IF_TYPE_ETHERNET_CSMACD;
    General.IfConnectorPresent = TRUE;
    return NdisMSetMiniportAttributes(MiniportAdapterHandle, (PNDIS_MINIPORT_ADAPTER_ATTRIBUTES)&General);
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
    ULONG Offset = 0;
    PUCHAR Bytes;
    ULONG i;

    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(Reserved);
    UNREFERENCED_PARAMETER(Context);

    if (List != NULL && NdisSgListBuffer != NULL)
    {
        NdisSgListReachable = 1;
        NdisSgListMatches = 1;
        for (i = 0; i < List->NumberOfElements; ++i)
        {
            if ((ULONG64)List->Elements[i].Address.QuadPart + List->Elements[i].Length > 0x100000000ULL)
                NdisSgListReachable = 0;
            Bytes = MmGetVirtualForPhysical(List->Elements[i].Address);
            if (Bytes == NULL || Offset + List->Elements[i].Length > NDISSG_HIGH_LENGTH ||
                RtlCompareMemory(Bytes, NdisSgListBuffer + Offset, List->Elements[i].Length) != List->Elements[i].Length)
            {
                NdisSgListMatches = 0;
            }
            Offset += List->Elements[i].Length;
        }
        NdisSgListLength = Offset;
        NdisSgList = List;
    }
    KeSetEvent(&NdisSgListEvent, IO_NO_INCREMENT, FALSE);
}

static
VOID
NdisSgMapHighBuffer(
    _In_ NDIS_HANDLE MiniportAdapterHandle,
    _In_ ULONG Width)
{
    PHYSICAL_ADDRESS Lowest, Highest, Boundary;
    NET_BUFFER_LIST_POOL_PARAMETERS PoolParameters;
    NDIS_SG_DMA_DESCRIPTION Description;
    NDIS_PHYSICAL_ADDRESS SharedAddress;
    LARGE_INTEGER Timeout;
    NDIS_HANDLE DmaHandle = NULL;
    NDIS_HANDLE Pool = NULL;
    PNET_BUFFER_LIST List = NULL;
    PVOID Shared = NULL;
    PUCHAR High;
    PMDL Mdl = NULL;
    NDIS_STATUS Status;
    KIRQL OldIrql;
    ULONG i;

    RtlZeroMemory(&Description, sizeof(Description));
    Description.Header.Type = NDIS_OBJECT_TYPE_SG_DMA_DESCRIPTION;
    Description.Header.Revision = NDIS_SG_DMA_DESCRIPTION_REVISION_1;
    Description.Header.Size = NDIS_SIZEOF_SG_DMA_DESCRIPTION_REVISION_1;
    Description.Flags = NdisSgWidthFlags[Width];
    Description.MaximumPhysicalMapping = 0x10000;
    Description.ProcessSGListHandler = NdisSgProcessList;
    Status = NdisMRegisterScatterGatherDma(MiniportAdapterHandle, &Description, &DmaHandle);
    NdisSgWrite(NdisSgWidthNames[Width][0], Status);
    if (Status != NDIS_STATUS_SUCCESS)
        return;

    SharedAddress.QuadPart = 0;
    NdisMAllocateSharedMemory(MiniportAdapterHandle, PAGE_SIZE, FALSE, &Shared, &SharedAddress);
    NdisSgWrite(NdisSgWidthNames[Width][1], Shared ? (ULONG)((ULONG64)SharedAddress.QuadPart >> 32 != 0) : NDISSG_UNSET);
    if (Shared != NULL)
        NdisMFreeSharedMemory(MiniportAdapterHandle, PAGE_SIZE, FALSE, Shared, SharedAddress);

    Lowest.QuadPart = 0x100000000LL;
    Highest.QuadPart = -1;
    Boundary.QuadPart = 0;
    High = MmAllocateContiguousMemorySpecifyCache(PAGE_SIZE, Lowest, Highest, Boundary, MmCached);
    if (High == NULL)
    {
        NdisSgWrite(L"NoHighMemory", 1);
        NdisMDeregisterScatterGatherDma(DmaHandle);
        return;
    }
    for (i = 0; i < PAGE_SIZE; ++i)
        High[i] = (UCHAR)(i * 7 + 3);

    RtlZeroMemory(&PoolParameters, sizeof(PoolParameters));
    PoolParameters.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    PoolParameters.Header.Revision = NET_BUFFER_LIST_POOL_PARAMETERS_REVISION_1;
    PoolParameters.Header.Size = NDIS_SIZEOF_NET_BUFFER_LIST_POOL_PARAMETERS_REVISION_1;
    PoolParameters.ProtocolId = NDIS_PROTOCOL_ID_DEFAULT;
    PoolParameters.fAllocateNetBuffer = TRUE;
    PoolParameters.PoolTag = 'gSdN';
    Mdl = NdisAllocateMdl(MiniportAdapterHandle, High + NDISSG_HIGH_OFFSET, NDISSG_HIGH_LENGTH);
    Pool = NdisAllocateNetBufferListPool(MiniportAdapterHandle, &PoolParameters);
    if (Mdl != NULL && Pool != NULL)
        List = NdisAllocateNetBufferAndNetBufferList(Pool, 0, 0, Mdl, 0, NDISSG_HIGH_LENGTH);

    Status = NDIS_STATUS_RESOURCES;
    if (List != NULL)
    {
        KeClearEvent(&NdisSgListEvent);
        NdisSgList = NULL;
        NdisSgListLength = NdisSgListReachable = NdisSgListMatches = NDISSG_UNSET;
        NdisSgListBuffer = High + NDISSG_HIGH_OFFSET;
        KeRaiseIrql(DISPATCH_LEVEL, &OldIrql);
        Status = NdisMAllocateNetBufferSGList(DmaHandle, NET_BUFFER_LIST_FIRST_NB(List), NULL,
                                              NDIS_SG_LIST_WRITE_TO_DEVICE, NULL, 0);
        KeLowerIrql(OldIrql);
    }
    NdisSgWrite(NdisSgWidthNames[Width][2], Status);
    Timeout.QuadPart = -10LL * 1000 * 1000;
    if (Status == NDIS_STATUS_SUCCESS &&
        KeWaitForSingleObject(&NdisSgListEvent, Executive, KernelMode, FALSE, &Timeout) == STATUS_SUCCESS &&
        NdisSgList != NULL)
    {
        NdisSgWrite(NdisSgWidthNames[Width][3], NdisSgList->NumberOfElements);
        NdisSgWrite(NdisSgWidthNames[Width][4], NdisSgListLength);
        NdisSgWrite(NdisSgWidthNames[Width][5], NdisSgListReachable);
        NdisSgWrite(NdisSgWidthNames[Width][6], NdisSgListMatches);
        NdisSgWrite(NdisSgWidthNames[Width][7],
                    NdisSgList->NumberOfElements != 0 &&
                    NdisSgList->Elements[0].Address.QuadPart == MmGetPhysicalAddress(NdisSgListBuffer).QuadPart);
        KeRaiseIrql(DISPATCH_LEVEL, &OldIrql);
        NdisMFreeNetBufferSGList(DmaHandle, NdisSgList, NET_BUFFER_LIST_FIRST_NB(List));
        KeLowerIrql(OldIrql);
    }
    NdisSgListBuffer = NULL;

    if (List != NULL)
        NdisFreeNetBufferList(List);
    if (Pool != NULL)
        NdisFreeNetBufferListPool(Pool);
    if (Mdl != NULL)
        NdisFreeMdl(Mdl);
    MmFreeContiguousMemorySpecifyCache(High, PAGE_SIZE, MmCached);
    NdisMDeregisterScatterGatherDma(DmaHandle);
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
    BOOLEAN HangCheck;
    ULONG i;

    UNREFERENCED_PARAMETER(MiniportDriverContext);
    UNREFERENCED_PARAMETER(InitParameters);

    NdisSgAdapterHandle = MiniportAdapterHandle;
    HangCheck = NdisSgRead(L"HangCheck") != 0;
    RtlZeroMemory(&Registration, sizeof(Registration));
    Registration.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES;
    Registration.Header.Revision = NDIS_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES_REVISION_1;
    Registration.Header.Size = NDIS_SIZEOF_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES_REVISION_1;
    Registration.MiniportAdapterContext = &NdisSgAdapterContext;
    Registration.AttributeFlags = NDIS_MINIPORT_ATTRIBUTES_HARDWARE_DEVICE | NDIS_MINIPORT_ATTRIBUTES_BUS_MASTER;
    Registration.InterfaceType = NdisInterfacePci;
    Registration.CheckForHangTimeInSeconds = HangCheck ? 1 : 0;
    Status = NdisMSetMiniportAttributes(MiniportAdapterHandle, (PNDIS_MINIPORT_ADAPTER_ATTRIBUTES)&Registration);
    NdisSgWrite(L"Registration", Status);
    if (Status != NDIS_STATUS_SUCCESS)
        return Status;
    if (HangCheck)
        return NdisSgStartForHangCheck(MiniportAdapterHandle);

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
    for (i = 0; i < NDISSG_WIDTH_COUNT; ++i)
        NdisSgMapHighBuffer(MiniportAdapterHandle, i);
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
    if (NdisSgHangWorkItem && (!NdisSgHangRecorded || NdisSgHangWritten))
    {
        NdisFreeIoWorkItem(NdisSgHangWorkItem);
        NdisSgHangWorkItem = NULL;
    }
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

    KeInitializeEvent(&NdisSgListEvent, NotificationEvent, FALSE);
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
    Characteristics.CheckForHangHandlerEx = NdisSgCheckForHang;
    Status = NdisMRegisterMiniportDriver(DriverObject, RegistryPath, NULL, &Characteristics, &NdisSgDriverHandle);
    if (Status != NDIS_STATUS_SUCCESS)
        ExFreePoolWithTag(NdisSgParameters.Buffer, 'gSdN');
    return Status;
}
