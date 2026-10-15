/*
 * DESCRIPTION: Default TDI event handlers.
 */

#include "precomp.h"

#define NDEBUG
#include <debug.h>

/*
 * ClientEventChainedReceiveDatagram
 *
 * @unimplemented
 */
NTSTATUS
NTAPI
TdiDefaultChainedRcvDatagramHandler (
	IN	PVOID	TdiEventContext,
	IN	LONG	SourceAddressLength,
	IN	PVOID	SourceAddress,
	IN	LONG	OptionsLength,
	IN	PVOID	Options,
	IN	ULONG	ReceiveDatagramFlags,
	IN	ULONG	ReceiveDatagramLength,
	IN	ULONG	StartingOffset,
	IN	PMDL	Tsdu,
	IN	PVOID	TsduDescriptor
	)
{
    UNIMPLEMENTED;
	return STATUS_NOT_IMPLEMENTED;
}


/*
 * ClientEventChainedReceiveExpedited
 *
 * @unimplemented
 */
NTSTATUS
NTAPI
TdiDefaultChainedRcvExpeditedHandler (
	IN	PVOID			TdiEventContext,
	IN	CONNECTION_CONTEXT	ConnectionContext,
	IN	ULONG			ReceiveFlags,
	IN	ULONG			ReceiveLength,
	IN	ULONG			StartingOffset,
	IN	PMDL			Tsdu,
	IN	PVOID			TsduDescriptor
	)
{
    UNIMPLEMENTED;
	return STATUS_NOT_IMPLEMENTED;
}


/*
 * ClientEventChainedReceive
 *
 * @unimplemented
 */
NTSTATUS
NTAPI
TdiDefaultChainedReceiveHandler (
	IN	PVOID			TdiEventContext,
	IN	CONNECTION_CONTEXT	ConnectionContext,
	IN	ULONG			ReceiveFlags,
	IN	ULONG			ReceiveLength,
	IN	ULONG			StartingOffset,
	IN	PMDL			Tsdu,
	IN	PVOID			TsduDescriptor
	)
{
    UNIMPLEMENTED;
	return STATUS_NOT_IMPLEMENTED;
}


/*
 * ClientEventConnect
 *
 * @unimplemented
 */
NTSTATUS
NTAPI
TdiDefaultConnectHandler (
	IN	PVOID			TdiEventContext,
        IN	LONG			RemoteAddressLength,
        IN	PVOID			RemoteAddress,
        IN	LONG			UserDataLength,
        IN	PVOID			UserData,
	IN	LONG			OptionsLength,
	IN	PVOID			Options,
	OUT	CONNECTION_CONTEXT	* ConnectionContext,
	OUT	PIRP			* AcceptIrp
	)
{
    UNIMPLEMENTED;
	return STATUS_NOT_IMPLEMENTED;
}


/*
 * ClientEventDisconnect
 *
 * @unimplemented
 */
NTSTATUS
NTAPI
TdiDefaultDisconnectHandler (
	IN	PVOID			TdiEventContext,
	IN	CONNECTION_CONTEXT	ConnectionContext,
	IN	LONG			DisconnectDataLength,
	IN	PVOID			DisconnectData,
	IN	LONG			DisconnectInformationLength,
	IN	PVOID			DisconnectInformation,
	IN	ULONG			DisconnectFlags
	)
{
    UNIMPLEMENTED;
	return STATUS_NOT_IMPLEMENTED;
}


/*
 * ClientEventError
 *
 * @unimplemented
 */
NTSTATUS
NTAPI
TdiDefaultErrorHandler (
	IN	PVOID		TdiEventContext,
	IN	NTSTATUS	Status
	)
{
    UNIMPLEMENTED;
	return STATUS_NOT_IMPLEMENTED;
}


/*
 * ClientEventReceiveDatagram
 *
 * @unimplemented
 */
NTSTATUS
NTAPI
TdiDefaultRcvDatagramHandler (
	IN	PVOID	TdiEventContext,
	IN	LONG	SourceAddressLength,
	IN	PVOID	SourceAddress,
	IN	LONG	OptionsLength,
	IN	PVOID	Options,
	IN	ULONG	ReceiveDatagramFlags,
	IN	ULONG	BytesIndicated,
	IN	ULONG	BytesAvailable,
	OUT	ULONG	* BytesTaken,
	IN	PVOID	Tsdu,
	OUT	PIRP	* IoRequestPacket
	)
{
    UNIMPLEMENTED;
	return STATUS_NOT_IMPLEMENTED;
}


/*
 * ClientEventReceiveExpedited
 *
 * @unimplemented
 */
TDI_STATUS
NTAPI
TdiDefaultRcvExpeditedHandler (
	IN	PVOID			TdiEventContext,
	IN	CONNECTION_CONTEXT	ConnectionContext,
	IN	ULONG			ReceiveFlags,
	IN	ULONG			BytesIndicated,
	IN	ULONG			BytesAvailable,
	OUT	ULONG			* BytesTaken,
	IN	PVOID			Tsdu,
	OUT	PIRP			* IoRequestPacket
	)
{
    UNIMPLEMENTED;
	return STATUS_NOT_IMPLEMENTED;
}


/*
 * ClientEventReceive
 *
 * @unimplemented
 */
NTSTATUS
NTAPI
TdiDefaultReceiveHandler (
	IN	PVOID			TdiEventContext,
	IN	CONNECTION_CONTEXT	ConnectionContext,
	IN	ULONG			ReceiveFlags,
	IN	ULONG			BytesIndicated,
	IN	ULONG			BytesAvailable,
	OUT	ULONG			* BytesTaken,
	IN	PVOID			Tsdu,
	OUT	PIRP			* IoRequestPacket
	)
{
    UNIMPLEMENTED;
	return STATUS_NOT_IMPLEMENTED;
}


/*
 * ClientEventSendPossible
 *
 * @unimplemented
 */
TDI_STATUS
NTAPI
TdiDefaultSendPossibleHandler (
	IN	PVOID	TdiEventContext,
	IN	PVOID	ConnectionContext,
	IN	ULONG	BytesAvailable
	)
{
    UNIMPLEMENTED;
	return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
TdiRegisterPnPHandlers(
    _In_ PTDI_CLIENT_INTERFACE_INFO ClientInterfaceInfo,
    _In_ ULONG InterfaceInfoSize,
    _Out_ HANDLE *BindingHandle)
{
    PTDI_PNP_CLIENT Client;
    PLIST_ENTRY Entry;

    if (!ClientInterfaceInfo || !BindingHandle || InterfaceInfoSize < sizeof(TDI_CLIENT_INTERFACE_INFO))
        return STATUS_INVALID_PARAMETER;

    Client = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Client), TAG_TDI_PNP);
    if (!Client)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlCopyMemory(&Client->Info, ClientInterfaceInfo, sizeof(Client->Info));

    KeWaitForMutexObject(&TdiPnpMutex, Executive, KernelMode, FALSE, NULL);

    InsertTailList(&TdiPnpClientList, &Client->ListEntry);

    for (Entry = TdiNetAddressList.Flink; Entry != &TdiNetAddressList; Entry = Entry->Flink)
        TdiPnpNotifyAddress(Client, CONTAINING_RECORD(Entry, TDI_NET_ADDRESS, ListEntry), TRUE);

    KeReleaseMutex(&TdiPnpMutex, FALSE);

    *BindingHandle = Client;

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
TdiDeregisterPnPHandlers(
    _In_ HANDLE BindingHandle)
{
    PTDI_PNP_CLIENT Client = BindingHandle;

    if (!Client)
        return STATUS_INVALID_PARAMETER;

    KeWaitForMutexObject(&TdiPnpMutex, Executive, KernelMode, FALSE, NULL);
    RemoveEntryList(&Client->ListEntry);
    KeReleaseMutex(&TdiPnpMutex, FALSE);

    ExFreePoolWithTag(Client, TAG_TDI_PNP);

    return STATUS_SUCCESS;
}

/* EOF */
