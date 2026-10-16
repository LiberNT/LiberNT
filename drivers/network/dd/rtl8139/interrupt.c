/*
 * ReactOS Realtek 8139 Driver
 *
 * Copyright (C) 2013 Cameron Gutman
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

#include "nic.h"

#define NDEBUG
#include <debug.h>

BOOLEAN
NTAPI
MiniportIsr(
    IN NDIS_HANDLE MiniportInterruptContext,
    OUT PBOOLEAN QueueDefaultInterruptDpc,
    OUT PULONG TargetProcessors)
{
    PRTL_ADAPTER adapter = (PRTL_ADAPTER)MiniportInterruptContext;
    BOOLEAN recognized;
    ULONG csConfig;

    *TargetProcessors = 0;
    adapter->InterruptPending |= NICInterruptRecognized(adapter, &recognized);
    if (!recognized)
    {
        *QueueDefaultInterruptDpc = FALSE;
        return FALSE;
    }

    if (adapter->InterruptPending & R_I_RXUNDRUN)
    {
        NdisRawReadPortUlong(adapter->IoBase + R_CSCFG, &csConfig);
        if (csConfig & R_CSCR_LINKCHNG)
        {
            adapter->LinkChange = TRUE;
            NICUpdateLinkStatus(adapter);
        }
    }

    NICAcknowledgeInterrupts(adapter);
    *QueueDefaultInterruptDpc = TRUE;
    return TRUE;
}

VOID
NTAPI
MiniportDisableInterrupt(
    IN NDIS_HANDLE MiniportInterruptContext)
{
    NICDisableInterrupts((PRTL_ADAPTER)MiniportInterruptContext);
}

VOID
NTAPI
MiniportEnableInterrupt(
    IN NDIS_HANDLE MiniportInterruptContext)
{
    NICApplyInterruptMask((PRTL_ADAPTER)MiniportInterruptContext);
}

VOID
NTAPI
MiniportInterruptDpc(
    IN NDIS_HANDLE MiniportInterruptContext,
    IN PVOID MiniportDpcContext,
    IN PVOID ReceiveThrottleParameters,
    IN PVOID NdisReserved2)
{
    PRTL_ADAPTER adapter = (PRTL_ADAPTER)MiniportInterruptContext;
    PNET_BUFFER_LIST completeHead = NULL, completeTail = NULL;
    PNET_BUFFER_LIST receiveHead = NULL, receiveTail = NULL, nbl;
    ULONG receiveCount = 0;
    ULONG frameLength;
    ULONG txStatus;
    UCHAR command;
    PPACKET_HEADER nicHeader;
    BOOLEAN linkChange;

    UNREFERENCED_PARAMETER(MiniportDpcContext);
    UNREFERENCED_PARAMETER(ReceiveThrottleParameters);
    UNREFERENCED_PARAMETER(NdisReserved2);

    NdisDprAcquireSpinLock(&adapter->Lock);

    linkChange = adapter->LinkChange;
    adapter->LinkChange = FALSE;

    if (adapter->InterruptPending & (R_I_TXOK | R_I_TXERR))
    {
        while (adapter->TxFull || adapter->DirtyTxDesc != adapter->CurrentTxDesc)
        {
            NdisRawReadPortUlong(adapter->IoBase + R_TXSTS0 +
                                 (adapter->DirtyTxDesc * sizeof(ULONG)), &txStatus);
            if (!(txStatus & (R_TXS_STATOK | R_TXS_UNDERRUN | R_TXS_ABORTED)))
                break;

            if (txStatus & R_TXS_STATOK)
                adapter->TransmitOk++;
            else
                adapter->TransmitError++;

            adapter->DirtyTxDesc++;
            adapter->DirtyTxDesc %= TX_DESC_COUNT;
            adapter->InterruptPending &= ~(R_I_TXOK | R_I_TXERR);
            adapter->TxFull = FALSE;
        }

        RtlProcessSendQueue(adapter, &completeHead, &completeTail);
    }

    if (adapter->InterruptPending & (R_I_RXOK | R_I_RXERR))
    {
        for (;;)
        {
            NdisRawReadPortUchar(adapter->IoBase + R_CMD, &command);
            if (command & R_CMD_RXEMPTY)
            {
                adapter->InterruptPending &= ~(R_I_RXOK | R_I_RXERR);
                break;
            }

            adapter->ReceiveOffset %= RECEIVE_BUFFER_SIZE;
            nicHeader = (PPACKET_HEADER)(adapter->ReceiveBuffer + adapter->ReceiveOffset);
            if (!(nicHeader->Status & RSR_ROK))
            {
                if (nicHeader->Status & RSR_FAE)
                    adapter->ReceiveAlignmentError++;
                else if (nicHeader->Status & RSR_CRC)
                    adapter->ReceiveCrcError++;
                adapter->ReceiveError++;
                goto NextPacket;
            }

            frameLength = nicHeader->PacketLength - RECV_CRC_LENGTH;
            if (nicHeader->PacketLength < RECV_CRC_LENGTH + sizeof(ETH_HEADER) || frameLength > MAXIMUM_FRAME_SIZE)
            {
                adapter->ReceiveError++;
                goto NextPacket;
            }

            if (adapter->Paused || adapter->FreeReceiveNbls == NULL)
            {
                adapter->ReceiveNoBufferSpace++;
                goto NextPacket;
            }

            nbl = adapter->FreeReceiveNbls;
            adapter->FreeReceiveNbls = NET_BUFFER_LIST_NEXT_NBL(nbl);
            adapter->ReceiveNblsOutstanding++;

            RtlCopyMemory(nbl->MiniportReserved[0], nicHeader + 1, frameLength);
            NdisAdjustMdlLength(NET_BUFFER_FIRST_MDL(NET_BUFFER_LIST_FIRST_NB(nbl)), frameLength);
            NET_BUFFER_DATA_OFFSET(NET_BUFFER_LIST_FIRST_NB(nbl)) = 0;
            NET_BUFFER_CURRENT_MDL(NET_BUFFER_LIST_FIRST_NB(nbl)) = NET_BUFFER_FIRST_MDL(NET_BUFFER_LIST_FIRST_NB(nbl));
            NET_BUFFER_CURRENT_MDL_OFFSET(NET_BUFFER_LIST_FIRST_NB(nbl)) = 0;
            NET_BUFFER_DATA_LENGTH(NET_BUFFER_LIST_FIRST_NB(nbl)) = frameLength;
            NET_BUFFER_LIST_STATUS(nbl) = NDIS_STATUS_SUCCESS;
            NET_BUFFER_LIST_NEXT_NBL(nbl) = NULL;
            if (receiveTail)
                NET_BUFFER_LIST_NEXT_NBL(receiveTail) = nbl;
            else
                receiveHead = nbl;
            receiveTail = nbl;
            receiveCount++;
            adapter->ReceiveOk++;

        NextPacket:
            adapter->ReceiveOffset += nicHeader->PacketLength + sizeof(PACKET_HEADER);
            adapter->ReceiveOffset = (adapter->ReceiveOffset + 3) & ~3;
            NdisRawWritePortUshort(adapter->IoBase + R_CAPR, adapter->ReceiveOffset - 0x10);

            if (adapter->InterruptPending & (R_I_RXOVRFLW | R_I_FIFOOVR))
            {
                NdisRawWritePortUshort(adapter->IoBase + R_IS, R_I_RXOVRFLW | R_I_FIFOOVR);
                adapter->InterruptPending &= ~(R_I_RXOVRFLW | R_I_FIFOOVR);
            }
        }
    }

    NdisDprReleaseSpinLock(&adapter->Lock);

    RtlCompleteSends(adapter, completeHead, TRUE);

    if (receiveHead != NULL)
    {
        NdisMIndicateReceiveNetBufferLists(adapter->MiniportAdapterHandle,
                                           receiveHead,
                                           NDIS_DEFAULT_PORT_NUMBER,
                                           receiveCount,
                                           NDIS_RECEIVE_FLAGS_DISPATCH_LEVEL);
    }

    if (linkChange)
        RtlIndicateLinkState(adapter);
}
