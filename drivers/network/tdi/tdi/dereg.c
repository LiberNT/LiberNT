#include "precomp.h"

/* De-/Register Action IDs for TdiDeRegister */

typedef
enum
{
	R_NOTIFICATION_HANDLER = 0,
	DT_NOTIFICATION_HANDLER,
	R_DEVICE_OBJECT,
	D_DEVICE_OBJECT,
	R_ADDRESS_CHANGE_HANDLER,
	D_ADDRESS_CHANGE_HANDLER,
	R_NET_ADDRESS,
	D_NET_ADDRESS

} TDI_OBJECT_ACTION;


/* ADDRESS_CHANGE_HANDLER */

/*
 * @unimplemented
 */
NTSTATUS
NTAPI
TdiRegisterAddressChangeHandler(IN TDI_ADD_ADDRESS_HANDLER AddHandler,
                                IN TDI_DEL_ADDRESS_HANDLER DeleteHandler,
                                OUT HANDLE *BindingHandle)
{
    return STATUS_NOT_IMPLEMENTED;
}


/*
 * @unimplemented
 */
NTSTATUS
NTAPI
TdiDeregisterAddressChangeHandler(IN HANDLE BindingHandle)
{
    return STATUS_NOT_IMPLEMENTED;
}


/* DEVICE_OBJECT */

/*
 * @unimplemented
 */
NTSTATUS
NTAPI
TdiRegisterDeviceObject(IN PUNICODE_STRING DeviceName,
                        OUT HANDLE *RegistrationHandle)
{
    return STATUS_NOT_IMPLEMENTED;
}


/*
 * @unimplemented
 */
NTSTATUS
NTAPI
TdiDeregisterDeviceObject(IN HANDLE RegistrationHandle)
{
    return STATUS_NOT_IMPLEMENTED;
}


/* NET_ADDRESS */

VOID
TdiPnpNotifyAddress(
    _In_ PTDI_PNP_CLIENT Client,
    _In_ PTDI_NET_ADDRESS NetAddress,
    _In_ BOOLEAN Add)
{
    if (Client->Info.MajorTdiVersion >= 2)
    {
        if (Add && Client->Info.AddAddressHandlerV2)
            Client->Info.AddAddressHandlerV2(&NetAddress->Address, &NetAddress->DeviceName, NetAddress->Context);
        else if (!Add && Client->Info.DelAddressHandlerV2)
            Client->Info.DelAddressHandlerV2(&NetAddress->Address, &NetAddress->DeviceName, NetAddress->Context);
    }
    else
    {
        if (Add && Client->Info.AddAddressHandler)
            Client->Info.AddAddressHandler(&NetAddress->Address);
        else if (!Add && Client->Info.DelAddressHandler)
            Client->Info.DelAddressHandler(&NetAddress->Address);
    }
}

static VOID
TdiFreeNetAddress(PTDI_NET_ADDRESS NetAddress)
{
    if (NetAddress->DeviceName.Buffer)
        ExFreePoolWithTag(NetAddress->DeviceName.Buffer, TAG_TDI_PNP);
    if (NetAddress->Context)
        ExFreePoolWithTag(NetAddress->Context, TAG_TDI_PNP);
    ExFreePoolWithTag(NetAddress, TAG_TDI_PNP);
}

NTSTATUS
NTAPI
TdiRegisterNetAddress(IN PTA_ADDRESS Address,
                      IN PUNICODE_STRING DeviceName,
                      IN PTDI_PNP_CONTEXT Context,
                      OUT HANDLE *RegistrationHandle)
{
    PTDI_NET_ADDRESS NetAddress;
    PLIST_ENTRY Entry;
    ULONG ContextLength;

    if (!Address || !RegistrationHandle)
        return STATUS_INVALID_PARAMETER;

    NetAddress = ExAllocatePoolZero(PagedPool,
                                    FIELD_OFFSET(TDI_NET_ADDRESS, Address.Address) + Address->AddressLength,
                                    TAG_TDI_PNP);
    if (!NetAddress)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlCopyMemory(&NetAddress->Address, Address, FIELD_OFFSET(TA_ADDRESS, Address) + Address->AddressLength);

    if (DeviceName && DeviceName->Length)
    {
        NetAddress->DeviceName.Buffer = ExAllocatePoolWithTag(PagedPool, DeviceName->Length, TAG_TDI_PNP);
        if (!NetAddress->DeviceName.Buffer)
        {
            TdiFreeNetAddress(NetAddress);
            return STATUS_INSUFFICIENT_RESOURCES;
        }
        RtlCopyMemory(NetAddress->DeviceName.Buffer, DeviceName->Buffer, DeviceName->Length);
        NetAddress->DeviceName.Length = DeviceName->Length;
        NetAddress->DeviceName.MaximumLength = DeviceName->Length;
    }

    if (Context)
    {
        ContextLength = FIELD_OFFSET(TDI_PNP_CONTEXT, ContextData) + Context->ContextSize;
        NetAddress->Context = ExAllocatePoolWithTag(PagedPool, ContextLength, TAG_TDI_PNP);
        if (!NetAddress->Context)
        {
            TdiFreeNetAddress(NetAddress);
            return STATUS_INSUFFICIENT_RESOURCES;
        }
        RtlCopyMemory(NetAddress->Context, Context, ContextLength);
    }

    KeWaitForMutexObject(&TdiPnpMutex, Executive, KernelMode, FALSE, NULL);

    InsertTailList(&TdiNetAddressList, &NetAddress->ListEntry);

    for (Entry = TdiPnpClientList.Flink; Entry != &TdiPnpClientList; Entry = Entry->Flink)
        TdiPnpNotifyAddress(CONTAINING_RECORD(Entry, TDI_PNP_CLIENT, ListEntry), NetAddress, TRUE);

    KeReleaseMutex(&TdiPnpMutex, FALSE);

    *RegistrationHandle = NetAddress;

    return STATUS_SUCCESS;
}


NTSTATUS
NTAPI
TdiDeregisterNetAddress(IN HANDLE RegistrationHandle)
{
    PTDI_NET_ADDRESS NetAddress = RegistrationHandle;
    PLIST_ENTRY Entry;

    if (!NetAddress)
        return STATUS_INVALID_PARAMETER;

    KeWaitForMutexObject(&TdiPnpMutex, Executive, KernelMode, FALSE, NULL);

    RemoveEntryList(&NetAddress->ListEntry);

    for (Entry = TdiPnpClientList.Flink; Entry != &TdiPnpClientList; Entry = Entry->Flink)
        TdiPnpNotifyAddress(CONTAINING_RECORD(Entry, TDI_PNP_CLIENT, ListEntry), NetAddress, FALSE);

    KeReleaseMutex(&TdiPnpMutex, FALSE);

    TdiFreeNetAddress(NetAddress);

    return STATUS_SUCCESS;
}


/* NOTIFICATION_HANDLER */

/*
 * @unimplemented
 */
NTSTATUS
NTAPI
TdiRegisterNotificationHandler(IN TDI_BIND_HANDLER BindHandler,
                               IN TDI_UNBIND_HANDLER UnbindHandler,
                               OUT HANDLE *BindingHandle)
{
    return STATUS_NOT_IMPLEMENTED;
}


/*
 * @unimplemented
 */
NTSTATUS
NTAPI
TdiDeregisterNotificationHandler(IN HANDLE BindingHandle)
{
    return STATUS_NOT_IMPLEMENTED;
}

/* EOF */
