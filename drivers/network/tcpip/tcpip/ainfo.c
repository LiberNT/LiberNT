/*
 * COPYRIGHT:   See COPYING in the top level directory
 * PROJECT:     ReactOS TCP/IP protocol driver
 * FILE:        tcpip/ainfo.c
 * PURPOSE:     Per-socket information.
 * PROGRAMMER:  Cameron Gutman
 */

#include "precomp.h"
#include <libernt/drivers/qosmark.h>

TDI_STATUS SetAddressFileInfo(TDIObjectID *ID,
                              PADDRESS_FILE AddrFile,
                              PVOID Buffer,
                              UINT BufferSize)
{
    switch (ID->toi_id)
    {
      case AO_OPTION_MCASTIF:
      {
         ULONG Selector;
         PIP_INTERFACE Interface;
         if (BufferSize < sizeof(Selector)) return TDI_INVALID_PARAMETER;
         RtlCopyMemory(&Selector, Buffer, sizeof(Selector));
         if (Selector)
         {
             Interface = GetMulticastInterface(Selector);
             if (!Interface) return TDI_INVALID_PARAMETER;
             IPDereferenceInterface(Interface);
         }
         LockObject(AddrFile);
         AddrFile->MulticastInterface = Selector;
         UnlockObject(AddrFile);
         return TDI_SUCCESS;
      }

      case AO_OPTION_IP_UCASTIF:
      {
         ULONG Selector;
         PIP_INTERFACE Interface;
         if (BufferSize < sizeof(Selector)) return TDI_INVALID_PARAMETER;
         RtlCopyMemory(&Selector, Buffer, sizeof(Selector));
         if (Selector)
         {
             if (DN2H(Selector) >= 0x01000000) return TDI_INVALID_PARAMETER;
             Interface = GetMulticastInterface(Selector);
             if (!Interface) return TDI_INVALID_PARAMETER;
             IPDereferenceInterface(Interface);
         }
         LockObject(AddrFile);
         AddrFile->UnicastInterface = Selector;
         UnlockObject(AddrFile);
         return TDI_SUCCESS;
      }

      case AO_OPTION_ADD_MCAST:
      case AO_OPTION_DEL_MCAST:
      {
         ULONG Request[2];
         PIP_INTERFACE Interface;
         ULONG Index;

         if (AddrFile->Family == TDI_ADDRESS_TYPE_IP6 && BufferSize >= 16 + sizeof(ULONG))
         {
             RtlCopyMemory(&Index, (PUCHAR)Buffer + 16, sizeof(Index));
             return LibIP6ChangeMembership(AddrFile, Index, Buffer, ID->toi_id == AO_OPTION_ADD_MCAST);
         }
         if (BufferSize < sizeof(Request)) return TDI_INVALID_PARAMETER;
         RtlCopyMemory(Request, Buffer, sizeof(Request));
         Interface = GetMulticastInterface(Request[1]);
         if (!Interface) return TDI_INVALID_PARAMETER;
         Index = Interface->Index;
         IPDereferenceInterface(Interface);
         if (ID->toi_id == AO_OPTION_ADD_MCAST)
             return IGMPJoinGroup(AddrFile, Index, Request[0]);
         return IGMPLeaveGroup(AddrFile, Index, Request[0]);
      }

      case AO_OPTION_XSUM:
         if (BufferSize < sizeof(ULONG))
             return TDI_INVALID_PARAMETER;

         LockObject(AddrFile);
         AddrFile->NoChecksum = (*(PULONG)Buffer == 0);
         UnlockObject(AddrFile);

         return TDI_SUCCESS;

      case AO_OPTION_UDP_CONNRESET:
         if (BufferSize < sizeof(ULONG))
             return TDI_INVALID_PARAMETER;

         LockObject(AddrFile);
         AddrFile->NoPortUnreachable = (*(PULONG)Buffer == 0);
         UnlockObject(AddrFile);

         return TDI_SUCCESS;

      case AO_OPTION_UDP_SEND_MSG_SIZE:
         if (BufferSize < sizeof(ULONG))
             return TDI_INVALID_PARAMETER;

         LockObject(AddrFile);
         AddrFile->SendMsgSize = *(PULONG)Buffer;
         UnlockObject(AddrFile);

         return TDI_SUCCESS;

      case AO_OPTION_IP_PKTINFO:
      case AO_OPTION_IP_RECVTTL:
      case AO_OPTION_IP_RECVTOS:
      case AO_OPTION_IP_RECVECN:
      case AO_OPTION_IP_RECVDSTADDR:
      case AO_OPTION_IPV6_PKTINFO:
      case AO_OPTION_IPV6_RECVHOPLIMIT:
      case AO_OPTION_IPV6_RECVTCLASS:
      case AO_OPTION_IPV6_RECVECN:
      {
         BOOLEAN Enable;

         if (BufferSize < sizeof(ULONG))
             return TDI_INVALID_PARAMETER;

         Enable = (*(PULONG)Buffer != 0);
         LockObject(AddrFile);
         switch (ID->toi_id)
         {
            case AO_OPTION_IP_PKTINFO: AddrFile->RecvPktInfo = Enable; break;
            case AO_OPTION_IP_RECVTTL: AddrFile->RecvTtl = Enable; break;
            case AO_OPTION_IP_RECVTOS: AddrFile->RecvTos = Enable; break;
            case AO_OPTION_IP_RECVECN: AddrFile->RecvEcn = Enable; break;
            case AO_OPTION_IPV6_PKTINFO: AddrFile->RecvPktInfo6 = Enable; break;
            case AO_OPTION_IPV6_RECVHOPLIMIT: AddrFile->RecvHopLimit6 = Enable; break;
            case AO_OPTION_IPV6_RECVTCLASS: AddrFile->RecvTclass6 = Enable; break;
            case AO_OPTION_IPV6_RECVECN: AddrFile->RecvEcn6 = Enable; break;
            default: AddrFile->RecvDstAddr = Enable; break;
         }
         UnlockObject(AddrFile);

         return TDI_SUCCESS;
      }

      case AO_OPTION_MCASTTTL:
         if (BufferSize < sizeof(UINT) || *(PUINT)Buffer > 255)
             return TDI_INVALID_PARAMETER;
         LockObject(AddrFile);
         AddrFile->MulticastTTL = (UCHAR)*(PUINT)Buffer;
         UnlockObject(AddrFile);
         return TDI_SUCCESS;

      case AO_OPTION_TTL:
         if (BufferSize < sizeof(UINT))
             return TDI_INVALID_PARAMETER;

         LockObject(AddrFile);
         AddrFile->TTL = *((PUCHAR)Buffer);
         UnlockObject(AddrFile);

         return TDI_SUCCESS;

      case AO_OPTION_QOS_DSCP:
         if (BufferSize < sizeof(ULONG) ||
             (*(PULONG)Buffer != QOS_MARK_UNMARKED && *(PULONG)Buffer > QOS_MARK_DSCP_LAST))
         {
             return TDI_INVALID_PARAMETER;
         }

         LockObject(AddrFile);
         AddrFile->QosTos = (*(PULONG)Buffer == QOS_MARK_UNMARKED) ? 0 : (UCHAR)(*(PULONG)Buffer << 2);
         UnlockObject(AddrFile);

         return TDI_SUCCESS;

      case AO_OPTION_IP_DONTFRAGMENT:
         if (BufferSize < sizeof(UINT))
             return TDI_INVALID_PARAMETER;

         LockObject(AddrFile);
         AddrFile->DF = *((PUINT)Buffer);
         UnlockObject(AddrFile);

         return TDI_SUCCESS;

      case AO_OPTION_BROADCAST:
         if (BufferSize < sizeof(UINT))
             return TDI_INVALID_PARAMETER;

         LockObject(AddrFile);
         AddrFile->BCast = *((PUINT)Buffer);
         UnlockObject(AddrFile);

         return TDI_SUCCESS;

      case AO_OPTION_IP_HDRINCL:
         if (BufferSize < sizeof(UINT))
             return TDI_INVALID_PARAMETER;

         LockObject(AddrFile);
         AddrFile->HeaderIncl = *((PUINT)Buffer);
         UnlockObject(AddrFile);

         return TDI_SUCCESS;

      default:
         DbgPrint("Unimplemented option %x\n", ID->toi_id);

         return TDI_INVALID_REQUEST;
    }
}

TDI_STATUS GetAddressFileInfo(TDIObjectID *ID,
                              PADDRESS_FILE AddrFile,
                              PVOID Buffer,
                              PUINT BufferSize)
{
    UNIMPLEMENTED;

    return TDI_INVALID_REQUEST;
}
