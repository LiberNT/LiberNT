/*
 * PROJECT:     ReactOS DC21x4 Driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Miniport information callbacks
 * COPYRIGHT:   Copyright 2023 Dmitry Borisov <di.sean@protonmail.com>
 */

/* INCLUDES *******************************************************************/

#include "dc21x4.h"

#include <debug.h>

/* GLOBALS ********************************************************************/

static const NDIS_OID DcpSupportedOidList[] =
{
    OID_GEN_SUPPORTED_LIST,
    OID_GEN_CURRENT_PACKET_FILTER,
    OID_GEN_HARDWARE_STATUS,
    OID_GEN_MEDIA_SUPPORTED,
    OID_GEN_MEDIA_IN_USE,
    OID_GEN_MAXIMUM_LOOKAHEAD,
    OID_GEN_MAXIMUM_FRAME_SIZE,
    OID_GEN_MAXIMUM_SEND_PACKETS,
    OID_GEN_LINK_SPEED,
    OID_GEN_TRANSMIT_BUFFER_SPACE,
    OID_GEN_RECEIVE_BUFFER_SPACE,
    OID_GEN_RECEIVE_BLOCK_SIZE,
    OID_GEN_TRANSMIT_BLOCK_SIZE,
    OID_GEN_VENDOR_ID,
    OID_GEN_VENDOR_DESCRIPTION,
    OID_GEN_VENDOR_DRIVER_VERSION,
    OID_GEN_CURRENT_LOOKAHEAD,
    OID_GEN_DRIVER_VERSION,
    OID_GEN_MAXIMUM_TOTAL_SIZE,
    OID_GEN_MAC_OPTIONS,
    OID_GEN_MEDIA_CONNECT_STATUS,
    OID_802_3_PERMANENT_ADDRESS,
    OID_802_3_CURRENT_ADDRESS,
    OID_802_3_MULTICAST_LIST,
    OID_802_3_MAXIMUM_LIST_SIZE,
    OID_GEN_PHYSICAL_MEDIUM,

    /* Statistics */
    OID_GEN_XMIT_OK,
    OID_GEN_RCV_OK,
    OID_GEN_XMIT_ERROR,
    OID_GEN_RCV_ERROR,
    OID_GEN_RCV_NO_BUFFER,
    OID_GEN_DIRECTED_FRAMES_RCV,
    OID_GEN_MULTICAST_FRAMES_RCV,
    OID_GEN_BROADCAST_FRAMES_RCV,
    OID_GEN_RCV_CRC_ERROR,
    OID_GEN_TRANSMIT_QUEUE_LENGTH,
    OID_802_3_RCV_ERROR_ALIGNMENT,
    OID_802_3_XMIT_ONE_COLLISION,
    OID_802_3_XMIT_MORE_COLLISIONS,
    OID_802_3_XMIT_DEFERRED,
    OID_802_3_XMIT_MAX_COLLISIONS,
    OID_802_3_RCV_OVERRUN,
    OID_802_3_XMIT_UNDERRUN,
    OID_802_3_XMIT_HEARTBEAT_FAILURE,
    OID_802_3_XMIT_TIMES_CRS_LOST,
    OID_802_3_XMIT_LATE_COLLISIONS,
    OID_GEN_STATISTICS,
    OID_GEN_INTERRUPT_MODERATION,

    /* Power management */
    OID_PNP_CAPABILITIES,
    OID_PNP_SET_POWER,
    OID_PNP_QUERY_POWER,
    OID_PNP_ADD_WAKE_UP_PATTERN,
    OID_PNP_REMOVE_WAKE_UP_PATTERN,
    OID_PNP_ENABLE_WAKE_UP
};

/* FUNCTIONS ******************************************************************/

static
VOID
DcQueryStatisticCounter(
    _In_ PDC21X4_ADAPTER Adapter,
    _In_ NDIS_OID Oid,
    _Out_ PULONG64 Counter)
{
    /* When there is no workaround, this function is used to read the hardware RX counters */
    if (!(Adapter->Features & DC_NEED_RX_OVERFLOW_WORKAROUND))
    {
        ULONG RxCounters;

        /*
         * Read the RX missed frame counter. Note that the RX overflow counter is not supported
         * on older chips without the workaround enabled and reads will return 0xFFFE****.
         */
        RxCounters = DC_READ(Adapter, DcCsr8_RxCounters);

        Adapter->Statistics.ReceiveNoBuffers += RxCounters & DC_COUNTER_RX_NO_BUFFER_MASK;
    }

    switch (Oid)
    {
        case OID_GEN_XMIT_OK:
            *Counter = Adapter->Statistics.TransmitOk;
            break;
        case OID_GEN_RCV_OK:
            *Counter = Adapter->Statistics.ReceiveOk;
            break;
        case OID_GEN_XMIT_ERROR:
            *Counter = Adapter->Statistics.TransmitErrors;
            break;
        case OID_GEN_RCV_ERROR:
            *Counter = Adapter->Statistics.ReceiveErrors;
            break;
        case OID_GEN_RCV_NO_BUFFER:
            *Counter = Adapter->Statistics.ReceiveNoBuffers;
            break;
        case OID_GEN_DIRECTED_FRAMES_RCV:
            *Counter = Adapter->Statistics.ReceiveUnicast;
            break;
        case OID_GEN_MULTICAST_FRAMES_RCV:
            *Counter = Adapter->Statistics.ReceiveMulticast;
            break;
        case OID_GEN_BROADCAST_FRAMES_RCV:
            *Counter = Adapter->Statistics.ReceiveBroadcast;
            break;
        case OID_GEN_RCV_CRC_ERROR:
            *Counter = Adapter->Statistics.ReceiveCrcErrors;
            break;
        case OID_802_3_RCV_ERROR_ALIGNMENT:
            *Counter = Adapter->Statistics.ReceiveAlignmentErrors;
            break;
        case OID_802_3_XMIT_ONE_COLLISION:
            *Counter = Adapter->Statistics.TransmitOneRetry;
            break;
        case OID_802_3_XMIT_MORE_COLLISIONS:
            *Counter = Adapter->Statistics.TransmitMoreCollisions;
            break;
        case OID_802_3_XMIT_DEFERRED:
            *Counter = Adapter->Statistics.TransmitDeferred;
            break;
        case OID_802_3_XMIT_MAX_COLLISIONS:
            *Counter = Adapter->Statistics.TransmitExcessiveCollisions;
            break;
        case OID_802_3_RCV_OVERRUN:
            *Counter = Adapter->Statistics.ReceiveOverrunErrors;
            break;
        case OID_802_3_XMIT_UNDERRUN:
            *Counter = Adapter->Statistics.TransmitUnderrunErrors;
            break;
        case OID_802_3_XMIT_HEARTBEAT_FAILURE:
            *Counter = Adapter->Statistics.TransmitHeartbeatErrors;
            break;
        case OID_802_3_XMIT_TIMES_CRS_LOST:
            *Counter = Adapter->Statistics.TransmitLostCarrierSense;
            break;
        case OID_802_3_XMIT_LATE_COLLISIONS:
            *Counter = Adapter->Statistics.TransmitLateCollisions;
            break;

        default:
            ASSERT(FALSE);
            UNREACHABLE;
            break;
    }
}

ULONG
DcGetLinkSpeed(
    _In_ PDC21X4_ADAPTER Adapter)
{
    ULONG LinkSpeedMbps;

    switch (Adapter->MediaNumber)
    {
        case MEDIA_HMR:
            LinkSpeedMbps = 1;
            break;

        case MEDIA_10T:
        case MEDIA_BNC:
        case MEDIA_AUI:
        case MEDIA_10T_FD:
            LinkSpeedMbps = 10;
            break;

        case MEDIA_100TX_HD:
        case MEDIA_100TX_FD:
        case MEDIA_100T4:
        case MEDIA_100FX_HD:
        case MEDIA_100FX_FD:
            LinkSpeedMbps = 100;
            break;

        case MEDIA_MII:
            LinkSpeedMbps = Adapter->LinkSpeedMbps;
            break;

        default:
            ASSERT(FALSE);
            UNREACHABLE;
            break;
    }

    return LinkSpeedMbps;
}

NDIS_STATUS
NTAPI
DcQueryInformation(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ NDIS_OID Oid,
    _In_ PVOID InformationBuffer,
    _In_ ULONG InformationBufferLength,
    _Out_ PULONG BytesWritten,
    _Out_ PULONG BytesNeeded)
{
    PDC21X4_ADAPTER Adapter = (PDC21X4_ADAPTER)MiniportAdapterContext;
    NDIS_STATUS Status = NDIS_STATUS_SUCCESS;
    ULONG InfoLength;
    PVOID InfoPtr;
    union _GENERIC_INFORMATION
    {
        USHORT Ushort;
        ULONG Ulong;
        ULONG64 Ulong64;
        NDIS_MEDIUM Medium;
        NDIS_HARDWARE_STATUS Status;
        NDIS_DEVICE_POWER_STATE PowerState;
        NDIS_STATISTICS_INFO Statistics;
        NDIS_INTERRUPT_MODERATION_PARAMETERS Moderation;
    } GenericInfo;

    InfoLength = sizeof(ULONG);
    InfoPtr = &GenericInfo;

    switch (Oid)
    {
        case OID_GEN_SUPPORTED_LIST:
            InfoPtr = (PVOID)&DcpSupportedOidList;
            InfoLength = sizeof(DcpSupportedOidList);
            break;

        case OID_GEN_CURRENT_PACKET_FILTER:
            GenericInfo.Ulong = Adapter->PacketFilter;
            break;

        case OID_802_3_MULTICAST_LIST:
            InfoPtr = Adapter->MulticastList;
            InfoLength = Adapter->MulticastCount * ETH_LENGTH_OF_ADDRESS;
            break;

        case OID_802_3_MAXIMUM_LIST_SIZE:
            GenericInfo.Ulong = Adapter->MulticastMaxEntries;
            break;

        case OID_GEN_MEDIA_SUPPORTED:
        case OID_GEN_MEDIA_IN_USE:
        {
            GenericInfo.Medium = NdisMedium802_3;
            InfoLength = sizeof(NDIS_MEDIUM);
            break;
        }

        case OID_GEN_HARDWARE_STATUS:
        {
            ULONG InterruptStatus;

            /* NOTE: Reading the status register has no effect on the events */
            InterruptStatus = DC_READ(Adapter, DcCsr5_Status);

            /* Inserted into the motherboard */
            if (InterruptStatus != 0xFFFFFFFF)
                GenericInfo.Status = NdisHardwareStatusReady;
            else
                GenericInfo.Status = NdisHardwareStatusNotReady;

            InfoLength = sizeof(NDIS_HARDWARE_STATUS);
            break;
        }

        case OID_GEN_MAXIMUM_FRAME_SIZE:
        case OID_GEN_MAXIMUM_LOOKAHEAD:
        case OID_GEN_CURRENT_LOOKAHEAD:
            GenericInfo.Ulong = DC_MAXIMUM_FRAME_SIZE - DC_ETHERNET_HEADER_SIZE;
            break;

        case OID_GEN_MAXIMUM_TOTAL_SIZE:
            GenericInfo.Ulong = DC_MAXIMUM_FRAME_SIZE;
            break;

        case OID_GEN_TRANSMIT_BLOCK_SIZE:
            GenericInfo.Ulong = DC_TRANSMIT_BLOCK_SIZE;
            break;

        case OID_GEN_TRANSMIT_BUFFER_SPACE:
            GenericInfo.Ulong = DC_TRANSMIT_BLOCK_SIZE * DC_TRANSMIT_BLOCKS;
            break;

        case OID_GEN_RECEIVE_BLOCK_SIZE:
            GenericInfo.Ulong = DC_RECEIVE_BLOCK_SIZE;
            break;

        case OID_GEN_RECEIVE_BUFFER_SPACE:
            GenericInfo.Ulong = DC_RECEIVE_BLOCK_SIZE * Adapter->RcbCount;
            break;

        case OID_GEN_LINK_SPEED:
            GenericInfo.Ulong = DcGetLinkSpeed(Adapter) * 10000;
            break;

        case OID_GEN_VENDOR_ID:
            GenericInfo.Ulong = 0;
            GenericInfo.Ulong |= (Adapter->PermanentMacAddress[0] << 16);
            GenericInfo.Ulong |= (Adapter->PermanentMacAddress[1] << 8);
            GenericInfo.Ulong |= (Adapter->PermanentMacAddress[2] & 0xFF);
            break;

        case OID_GEN_VENDOR_DESCRIPTION:
        {
            static const CHAR VendorDesc[] = "DC21x4 compatible Ethernet Adapter";
            InfoPtr = (PVOID)&VendorDesc;
            InfoLength = sizeof(VendorDesc);
            break;
        }

        case OID_GEN_VENDOR_DRIVER_VERSION:
            /* 1.0.0 */
            GenericInfo.Ulong = 0x100;
            break;

        case OID_GEN_DRIVER_VERSION:
        {
            InfoLength = sizeof(USHORT);
            GenericInfo.Ushort = (NDIS_MINIPORT_MAJOR_VERSION << 8) | NDIS_MINIPORT_MINOR_VERSION;
            break;
        }

        case OID_GEN_MAXIMUM_SEND_PACKETS:
            GenericInfo.Ulong = DC_TRANSMIT_BLOCKS - DC_TCB_RESERVE;
            break;

        case OID_GEN_MAC_OPTIONS:
            GenericInfo.Ulong = NDIS_MAC_OPTION_COPY_LOOKAHEAD_DATA |
                                NDIS_MAC_OPTION_TRANSFERS_NOT_PEND |
                                NDIS_MAC_OPTION_NO_LOOPBACK;
            break;

        case OID_GEN_MEDIA_CONNECT_STATUS:
            GenericInfo.Ulong = Adapter->LinkUp ? NdisMediaStateConnected
                                                : NdisMediaStateDisconnected;
            break;

        case OID_802_3_PERMANENT_ADDRESS:
            InfoPtr = Adapter->PermanentMacAddress;
            InfoLength = ETH_LENGTH_OF_ADDRESS;
            break;

        case OID_802_3_CURRENT_ADDRESS:
            InfoPtr = Adapter->CurrentMacAddress;
            InfoLength = ETH_LENGTH_OF_ADDRESS;
            break;

        case OID_GEN_PHYSICAL_MEDIUM:
            GenericInfo.Ulong = NdisPhysicalMedium802_3;
            break;

        case OID_GEN_XMIT_OK:
        case OID_GEN_RCV_OK:
        case OID_GEN_XMIT_ERROR:
        case OID_GEN_RCV_ERROR:
        case OID_GEN_RCV_NO_BUFFER:
        case OID_GEN_DIRECTED_FRAMES_RCV:
        case OID_GEN_MULTICAST_FRAMES_RCV:
        case OID_GEN_BROADCAST_FRAMES_RCV:
        case OID_GEN_RCV_CRC_ERROR:
        case OID_802_3_RCV_ERROR_ALIGNMENT:
        case OID_802_3_XMIT_ONE_COLLISION:
        case OID_802_3_XMIT_MORE_COLLISIONS:
        case OID_802_3_XMIT_DEFERRED:
        case OID_802_3_XMIT_MAX_COLLISIONS:
        case OID_802_3_RCV_OVERRUN:
        case OID_802_3_XMIT_UNDERRUN:
        case OID_802_3_XMIT_HEARTBEAT_FAILURE:
        case OID_802_3_XMIT_TIMES_CRS_LOST:
        case OID_802_3_XMIT_LATE_COLLISIONS:
        {
            DcQueryStatisticCounter(Adapter, Oid, &GenericInfo.Ulong64);

            *BytesNeeded = sizeof(ULONG64);
            if (InformationBufferLength < sizeof(ULONG))
            {
                *BytesWritten = 0;
                return NDIS_STATUS_BUFFER_TOO_SHORT;
            }
            if (InformationBufferLength >= sizeof(ULONG64))
            {
                *BytesWritten = sizeof(ULONG64);
                NdisMoveMemory(InformationBuffer, InfoPtr, sizeof(ULONG64));
            }
            else
            {
                *BytesWritten = sizeof(ULONG);
                NdisMoveMemory(InformationBuffer, InfoPtr, sizeof(ULONG));
            }

            return NDIS_STATUS_SUCCESS;
        }

        case OID_GEN_TRANSMIT_QUEUE_LENGTH:
            GenericInfo.Ulong = (DC_TRANSMIT_BLOCKS - DC_TCB_RESERVE) - Adapter->TcbSlots;
            break;

        case OID_GEN_STATISTICS:
        {
            ULONG64 NoBuffers;

            DcQueryStatisticCounter(Adapter, OID_GEN_RCV_NO_BUFFER, &NoBuffers);

            NdisZeroMemory(&GenericInfo.Statistics, sizeof(GenericInfo.Statistics));
            GenericInfo.Statistics.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
            GenericInfo.Statistics.Header.Revision = NDIS_STATISTICS_INFO_REVISION_1;
            GenericInfo.Statistics.Header.Size = NDIS_SIZEOF_STATISTICS_INFO_REVISION_1;
            GenericInfo.Statistics.SupportedStatistics =
                NDIS_STATISTICS_FLAGS_VALID_DIRECTED_FRAMES_RCV |
                NDIS_STATISTICS_FLAGS_VALID_MULTICAST_FRAMES_RCV |
                NDIS_STATISTICS_FLAGS_VALID_BROADCAST_FRAMES_RCV |
                NDIS_STATISTICS_FLAGS_VALID_RCV_DISCARDS |
                NDIS_STATISTICS_FLAGS_VALID_RCV_ERROR |
                NDIS_STATISTICS_FLAGS_VALID_DIRECTED_FRAMES_XMIT |
                NDIS_STATISTICS_FLAGS_VALID_XMIT_ERROR;
            GenericInfo.Statistics.ifInDiscards = Adapter->Statistics.ReceiveErrors + NoBuffers;
            GenericInfo.Statistics.ifInErrors = Adapter->Statistics.ReceiveErrors;
            GenericInfo.Statistics.ifHCInUcastPkts = Adapter->Statistics.ReceiveUnicast;
            GenericInfo.Statistics.ifHCInMulticastPkts = Adapter->Statistics.ReceiveMulticast;
            GenericInfo.Statistics.ifHCInBroadcastPkts = Adapter->Statistics.ReceiveBroadcast;
            GenericInfo.Statistics.ifHCOutUcastPkts = Adapter->Statistics.TransmitOk;
            GenericInfo.Statistics.ifOutErrors = Adapter->Statistics.TransmitErrors;
            InfoLength = sizeof(GenericInfo.Statistics);
            break;
        }

        case OID_GEN_INTERRUPT_MODERATION:
            NdisZeroMemory(&GenericInfo.Moderation, sizeof(GenericInfo.Moderation));
            GenericInfo.Moderation.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
            GenericInfo.Moderation.Header.Revision = NDIS_INTERRUPT_MODERATION_PARAMETERS_REVISION_1;
            GenericInfo.Moderation.Header.Size = NDIS_SIZEOF_INTERRUPT_MODERATION_PARAMETERS_REVISION_1;
            GenericInfo.Moderation.InterruptModeration = NdisInterruptModerationNotSupported;
            InfoLength = sizeof(GenericInfo.Moderation);
            break;

        case OID_PNP_CAPABILITIES:
        {
            PNDIS_PNP_CAPABILITIES Capabilities;

            InfoLength = sizeof(NDIS_PNP_CAPABILITIES);

            if (InformationBufferLength < InfoLength)
            {
                *BytesWritten = 0;
                *BytesNeeded = InfoLength;
                return NDIS_STATUS_BUFFER_TOO_SHORT;
            }

            if (!(Adapter->Features & DC_HAS_POWER_MANAGEMENT))
                return NDIS_STATUS_NOT_SUPPORTED;

            *BytesWritten = InfoLength;
            *BytesNeeded = 0;

            Capabilities = InformationBuffer;
            Capabilities->WakeUpCapabilities.MinMagicPacketWakeUp = NdisDeviceStateD3;
            Capabilities->WakeUpCapabilities.MinPatternWakeUp = NdisDeviceStateD3;
            Capabilities->WakeUpCapabilities.MinLinkChangeWakeUp = NdisDeviceStateD3;

            return NDIS_STATUS_SUCCESS;
        }

        case OID_PNP_QUERY_POWER:
        {
            if (!(Adapter->Features & DC_HAS_POWER_MANAGEMENT))
                return NDIS_STATUS_NOT_SUPPORTED;

            return NDIS_STATUS_SUCCESS;
        }

        case OID_PNP_ENABLE_WAKE_UP:
        {
            if (!(Adapter->Features & DC_HAS_POWER_MANAGEMENT))
                return NDIS_STATUS_NOT_SUPPORTED;

            GenericInfo.Ulong = Adapter->WakeUpFlags & (NDIS_PNP_WAKE_UP_MAGIC_PACKET |
                                                        NDIS_PNP_WAKE_UP_PATTERN_MATCH |
                                                        NDIS_PNP_WAKE_UP_LINK_CHANGE);
            break;
        }

        default:
            Status = NDIS_STATUS_NOT_SUPPORTED;
            break;
    }

    if (Status == NDIS_STATUS_SUCCESS)
    {
        if (InfoLength > InformationBufferLength)
        {
            *BytesWritten = 0;
            *BytesNeeded = InfoLength;
            Status = NDIS_STATUS_BUFFER_TOO_SHORT;
        }
        else
        {
            NdisMoveMemory(InformationBuffer, InfoPtr, InfoLength);
            *BytesWritten = InfoLength;
            *BytesNeeded = 0;
        }
    }
    else
    {
        *BytesWritten = 0;
        *BytesNeeded = 0;
    }

    return Status;
}

NDIS_STATUS
NTAPI
DcSetInformation(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ NDIS_OID Oid,
    _In_ PVOID InformationBuffer,
    _In_ ULONG InformationBufferLength,
    _Out_ PULONG BytesRead,
    _Out_ PULONG BytesNeeded)
{
    PDC21X4_ADAPTER Adapter = (PDC21X4_ADAPTER)MiniportAdapterContext;
    NDIS_STATUS Status = NDIS_STATUS_SUCCESS;
    ULONG GenericUlong;

    *BytesRead = 0;
    *BytesNeeded = 0;

    switch (Oid)
    {
        case OID_GEN_CURRENT_PACKET_FILTER:
        {
            if (InformationBufferLength < sizeof(ULONG))
            {
                *BytesNeeded = sizeof(ULONG);
                Status = NDIS_STATUS_INVALID_LENGTH;
                break;
            }

            *BytesRead = sizeof(ULONG);
            NdisMoveMemory(&GenericUlong, InformationBuffer, sizeof(ULONG));

            if (GenericUlong & ~DC_PACKET_FILTERS)
            {
                Status = NDIS_STATUS_NOT_SUPPORTED;
                break;
            }

            Status = DcApplyPacketFilter(Adapter, GenericUlong);
            break;
        }

        case OID_802_3_MULTICAST_LIST:
        {
            ULONG Size;

            if (InformationBufferLength % ETH_LENGTH_OF_ADDRESS)
            {
                *BytesNeeded = (InformationBufferLength / ETH_LENGTH_OF_ADDRESS) *
                               ETH_LENGTH_OF_ADDRESS;
                Status = NDIS_STATUS_INVALID_LENGTH;
                break;
            }

            Size = Adapter->MulticastMaxEntries * ETH_LENGTH_OF_ADDRESS;
            if (InformationBufferLength > Size)
            {
                *BytesNeeded = Size;
                Status = NDIS_STATUS_MULTICAST_FULL;
                break;
            }

            *BytesRead = InformationBufferLength;
            NdisMoveMemory(Adapter->MulticastList, InformationBuffer, InformationBufferLength);

            Adapter->MulticastCount = InformationBufferLength / ETH_LENGTH_OF_ADDRESS;

            Status = DcUpdateMulticastList(Adapter);
            break;
        }

        case OID_GEN_CURRENT_LOOKAHEAD:
        {
            if (InformationBufferLength < sizeof(ULONG))
            {
                *BytesNeeded = sizeof(ULONG);
                Status = NDIS_STATUS_INVALID_LENGTH;
                break;
            }

            /* Nothing to do */
            *BytesRead = sizeof(ULONG);
            break;
        }

        case OID_PNP_ENABLE_WAKE_UP:
        {
            if (InformationBufferLength < sizeof(ULONG))
            {
                *BytesNeeded = sizeof(ULONG);
                Status = NDIS_STATUS_INVALID_LENGTH;
                break;
            }

            if (!(Adapter->Features & DC_HAS_POWER_MANAGEMENT))
            {
                return NDIS_STATUS_NOT_SUPPORTED;
            }

            *BytesRead = sizeof(ULONG);
            NdisMoveMemory(&GenericUlong, InformationBuffer, sizeof(ULONG));

            Adapter->WakeUpFlags = GenericUlong;
            break;
        }

        case OID_PNP_ADD_WAKE_UP_PATTERN:
        {
            if (InformationBufferLength < sizeof(NDIS_PM_PACKET_PATTERN))
            {
                *BytesNeeded = sizeof(NDIS_PM_PACKET_PATTERN);
                Status = NDIS_STATUS_INVALID_LENGTH;
                break;
            }

            if (!(Adapter->Features & DC_HAS_POWER_MANAGEMENT))
            {
                return NDIS_STATUS_NOT_SUPPORTED;
            }

            *BytesRead = sizeof(NDIS_PM_PACKET_PATTERN);

            Status = DcAddWakeUpPattern(Adapter, InformationBuffer);
            break;
        }

        case OID_PNP_REMOVE_WAKE_UP_PATTERN:
        {
            if (InformationBufferLength < sizeof(NDIS_PM_PACKET_PATTERN))
            {
                *BytesNeeded = sizeof(NDIS_PM_PACKET_PATTERN);
                Status = NDIS_STATUS_INVALID_LENGTH;
                break;
            }

            if (!(Adapter->Features & DC_HAS_POWER_MANAGEMENT))
            {
                return NDIS_STATUS_NOT_SUPPORTED;
            }

            *BytesRead = sizeof(NDIS_PM_PACKET_PATTERN);

            Status = DcRemoveWakeUpPattern(Adapter, InformationBuffer);
            break;
        }

        case OID_PNP_SET_POWER:
        {
            if (InformationBufferLength < sizeof(NDIS_DEVICE_POWER_STATE))
            {
                *BytesNeeded = sizeof(NDIS_DEVICE_POWER_STATE);
                Status = NDIS_STATUS_INVALID_LENGTH;
                break;
            }

            if (!(Adapter->Features & DC_HAS_POWER_MANAGEMENT))
            {
                return NDIS_STATUS_NOT_SUPPORTED;
            }

            *BytesRead = sizeof(ULONG);
            NdisMoveMemory(&GenericUlong, InformationBuffer, sizeof(ULONG));

            if (GenericUlong < NdisDeviceStateD0 || GenericUlong > NdisDeviceStateD3)
            {
                ASSERT(FALSE);
                Status = NDIS_STATUS_INVALID_DATA;
                break;
            }

            DcSetPower(Adapter, GenericUlong);
            break;
        }

        default:
            Status = NDIS_STATUS_NOT_SUPPORTED;
            break;
    }

    return Status;
}

NDIS_STATUS
DcSetGeneralAttributes(
    _In_ PDC21X4_ADAPTER Adapter)
{
    NDIS_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES General;
    ULONG64 Speed;

    NdisZeroMemory(&General, sizeof(General));
    General.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES;
    General.Header.Revision = NDIS_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES_REVISION_1;
    General.Header.Size = NDIS_SIZEOF_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES_REVISION_1;
    General.MediaType = NdisMedium802_3;
    General.PhysicalMediumType = NdisPhysicalMedium802_3;
    General.MtuSize = DC_MAXIMUM_FRAME_SIZE - DC_ETHERNET_HEADER_SIZE;
    General.MaxXmitLinkSpeed = 100000000;
    General.MaxRcvLinkSpeed = 100000000;
    if (Adapter->LinkUp)
    {
        Speed = (ULONG64)DcGetLinkSpeed(Adapter) * 1000000;
        General.XmitLinkSpeed = Speed;
        General.RcvLinkSpeed = Speed;
        General.MediaConnectState = MediaConnectStateConnected;
        General.MediaDuplexState = (Adapter->OpMode & DC_OPMODE_FULL_DUPLEX) ?
                                   MediaDuplexStateFull : MediaDuplexStateHalf;
    }
    else
    {
        General.XmitLinkSpeed = NDIS_LINK_SPEED_UNKNOWN;
        General.RcvLinkSpeed = NDIS_LINK_SPEED_UNKNOWN;
        General.MediaConnectState = MediaConnectStateUnknown;
        General.MediaDuplexState = MediaDuplexStateUnknown;
    }
    General.LookaheadSize = DC_MAXIMUM_FRAME_SIZE - DC_ETHERNET_HEADER_SIZE;
    General.MacOptions = NDIS_MAC_OPTION_COPY_LOOKAHEAD_DATA |
                         NDIS_MAC_OPTION_TRANSFERS_NOT_PEND |
                         NDIS_MAC_OPTION_NO_LOOPBACK;
    General.SupportedPacketFilters = DC_PACKET_FILTERS;
    General.MaxMulticastListSize = Adapter->MulticastMaxEntries;
    General.MacAddressLength = ETH_LENGTH_OF_ADDRESS;
    NdisMoveMemory(General.PermanentMacAddress, Adapter->PermanentMacAddress, ETH_LENGTH_OF_ADDRESS);
    NdisMoveMemory(General.CurrentMacAddress, Adapter->CurrentMacAddress, ETH_LENGTH_OF_ADDRESS);
    General.AccessType = NET_IF_ACCESS_BROADCAST;
    General.DirectionType = NET_IF_DIRECTION_SENDRECEIVE;
    General.ConnectionType = NET_IF_CONNECTION_DEDICATED;
    General.IfType = IF_TYPE_ETHERNET_CSMACD;
    General.IfConnectorPresent = TRUE;
    General.SupportedStatistics = NDIS_STATISTICS_XMIT_OK_SUPPORTED |
                                  NDIS_STATISTICS_RCV_OK_SUPPORTED |
                                  NDIS_STATISTICS_XMIT_ERROR_SUPPORTED |
                                  NDIS_STATISTICS_RCV_ERROR_SUPPORTED |
                                  NDIS_STATISTICS_RCV_NO_BUFFER_SUPPORTED |
                                  NDIS_STATISTICS_DIRECTED_FRAMES_RCV_SUPPORTED |
                                  NDIS_STATISTICS_MULTICAST_FRAMES_RCV_SUPPORTED |
                                  NDIS_STATISTICS_BROADCAST_FRAMES_RCV_SUPPORTED |
                                  NDIS_STATISTICS_RCV_CRC_ERROR_SUPPORTED |
                                  NDIS_STATISTICS_TRANSMIT_QUEUE_LENGTH_SUPPORTED |
                                  NDIS_STATISTICS_GEN_STATISTICS_SUPPORTED;
    General.SupportedPauseFunctions = NdisPauseFunctionsUnsupported;
    General.SupportedOidList = (PNDIS_OID)DcpSupportedOidList;
    General.SupportedOidListLength = sizeof(DcpSupportedOidList);

    return NdisMSetMiniportAttributes(Adapter->AdapterHandle,
                                      (PNDIS_MINIPORT_ADAPTER_ATTRIBUTES)&General);
}

VOID
DcCompletePendingOid(
    _In_ PDC21X4_ADAPTER Adapter,
    _In_ NDIS_STATUS Status)
{
    PNDIS_OID_REQUEST OidRequest;

    OidRequest = InterlockedExchangePointer((PVOID*)&Adapter->PendingOidRequest, NULL);
    if (OidRequest)
        NdisMOidRequestComplete(Adapter->AdapterHandle, OidRequest, Status);
}

NDIS_STATUS
NTAPI
DcOidRequest(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNDIS_OID_REQUEST OidRequest)
{
    PDC21X4_ADAPTER Adapter = (PDC21X4_ADAPTER)MiniportAdapterContext;
    NDIS_STATUS Status;

    switch (OidRequest->RequestType)
    {
        case NdisRequestQueryInformation:
        case NdisRequestQueryStatistics:
            return DcQueryInformation(MiniportAdapterContext,
                                      OidRequest->DATA.QUERY_INFORMATION.Oid,
                                      OidRequest->DATA.QUERY_INFORMATION.InformationBuffer,
                                      OidRequest->DATA.QUERY_INFORMATION.InformationBufferLength,
                                      (PULONG)&OidRequest->DATA.QUERY_INFORMATION.BytesWritten,
                                      (PULONG)&OidRequest->DATA.QUERY_INFORMATION.BytesNeeded);

        case NdisRequestSetInformation:
            InterlockedExchangePointer((PVOID*)&Adapter->PendingOidRequest, OidRequest);

            Status = DcSetInformation(MiniportAdapterContext,
                                      OidRequest->DATA.SET_INFORMATION.Oid,
                                      OidRequest->DATA.SET_INFORMATION.InformationBuffer,
                                      OidRequest->DATA.SET_INFORMATION.InformationBufferLength,
                                      (PULONG)&OidRequest->DATA.SET_INFORMATION.BytesRead,
                                      (PULONG)&OidRequest->DATA.SET_INFORMATION.BytesNeeded);
            if (Status != NDIS_STATUS_PENDING)
                InterlockedExchangePointer((PVOID*)&Adapter->PendingOidRequest, NULL);

            return Status;

        default:
            return NDIS_STATUS_NOT_SUPPORTED;
    }
}

VOID
NTAPI
DcCancelOidRequest(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PVOID RequestId)
{
    UNREFERENCED_PARAMETER(MiniportAdapterContext);
    UNREFERENCED_PARAMETER(RequestId);
}
