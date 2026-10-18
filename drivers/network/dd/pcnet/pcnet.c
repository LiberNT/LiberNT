/*
 * ReactOS AMD PCNet Driver
 *
 * Copyright (C) 2003 Vizzini <vizzini@plasmic.com>
 * Copyright (C) 2004 Filip Navara <navaraf@reactos.com>
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
 * REVISIONS:
 *     09-Sep-2003 vizzini - Created
 *     10-Oct-2004 navaraf - Fix receive to work on VMware adapters (
 *                           need to set busmaster bit on PCI).
 *                         - Indicate receive completion.
 *                         - Implement packet transmitting.
 *                         - Don't read slot number from registry and
 *                           report itself as NDIS 5.0 miniport.
 *     11-Oct-2004 navaraf - Fix nasty bugs in halt code path.
 *     17-Oct-2004 navaraf - Add multicast support.
 *                         - Add media state detection support.
 *                         - Protect the adapter context with spinlock
 *                           and move code talking to card to inside
 *                           NdisMSynchronizeWithInterrupt calls where
 *                           necessary.
 *
 * NOTES:
 *     - this assumes a 32-bit machine
 */

#include "pcnet.h"

#define NDEBUG
#include <debug.h>

NTSTATUS
NTAPI
DriverEntry(
    IN PDRIVER_OBJECT DriverObject,
    IN PUNICODE_STRING RegistryPath);

static MINIPORT_ISR MiniportIsr;
static MINIPORT_DISABLE_INTERRUPT MiniportDisableInterrupt;
static MINIPORT_ENABLE_INTERRUPT MiniportEnableInterrupt;

static NDIS_HANDLE MiniportDriverHandle;

static VOID
MiAppendNbl(
    IN OUT PNET_BUFFER_LIST *Head,
    IN OUT PNET_BUFFER_LIST *Tail,
    IN PNET_BUFFER_LIST Nbl)
{
  NET_BUFFER_LIST_NEXT_NBL(Nbl) = NULL;
  if (*Tail)
    NET_BUFFER_LIST_NEXT_NBL(*Tail) = Nbl;
  else
    *Head = Nbl;
  *Tail = Nbl;
}

static PNET_BUFFER_LIST
MiFailNblChain(
    IN PNET_BUFFER_LIST Nbl,
    IN NDIS_STATUS Status)
{
  PNET_BUFFER_LIST Current;

  for (Current = Nbl; Current != NULL; Current = NET_BUFFER_LIST_NEXT_NBL(Current))
    NET_BUFFER_LIST_STATUS(Current) = Status;

  return Nbl;
}

static VOID
MiCompleteSends(
    IN PADAPTER Adapter,
    IN PNET_BUFFER_LIST CompleteHead,
    IN BOOLEAN DispatchLevel)
{
  if (CompleteHead != NULL)
    NdisMSendNetBufferListsComplete(Adapter->MiniportAdapterHandle, CompleteHead,
                                    DispatchLevel ? NDIS_SEND_COMPLETE_FLAGS_DISPATCH_LEVEL : 0);
}

static BOOLEAN
MiCopyNetBuffer(
    IN PNET_BUFFER NetBuffer,
    OUT PCHAR Destination,
    OUT PULONG Length)
{
  PMDL Mdl = NET_BUFFER_CURRENT_MDL(NetBuffer);
  ULONG Offset = NET_BUFFER_CURRENT_MDL_OFFSET(NetBuffer);
  ULONG Remaining = NET_BUFFER_DATA_LENGTH(NetBuffer);
  ULONG Copied = 0;
  ULONG MdlLength;
  PCHAR Source;

  if (Remaining == 0 || Remaining > BUFFER_SIZE)
    return FALSE;

  while (Mdl != NULL && Remaining != 0)
    {
      NdisQueryMdl(Mdl, (PVOID *)&Source, &MdlLength, NormalPagePriority);
      if (Source == NULL)
        return FALSE;

      if (MdlLength > Offset)
        {
          MdlLength = min(MdlLength - Offset, Remaining);
          RtlCopyMemory(Destination + Copied, Source + Offset, MdlLength);
          Copied += MdlLength;
          Remaining -= MdlLength;
          Offset = 0;
        }
      else
        {
          Offset -= MdlLength;
        }

      NdisGetNextMdl(Mdl, &Mdl);
    }

  *Length = Copied;
  return Remaining == 0;
}

static BOOLEAN
MiProcessSendQueue(
    IN PADAPTER Adapter,
    IN OUT PNET_BUFFER_LIST *CompleteHead,
    IN OUT PNET_BUFFER_LIST *CompleteTail)
{
  PNET_BUFFER_LIST Nbl;
  PNET_BUFFER NetBuffer;
  PTRANSMIT_DESCRIPTOR Desc;
  ULONG Length;
  BOOLEAN Queued = FALSE;

  while ((Nbl = Adapter->SendQueueHead) != NULL)
    {
      NetBuffer = (PNET_BUFFER)Nbl->MiniportReserved[0];

      while (NetBuffer != NULL)
        {
          if ((Adapter->CurrentTransmitEndIndex + 1) % Adapter->BufferCount ==
              Adapter->CurrentTransmitStartIndex)
            return Queued;

          Desc = Adapter->TransmitDescriptorRingVirt + Adapter->CurrentTransmitEndIndex;

          if (!MiCopyNetBuffer(NetBuffer,
                               Adapter->TransmitBufferPtrVirt + Adapter->CurrentTransmitEndIndex * BUFFER_SIZE,
                               &Length))
            {
              NET_BUFFER_LIST_STATUS(Nbl) = NDIS_STATUS_INVALID_LENGTH;
              Adapter->Statistics.XmtBufferErrors++;
            }
          else
            {
              Desc->BCNT = 0xf000 | -(INT)Length;
              KeMemoryBarrier();
              Desc->FLAGS = TD1_OWN | TD1_STP | TD1_ENP;

              Adapter->CurrentTransmitEndIndex++;
              Adapter->CurrentTransmitEndIndex %= Adapter->BufferCount;
              Queued = TRUE;
            }

          NetBuffer = NET_BUFFER_NEXT_NB(NetBuffer);
          Nbl->MiniportReserved[0] = NetBuffer;
        }

      Adapter->SendQueueHead = NET_BUFFER_LIST_NEXT_NBL(Nbl);
      if (Adapter->SendQueueHead == NULL)
        Adapter->SendQueueTail = NULL;
      MiAppendNbl(CompleteHead, CompleteTail, Nbl);
    }

  return Queued;
}

static VOID
MiIndicateLinkState(
    IN PADAPTER Adapter)
{
  NDIS_STATUS_INDICATION Indication;
  NDIS_LINK_STATE LinkState;

  NdisZeroMemory(&LinkState, sizeof(LinkState));
  LinkState.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
  LinkState.Header.Revision = NDIS_LINK_STATE_REVISION_1;
  LinkState.Header.Size = NDIS_SIZEOF_LINK_STATE_REVISION_1;
  if (Adapter->MediaState == NdisMediaStateConnected)
    {
      LinkState.MediaConnectState = MediaConnectStateConnected;
      LinkState.MediaDuplexState = Adapter->FullDuplex ? MediaDuplexStateFull : MediaDuplexStateHalf;
      LinkState.XmitLinkSpeed = (ULONG64)Adapter->MediaSpeed * 1000000;
      LinkState.RcvLinkSpeed = LinkState.XmitLinkSpeed;
    }
  else
    {
      LinkState.MediaConnectState = MediaConnectStateDisconnected;
      LinkState.MediaDuplexState = MediaDuplexStateUnknown;
      LinkState.XmitLinkSpeed = NDIS_LINK_SPEED_UNKNOWN;
      LinkState.RcvLinkSpeed = NDIS_LINK_SPEED_UNKNOWN;
    }
  LinkState.PauseFunctions = NdisPauseFunctionsUnsupported;

  NdisZeroMemory(&Indication, sizeof(Indication));
  Indication.Header.Type = NDIS_OBJECT_TYPE_STATUS_INDICATION;
  Indication.Header.Revision = NDIS_STATUS_INDICATION_REVISION_1;
  Indication.Header.Size = NDIS_SIZEOF_STATUS_INDICATION_REVISION_1;
  Indication.SourceHandle = Adapter->MiniportAdapterHandle;
  Indication.StatusCode = NDIS_STATUS_LINK_STATE;
  Indication.StatusBuffer = &LinkState;
  Indication.StatusBufferSize = sizeof(LinkState);

  NdisMIndicateStatusEx(Adapter->MiniportAdapterHandle, &Indication);
}

static VOID
NTAPI
MiniportInterruptDpc(
    IN NDIS_HANDLE MiniportInterruptContext,
    IN PVOID MiniportDpcContext,
    IN PVOID ReceiveThrottleParameters,
    IN PVOID NdisReserved2)
{
  PADAPTER Adapter = (PADAPTER)MiniportInterruptContext;
  PNET_BUFFER_LIST CompleteHead = NULL, CompleteTail = NULL;
  PNET_BUFFER_LIST ReceiveHead = NULL, ReceiveTail = NULL;
  ULONG ReceiveCount = 0;
  USHORT Data;
  UINT i = 0;

  UNREFERENCED_PARAMETER(MiniportDpcContext);
  UNREFERENCED_PARAMETER(ReceiveThrottleParameters);
  UNREFERENCED_PARAMETER(NdisReserved2);

  DPRINT("Called\n");

  ASSERT_IRQL_EQUAL(DISPATCH_LEVEL);

  NdisDprAcquireSpinLock(&Adapter->Lock);

  NdisRawWritePortUshort(Adapter->PortOffset + RAP, CSR0);
  NdisRawReadPortUshort(Adapter->PortOffset + RDP, &Data);

  DPRINT("CSR0 is 0x%x\n", Data);

  while((Data & CSR0_INTR) && i++ < INTERRUPT_LIMIT)
    {
      /* Clear interrupt flags early to avoid race conditions. */
      NdisRawWritePortUshort(Adapter->PortOffset + RDP, Data);

      if(Data & CSR0_ERR)
        {
          DPRINT("error: %x\n", Data & (CSR0_MERR|CSR0_BABL|CSR0_CERR|CSR0_MISS));
          if (Data & CSR0_CERR)
            Adapter->Statistics.XmtCollisions++;
        }
      if(Data & CSR0_IDON)
        {
          DPRINT("IDON\n");
        }
      if(Data & CSR0_RINT)
        {
          DPRINT("receive interrupt\n");

          while(1)
            {
              PRECEIVE_DESCRIPTOR Descriptor = Adapter->ReceiveDescriptorRingVirt + Adapter->CurrentReceiveDescriptorIndex;
              PNET_BUFFER_LIST Nbl;
              PNET_BUFFER NetBuffer;
              PCHAR Buffer;
              ULONG ByteCount;

              if(Descriptor->FLAGS & RD_OWN)
                {
                  DPRINT("no more receive descriptors to process\n");
                  break;
                }

              KeMemoryBarrier();

              if(Descriptor->FLAGS & RD_ERR)
                {
                  DPRINT("receive descriptor error: 0x%x\n", Descriptor->FLAGS);
                  if (Descriptor->FLAGS & RD_BUFF)
                    Adapter->Statistics.RcvBufferErrors++;
                  if (Descriptor->FLAGS & RD_CRC)
                    Adapter->Statistics.RcvCrcErrors++;
                  if (Descriptor->FLAGS & RD_OFLO)
                    Adapter->Statistics.RcvOverflowErrors++;
                  if (Descriptor->FLAGS & RD_FRAM)
                    Adapter->Statistics.RcvFramingErrors++;
                }
              else if(!((Descriptor->FLAGS & RD_STP) && (Descriptor->FLAGS & RD_ENP)))
                {
                  DPRINT("receive descriptor not start&end: 0x%x\n", Descriptor->FLAGS);
                }
              else
                {
                  Buffer = Adapter->ReceiveBufferPtrVirt + Adapter->CurrentReceiveDescriptorIndex * BUFFER_SIZE;
                  ByteCount = Descriptor->MCNT & 0xfff;

                  if (ByteCount < 14 + FCS_LENGTH || ByteCount > BUFFER_SIZE)
                    {
                      DPRINT("bad receive length %lu\n", ByteCount);
                    }
                  else if (Adapter->Paused || Adapter->FreeReceiveNbls == NULL)
                    {
                      Adapter->Statistics.RcvNoBuffers++;
                    }
                  else
                    {
                      ByteCount -= FCS_LENGTH;

                      DPRINT("Indicating a %d-byte packet (index %d)\n", ByteCount, Adapter->CurrentReceiveDescriptorIndex);

                      Nbl = Adapter->FreeReceiveNbls;
                      Adapter->FreeReceiveNbls = NET_BUFFER_LIST_NEXT_NBL(Nbl);
                      Adapter->ReceiveNblsOutstanding++;

                      NetBuffer = NET_BUFFER_LIST_FIRST_NB(Nbl);
                      RtlCopyMemory(Nbl->MiniportReserved[0], Buffer, ByteCount);
                      NdisAdjustMdlLength(NET_BUFFER_FIRST_MDL(NetBuffer), ByteCount);
                      NET_BUFFER_DATA_OFFSET(NetBuffer) = 0;
                      NET_BUFFER_CURRENT_MDL(NetBuffer) = NET_BUFFER_FIRST_MDL(NetBuffer);
                      NET_BUFFER_CURRENT_MDL_OFFSET(NetBuffer) = 0;
                      NET_BUFFER_DATA_LENGTH(NetBuffer) = ByteCount;
                      NET_BUFFER_LIST_STATUS(Nbl) = NDIS_STATUS_SUCCESS;
                      MiAppendNbl(&ReceiveHead, &ReceiveTail, Nbl);
                      ReceiveCount++;

                      Adapter->Statistics.RcvGoodFrames++;
                    }
                }

              RtlZeroMemory(Descriptor, sizeof(RECEIVE_DESCRIPTOR));
              Descriptor->RBADR = Adapter->ReceiveBufferPtrPhys.QuadPart +
                                  (Adapter->CurrentReceiveDescriptorIndex * BUFFER_SIZE);
              Descriptor->BCNT = (-BUFFER_SIZE) | 0xf000;
              KeMemoryBarrier();
              Descriptor->FLAGS |= RD_OWN;

              Adapter->CurrentReceiveDescriptorIndex++;
              Adapter->CurrentReceiveDescriptorIndex %= Adapter->BufferCount;
            }
        }
      if(Data & CSR0_TINT)
        {
          PTRANSMIT_DESCRIPTOR Descriptor;

          DPRINT("transmit interrupt\n");

          while (Adapter->CurrentTransmitStartIndex !=
                 Adapter->CurrentTransmitEndIndex)
            {
              Descriptor = Adapter->TransmitDescriptorRingVirt + Adapter->CurrentTransmitStartIndex;

              DPRINT("buffer %d flags %x flags2 %x\n",
                     Adapter->CurrentTransmitStartIndex,
                     Descriptor->FLAGS, Descriptor->FLAGS2);

              if (Descriptor->FLAGS & TD1_OWN)
                {
                  DPRINT("non-TXed buffer\n");
                  break;
                }

              KeMemoryBarrier();

              if (Descriptor->FLAGS & TD1_STP)
                {
                  if (Descriptor->FLAGS & TD1_ONE)
                    Adapter->Statistics.XmtOneRetry++;
                  else if (Descriptor->FLAGS & TD1_MORE)
                    Adapter->Statistics.XmtMoreThanOneRetry++;
                }

              if (Descriptor->FLAGS & TD1_ERR)
                {
                  DPRINT("major error: %x\n", Descriptor->FLAGS2);
                  if (Descriptor->FLAGS2 & TD2_RTRY)
                    Adapter->Statistics.XmtRetryErrors++;
                  if (Descriptor->FLAGS2 & TD2_LCAR)
                    Adapter->Statistics.XmtLossesOfCarrier++;
                  if (Descriptor->FLAGS2 & TD2_LCOL)
                    Adapter->Statistics.XmtLateCollisions++;
                  if (Descriptor->FLAGS2 & TD2_EXDEF)
                    Adapter->Statistics.XmtExcessiveDeferrals++;
                  if (Descriptor->FLAGS2 & TD2_UFLO)
                    Adapter->Statistics.XmtBufferUnderflows++;
                  if (Descriptor->FLAGS2 & TD2_BUFF)
                    Adapter->Statistics.XmtBufferErrors++;
                }
              else
                {
                  Adapter->Statistics.XmtGoodFrames++;
                }

              Adapter->CurrentTransmitStartIndex++;
              Adapter->CurrentTransmitStartIndex %= Adapter->BufferCount;
            }

          if (MiProcessSendQueue(Adapter, &CompleteHead, &CompleteTail))
            NdisRawWritePortUshort(Adapter->PortOffset + RDP, CSR0_TDMD);
        }
      if(Data & ~(CSR0_ERR | CSR0_IDON | CSR0_RINT | CSR0_TINT))
        {
          DPRINT("UNHANDLED INTERRUPT CSR0 0x%x\n", Data);
        }

      NdisRawReadPortUshort(Adapter->PortOffset + RDP, &Data);
    }

  /* re-enable interrupts */
  NdisRawWritePortUshort(Adapter->PortOffset + RDP, CSR0_IENA);

  NdisRawReadPortUshort(Adapter->PortOffset + RDP, &Data);
  DPRINT("CSR0 is now 0x%x\n", Data);

  NdisDprReleaseSpinLock(&Adapter->Lock);

  MiCompleteSends(Adapter, CompleteHead, TRUE);

  if (ReceiveHead != NULL)
    NdisMIndicateReceiveNetBufferLists(Adapter->MiniportAdapterHandle, ReceiveHead,
                                       NDIS_DEFAULT_PORT_NUMBER, ReceiveCount,
                                       NDIS_RECEIVE_FLAGS_DISPATCH_LEVEL);
}

static VOID
MiFreeSharedMemory(
    PADAPTER Adapter)
/*
 * FUNCTION: Free all allocated shared memory
 * ARGUMENTS:
 *     Adapter: pointer to the miniport's adapter struct
 */
{
  NDIS_PHYSICAL_ADDRESS PhysicalAddress;

  if(Adapter->InitializationBlockVirt)
    {
      PhysicalAddress = Adapter->InitializationBlockPhys;
      NdisMFreeSharedMemory(Adapter->MiniportAdapterHandle, Adapter->InitializationBlockLength,
          FALSE, Adapter->InitializationBlockVirt, PhysicalAddress);
      Adapter->InitializationBlockVirt = NULL;
    }

  if(Adapter->TransmitDescriptorRingVirt)
    {
      PhysicalAddress = Adapter->TransmitDescriptorRingPhys;
      NdisMFreeSharedMemory(Adapter->MiniportAdapterHandle, Adapter->TransmitDescriptorRingLength,
        FALSE, Adapter->TransmitDescriptorRingVirt, PhysicalAddress);
      Adapter->TransmitDescriptorRingVirt = NULL;
    }

  if(Adapter->ReceiveDescriptorRingVirt)
    {
      PhysicalAddress = Adapter->ReceiveDescriptorRingPhys;
      NdisMFreeSharedMemory(Adapter->MiniportAdapterHandle, Adapter->ReceiveDescriptorRingLength,
          FALSE, Adapter->ReceiveDescriptorRingVirt, PhysicalAddress);
      Adapter->ReceiveDescriptorRingVirt = NULL;
    }

  if(Adapter->TransmitBufferPtrVirt)
    {
      PhysicalAddress = Adapter->TransmitBufferPtrPhys;
      NdisMFreeSharedMemory(Adapter->MiniportAdapterHandle, Adapter->TransmitBufferLength,
          TRUE, Adapter->TransmitBufferPtrVirt, PhysicalAddress);
      Adapter->TransmitBufferPtrVirt = NULL;
    }

  if(Adapter->ReceiveBufferPtrVirt)
    {
      PhysicalAddress = Adapter->ReceiveBufferPtrPhys;
      NdisMFreeSharedMemory(Adapter->MiniportAdapterHandle, Adapter->ReceiveBufferLength,
          TRUE, Adapter->ReceiveBufferPtrVirt, PhysicalAddress);
      Adapter->ReceiveBufferPtrVirt = NULL;
    }
}

static NDIS_STATUS
MiAllocateSharedMemory(
    PADAPTER Adapter)
/*
 * FUNCTION: Allocate all shared memory used by the miniport
 * ARGUMENTS:
 *     Adapter: Pointer to the miniport's adapter object
 * RETURNS:
 *     NDIS_STATUS_RESOURCES on insufficient memory
 *     NDIS_STATUS_SUCCESS on success
 */
{
  PTRANSMIT_DESCRIPTOR TransmitDescriptor;
  PRECEIVE_DESCRIPTOR  ReceiveDescriptor;
  NDIS_PHYSICAL_ADDRESS PhysicalAddress;
  ULONG i;
  ULONG BufferCount = NUMBER_OF_BUFFERS;
  ULONG LogBufferCount = LOG_NUMBER_OF_BUFFERS;

  while (BufferCount != 0)
  {
      /* allocate the initialization block (we have this in the loop so we can use MiFreeSharedMemory) */
      Adapter->InitializationBlockLength = sizeof(INITIALIZATION_BLOCK);
      NdisMAllocateSharedMemory(Adapter->MiniportAdapterHandle, Adapter->InitializationBlockLength,
          FALSE, (PVOID *)&Adapter->InitializationBlockVirt, &PhysicalAddress);
      if(!Adapter->InitializationBlockVirt)
      {
         /* Buffer backoff won't help us here */
         DPRINT1("insufficient resources\n");
         return NDIS_STATUS_RESOURCES;
      }

      if (((ULONG_PTR)Adapter->InitializationBlockVirt & 0x00000003) != 0)
      {
         DPRINT1("address 0x%x not dword-aligned\n", Adapter->InitializationBlockVirt);
         return NDIS_STATUS_RESOURCES;
      }

      Adapter->InitializationBlockPhys = PhysicalAddress;

      /* allocate the transport descriptor ring */
      Adapter->TransmitDescriptorRingLength = sizeof(TRANSMIT_DESCRIPTOR) * BufferCount;
      NdisMAllocateSharedMemory(Adapter->MiniportAdapterHandle, Adapter->TransmitDescriptorRingLength,
          FALSE, (PVOID *)&Adapter->TransmitDescriptorRingVirt, &PhysicalAddress);
      if (!Adapter->TransmitDescriptorRingVirt)
      {
          DPRINT1("Backing off buffer count by %d buffers due to allocation failure\n", (BufferCount >> 1));
          BufferCount = BufferCount >> 1;
          LogBufferCount--;
          MiFreeSharedMemory(Adapter);
          continue;
      }

      if (((ULONG_PTR)Adapter->TransmitDescriptorRingVirt & 0x00000003) != 0)
      {
         DPRINT1("address 0x%x not dword-aligned\n", Adapter->TransmitDescriptorRingVirt);
         return NDIS_STATUS_RESOURCES;
      }

      Adapter->TransmitDescriptorRingPhys = PhysicalAddress;
      RtlZeroMemory(Adapter->TransmitDescriptorRingVirt, sizeof(TRANSMIT_DESCRIPTOR) * BufferCount);

      /* allocate the receive descriptor ring */
      Adapter->ReceiveDescriptorRingLength = sizeof(RECEIVE_DESCRIPTOR) * BufferCount;
      NdisMAllocateSharedMemory(Adapter->MiniportAdapterHandle, Adapter->ReceiveDescriptorRingLength,
          FALSE, (PVOID *)&Adapter->ReceiveDescriptorRingVirt, &PhysicalAddress);
      if (!Adapter->ReceiveDescriptorRingVirt)
      {
          DPRINT1("Backing off buffer count by %d buffers due to allocation failure\n", (BufferCount >> 1));
          BufferCount = BufferCount >> 1;
          LogBufferCount--;
          MiFreeSharedMemory(Adapter);
          continue;
      }

      if (((ULONG_PTR)Adapter->ReceiveDescriptorRingVirt & 0x00000003) != 0)
      {
          DPRINT1("address 0x%x not dword-aligned\n", Adapter->ReceiveDescriptorRingVirt);
          return NDIS_STATUS_RESOURCES;
      }

      Adapter->ReceiveDescriptorRingPhys = PhysicalAddress;
      RtlZeroMemory(Adapter->ReceiveDescriptorRingVirt, sizeof(RECEIVE_DESCRIPTOR) * BufferCount);

      /* allocate transmit buffers */
      Adapter->TransmitBufferLength = BUFFER_SIZE * BufferCount;
      NdisMAllocateSharedMemory(Adapter->MiniportAdapterHandle, Adapter->TransmitBufferLength,
         TRUE, (PVOID *)&Adapter->TransmitBufferPtrVirt, &PhysicalAddress);
      if(!Adapter->TransmitBufferPtrVirt)
      {
          DPRINT1("Backing off buffer count by %d buffers due to allocation failure\n", (BufferCount >> 1));
          BufferCount = BufferCount >> 1;
          LogBufferCount--;
          MiFreeSharedMemory(Adapter);
          continue;
      }

      if(((ULONG_PTR)Adapter->TransmitBufferPtrVirt & 0x00000003) != 0)
      {
          DPRINT1("address 0x%x not dword-aligned\n", Adapter->TransmitBufferPtrVirt);
          return NDIS_STATUS_RESOURCES;
      }

      Adapter->TransmitBufferPtrPhys = PhysicalAddress;
      RtlZeroMemory(Adapter->TransmitBufferPtrVirt, BUFFER_SIZE * BufferCount);

      /* allocate receive buffers */
      Adapter->ReceiveBufferLength = BUFFER_SIZE * BufferCount;
      NdisMAllocateSharedMemory(Adapter->MiniportAdapterHandle, Adapter->ReceiveBufferLength,
         TRUE, (PVOID *)&Adapter->ReceiveBufferPtrVirt, &PhysicalAddress);
      if(!Adapter->ReceiveBufferPtrVirt)
      {
          DPRINT1("Backing off buffer count by %d buffers due to allocation failure\n", (BufferCount >> 1));
          BufferCount = BufferCount >> 1;
          LogBufferCount--;
          MiFreeSharedMemory(Adapter);
          continue;
      }

      if (((ULONG_PTR)Adapter->ReceiveBufferPtrVirt & 0x00000003) != 0)
      {
          DPRINT1("address 0x%x not dword-aligned\n", Adapter->ReceiveBufferPtrVirt);
          return NDIS_STATUS_RESOURCES;
      }

      Adapter->ReceiveBufferPtrPhys = PhysicalAddress;
      RtlZeroMemory(Adapter->ReceiveBufferPtrVirt, BUFFER_SIZE * BufferCount);

      break;
  }

  if (!BufferCount)
  {
      DPRINT1("Failed to allocate adapter buffers\n");
      return NDIS_STATUS_RESOURCES;
  }

  Adapter->BufferCount = BufferCount;
  Adapter->LogBufferCount = LogBufferCount;

  /* initialize tx descriptors */
  TransmitDescriptor = Adapter->TransmitDescriptorRingVirt;
  for(i = 0; i < BufferCount; i++)
    {
      (TransmitDescriptor+i)->TBADR = Adapter->TransmitBufferPtrPhys.QuadPart + i * BUFFER_SIZE;
      (TransmitDescriptor+i)->BCNT = 0xf000 | -BUFFER_SIZE; /* 2's compliment  + set top 4 bits */
      (TransmitDescriptor+i)->FLAGS = TD1_STP | TD1_ENP;
    }

  DPRINT("transmit ring initialized\n");

  /* initialize rx */
  ReceiveDescriptor = Adapter->ReceiveDescriptorRingVirt;
  for(i = 0; i < BufferCount; i++)
    {
      (ReceiveDescriptor+i)->RBADR = Adapter->ReceiveBufferPtrPhys.QuadPart + i * BUFFER_SIZE;
      (ReceiveDescriptor+i)->BCNT = 0xf000 | -BUFFER_SIZE; /* 2's compliment  + set top 4 bits */
      (ReceiveDescriptor+i)->FLAGS = RD_OWN;
    }

  DPRINT("receive ring initialized\n");

  return NDIS_STATUS_SUCCESS;
}

static VOID
MiPrepareInitializationBlock(
    PADAPTER Adapter)
/*
 * FUNCTION: Initialize the initialization block
 * ARGUMENTS:
 *     Adapter: pointer to the miniport's adapter object
 */
{
  ULONG i = 0;

  RtlZeroMemory(Adapter->InitializationBlockVirt, sizeof(INITIALIZATION_BLOCK));

  /* read burned-in address from card */
  for(i = 0; i < 6; i++)
    NdisRawReadPortUchar(Adapter->PortOffset + i, Adapter->InitializationBlockVirt->PADR + i);
  DPRINT("MAC address: %02x-%02x-%02x-%02x-%02x-%02x\n",
         Adapter->InitializationBlockVirt->PADR[0],
         Adapter->InitializationBlockVirt->PADR[1],
         Adapter->InitializationBlockVirt->PADR[2],
         Adapter->InitializationBlockVirt->PADR[3],
         Adapter->InitializationBlockVirt->PADR[4],
         Adapter->InitializationBlockVirt->PADR[5]);

  /* set up receive ring */
  DPRINT("Receive ring physical address: 0x%x\n", Adapter->ReceiveDescriptorRingPhys);
  Adapter->InitializationBlockVirt->RDRA = Adapter->ReceiveDescriptorRingPhys.QuadPart;
  Adapter->InitializationBlockVirt->RLEN = (Adapter->LogBufferCount << 4) & 0xf0;

  /* set up transmit ring */
  DPRINT("Transmit ring physical address: 0x%x\n", Adapter->TransmitDescriptorRingPhys);
  Adapter->InitializationBlockVirt->TDRA = Adapter->TransmitDescriptorRingPhys.QuadPart;
  Adapter->InitializationBlockVirt->TLEN = (Adapter->LogBufferCount << 4) & 0xf0;
}

static BOOLEAN
NTAPI
MiSyncStop(
    IN PVOID SynchronizeContext)
/*
 * FUNCTION: Stop the adapter
 * ARGUMENTS:
 *     SynchronizeContext: Adapter context
 */
{
  PADAPTER Adapter = (PADAPTER)SynchronizeContext;
  NdisRawWritePortUshort(Adapter->PortOffset + RAP, CSR0);
  NdisRawWritePortUshort(Adapter->PortOffset + RDP, CSR0_STOP);
  return TRUE;
}

static VOID
MiFreeReceiveNbls(
    IN PADAPTER Adapter)
{
  PNET_BUFFER_LIST Nbl;
  PMDL Mdl;
  ULONG i;

  for (i = 0; i < RECEIVE_NBL_COUNT; i++)
    {
      Nbl = Adapter->ReceiveNbls[i];
      if (Nbl == NULL)
        continue;

      Mdl = NET_BUFFER_FIRST_MDL(NET_BUFFER_LIST_FIRST_NB(Nbl));
      if (Nbl->MiniportReserved[0] != NULL)
        NdisFreeMemory(Nbl->MiniportReserved[0], 0, 0);
      NdisFreeNetBufferList(Nbl);
      if (Mdl != NULL)
        NdisFreeMdl(Mdl);
      Adapter->ReceiveNbls[i] = NULL;
    }
  Adapter->FreeReceiveNbls = NULL;

  if (Adapter->ReceiveNblPool != NULL)
    {
      NdisFreeNetBufferListPool(Adapter->ReceiveNblPool);
      Adapter->ReceiveNblPool = NULL;
    }
}

static NDIS_STATUS
MiAllocateReceiveNbls(
    IN PADAPTER Adapter)
{
  NET_BUFFER_LIST_POOL_PARAMETERS PoolParameters;
  PNET_BUFFER_LIST Nbl;
  PCHAR Buffer;
  PMDL Mdl;
  ULONG i;

  NdisZeroMemory(&PoolParameters, sizeof(PoolParameters));
  PoolParameters.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
  PoolParameters.Header.Revision = NET_BUFFER_LIST_POOL_PARAMETERS_REVISION_1;
  PoolParameters.Header.Size = NDIS_SIZEOF_NET_BUFFER_LIST_POOL_PARAMETERS_REVISION_1;
  PoolParameters.ProtocolId = NDIS_PROTOCOL_ID_DEFAULT;
  PoolParameters.fAllocateNetBuffer = TRUE;
  PoolParameters.PoolTag = PCNET_RECEIVE_TAG;

  Adapter->ReceiveNblPool = NdisAllocateNetBufferListPool(Adapter->MiniportAdapterHandle, &PoolParameters);
  if (Adapter->ReceiveNblPool == NULL)
    return NDIS_STATUS_RESOURCES;

  for (i = 0; i < RECEIVE_NBL_COUNT; i++)
    {
      Buffer = NdisAllocateMemoryWithTagPriority(Adapter->MiniportAdapterHandle, BUFFER_SIZE,
                                                 PCNET_RECEIVE_TAG, NormalPoolPriority);
      if (Buffer == NULL)
        return NDIS_STATUS_RESOURCES;

      Mdl = NdisAllocateMdl(Adapter->MiniportAdapterHandle, Buffer, BUFFER_SIZE);
      if (Mdl == NULL)
        {
          NdisFreeMemory(Buffer, 0, 0);
          return NDIS_STATUS_RESOURCES;
        }

      Nbl = NdisAllocateNetBufferAndNetBufferList(Adapter->ReceiveNblPool, 0, 0, Mdl, 0, BUFFER_SIZE);
      if (Nbl == NULL)
        {
          NdisFreeMdl(Mdl);
          NdisFreeMemory(Buffer, 0, 0);
          return NDIS_STATUS_RESOURCES;
        }

      Nbl->MiniportReserved[0] = Buffer;
      Nbl->SourceHandle = Adapter->MiniportAdapterHandle;
      Adapter->ReceiveNbls[i] = Nbl;
      NET_BUFFER_LIST_NEXT_NBL(Nbl) = Adapter->FreeReceiveNbls;
      Adapter->FreeReceiveNbls = Nbl;
    }

  return NDIS_STATUS_SUCCESS;
}

static VOID
NTAPI
MiniportHaltEx(
    IN NDIS_HANDLE MiniportAdapterContext,
    IN NDIS_HALT_ACTION HaltAction)
/*
 * FUNCTION: Stop the adapter and release any per-adapter resources
 * ARGUMENTS:
 *     MiniportAdapterContext: context specified to NdisMSetAttributes
 * NOTES:
 *     - Called by NDIS at PASSIVE_LEVEL
 */
{
  PADAPTER Adapter = (PADAPTER)MiniportAdapterContext;

  UNREFERENCED_PARAMETER(HaltAction);

  DPRINT("Called\n");
  ASSERT(Adapter);

  /* stop the media detection timer */
  if (Adapter->MediaDetectionTimer)
    {
      NdisCancelTimerObject(Adapter->MediaDetectionTimer);
      KeFlushQueuedDpcs();
      NdisFreeTimerObject(Adapter->MediaDetectionTimer);
    }

  /* stop the chip */
  if (Adapter->ChipStarted)
    NdisMSynchronizeWithInterruptEx(Adapter->InterruptHandle, 0, MiSyncStop, Adapter);

  /* deregister the interrupt */
  if (Adapter->InterruptHandle)
    NdisMDeregisterInterruptEx(Adapter->InterruptHandle);

  /* deregister i/o port range */
  if (Adapter->PortOffset)
    NdisMDeregisterIoPortRange(Adapter->MiniportAdapterHandle, Adapter->IoBaseAddress, NUMBER_OF_PORTS, (PVOID)Adapter->PortOffset);

  /* free shared memory */
  MiFreeSharedMemory(Adapter);

  if (Adapter->DmaHandle)
    NdisMDeregisterScatterGatherDma(Adapter->DmaHandle);

  MiFreeReceiveNbls(Adapter);

  /* free the lock */
  NdisFreeSpinLock(&Adapter->Lock);

  /* free the adapter */
  NdisFreeMemory(Adapter, 0, 0);
}

static BOOLEAN
NTAPI
MiSyncMediaDetection(
    IN PVOID SynchronizeContext)
/*
 * FUNCTION: Stop the adapter
 * ARGUMENTS:
 *     SynchronizeContext: Adapter context
 */
{
  PADAPTER Adapter = (PADAPTER)SynchronizeContext;
  NDIS_MEDIA_STATE MediaState = MiGetMediaState(Adapter);
  UINT MediaSpeed = MiGetMediaSpeed(Adapter);
  BOOLEAN FullDuplex = MiGetMediaDuplex(Adapter);

  DPRINT("Called\n");
  DPRINT("MediaState: %d\n", MediaState);
  if (MediaState != Adapter->MediaState ||
      MediaSpeed != Adapter->MediaSpeed ||
      FullDuplex != Adapter->FullDuplex)
    {
      Adapter->MediaState = MediaState;
      Adapter->MediaSpeed = MediaSpeed;
      Adapter->FullDuplex = FullDuplex;
      return TRUE;
    }
  return FALSE;
}

static VOID
NTAPI
MiniportMediaDetectionTimer(
    IN PVOID SystemSpecific1,
    IN PVOID FunctionContext,
    IN PVOID SystemSpecific2,
    IN PVOID SystemSpecific3)
/*
 * FUNCTION: Periodically query media state
 * ARGUMENTS:
 *     FunctionContext: Adapter context
 * NOTES:
 *     - Called by NDIS at DISPATCH_LEVEL
 */
{
  PADAPTER Adapter = (PADAPTER)FunctionContext;
  BOOLEAN Changed;

  ASSERT_IRQL_EQUAL(DISPATCH_LEVEL);

  NdisDprAcquireSpinLock(&Adapter->Lock);
  Changed = NdisMSynchronizeWithInterruptEx(Adapter->InterruptHandle, 0,
                                            MiSyncMediaDetection,
                                            FunctionContext);
  NdisDprReleaseSpinLock(&Adapter->Lock);

  if (Changed)
    MiIndicateLinkState(Adapter);
}

static VOID
MiInitChip(
    PADAPTER Adapter)
/*
 * FUNCTION: Initialize and start the PCNET chip
 * ARGUMENTS:
 *     Adapter: pointer to the miniport's adapter struct
 * NOTES:
 *     - should be coded to detect failure and return an error
 *     - the vmware virtual lance chip doesn't support 32-bit i/o so don't do that.
 */
{
  USHORT Data = 0;

  DPRINT("Called\n");

  /*
   * first reset the chip - 32-bit reset followed by 16-bit reset.  if it's in 32-bit mode, it'll reset
   * twice.  if it's in 16-bit mode, the first read will be nonsense and the second will be a reset.  the
   * card is reset by reading from the reset register.  on reset it's in 16-bit i/o mode.
   */
  NdisRawReadPortUshort(Adapter->PortOffset + RESET32, &Data);
  NdisRawReadPortUshort(Adapter->PortOffset + RESET16, &Data);

  /* stop the chip */
  NdisRawWritePortUshort(Adapter->PortOffset + RAP, CSR0);
  NdisRawWritePortUshort(Adapter->PortOffset + RDP, CSR0_STOP);

  /* pause for 1ms so the chip will have time to reset */
  NdisStallExecution(1);

  DPRINT("chip stopped\n");

  /* set the software style to 2 (32 bits) */
  NdisRawWritePortUshort(Adapter->PortOffset + RAP, CSR58);
  NdisRawReadPortUshort(Adapter->PortOffset + RDP, &Data);

  Data |= SW_STYLE_2;

  NdisRawWritePortUshort(Adapter->PortOffset + RDP, Data);

  /* set up csr4: auto transmit pad, disable polling, disable transmit interrupt, dmaplus */
  NdisRawWritePortUshort(Adapter->PortOffset + RAP, CSR4);
  NdisRawReadPortUshort(Adapter->PortOffset + RDP, &Data);

  Data |= CSR4_APAD_XMT | /* CSR4_DPOLL |*/ CSR4_TXSTRTM | CSR4_DMAPLUS;
  NdisRawWritePortUshort(Adapter->PortOffset + RDP, Data);

  /* set up bcr18: burst read/write enable */
  NdisRawWritePortUshort(Adapter->PortOffset + RAP, BCR18);
  NdisRawReadPortUshort(Adapter->PortOffset + BDP, &Data);

  Data |= BCR18_BREADE | BCR18_BWRITE ;
  NdisRawWritePortUshort(Adapter->PortOffset + BDP, Data);

  /* set up csr1 and csr2 with init block */
  NdisRawWritePortUshort(Adapter->PortOffset + RAP, CSR1);
  NdisRawWritePortUshort(Adapter->PortOffset + RDP, (USHORT)(Adapter->InitializationBlockPhys.LowPart & 0xffff));
  NdisRawWritePortUshort(Adapter->PortOffset + RAP, CSR2);
  NdisRawWritePortUshort(Adapter->PortOffset + RDP, (USHORT)(Adapter->InitializationBlockPhys.LowPart >> 16) & 0xffff);

  DPRINT("programmed with init block\n");

  /* Set mode to 0 */
  Data = 0;
  NdisRawWritePortUshort(Adapter->PortOffset + RAP, CSR15);
  NdisRawWritePortUshort(Adapter->PortOffset + RDP, Data);

  /* load init block and start the card */
  NdisRawWritePortUshort(Adapter->PortOffset + RAP, CSR0);
  NdisRawWritePortUshort(Adapter->PortOffset + RDP, CSR0_STRT|CSR0_INIT|CSR0_IENA);

  /* Allow LED programming */
  NdisRawWritePortUshort(Adapter->PortOffset + RAP, BCR2);
  NdisRawWritePortUshort(Adapter->PortOffset + BDP, BCR2_LEDPE);

  /* LED0 is configured for link status (on = up, off = down) */
  NdisRawWritePortUshort(Adapter->PortOffset + RAP, BCR4);
  NdisRawWritePortUshort(Adapter->PortOffset + BDP, BCR4_LNKSTE | BCR4_PSE);

  /* LED1 is configured for link duplex (on = full, off = half) */
  NdisRawWritePortUshort(Adapter->PortOffset + RAP, BCR5);
  NdisRawWritePortUshort(Adapter->PortOffset + BDP, BCR5_FDLSE | BCR5_PSE);

  /* LED2 is configured for link speed (on = 100M, off = 10M) */
  NdisRawWritePortUshort(Adapter->PortOffset + RAP, BCR6);
  NdisRawWritePortUshort(Adapter->PortOffset + BDP, BCR6_E100 | BCR6_PSE);

  /* LED3 is configured for trasmit/receive activity */
  NdisRawWritePortUshort(Adapter->PortOffset + RAP, BCR7);
  NdisRawWritePortUshort(Adapter->PortOffset + BDP, BCR7_XMTE | BCR7_RCVE | BCR7_PSE);

  Adapter->MediaState = MiGetMediaState(Adapter);
  Adapter->FullDuplex = MiGetMediaDuplex(Adapter);
  Adapter->MediaSpeed = MiGetMediaSpeed(Adapter);

  DPRINT("card started\n");

  Adapter->Flags &= ~RESET_IN_PROGRESS;
}

#if DBG
static BOOLEAN
MiTestCard(
    PADAPTER Adapter)
/*
 * FUNCTION: Test the NIC
 * ARGUMENTS:
 *     Adapter: pointer to the miniport's adapter struct
 * RETURNS:
 *     TRUE if the test succeeds
 *     FALSE otherwise
 * NOTES:
 *     - this is where to add diagnostics.  This is called
 *       at the very end of initialization.
 */
{
  int i = 0;
  UCHAR address[6];
  USHORT Data = 0;

  /* see if we can read/write now */
  NdisRawWritePortUshort(Adapter->PortOffset + RAP, CSR0);
  NdisRawReadPortUshort(Adapter->PortOffset + RDP, &Data);
  DPRINT("Port 0x%x RAP 0x%x CSR0 0x%x RDP 0x%x, Interrupt status register is 0x%x\n", Adapter->PortOffset, RAP, CSR0, RDP, Data);

  /* read the BIA */
  for(i = 0; i < 6; i++)
      NdisRawReadPortUchar(Adapter->PortOffset + i, &address[i]);

  DPRINT("burned-in address: %02x:%02x:%02x:%02x:%02x:%02x\n", address[0], address[1], address[2], address[3], address[4], address[5]);
  /* Read status flags from CSR0 */
  NdisRawWritePortUshort(Adapter->PortOffset + RAP, CSR0);
  NdisRawReadPortUshort(Adapter->PortOffset + RDP, &Data);
  DPRINT("CSR0: 0x%x\n", Data);

  /* Read status flags from CSR3 */
  NdisRawWritePortUshort(Adapter->PortOffset + RAP, CSR3);
  NdisRawReadPortUshort(Adapter->PortOffset + RDP, &Data);
  DPRINT("CSR3: 0x%x\n", Data);

  /* Read status flags from CSR4 */
  NdisRawWritePortUshort(Adapter->PortOffset + RAP, CSR4);
  NdisRawReadPortUshort(Adapter->PortOffset + RDP, &Data);
  DPRINT("CSR4: 0x%x\n", Data);

  /* Read status flags from CSR5 */
  NdisRawWritePortUshort(Adapter->PortOffset + RAP, CSR5);
  NdisRawReadPortUshort(Adapter->PortOffset + RDP, &Data);
  DPRINT("CSR5: 0x%x\n", Data);

  /* Read status flags from CSR6 */
  NdisRawWritePortUshort(Adapter->PortOffset + RAP, CSR6);
  NdisRawReadPortUshort(Adapter->PortOffset + RDP, &Data);
  DPRINT("CSR6: 0x%x\n", Data);

  /* Read status flags from BCR4 */
  NdisRawWritePortUshort(Adapter->PortOffset + RAP, BCR4);
  NdisRawReadPortUshort(Adapter->PortOffset + BDP, &Data);
  DPRINT("BCR4: 0x%x\n", Data);

  return TRUE;
}
#endif

static BOOLEAN
NTAPI
MiSyncInitChip(
    IN PVOID SynchronizeContext)
{
  PADAPTER Adapter = (PADAPTER)SynchronizeContext;

  MiInitChip(Adapter);
#if DBG
  if(!MiTestCard(Adapter))
    ASSERT(0);
#endif
  return TRUE;
}

static VOID
NTAPI
MiniportShutdownEx(
    IN NDIS_HANDLE MiniportAdapterContext,
    IN NDIS_SHUTDOWN_ACTION ShutdownAction)
{
  PADAPTER Adapter = (PADAPTER)MiniportAdapterContext;

  UNREFERENCED_PARAMETER(ShutdownAction);

  DPRINT("Stopping the chip\n");

  NdisRawWritePortUshort(Adapter->PortOffset + RAP, CSR0);
  NdisRawWritePortUshort(Adapter->PortOffset + RDP, CSR0_STOP);
}

static NDIS_STATUS
MiSetRegistrationAttributes(
    IN PADAPTER Adapter)
{
  NDIS_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES Registration;

  NdisZeroMemory(&Registration, sizeof(Registration));
  Registration.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES;
  Registration.Header.Revision = NDIS_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES_REVISION_1;
  Registration.Header.Size = NDIS_SIZEOF_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES_REVISION_1;
  Registration.MiniportAdapterContext = Adapter;
  Registration.AttributeFlags = NDIS_MINIPORT_ATTRIBUTES_HARDWARE_DEVICE | NDIS_MINIPORT_ATTRIBUTES_BUS_MASTER;
  Registration.InterfaceType = NdisInterfacePci;

  return NdisMSetMiniportAttributes(Adapter->MiniportAdapterHandle,
                                    (PNDIS_MINIPORT_ADAPTER_ATTRIBUTES)&Registration);
}

static NDIS_STATUS
MiSetGeneralAttributes(
    IN PADAPTER Adapter)
{
  NDIS_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES General;

  NdisZeroMemory(&General, sizeof(General));
  General.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES;
  General.Header.Revision = NDIS_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES_REVISION_1;
  General.Header.Size = NDIS_SIZEOF_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES_REVISION_1;
  General.MediaType = NdisMedium802_3;
  General.PhysicalMediumType = NdisPhysicalMedium802_3;
  General.MtuSize = MAXIMUM_FRAME_SIZE - 14;
  General.MaxXmitLinkSpeed = 100000000;
  General.MaxRcvLinkSpeed = 100000000;
  if (Adapter->MediaState == NdisMediaStateConnected)
    {
      General.XmitLinkSpeed = (ULONG64)Adapter->MediaSpeed * 1000000;
      General.RcvLinkSpeed = General.XmitLinkSpeed;
      General.MediaConnectState = MediaConnectStateConnected;
      General.MediaDuplexState = Adapter->FullDuplex ? MediaDuplexStateFull : MediaDuplexStateHalf;
    }
  else
    {
      General.XmitLinkSpeed = NDIS_LINK_SPEED_UNKNOWN;
      General.RcvLinkSpeed = NDIS_LINK_SPEED_UNKNOWN;
      General.MediaConnectState = MediaConnectStateDisconnected;
      General.MediaDuplexState = MediaDuplexStateUnknown;
    }
  General.LookaheadSize = MAXIMUM_FRAME_SIZE - 14;
  General.MacOptions = NDIS_MAC_OPTION_COPY_LOOKAHEAD_DATA |
                       NDIS_MAC_OPTION_TRANSFERS_NOT_PEND |
                       NDIS_MAC_OPTION_NO_LOOPBACK;
  General.SupportedPacketFilters = NDIS_PACKET_TYPE_DIRECTED |
                                   NDIS_PACKET_TYPE_MULTICAST |
                                   NDIS_PACKET_TYPE_BROADCAST;
  General.MaxMulticastListSize = MAX_MULTICAST_ADDRESSES;
  General.MacAddressLength = 6;
  NdisMoveMemory(General.PermanentMacAddress, Adapter->PermanentAddress, 6);
  NdisMoveMemory(General.CurrentMacAddress, Adapter->InitializationBlockVirt->PADR, 6);
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
                                NDIS_STATISTICS_RCV_CRC_ERROR_SUPPORTED;
  General.SupportedPauseFunctions = NdisPauseFunctionsUnsupported;
  General.AutoNegotiationFlags = NDIS_LINK_STATE_XMIT_LINK_SPEED_AUTO_NEGOTIATED |
                                 NDIS_LINK_STATE_RCV_LINK_SPEED_AUTO_NEGOTIATED |
                                 NDIS_LINK_STATE_DUPLEX_AUTO_NEGOTIATED;
  General.SupportedOidList = MiniportOIDList;
  General.SupportedOidListLength = MiniportOIDCount * sizeof(NDIS_OID);

  return NdisMSetMiniportAttributes(Adapter->MiniportAdapterHandle,
                                    (PNDIS_MINIPORT_ADAPTER_ATTRIBUTES)&General);
}

static NDIS_STATUS
NTAPI
MiniportInitializeEx(
    IN NDIS_HANDLE MiniportAdapterHandle,
    IN NDIS_HANDLE MiniportDriverContext,
    IN PNDIS_MINIPORT_INIT_PARAMETERS MiniportInitParameters)
{
  NDIS_MINIPORT_INTERRUPT_CHARACTERISTICS InterruptCharacteristics;
  NDIS_TIMER_CHARACTERISTICS TimerCharacteristics;
  NDIS_SG_DMA_DESCRIPTION DmaDescription;
  NDIS_CONFIGURATION_OBJECT ConfigurationObject;
  PCM_PARTIAL_RESOURCE_LIST ResourceList;
  PCM_PARTIAL_RESOURCE_DESCRIPTOR Descriptor;
  PADAPTER Adapter;
  NDIS_STATUS Status;
  NDIS_HANDLE ConfigurationHandle;
  PUCHAR RegNetworkAddress = NULL;
  UINT RegNetworkAddressLength = 0;
  LARGE_INTEGER DueTime;
  ULONG i;

  UNREFERENCED_PARAMETER(MiniportDriverContext);

  ASSERT_IRQL_EQUAL(PASSIVE_LEVEL);

  /* allocate our adapter struct */
  Adapter = NdisAllocateMemoryWithTagPriority(MiniportAdapterHandle, sizeof(ADAPTER), PCNET_TAG, NormalPoolPriority);
  if(!Adapter)
    {
      DPRINT1("Insufficient resources\n");
      return NDIS_STATUS_RESOURCES;
    }

  RtlZeroMemory(Adapter, sizeof(ADAPTER));

  Adapter->MiniportAdapterHandle = MiniportAdapterHandle;
  Adapter->Paused = TRUE;
  NdisAllocateSpinLock(&Adapter->Lock);

  /* register our adapter structwith ndis */
  Status = MiSetRegistrationAttributes(Adapter);
  if(Status != NDIS_STATUS_SUCCESS)
    goto Failure;

  ResourceList = MiniportInitParameters->AllocatedResources;
  for (i = 0; ResourceList && i < ResourceList->Count; i++)
    {
      Descriptor = &ResourceList->PartialDescriptors[i];
      if (Descriptor->Type == CmResourceTypePort &&
          !Adapter->IoBaseAddress &&
          Descriptor->u.Port.Start.HighPart == 0 &&
          Descriptor->u.Port.Length >= NUMBER_OF_PORTS)
        {
          Adapter->IoBaseAddress = Descriptor->u.Port.Start.LowPart;
        }
    }

  if(!Adapter->IoBaseAddress)
    {
      DPRINT1("No base i/o address set\n");
      Status = NDIS_STATUS_RESOURCES;
      goto Failure;
    }

  Status = NdisMRegisterIoPortRange((PVOID*)&Adapter->PortOffset, Adapter->MiniportAdapterHandle,
      (UINT)Adapter->IoBaseAddress, NUMBER_OF_PORTS);
  if(Status != NDIS_STATUS_SUCCESS)
    {
      DPRINT1("NdisMRegisterIoPortRange failed: 0x%x\n", Status);
      Adapter->PortOffset = 0;
      goto Failure;
    }

  NdisZeroMemory(&DmaDescription, sizeof(DmaDescription));
  DmaDescription.Header.Type = NDIS_OBJECT_TYPE_SG_DMA_DESCRIPTION;
  DmaDescription.Header.Revision = NDIS_SG_DMA_DESCRIPTION_REVISION_1;
  DmaDescription.Header.Size = NDIS_SIZEOF_SG_DMA_DESCRIPTION_REVISION_1;
  DmaDescription.MaximumPhysicalMapping = BUFFER_SIZE;
  Status = NdisMRegisterScatterGatherDma(Adapter->MiniportAdapterHandle, &DmaDescription, &Adapter->DmaHandle);
  if(Status != NDIS_STATUS_SUCCESS)
    {
      DPRINT1("NdisMRegisterScatterGatherDma failed: 0x%x\n", Status);
      Adapter->DmaHandle = NULL;
      goto Failure;
    }

  Status = MiAllocateSharedMemory(Adapter);
  if(Status != NDIS_STATUS_SUCCESS)
    {
      Status = NDIS_STATUS_RESOURCES;
      DPRINT1("MiAllocateSharedMemory failed\n", Status);
      goto Failure;
    }

  Status = MiAllocateReceiveNbls(Adapter);
  if(Status != NDIS_STATUS_SUCCESS)
    goto Failure;

  MiPrepareInitializationBlock(Adapter);
  NdisMoveMemory(Adapter->PermanentAddress, Adapter->InitializationBlockVirt->PADR, 6);

  ConfigurationObject.Header.Type = NDIS_OBJECT_TYPE_CONFIGURATION_OBJECT;
  ConfigurationObject.Header.Revision = NDIS_CONFIGURATION_OBJECT_REVISION_1;
  ConfigurationObject.Header.Size = NDIS_SIZEOF_CONFIGURATION_OBJECT_REVISION_1;
  ConfigurationObject.NdisHandle = Adapter->MiniportAdapterHandle;
  ConfigurationObject.Flags = 0;
  if (NdisOpenConfigurationEx(&ConfigurationObject, &ConfigurationHandle) == NDIS_STATUS_SUCCESS)
  {
     NdisReadNetworkAddress(&Status, (PVOID *)&RegNetworkAddress, &RegNetworkAddressLength, ConfigurationHandle);
     if(Status == NDIS_STATUS_SUCCESS && RegNetworkAddressLength == 6 && !(RegNetworkAddress[0] & 0x01))
     {
         DPRINT("NdisReadNetworkAddress returned successfully, address %x:%x:%x:%x:%x:%x\n",
                 RegNetworkAddress[0], RegNetworkAddress[1], RegNetworkAddress[2], RegNetworkAddress[3],
                 RegNetworkAddress[4], RegNetworkAddress[5]);

         NdisMoveMemory(Adapter->InitializationBlockVirt->PADR, RegNetworkAddress, 6);
     }

     NdisCloseConfiguration(ConfigurationHandle);
  }

  NdisZeroMemory(&InterruptCharacteristics, sizeof(InterruptCharacteristics));
  InterruptCharacteristics.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_INTERRUPT;
  InterruptCharacteristics.Header.Revision = NDIS_MINIPORT_INTERRUPT_REVISION_1;
  InterruptCharacteristics.Header.Size = NDIS_SIZEOF_MINIPORT_INTERRUPT_CHARACTERISTICS_REVISION_1;
  InterruptCharacteristics.InterruptHandler = MiniportIsr;
  InterruptCharacteristics.InterruptDpcHandler = MiniportInterruptDpc;
  InterruptCharacteristics.DisableInterruptHandler = MiniportDisableInterrupt;
  InterruptCharacteristics.EnableInterruptHandler = MiniportEnableInterrupt;
  Status = NdisMRegisterInterruptEx(Adapter->MiniportAdapterHandle, Adapter, &InterruptCharacteristics, &Adapter->InterruptHandle);
  if(Status != NDIS_STATUS_SUCCESS)
    {
      DPRINT1("NdisMRegisterInterruptEx failed: 0x%x\n", Status);
      Adapter->InterruptHandle = NULL;
      goto Failure;
    }

  DPRINT("Interrupt registered successfully\n");

  NdisAcquireSpinLock(&Adapter->Lock);
  NdisMSynchronizeWithInterruptEx(Adapter->InterruptHandle, 0, MiSyncInitChip, Adapter);
  Adapter->ChipStarted = TRUE;
  NdisReleaseSpinLock(&Adapter->Lock);

  Status = MiSetGeneralAttributes(Adapter);
  if(Status != NDIS_STATUS_SUCCESS)
    goto Failure;

  NdisZeroMemory(&TimerCharacteristics, sizeof(TimerCharacteristics));
  TimerCharacteristics.Header.Type = NDIS_OBJECT_TYPE_TIMER_CHARACTERISTICS;
  TimerCharacteristics.Header.Revision = NDIS_TIMER_CHARACTERISTICS_REVISION_1;
  TimerCharacteristics.Header.Size = NDIS_SIZEOF_TIMER_CHARACTERISTICS_REVISION_1;
  TimerCharacteristics.AllocationTag = PCNET_TAG;
  TimerCharacteristics.TimerFunction = MiniportMediaDetectionTimer;
  TimerCharacteristics.FunctionContext = Adapter;
  Status = NdisAllocateTimerObject(Adapter->MiniportAdapterHandle, &TimerCharacteristics, &Adapter->MediaDetectionTimer);
  if(Status != NDIS_STATUS_SUCCESS)
    {
      Adapter->MediaDetectionTimer = NULL;
      goto Failure;
    }

  DueTime.QuadPart = -10000LL * MEDIA_DETECTION_INTERVAL;
  NdisSetTimerObject(Adapter->MediaDetectionTimer, DueTime, MEDIA_DETECTION_INTERVAL, Adapter);

  return NDIS_STATUS_SUCCESS;

Failure:
  DPRINT("Error; freeing stuff\n");
  MiniportHaltEx(Adapter, NdisHaltDeviceInitializationFailed);
  return Status;
}

static BOOLEAN
NTAPI
MiniportIsr(
    IN NDIS_HANDLE MiniportInterruptContext,
    OUT PBOOLEAN QueueDefaultInterruptDpc,
    OUT PULONG TargetProcessors)
{
  USHORT Data;
  USHORT Rap;
  PADAPTER Adapter = (PADAPTER)MiniportInterruptContext;

  DPRINT("Called\n");

  *TargetProcessors = 0;

  /* save the old RAP value */
  NdisRawReadPortUshort(Adapter->PortOffset + RAP, &Rap);

  /* is this ours? */
  NdisRawWritePortUshort(Adapter->PortOffset + RAP, CSR0);
  NdisRawReadPortUshort(Adapter->PortOffset + RDP, &Data);

  if(!(Data & CSR0_INTR) || !(Data & CSR0_IENA))
    {
      DPRINT("not our interrupt.\n");
      *QueueDefaultInterruptDpc = FALSE;
    }
  else
    {
      DPRINT("detected our interrupt\n");

      /* disable interrupts */
      NdisRawWritePortUshort(Adapter->PortOffset + RAP, CSR0);
      NdisRawWritePortUshort(Adapter->PortOffset + RDP, 0);

      *QueueDefaultInterruptDpc = TRUE;
    }

  /* restore the rap */
  NdisRawWritePortUshort(Adapter->PortOffset + RAP, Rap);

  return *QueueDefaultInterruptDpc;
}

static VOID
NTAPI
MiniportDisableInterrupt(
    IN NDIS_HANDLE MiniportInterruptContext)
{
  PADAPTER Adapter = (PADAPTER)MiniportInterruptContext;
  USHORT Rap;

  NdisRawReadPortUshort(Adapter->PortOffset + RAP, &Rap);
  NdisRawWritePortUshort(Adapter->PortOffset + RAP, CSR0);
  NdisRawWritePortUshort(Adapter->PortOffset + RDP, 0);
  NdisRawWritePortUshort(Adapter->PortOffset + RAP, Rap);
}

static VOID
NTAPI
MiniportEnableInterrupt(
    IN NDIS_HANDLE MiniportInterruptContext)
{
  PADAPTER Adapter = (PADAPTER)MiniportInterruptContext;
  USHORT Rap;

  NdisRawReadPortUshort(Adapter->PortOffset + RAP, &Rap);
  NdisRawWritePortUshort(Adapter->PortOffset + RAP, CSR0);
  NdisRawWritePortUshort(Adapter->PortOffset + RDP, CSR0_IENA);
  NdisRawWritePortUshort(Adapter->PortOffset + RAP, Rap);
}


static BOOLEAN
NTAPI
MiSyncStartTransmit(
    IN PVOID SynchronizeContext)
/*
 * FUNCTION: Stop the adapter
 * ARGUMENTS:
 *     SynchronizeContext: Adapter context
 */
{
  PADAPTER Adapter = (PADAPTER)SynchronizeContext;
  NdisRawWritePortUshort(Adapter->PortOffset + RAP, CSR0);
  NdisRawWritePortUshort(Adapter->PortOffset + RDP, CSR0_IENA | CSR0_TDMD);
  return TRUE;
}

static VOID
NTAPI
MiniportSendNetBufferLists(
    IN NDIS_HANDLE MiniportAdapterContext,
    IN PNET_BUFFER_LIST NetBufferLists,
    IN NDIS_PORT_NUMBER PortNumber,
    IN ULONG SendFlags)
{
  PADAPTER Adapter = (PADAPTER)MiniportAdapterContext;
  BOOLEAN DispatchLevel = NDIS_TEST_SEND_AT_DISPATCH_LEVEL(SendFlags);
  PNET_BUFFER_LIST CompleteHead = NULL, CompleteTail = NULL;
  PNET_BUFFER_LIST Nbl, Next;

  UNREFERENCED_PARAMETER(PortNumber);

  DPRINT("Called\n");

  NdisAcquireSpinLock(&Adapter->Lock);

  if (Adapter->Paused)
    {
      NdisReleaseSpinLock(&Adapter->Lock);
      MiCompleteSends(Adapter, MiFailNblChain(NetBufferLists, NDIS_STATUS_PAUSED), DispatchLevel);
      return;
    }

  for (Nbl = NetBufferLists; Nbl != NULL; Nbl = Next)
    {
      Next = NET_BUFFER_LIST_NEXT_NBL(Nbl);
      NET_BUFFER_LIST_STATUS(Nbl) = NDIS_STATUS_SUCCESS;
      Nbl->MiniportReserved[0] = NET_BUFFER_LIST_FIRST_NB(Nbl);
      MiAppendNbl(&Adapter->SendQueueHead, &Adapter->SendQueueTail, Nbl);
    }

  if (MiProcessSendQueue(Adapter, &CompleteHead, &CompleteTail))
    NdisMSynchronizeWithInterruptEx(Adapter->InterruptHandle, 0, MiSyncStartTransmit, Adapter);

  NdisReleaseSpinLock(&Adapter->Lock);

  MiCompleteSends(Adapter, CompleteHead, DispatchLevel);
}

static VOID
NTAPI
MiniportCancelSend(
    IN NDIS_HANDLE MiniportAdapterContext,
    IN PVOID CancelId)
{
  PADAPTER Adapter = (PADAPTER)MiniportAdapterContext;
  PNET_BUFFER_LIST CompleteHead = NULL, CompleteTail = NULL;
  PNET_BUFFER_LIST Nbl, Next, KeepHead = NULL, KeepTail = NULL;

  NdisAcquireSpinLock(&Adapter->Lock);

  Nbl = Adapter->SendQueueHead;
  if (Nbl != NULL && Nbl->MiniportReserved[0] != NET_BUFFER_LIST_FIRST_NB(Nbl))
    {
      Next = NET_BUFFER_LIST_NEXT_NBL(Nbl);
      MiAppendNbl(&KeepHead, &KeepTail, Nbl);
      Nbl = Next;
    }

  for (; Nbl != NULL; Nbl = Next)
    {
      Next = NET_BUFFER_LIST_NEXT_NBL(Nbl);
      if (NDIS_GET_NET_BUFFER_LIST_CANCEL_ID(Nbl) == CancelId)
        {
          NET_BUFFER_LIST_STATUS(Nbl) = NDIS_STATUS_SEND_ABORTED;
          MiAppendNbl(&CompleteHead, &CompleteTail, Nbl);
        }
      else
        {
          MiAppendNbl(&KeepHead, &KeepTail, Nbl);
        }
    }

  Adapter->SendQueueHead = KeepHead;
  Adapter->SendQueueTail = KeepTail;
  NdisReleaseSpinLock(&Adapter->Lock);

  MiCompleteSends(Adapter, CompleteHead, FALSE);
}

static VOID
NTAPI
MiniportReturnNetBufferLists(
    IN NDIS_HANDLE MiniportAdapterContext,
    IN PNET_BUFFER_LIST NetBufferLists,
    IN ULONG ReturnFlags)
{
  PADAPTER Adapter = (PADAPTER)MiniportAdapterContext;
  PNET_BUFFER_LIST Nbl, Next;
  BOOLEAN CompletePause = FALSE;

  UNREFERENCED_PARAMETER(ReturnFlags);

  NdisAcquireSpinLock(&Adapter->Lock);
  for (Nbl = NetBufferLists; Nbl != NULL; Nbl = Next)
    {
      Next = NET_BUFFER_LIST_NEXT_NBL(Nbl);
      NET_BUFFER_LIST_NEXT_NBL(Nbl) = Adapter->FreeReceiveNbls;
      Adapter->FreeReceiveNbls = Nbl;
      Adapter->ReceiveNblsOutstanding--;
    }

  if (Adapter->PausePending && Adapter->ReceiveNblsOutstanding == 0)
    {
      Adapter->PausePending = FALSE;
      CompletePause = TRUE;
    }
  NdisReleaseSpinLock(&Adapter->Lock);

  if (CompletePause)
    NdisMPauseComplete(Adapter->MiniportAdapterHandle);
}

static NDIS_STATUS
NTAPI
MiniportPause(
    IN NDIS_HANDLE MiniportAdapterContext,
    IN PNDIS_MINIPORT_PAUSE_PARAMETERS PauseParameters)
{
  PADAPTER Adapter = (PADAPTER)MiniportAdapterContext;
  PNET_BUFFER_LIST Pending;
  NDIS_STATUS Status = NDIS_STATUS_SUCCESS;

  UNREFERENCED_PARAMETER(PauseParameters);

  NdisAcquireSpinLock(&Adapter->Lock);
  Adapter->Paused = TRUE;
  Pending = Adapter->SendQueueHead;
  Adapter->SendQueueHead = NULL;
  Adapter->SendQueueTail = NULL;
  if (Adapter->ReceiveNblsOutstanding != 0)
    {
      Adapter->PausePending = TRUE;
      Status = NDIS_STATUS_PENDING;
    }
  NdisReleaseSpinLock(&Adapter->Lock);

  MiCompleteSends(Adapter, MiFailNblChain(Pending, NDIS_STATUS_PAUSED), FALSE);
  return Status;
}

static NDIS_STATUS
NTAPI
MiniportRestart(
    IN NDIS_HANDLE MiniportAdapterContext,
    IN PNDIS_MINIPORT_RESTART_PARAMETERS RestartParameters)
{
  PADAPTER Adapter = (PADAPTER)MiniportAdapterContext;

  UNREFERENCED_PARAMETER(RestartParameters);

  NdisAcquireSpinLock(&Adapter->Lock);
  Adapter->Paused = FALSE;
  NdisReleaseSpinLock(&Adapter->Lock);

  MiIndicateLinkState(Adapter);
  return NDIS_STATUS_SUCCESS;
}

static ULONG
NTAPI
MiEthernetCrc(UCHAR *Address)
/*
 * FUNCTION: Calculate Ethernet CRC32
 * ARGUMENTS:
 *     Address: 6-byte ethernet address
 * RETURNS:
 *     The calculated CRC32 value.
 */
{
  UINT Counter, Length;
  ULONG Value = ~0;

  for (Length = 0; Length < 6; Length++)
    {
      Value ^= *Address++;
      for (Counter = 0; Counter < 8; Counter++)
        Value = (Value >> 1) ^ ((Value & 1) * 0xedb88320);
    }

  return Value;
}

static BOOLEAN
NTAPI
MiSyncLoadLogicalAddressFilter(
    IN PVOID SynchronizeContext)
{
  PADAPTER Adapter = (PADAPTER)SynchronizeContext;
  PUCHAR Ladr = Adapter->InitializationBlockVirt->LADR;
  USHORT Rap;
  USHORT Csr5;
  USHORT Data;
  UINT Index;

  NdisRawReadPortUshort(Adapter->PortOffset + RAP, &Rap);
  NdisRawWritePortUshort(Adapter->PortOffset + RAP, CSR5);
  NdisRawReadPortUshort(Adapter->PortOffset + RDP, &Csr5);
  Csr5 &= ~(CSR5_SINT | CSR5_SLPINT | CSR5_EXDINT | CSR5_MPINT);
  NdisRawWritePortUshort(Adapter->PortOffset + RDP, Csr5 | CSR5_SPND);
  for (Index = 0; Index < 2000; Index++)
    {
      NdisRawReadPortUshort(Adapter->PortOffset + RDP, &Data);
      if (Data & CSR5_SPND)
        break;
      NdisStallExecution(1);
    }

  for (Index = 0; Index < 4; Index++)
    {
      NdisRawWritePortUshort(Adapter->PortOffset + RAP, CSR8 + Index);
      NdisRawWritePortUshort(Adapter->PortOffset + RDP, Ladr[Index * 2] | (Ladr[Index * 2 + 1] << 8));
    }

  NdisRawWritePortUshort(Adapter->PortOffset + RAP, CSR5);
  NdisRawWritePortUshort(Adapter->PortOffset + RDP, Csr5 & ~CSR5_SPND);
  NdisRawWritePortUshort(Adapter->PortOffset + RAP, Rap);
  return TRUE;
}

NDIS_STATUS
NTAPI
MiSetMulticast(
    PADAPTER Adapter,
    UCHAR *Addresses,
    UINT AddressCount)
{
  UINT Index;
  ULONG CrcIndex;

  NdisZeroMemory(Adapter->InitializationBlockVirt->LADR, 8);
  for (Index = 0; Index < AddressCount; Index++)
    {
      CrcIndex = MiEthernetCrc(Addresses) >> 26;
      Adapter->InitializationBlockVirt->LADR[CrcIndex >> 3] |= 1 << (CrcIndex & 7);
      Addresses += 6;
    }

  if (Adapter->InterruptHandle)
    NdisMSynchronizeWithInterruptEx(Adapter->InterruptHandle, 0, MiSyncLoadLogicalAddressFilter, Adapter);

  return NDIS_STATUS_SUCCESS;
}

BOOLEAN
NTAPI
MiGetMediaDuplex(PADAPTER Adapter)
{
  ULONG Data;

  NdisRawWritePortUshort(Adapter->PortOffset + RAP, BCR5);
  NdisRawReadPortUshort(Adapter->PortOffset + BDP, &Data);

  return (Data & BCR5_LEDOUT) != 0;
}

UINT
NTAPI
MiGetMediaSpeed(PADAPTER Adapter)
{
  ULONG Data;

  NdisRawWritePortUshort(Adapter->PortOffset + RAP, BCR6);
  NdisRawReadPortUshort(Adapter->PortOffset + BDP, &Data);

  return Data & BCR6_LEDOUT ? 100 : 10;
}

NDIS_MEDIA_STATE
NTAPI
MiGetMediaState(PADAPTER Adapter)
/*
 * FUNCTION: Determine the link state
 * ARGUMENTS:
 *     Adapter: Adapter context
 * RETURNS:
 *     NdisMediaStateConnected if the cable is connected
 *     NdisMediaStateDisconnected if the cable is disconnected
 */
{
  ULONG Data;
  NdisRawWritePortUshort(Adapter->PortOffset + RAP, BCR4);
  NdisRawReadPortUshort(Adapter->PortOffset + BDP, &Data);
  return Data & BCR4_LEDOUT ? NdisMediaStateConnected : NdisMediaStateDisconnected;
}

static VOID
NTAPI
MiniportDevicePnPEventNotify(
    IN NDIS_HANDLE MiniportAdapterContext,
    IN PNET_DEVICE_PNP_EVENT NetDevicePnPEvent)
{
  UNREFERENCED_PARAMETER(MiniportAdapterContext);
  UNREFERENCED_PARAMETER(NetDevicePnPEvent);
}

static VOID
NTAPI
MiniportUnload(
    IN PDRIVER_OBJECT DriverObject)
{
  UNREFERENCED_PARAMETER(DriverObject);
  NdisMDeregisterMiniportDriver(MiniportDriverHandle);
}

NTSTATUS
NTAPI
DriverEntry(
    IN PDRIVER_OBJECT DriverObject,
    IN PUNICODE_STRING RegistryPath)
{
  NDIS_MINIPORT_DRIVER_CHARACTERISTICS Characteristics;

  RtlZeroMemory(&Characteristics, sizeof(Characteristics));
  Characteristics.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_DRIVER_CHARACTERISTICS;
  Characteristics.Header.Revision = NDIS_MINIPORT_DRIVER_CHARACTERISTICS_REVISION_1;
  Characteristics.Header.Size = NDIS_SIZEOF_MINIPORT_DRIVER_CHARACTERISTICS_REVISION_1;
  Characteristics.MajorNdisVersion = NDIS_MINIPORT_MAJOR_VERSION;
  Characteristics.MinorNdisVersion = NDIS_MINIPORT_MINOR_VERSION;
  Characteristics.MajorDriverVersion = 1;
  Characteristics.InitializeHandlerEx = MiniportInitializeEx;
  Characteristics.HaltHandlerEx = MiniportHaltEx;
  Characteristics.UnloadHandler = MiniportUnload;
  Characteristics.PauseHandler = MiniportPause;
  Characteristics.RestartHandler = MiniportRestart;
  Characteristics.OidRequestHandler = MiniportOidRequest;
  Characteristics.SendNetBufferListsHandler = MiniportSendNetBufferLists;
  Characteristics.ReturnNetBufferListsHandler = MiniportReturnNetBufferLists;
  Characteristics.CancelSendHandler = MiniportCancelSend;
  Characteristics.DevicePnPEventNotifyHandler = MiniportDevicePnPEventNotify;
  Characteristics.ShutdownHandlerEx = MiniportShutdownEx;
  Characteristics.CancelOidRequestHandler = MiniportCancelOidRequest;

  return NdisMRegisterMiniportDriver(DriverObject, RegistryPath, NULL, &Characteristics, &MiniportDriverHandle);
}
