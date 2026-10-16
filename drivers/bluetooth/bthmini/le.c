/*
 * PROJECT:     LiberNT Bluetooth stack
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Bluetooth Low Energy controller setup, scanning and advertising reports
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "bthmini.h"

#define NDEBUG
#include <debug.h>

#define HCI_LE_SET_EVENT_MASK               HCI_OP(0x08, 0x0001)
#define HCI_LE_READ_LOCAL_FEATURES          HCI_OP(0x08, 0x0003)
#define HCI_LE_SET_SCAN_PARAMETERS          HCI_OP(0x08, 0x000B)
#define HCI_LE_SET_SCAN_ENABLE              HCI_OP(0x08, 0x000C)
#define HCI_LE_SET_EXTENDED_SCAN_PARAMETERS HCI_OP(0x08, 0x0041)
#define HCI_LE_SET_EXTENDED_SCAN_ENABLE     HCI_OP(0x08, 0x0042)
#define HCI_LE_CREATE_CONNECTION            HCI_OP(0x08, 0x000D)
#define HCI_LE_CREATE_CONNECTION_CANCEL     HCI_OP(0x08, 0x000E)
#define HCI_LE_CONNECTION_UPDATE            HCI_OP(0x08, 0x0013)
#define HCI_LE_LTK_NEGATIVE_REPLY           HCI_OP(0x08, 0x001B)
#define HCI_LE_REMOTE_PARAMETER_REPLY       HCI_OP(0x08, 0x0020)
#define HCI_LE_EXTENDED_CREATE_CONNECTION   HCI_OP(0x08, 0x0043)
#define HCI_LE_READ_ACCEPT_LIST_SIZE        HCI_OP(0x08, 0x000F)
#define HCI_LE_WRITE_DEFAULT_DATA_LENGTH    HCI_OP(0x08, 0x0024)
#define HCI_LE_READ_MAXIMUM_DATA_LENGTH     HCI_OP(0x08, 0x002F)
#define HCI_LE_SET_DEFAULT_PHY              HCI_OP(0x08, 0x0031)
#define HCI_LE_ADD_TO_RESOLVING_LIST        HCI_OP(0x08, 0x0027)
#define HCI_LE_CLEAR_RESOLVING_LIST         HCI_OP(0x08, 0x0029)
#define HCI_LE_READ_RESOLVING_LIST_SIZE     HCI_OP(0x08, 0x002A)
#define HCI_LE_SET_ADDRESS_RESOLUTION       HCI_OP(0x08, 0x002D)
#define HCI_LE_SET_PRIVACY_MODE             HCI_OP(0x08, 0x004E)
#define HCI_LE_CLEAR_ACCEPT_LIST            HCI_OP(0x08, 0x0010)
#define HCI_LE_ADD_TO_ACCEPT_LIST           HCI_OP(0x08, 0x0011)
#define HCI_EV_ENCRYPTION_KEY_REFRESH_LE    0x30

#define HCI_EV_LE_META                      0x3E
#define HCI_LE_ADVERTISING_REPORT           0x02
#define HCI_LE_EXTENDED_ADVERTISING_REPORT  0x0D
#define HCI_LE_CONNECTION_COMPLETE          0x01
#define HCI_LE_CONNECTION_UPDATE_COMPLETE   0x03
#define HCI_LE_LTK_REQUEST                  0x05
#define HCI_LE_REMOTE_PARAMETER_REQUEST     0x06
#define HCI_LE_ENHANCED_CONNECTION_COMPLETE 0x0A

#define LE_CID_ATT                          0x0004
#define LE_CID_SIGNALING                    0x0005
#define LE_CID_SMP                          0x0006
#define LE_SIGNAL_COMMAND_REJECT            0x01
#define LE_SIGNAL_PARAMETER_UPDATE_REQUEST  0x12
#define LE_SIGNAL_PARAMETER_UPDATE_RESPONSE 0x13
#define LE_SIGNAL_CREDIT_CONNECT_REQUEST    0x14
#define LE_SIGNAL_CREDIT_CONNECT_RESPONSE   0x15
#define LE_CONN_INTERVAL_MIN                0x0018
#define LE_CONN_INTERVAL_MAX                0x0028
#define LE_SUPERVISION_TIMEOUT              0x01F4
#define LE_CONNECT_TIMEOUT_MS               15000
#define LE_AUTO_CONNECT_RETRY_MS            5000
#define LE_INITIATOR_IDLE                   0
#define LE_INITIATOR_DIRECT                 1
#define LE_INITIATOR_ACCEPT_LIST            2
#define LE_FILTER_PEER_ADDRESS              0x00
#define LE_FILTER_ACCEPT_LIST               0x01

#define LE_FEATURE_DATA_LENGTH              5
#define LE_FEATURE_LL_PRIVACY               6
#define LE_PRIVACY_MODE_DEVICE              0x01
#define LE_FEATURE_2M_PHY                   8
#define LE_FEATURE_CODED_PHY                11
#define LE_FEATURE_EXTENDED_ADVERTISING     12

#define LE_SCAN_ACTIVE                      0x01
#define LE_SCAN_INTERVAL                    0x0060
#define LE_SCAN_WINDOW                      0x0030
#define LE_PHY_1M                           0x01
#define LE_PHY_CODED                        0x04

#define LE_AD_FLAGS                         0x01
#define LE_AD_FLAGS_DISCOVERABLE            0x03
#define LE_AD_SHORT_NAME                    0x08
#define LE_AD_COMPLETE_NAME                 0x09

static const UCHAR BthLeMaskedSubevents[] =
{
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0C, 0x0D
};

VOID
BthLeInitialize(
    _In_ PBTHMINI_DEVICE Dev)
{
    UCHAR Result[9], Parameters[8];
    ULONGLONG Mask = 0;
    ULONG Index;

    if (!Dev->LeEnabled)
        return;
    if (NT_SUCCESS(BthHciCommand(Dev, HCI_LE_READ_LOCAL_FEATURES, NULL, 0, Result, sizeof(Result))))
    {
        for (Index = 0; Index < 8; Index++)
            Dev->LeFeatures |= (ULONGLONG)Result[1 + Index] << (Index * 8);
    }
    Dev->LeExtendedScan = (Dev->LeFeatures >> LE_FEATURE_EXTENDED_ADVERTISING) & 1;

    for (Index = 0; Index < RTL_NUMBER_OF(BthLeMaskedSubevents); Index++)
        Mask |= 1ULL << (BthLeMaskedSubevents[Index] - 1);
    for (Index = 0; Index < 8; Index++)
        Parameters[Index] = (UCHAR)(Mask >> (Index * 8));
    BthHciCommand(Dev, HCI_LE_SET_EVENT_MASK, Parameters, sizeof(Parameters), NULL, 0);
    if (NT_SUCCESS(BthHciCommand(Dev, HCI_LE_READ_ACCEPT_LIST_SIZE, NULL, 0, Result, 2)) && !Result[0])
        Dev->LeAcceptListSize = Result[1];
    if (((Dev->LeFeatures >> LE_FEATURE_DATA_LENGTH) & 1) &&
        NT_SUCCESS(BthHciCommand(Dev, HCI_LE_READ_MAXIMUM_DATA_LENGTH, NULL, 0, Result, 9)) && !Result[0])
    {
        RtlCopyMemory(Parameters, Result + 1, 4);
        BthHciCommand(Dev, HCI_LE_WRITE_DEFAULT_DATA_LENGTH, Parameters, 4, NULL, 0);
    }
    if (((Dev->LeFeatures >> LE_FEATURE_2M_PHY) & 1) || ((Dev->LeFeatures >> LE_FEATURE_CODED_PHY) & 1))
    {
        Parameters[0] = 0x00;
        Parameters[1] = LE_PHY_1M | (((Dev->LeFeatures >> LE_FEATURE_2M_PHY) & 1) ? 0x02 : 0) |
                        (((Dev->LeFeatures >> LE_FEATURE_CODED_PHY) & 1) ? LE_PHY_CODED : 0);
        Parameters[2] = Parameters[1];
        BthHciCommand(Dev, HCI_LE_SET_DEFAULT_PHY, Parameters, 3, NULL, 0);
    }
    if (((Dev->LeFeatures >> LE_FEATURE_LL_PRIVACY) & 1) &&
        NT_SUCCESS(BthHciCommand(Dev, HCI_LE_READ_RESOLVING_LIST_SIZE, NULL, 0, Result, 2)) && !Result[0] && Result[1])
    {
        Dev->LeResolvingListSize = Result[1];
        Dev->LePrivacy = TRUE;
    }
    Dev->LeAcceptListDirty = TRUE;
    DPRINT1("BTHMINI: LE features 0x%I64x, %s scanning\n", Dev->LeFeatures, Dev->LeExtendedScan ? "extended" : "legacy");
}

static
VOID
BthLePut16(
    _Out_writes_(2) PUCHAR Data,
    _In_ USHORT Value)
{
    Data[0] = (UCHAR)Value;
    Data[1] = (UCHAR)(Value >> 8);
}

VOID
BthLeSetScan(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ BOOLEAN Enable)
{
    UCHAR Parameters[13];

    if (!Dev->LeEnabled || Dev->LeScanning == Enable)
        return;
    if (Dev->LeExtendedScan)
    {
        if (Enable)
        {
            BOOLEAN Coded = (Dev->LeFeatures >> LE_FEATURE_CODED_PHY) & 1;
            ULONG Length = 3;

            Parameters[0] = 0x00;
            Parameters[1] = 0x00;
            Parameters[2] = LE_PHY_1M | (Coded ? LE_PHY_CODED : 0);
            Parameters[Length++] = LE_SCAN_ACTIVE;
            BthLePut16(Parameters + Length, LE_SCAN_INTERVAL);
            BthLePut16(Parameters + Length + 2, LE_SCAN_WINDOW);
            Length += 4;
            if (Coded)
            {
                Parameters[Length++] = LE_SCAN_ACTIVE;
                BthLePut16(Parameters + Length, LE_SCAN_INTERVAL * 3);
                BthLePut16(Parameters + Length + 2, LE_SCAN_WINDOW * 3);
                Length += 4;
            }
            BthHciQueueCommand(Dev, HCI_LE_SET_EXTENDED_SCAN_PARAMETERS, Parameters, (UCHAR)Length);
        }
        RtlZeroMemory(Parameters, 6);
        Parameters[0] = Enable;
        Parameters[1] = 0x01;
        BthHciQueueCommand(Dev, HCI_LE_SET_EXTENDED_SCAN_ENABLE, Parameters, 6);
    }
    else
    {
        if (Enable)
        {
            Parameters[0] = LE_SCAN_ACTIVE;
            BthLePut16(Parameters + 1, LE_SCAN_INTERVAL);
            BthLePut16(Parameters + 3, LE_SCAN_WINDOW);
            Parameters[5] = 0x00;
            Parameters[6] = 0x00;
            BthHciQueueCommand(Dev, HCI_LE_SET_SCAN_PARAMETERS, Parameters, 7);
        }
        Parameters[0] = Enable;
        Parameters[1] = 0x01;
        BthHciQueueCommand(Dev, HCI_LE_SET_SCAN_ENABLE, Parameters, 2);
    }
    Dev->LeScanning = Enable;
}

static
VOID
BthLeParseAdvertisingData(
    _Inout_ PBTHMINI_REMOTE Remote,
    _In_reads_bytes_(Length) const UCHAR *Data,
    _In_ ULONG Length)
{
    ULONG Offset = 0;

    while (Offset < Length && Data[Offset])
    {
        ULONG FieldLength = Data[Offset];
        UCHAR Type;

        if (Offset + 1 + FieldLength > Length)
            break;
        Type = Data[Offset + 1];
        if (Type == LE_AD_FLAGS && FieldLength > 1 && (Data[Offset + 2] & LE_AD_FLAGS_DISCOVERABLE))
            Remote->Flags |= BDIF_LE_DISCOVERABLE;
        if ((Type == LE_AD_COMPLETE_NAME || (Type == LE_AD_SHORT_NAME && !(Remote->Flags & BDIF_NAME))) &&
            FieldLength > 1)
        {
            ULONG NameLength = min(FieldLength - 1, BTH_MAX_NAME_SIZE - 1);

            RtlZeroMemory(Remote->Name, sizeof(Remote->Name));
            RtlCopyMemory(Remote->Name, Data + Offset + 2, NameLength);
            Remote->Flags |= BDIF_NAME | BDIF_LE_NAME;
            if (Type == LE_AD_SHORT_NAME)
                Remote->Flags |= BDIF_SHORT_NAME;
            else
                Remote->Flags &= ~BDIF_SHORT_NAME;
        }
        Offset += 1 + FieldLength;
    }
}

static
VOID
BthLeReport(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ UCHAR AddressType,
    _In_reads_(6) const UCHAR *AddressData,
    _In_ BOOLEAN Connectable,
    _In_reads_bytes_(Length) const UCHAR *Data,
    _In_ ULONG Length)
{
    PBTHMINI_REMOTE Remote;
    ULONG Previous;

    if (!Dev->Discovering)
        return;
    Remote = BthRadioGetRemote(Dev, BthReadAddress(AddressData));
    if (!Remote)
        return;
    Previous = Remote->Flags;
    if (!(Previous & (BDIF_COD | BDIF_PAIRED)))
        Remote->Flags &= ~BDIF_BR;
    Remote->Flags |= BDIF_LE | BDIF_LE_VISIBLE | BDIF_RSSI;
    Remote->LeAddressType = AddressType & 0x01;
    if (Remote->LeAddressType)
        Remote->Flags |= BDIF_LE_RANDOM_ADDRESS_TYPE;
    if (Connectable)
        Remote->Flags |= BDIF_LE_CONNECTABLE;
    BthLeParseAdvertisingData(Remote, Data, Length);
    if (Remote->Flags != Previous)
        BthRadioReportInRange(Dev, Remote, Previous);
}

PBTHMINI_REMOTE
BthLeFindHandle(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ USHORT Handle)
{
    PLIST_ENTRY Entry;

    for (Entry = Dev->Remotes.Flink; Entry != &Dev->Remotes; Entry = Entry->Flink)
    {
        PBTHMINI_REMOTE Remote = CONTAINING_RECORD(Entry, BTHMINI_REMOTE, Link);

        if (Remote->LeConnected && Remote->LeHandle == Handle)
            return Remote;
    }
    return NULL;
}

static
USHORT
BthLeGet16(
    _In_reads_(2) const UCHAR *Data)
{
    return (USHORT)(Data[0] | (Data[1] << 8));
}

static
NTSTATUS
BthLeCreateConnection(
    _In_ PBTHMINI_DEVICE Dev,
    _In_opt_ PBTHMINI_REMOTE Remote)
{
    BTHMINI_LE_PARAMETERS Settings;
    UCHAR Parameters[26];
    UCHAR Filter = Remote ? LE_FILTER_PEER_ADDRESS : LE_FILTER_ACCEPT_LIST;
    UCHAR AddressType = Remote ? Remote->LeAddressType : 0;
    BTH_ADDR Address = Remote ? Remote->Address : 0;
    USHORT ScanInterval, ScanWindow;

    if (Remote && Remote->HasLeIrk && Dev->LePrivacy && !Dev->LeAcceptListDirty)
        AddressType |= 0x02;
    Settings.HighDutyScanInterval = LE_SCAN_INTERVAL;
    Settings.HighDutyScanWindow = LE_SCAN_WINDOW;
    Settings.LowDutyScanInterval = LE_SCAN_INTERVAL;
    Settings.LowDutyScanWindow = LE_SCAN_WINDOW;
    Settings.IntervalMin = LE_CONN_INTERVAL_MIN;
    Settings.IntervalMax = LE_CONN_INTERVAL_MAX;
    Settings.Latency = 0;
    Settings.SupervisionTimeout = LE_SUPERVISION_TIMEOUT;
    if (Remote)
    {
        BthGattConnectionParameters(Remote, &Settings);
    }
    else
    {
        PLIST_ENTRY Entry;

        for (Entry = Dev->Remotes.Flink; Entry != &Dev->Remotes; Entry = Entry->Flink)
        {
            PBTHMINI_REMOTE Bonded = CONTAINING_RECORD(Entry, BTHMINI_REMOTE, Link);

            if (Bonded->HasLeKey && !Bonded->LeConnected && BthGattConnectionParameters(Bonded, &Settings))
                break;
        }
    }
    ScanInterval = Remote ? Settings.HighDutyScanInterval : Settings.LowDutyScanInterval;
    ScanWindow = Remote ? Settings.HighDutyScanWindow : Settings.LowDutyScanWindow;

    if (Dev->LeExtendedScan)
    {
        Parameters[0] = Filter;
        Parameters[1] = 0x00;
        Parameters[2] = AddressType;
        BthWriteAddress(Parameters + 3, Address);
        Parameters[9] = LE_PHY_1M;
        BthLePut16(Parameters + 10, ScanInterval);
        BthLePut16(Parameters + 12, ScanWindow);
        BthLePut16(Parameters + 14, Settings.IntervalMin);
        BthLePut16(Parameters + 16, Settings.IntervalMax);
        BthLePut16(Parameters + 18, Settings.Latency);
        BthLePut16(Parameters + 20, Settings.SupervisionTimeout);
        BthLePut16(Parameters + 22, 0);
        BthLePut16(Parameters + 24, 0);
        return BthHciQueueCommand(Dev, HCI_LE_EXTENDED_CREATE_CONNECTION, Parameters, 26);
    }
    BthLePut16(Parameters, ScanInterval);
    BthLePut16(Parameters + 2, ScanWindow);
    Parameters[4] = Filter;
    Parameters[5] = AddressType;
    BthWriteAddress(Parameters + 6, Address);
    Parameters[12] = 0x00;
    BthLePut16(Parameters + 13, Settings.IntervalMin);
    BthLePut16(Parameters + 15, Settings.IntervalMax);
    BthLePut16(Parameters + 17, Settings.Latency);
    BthLePut16(Parameters + 19, Settings.SupervisionTimeout);
    BthLePut16(Parameters + 21, 0);
    BthLePut16(Parameters + 23, 0);
    return BthHciQueueCommand(Dev, HCI_LE_CREATE_CONNECTION, Parameters, 25);
}

static
VOID
BthLeCancelConnection(
    _In_ PBTHMINI_DEVICE Dev)
{
    if (Dev->LeInitiator == LE_INITIATOR_IDLE || Dev->LeConnectCancelled)
        return;
    Dev->LeConnectCancelled = TRUE;
    BthHciQueueCommand(Dev, HCI_LE_CREATE_CONNECTION_CANCEL, NULL, 0);
}

static
NTSTATUS
BthLeStartDirect(
    _In_ PBTHMINI_DEVICE Dev)
{
    PBTHMINI_REMOTE Remote = Dev->LeConnectPending;
    NTSTATUS Status;

    Status = BthLeCreateConnection(Dev, Remote);
    if (!NT_SUCCESS(Status))
        return Status;
    Dev->LeInitiator = LE_INITIATOR_DIRECT;
    Dev->LeConnectDeadline = KeQueryInterruptTime() / 10000 + LE_CONNECT_TIMEOUT_MS;
    DPRINT1("BTHMINI: LE connecting to %012I64x (%s address)\n", Remote->Address,
            Remote->LeAddressType ? "random" : "public");
    return STATUS_SUCCESS;
}

NTSTATUS
BthLeConnect(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_REMOTE Remote)
{
    NTSTATUS Status;

    if (Remote->LeConnected)
        return STATUS_SUCCESS;
    if (Remote->LeConnecting)
        return STATUS_PENDING;
    if (!Dev->LeEnabled)
        return STATUS_NOT_SUPPORTED;
    if (Dev->LeConnectPending)
        return STATUS_DEVICE_BUSY;

    Dev->LeConnectPending = Remote;
    Remote->LeConnecting = TRUE;
    if (Dev->LeInitiator == LE_INITIATOR_ACCEPT_LIST)
    {
        BthLeCancelConnection(Dev);
        return STATUS_PENDING;
    }
    Status = BthLeStartDirect(Dev);
    if (!NT_SUCCESS(Status))
    {
        Dev->LeConnectPending = NULL;
        Remote->LeConnecting = FALSE;
        return Status;
    }
    return STATUS_PENDING;
}

VOID
BthLeBondsChanged(
    _In_ PBTHMINI_DEVICE Dev)
{
    Dev->LeAcceptListDirty = TRUE;
    Dev->LeAutoConnectAfter = 0;
    if (Dev->LeInitiator == LE_INITIATOR_ACCEPT_LIST)
        BthLeCancelConnection(Dev);
}

static
VOID
BthLeAutoConnect(
    _In_ PBTHMINI_DEVICE Dev)
{
    PLIST_ENTRY Entry;
    BOOLEAN Wanted = FALSE;
    ULONG Added = 0;

    if (!Dev->LeEnabled || !Dev->LeAcceptListSize || KeQueryInterruptTime() / 10000 < Dev->LeAutoConnectAfter)
        return;
    for (Entry = Dev->Remotes.Flink; Entry != &Dev->Remotes; Entry = Entry->Flink)
    {
        PBTHMINI_REMOTE Remote = CONTAINING_RECORD(Entry, BTHMINI_REMOTE, Link);

        if (Remote->HasLeKey && !Remote->LeConnected)
            Wanted = TRUE;
    }
    if (!Wanted)
        return;
    if (Dev->LeAcceptListDirty && Dev->LePrivacy && Dev->LeScanning)
        return;
    if (Dev->LeAcceptListDirty && Dev->LePrivacy)
    {
        UCHAR Disable = 0, Enable = 1;
        ULONG Resolving = 0;

        BthHciQueueCommand(Dev, HCI_LE_SET_ADDRESS_RESOLUTION, &Disable, 1);
        BthHciQueueCommand(Dev, HCI_LE_CLEAR_RESOLVING_LIST, NULL, 0);
        for (Entry = Dev->Remotes.Flink; Entry != &Dev->Remotes && Resolving < Dev->LeResolvingListSize;
             Entry = Entry->Flink)
        {
            PBTHMINI_REMOTE Remote = CONTAINING_RECORD(Entry, BTHMINI_REMOTE, Link);
            UCHAR Parameters[39];

            if (!Remote->HasLeKey || !Remote->HasLeIrk)
                continue;
            Parameters[0] = Remote->LeAddressType;
            BthWriteAddress(Parameters + 1, Remote->Address);
            RtlCopyMemory(Parameters + 7, Remote->LeIrk, 16);
            RtlZeroMemory(Parameters + 23, 16);
            BthHciQueueCommand(Dev, HCI_LE_ADD_TO_RESOLVING_LIST, Parameters, sizeof(Parameters));
            if (Dev->SupportedCommands[39] & 0x04)
            {
                Parameters[7] = LE_PRIVACY_MODE_DEVICE;
                BthHciQueueCommand(Dev, HCI_LE_SET_PRIVACY_MODE, Parameters, 8);
            }
            Resolving++;
        }
        BthHciQueueCommand(Dev, HCI_LE_SET_ADDRESS_RESOLUTION, &Enable, 1);
    }
    if (Dev->LeAcceptListDirty)
    {
        BthHciQueueCommand(Dev, HCI_LE_CLEAR_ACCEPT_LIST, NULL, 0);
        for (Entry = Dev->Remotes.Flink; Entry != &Dev->Remotes && Added < Dev->LeAcceptListSize; Entry = Entry->Flink)
        {
            PBTHMINI_REMOTE Remote = CONTAINING_RECORD(Entry, BTHMINI_REMOTE, Link);
            UCHAR Parameters[7];

            if (!Remote->HasLeKey)
                continue;
            Parameters[0] = Remote->LeAddressType;
            BthWriteAddress(Parameters + 1, Remote->Address);
            BthHciQueueCommand(Dev, HCI_LE_ADD_TO_ACCEPT_LIST, Parameters, sizeof(Parameters));
            Added++;
        }
        Dev->LeAcceptListDirty = FALSE;
    }
    if (NT_SUCCESS(BthLeCreateConnection(Dev, NULL)))
        Dev->LeInitiator = LE_INITIATOR_ACCEPT_LIST;
}

VOID
BthLeDisconnect(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_REMOTE Remote)
{
    UCHAR Parameters[3];

    if (!Remote->LeConnected)
        return;
    BthLePut16(Parameters, Remote->LeHandle);
    Parameters[2] = HCI_REMOTE_USER_TERMINATED;
    BthHciQueueCommand(Dev, HCI_DISCONNECT, Parameters, sizeof(Parameters));
}

static
VOID
BthLeConnectFailed(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_REMOTE Remote,
    _In_ NTSTATUS Status)
{
    Remote->LeConnecting = FALSE;
    if (Dev->LeConnectPending == Remote)
        Dev->LeConnectPending = NULL;
    DPRINT1("BTHMINI: LE connection to %012I64x failed 0x%08lx\n", Remote->Address, Status);
    BthSmpLinkDown(Dev, Remote, Status);
    BthGattLinkDown(Dev, Remote, Status);
}

VOID
BthLeCommandFailed(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ USHORT Opcode)
{
    UCHAR Initiator = Dev->LeInitiator;

    if (Opcode != HCI_LE_CREATE_CONNECTION && Opcode != HCI_LE_EXTENDED_CREATE_CONNECTION)
        return;
    Dev->LeInitiator = LE_INITIATOR_IDLE;
    Dev->LeConnectCancelled = FALSE;
    if (Initiator == LE_INITIATOR_DIRECT && Dev->LeConnectPending)
        BthLeConnectFailed(Dev, Dev->LeConnectPending, STATUS_UNSUCCESSFUL);
    else if (Initiator == LE_INITIATOR_ACCEPT_LIST)
        Dev->LeAutoConnectAfter = KeQueryInterruptTime() / 10000 + LE_AUTO_CONNECT_RETRY_MS;
}

VOID
BthLeTick(
    _In_ PBTHMINI_DEVICE Dev)
{
    if (Dev->LeInitiator == LE_INITIATOR_DIRECT && KeQueryInterruptTime() / 10000 >= Dev->LeConnectDeadline)
        BthLeCancelConnection(Dev);
    if (Dev->LeInitiator == LE_INITIATOR_IDLE && Dev->LeConnectPending && !NT_SUCCESS(BthLeStartDirect(Dev)))
        BthLeConnectFailed(Dev, Dev->LeConnectPending, STATUS_UNSUCCESSFUL);
    if (Dev->LeInitiator == LE_INITIATOR_IDLE && !Dev->LeConnectPending)
        BthLeAutoConnect(Dev);
    BthSmpTick(Dev);
    BthGattTick(Dev);
}

static
VOID
BthLeConnectionComplete(
    _In_ PBTHMINI_DEVICE Dev,
    _In_reads_bytes_(Length) const UCHAR *Parameters,
    _In_ ULONG Length)
{
    UCHAR Initiator = Dev->LeInitiator;
    PBTHMINI_REMOTE Remote;
    ULONG Previous;

    if (Length < 11)
        return;
    Dev->LeInitiator = LE_INITIATOR_IDLE;
    Dev->LeConnectCancelled = FALSE;
    if (Parameters[0])
    {
        if (Initiator == LE_INITIATOR_DIRECT && Dev->LeConnectPending)
            BthLeConnectFailed(Dev, Dev->LeConnectPending, Parameters[0] == 0x02 ? STATUS_IO_TIMEOUT : STATUS_UNSUCCESSFUL);
        else if (Initiator == LE_INITIATOR_ACCEPT_LIST && Parameters[0] != 0x02)
            Dev->LeAutoConnectAfter = KeQueryInterruptTime() / 10000 + LE_AUTO_CONNECT_RETRY_MS;
        return;
    }
    Remote = BthRadioFindRemote(Dev, BthReadAddress(Parameters + 5));
    if (!Remote)
        Remote = BthRadioGetRemote(Dev, BthReadAddress(Parameters + 5));
    if (!Remote)
        return;
    if (Dev->LeConnectPending == Remote)
        Dev->LeConnectPending = NULL;
    Previous = Remote->Flags;
    Remote->LeHandle = BthLeGet16(Parameters + 1) & 0x0FFF;
    Remote->LeAddressType = Parameters[4] & 0x01;
    Remote->LeConnected = TRUE;
    Remote->LeConnecting = FALSE;
    Remote->LeEncrypted = FALSE;
    Remote->Flags |= BDIF_LE | BDIF_LE_CONNECTED;
    if (Parameters[3])
        Remote->Flags |= BDIF_CONNECTION_INBOUND;
    BthRadioReportInRange(Dev, Remote, Previous);
    DPRINT1("BTHMINI: LE connected to %012I64x, handle 0x%03x, role %u\n",
            Remote->Address, Remote->LeHandle, Parameters[3]);
    BthSmpLinkUp(Dev, Remote);
    if (!Remote->Smp)
        BthGattLinkReady(Dev, Remote);
}

static
VOID
BthLeSignaling(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_REMOTE Remote,
    _In_reads_bytes_(Length) const UCHAR *Data,
    _In_ ULONG Length)
{
    UCHAR Response[14];
    USHORT CommandLength;

    if (Length < 4)
        return;
    CommandLength = BthLeGet16(Data + 2);
    if (4 + (ULONG)CommandLength > Length)
        return;
    Response[1] = Data[1];
    switch (Data[0])
    {
        case LE_SIGNAL_PARAMETER_UPDATE_REQUEST:
        {
            USHORT Minimum, Maximum, Latency, Timeout;
            BOOLEAN Accept;

            if (CommandLength < 8)
                return;
            Minimum = BthLeGet16(Data + 4);
            Maximum = BthLeGet16(Data + 6);
            Latency = BthLeGet16(Data + 8);
            Timeout = BthLeGet16(Data + 10);
            Accept = Minimum >= 6 && Minimum <= Maximum && Maximum <= 3200 && Latency <= 499 &&
                     Timeout >= 10 && Timeout <= 3200 && (ULONG)Timeout * 4 > (ULONG)Maximum * (Latency + 1);
            Response[0] = LE_SIGNAL_PARAMETER_UPDATE_RESPONSE;
            BthLePut16(Response + 2, 2);
            BthLePut16(Response + 4, Accept ? 0 : 1);
            BthL2capSendLe(Dev, Remote, LE_CID_SIGNALING, Response, 6);
            if (Accept)
            {
                BthLePut16(Response, Remote->LeHandle);
                RtlCopyMemory(Response + 2, Data + 4, 8);
                BthLePut16(Response + 10, 0);
                BthLePut16(Response + 12, 0);
                BthHciQueueCommand(Dev, HCI_LE_CONNECTION_UPDATE, Response, 14);
            }
            break;
        }

        case LE_SIGNAL_CREDIT_CONNECT_REQUEST:
            Response[0] = LE_SIGNAL_CREDIT_CONNECT_RESPONSE;
            BthLePut16(Response + 2, 10);
            RtlZeroMemory(Response + 4, 10);
            BthLePut16(Response + 12, 0x0002);
            BthL2capSendLe(Dev, Remote, LE_CID_SIGNALING, Response, 14);
            break;

        case LE_SIGNAL_COMMAND_REJECT:
        case LE_SIGNAL_PARAMETER_UPDATE_RESPONSE:
            break;

        default:
            Response[0] = LE_SIGNAL_COMMAND_REJECT;
            BthLePut16(Response + 2, 2);
            BthLePut16(Response + 4, 0);
            BthL2capSendLe(Dev, Remote, LE_CID_SIGNALING, Response, 6);
            break;
    }
}

VOID
BthLeFrame(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_REMOTE Remote,
    _In_ USHORT Cid,
    _In_reads_bytes_(Length) const UCHAR *Data,
    _In_ ULONG Length)
{
    switch (Cid)
    {
        case LE_CID_ATT:
            BthGattReceive(Dev, Remote, Data, Length);
            break;
        case LE_CID_SIGNALING:
            BthLeSignaling(Dev, Remote, Data, Length);
            break;
        case LE_CID_SMP:
            BthSmpReceive(Dev, Remote, Data, Length);
            break;
        default:
            break;
    }
}

BOOLEAN
BthLeLinkEvent(
    _In_ PBTHMINI_DEVICE Dev,
    _In_reads_bytes_(Length) const UCHAR *Event,
    _In_ ULONG Length)
{
    const UCHAR *Parameters = Event + 2;
    PBTHMINI_REMOTE Remote;
    ULONG Previous;

    if (Length < 5)
        return FALSE;
    switch (Event[0])
    {
        case HCI_EV_DISCONNECTION_COMPLETE:
            if (Length < 6 || Parameters[0])
                return FALSE;
            Remote = BthLeFindHandle(Dev, BthLeGet16(Parameters + 1) & 0x0FFF);
            if (!Remote)
                return FALSE;
            Previous = Remote->Flags;
            BthL2capLeLinkDown(Dev, Remote);
            Remote->LeConnected = FALSE;
            Remote->LeEncrypted = FALSE;
            Remote->Flags &= ~(BDIF_LE_CONNECTED | BDIF_CONNECTION_INBOUND);
            BthRadioReportInRange(Dev, Remote, Previous);
            DPRINT1("BTHMINI: LE disconnected from %012I64x, reason 0x%02x\n", Remote->Address, Parameters[3]);
            BthSmpLinkDown(Dev, Remote, STATUS_REMOTE_DISCONNECT);
            BthGattLinkDown(Dev, Remote, STATUS_REMOTE_DISCONNECT);
            return TRUE;

        case HCI_EV_ENCRYPTION_CHANGE:
            if (Length < 6)
                return FALSE;
            Remote = BthLeFindHandle(Dev, BthLeGet16(Parameters + 1) & 0x0FFF);
            if (!Remote)
                return FALSE;
            Remote->LeEncrypted = !Parameters[0] && Parameters[3];
            BthSmpEncryptionChanged(Dev, Remote, Remote->LeEncrypted ? STATUS_SUCCESS : STATUS_ACCESS_DENIED);
            return TRUE;

        case HCI_EV_ENCRYPTION_KEY_REFRESH_LE:
            Remote = BthLeFindHandle(Dev, BthLeGet16(Parameters + 1) & 0x0FFF);
            if (!Remote)
                return FALSE;
            if (!Parameters[0])
                Remote->LeEncrypted = TRUE;
            BthSmpEncryptionChanged(Dev, Remote, Parameters[0] ? STATUS_ACCESS_DENIED : STATUS_SUCCESS);
            return TRUE;

        default:
            return FALSE;
    }
}

BOOLEAN
BthLeEvent(
    _In_ PBTHMINI_DEVICE Dev,
    _In_reads_bytes_(Length) const UCHAR *Event,
    _In_ ULONG Length)
{
    const UCHAR *Parameters = Event + 3;
    ULONG Remaining, Index, Count;

    if (Event[0] != HCI_EV_LE_META || Length < 3)
        return FALSE;
    Remaining = Length - 3;
    switch (Event[2])
    {
        case HCI_LE_ADVERTISING_REPORT:
            if (!Remaining)
                break;
            Count = Parameters[0];
            Parameters++;
            Remaining--;
            for (Index = 0; Index < Count && Remaining >= 9; Index++)
            {
                ULONG DataLength = Parameters[8];

                if (Remaining < 9 + DataLength + 1)
                    break;
                BthLeReport(Dev, Parameters[1], Parameters + 2, Parameters[0] <= 0x01, Parameters + 9, DataLength);
                Parameters += 10 + DataLength;
                Remaining -= 10 + DataLength;
            }
            break;

        case HCI_LE_EXTENDED_ADVERTISING_REPORT:
            if (!Remaining)
                break;
            Count = Parameters[0];
            Parameters++;
            Remaining--;
            for (Index = 0; Index < Count && Remaining >= 24; Index++)
            {
                ULONG DataLength = Parameters[23];
                USHORT EventType = (USHORT)(Parameters[0] | (Parameters[1] << 8));

                if (Remaining < 24 + DataLength)
                    break;
                BthLeReport(Dev, Parameters[2], Parameters + 3, (EventType & 0x0001) != 0, Parameters + 24, DataLength);
                Parameters += 24 + DataLength;
                Remaining -= 24 + DataLength;
            }
            break;

        case HCI_LE_CONNECTION_COMPLETE:
        case HCI_LE_ENHANCED_CONNECTION_COMPLETE:
            BthLeConnectionComplete(Dev, Parameters, Remaining);
            break;

        case HCI_LE_LTK_REQUEST:
            if (Remaining >= 2)
                BthHciQueueCommand(Dev, HCI_LE_LTK_NEGATIVE_REPLY, Parameters, 2);
            break;

        case HCI_LE_REMOTE_PARAMETER_REQUEST:
            if (Remaining >= 10)
            {
                UCHAR Reply[14];

                RtlCopyMemory(Reply, Parameters, 10);
                BthLePut16(Reply + 10, 0);
                BthLePut16(Reply + 12, 0);
                BthHciQueueCommand(Dev, HCI_LE_REMOTE_PARAMETER_REPLY, Reply, 14);
            }
            break;

        default:
            break;
    }
    return TRUE;
}
