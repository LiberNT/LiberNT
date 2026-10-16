/*
 * PROJECT:     LiberNT VirtIO Network Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     NDIS 6 miniport interface of the NetKVM driver
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include "ndis56common.h"

#define SEND_ENTRY_FLAG_READY       0x0001

typedef struct _tagSendEntry
{
    LIST_ENTRY          list;
    PNET_BUFFER_LIST    nbl;
    PNET_BUFFER         nb;
    NDIS_STATUS         status;
    ULONG               flags;
    ULONG               ipTransferUnit;
    union
    {
        ULONG PriorityDataLong;
        UCHAR PriorityData[4];
    };
} tSendEntry;

#define PARANDIS_NBL_PENDING(Nbl) ((PLONG)&(Nbl)->MiniportReserved[0])
#define PARANDIS_NBL_DESCRIPTOR(Nbl) (*(pIONetDescriptor *)&(Nbl)->MiniportReserved[0])

MINIPORT_SEND_NET_BUFFER_LISTS ParaNdis6_SendNetBufferLists;
MINIPORT_CANCEL_SEND ParaNdis6_CancelSend;
MINIPORT_RETURN_NET_BUFFER_LISTS ParaNdis6_ReturnNetBufferLists;
MINIPORT_OID_REQUEST ParaNdis6_OidRequest;
MINIPORT_CANCEL_OID_REQUEST ParaNdis6_CancelOidRequest;
MINIPORT_ISR ParaNdis6_MiniportISR;
MINIPORT_INTERRUPT_DPC ParaNdis6_MiniportDPC;
MINIPORT_DISABLE_INTERRUPT ParaNdis6_DisableInterrupt;
MINIPORT_ENABLE_INTERRUPT ParaNdis6_EnableInterrupt;

NDIS_STATUS
ParaNdis6_StopSend(
    PARANDIS_ADAPTER *pContext,
    BOOLEAN bStop,
    ONPAUSECOMPLETEPROC Callback);

NDIS_STATUS
ParaNdis6_StopReceive(
    PARANDIS_ADAPTER *pContext,
    BOOLEAN bStop,
    ONPAUSECOMPLETEPROC Callback);

NDIS_STATUS
ParaNdis6_SetGeneralAttributes(
    PARANDIS_ADAPTER *pContext);
