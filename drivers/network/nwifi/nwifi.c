/*
 * PROJECT:     LiberNT Native WiFi (802.11) stack
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     DriverEntry and media-converting filter registration
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "nwifi.h"

#define NDEBUG
#include <debug.h>

NWIFI_GLOBALS gNwifi = { 0 };

/* ===========================================================================
 *  Pool helpers
 * ===========================================================================
 */
PVOID
NwifiAllocate(
    _In_ SIZE_T Size)
{
    PVOID Buffer = ExAllocatePoolWithTag(NonPagedPool, Size, NWIFI_TAG);
    if (Buffer != NULL)
    {
        RtlZeroMemory(Buffer, Size);
    }
    return Buffer;
}

VOID
NwifiFree(
    _In_ PVOID Buffer)
{
    if (Buffer != NULL)
    {
        ExFreePoolWithTag(Buffer, NWIFI_TAG);
    }
}

/* The caller uses the result only while servicing an IOCTL; interface
 * teardown is serialised by NDIS unbind at PASSIVE_LEVEL. */
PNWIFI_ADAPTER
NwifiFindAdapterByIndex(
    _In_ ULONG InterfaceIndex)
{
    PLIST_ENTRY Entry;
    PNWIFI_ADAPTER Found = NULL;

    NdisAcquireSpinLock(&gNwifi.AdapterLock);
    for (Entry = gNwifi.AdapterList.Flink;
         Entry != &gNwifi.AdapterList;
         Entry = Entry->Flink)
    {
        PNWIFI_ADAPTER Adapter = CONTAINING_RECORD(Entry, NWIFI_ADAPTER, Link);
        if (Adapter->InterfaceIndex == InterfaceIndex)
        {
            Found = Adapter;
            break;
        }
    }
    NdisReleaseSpinLock(&gNwifi.AdapterLock);
    return Found;
}

static const WCHAR NwifiFilterFriendlyName[] = L"LiberNT Native WiFi Filter";
static const WCHAR NwifiFilterUniqueName[] = L"{DDB50DEB-F71C-464F-91D6-AC8825F5A2D9}";
static const WCHAR NwifiFilterServiceName[] = L"Nwifi";

static
VOID
NwifiInitString(
    _Out_ PNDIS_STRING String,
    _In_z_ PCWSTR Buffer)
{
    String->Buffer = (PWCH)Buffer;
    String->Length = (USHORT)(wcslen(Buffer) * sizeof(WCHAR));
    String->MaximumLength = (USHORT)(String->Length + sizeof(WCHAR));
}

static
NDIS_STATUS
NwifiRegisterFilter(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    NDIS_FILTER_DRIVER_CHARACTERISTICS Chars;

    RtlZeroMemory(&Chars, sizeof(Chars));
    Chars.Header.Type = NDIS_OBJECT_TYPE_FILTER_DRIVER_CHARACTERISTICS;
    Chars.Header.Revision = NDIS_FILTER_CHARACTERISTICS_REVISION_2;
    Chars.Header.Size = NDIS_SIZEOF_FILTER_DRIVER_CHARACTERISTICS_REVISION_2;
    Chars.MajorNdisVersion = 6;
    Chars.MinorNdisVersion = 20;
    Chars.MajorDriverVersion = 1;
    Chars.MinorDriverVersion = 0;
    Chars.Flags = 0;
    NwifiInitString(&Chars.FriendlyName, NwifiFilterFriendlyName);
    NwifiInitString(&Chars.UniqueName, NwifiFilterUniqueName);
    NwifiInitString(&Chars.ServiceName, NwifiFilterServiceName);

    Chars.SetOptionsHandler = NwifiFilterSetOptions;
    Chars.AttachHandler = NwifiFilterAttach;
    Chars.DetachHandler = NwifiFilterDetach;
    Chars.RestartHandler = NwifiFilterRestart;
    Chars.PauseHandler = NwifiFilterPause;
    Chars.SendNetBufferListsHandler = NwifiFilterSendNetBufferLists;
    Chars.SendNetBufferListsCompleteHandler = NwifiFilterSendNetBufferListsComplete;
    Chars.CancelSendNetBufferListsHandler = NwifiFilterCancelSendNetBufferLists;
    Chars.ReceiveNetBufferListsHandler = NwifiFilterReceiveNetBufferLists;
    Chars.ReturnNetBufferListsHandler = NwifiFilterReturnNetBufferLists;
    Chars.OidRequestHandler = NwifiFilterOidRequest;
    Chars.OidRequestCompleteHandler = NwifiFilterOidRequestComplete;
    Chars.CancelOidRequestHandler = NwifiFilterCancelOidRequest;
    Chars.StatusHandler = NwifiFilterStatus;
    Chars.NetPnPEventHandler = NwifiFilterNetPnPEvent;
    Chars.DevicePnPEventNotifyHandler = NwifiFilterDevicePnPEventNotify;

    UNREFERENCED_PARAMETER(RegistryPath);

    return NdisFRegisterFilterDriver(DriverObject,
                                     (NDIS_HANDLE)DriverObject,
                                     &Chars,
                                     &gNwifi.FilterDriverHandle);
}

/* ===========================================================================
 *  Unload
 * ===========================================================================
 */
VOID
NTAPI
NwifiUnload(
    _In_ PDRIVER_OBJECT DriverObject)
{
    UNREFERENCED_PARAMETER(DriverObject);

    DPRINT1("NWIFI: Unload\n");

    /* Remove the control device + drain any pended notification IRPs first. */
    NwifiDeleteControlDevice();

    if (gNwifi.FilterDriverHandle != NULL)
    {
        NdisFDeregisterFilterDriver(gNwifi.FilterDriverHandle);
        gNwifi.FilterDriverHandle = NULL;
    }

    NdisFreeSpinLock(&gNwifi.AdapterLock);
}

/* ===========================================================================
 *  DriverEntry
 * ===========================================================================
 */
NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    NDIS_STATUS Status;

    DPRINT1("NWIFI: DriverEntry\n");

    RtlZeroMemory(&gNwifi, sizeof(gNwifi));
    gNwifi.DriverObject = DriverObject;
    InitializeListHead(&gNwifi.AdapterList);
    NdisAllocateSpinLock(&gNwifi.AdapterLock);
    InitializeListHead(&gNwifi.NotifyIrpQueue);
    KeInitializeSpinLock(&gNwifi.NotifyLock);
    gNwifi.NextInterfaceIndex = 0;

    DriverObject->DriverUnload = NwifiUnload;

    Status = NwifiRegisterFilter(DriverObject, RegistryPath);
    if (Status != NDIS_STATUS_SUCCESS)
    {
        DPRINT1("NWIFI: NdisFRegisterFilterDriver failed 0x%08X\n", Status);
        NdisFreeSpinLock(&gNwifi.AdapterLock);
        return (NTSTATUS)Status;
    }

    /* Non-fatal on failure: the data path works without the wlansvc channel. */
    Status = NwifiCreateControlDevice();
    if (Status != NDIS_STATUS_SUCCESS)
    {
        DPRINT1("NWIFI: control device creation failed 0x%08X (continuing)\n", Status);
    }

    DPRINT1("NWIFI: DriverEntry succeeded\n");
    return STATUS_SUCCESS;
}
