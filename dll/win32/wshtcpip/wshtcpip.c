/*
 * COPYRIGHT:   See COPYING in the top level directory
 * PROJECT:     ReactOS WinSock Helper DLL for TCP/IP
 * FILE:        wshtcpip.c
 * PURPOSE:     DLL entry
 * PROGRAMMERS: Casper S. Hornstrup (chorns@users.sourceforge.net)
 * REVISIONS:
 *   CSH 01/09-2000 Created
 */
#include "wshtcpip.h"
#include <netioapi.h>
#define NDEBUG
#include <debug.h>

BOOL
EXPORT
DllMain(HANDLE hInstDll,
        ULONG dwReason,
        PVOID Reserved)
{
    DPRINT("DllMain of wshtcpip.dll\n");

    switch (dwReason) {
    case DLL_PROCESS_ATTACH:
        /* Don't need thread attach notifications
           so disable them to improve performance */
        DisableThreadLibraryCalls(hInstDll);
        break;

    case DLL_THREAD_ATTACH:
        break;

    case DLL_THREAD_DETACH:
        break;

    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}


INT
EXPORT
WSHAddressToString(
    IN      LPSOCKADDR Address,
    IN      INT AddressLength,
    IN      LPWSAPROTOCOL_INFOW ProtocolInfo    OPTIONAL,
    OUT     LPWSTR AddressString,
    IN OUT  LPDWORD AddressStringLength)
{
    UNIMPLEMENTED;

    return NO_ERROR;
}


INT
EXPORT
WSHEnumProtocols(
    IN      LPINT lpiProtocols  OPTIONAL,
    IN      LPWSTR lpTransportKeyName,
    IN OUT  LPVOID lpProtocolBuffer,
    IN OUT  LPDWORD lpdwBufferLength)
{
    UNIMPLEMENTED;

    return NO_ERROR;
}


INT
EXPORT
WSHGetBroadcastSockaddr(
    IN  PVOID HelperDllSocketContext,
    OUT PSOCKADDR Sockaddr,
    OUT PINT SockaddrLength)
{
    INT Size = 2 * sizeof(UINT);

    if (*SockaddrLength < Size)
    {
        DPRINT1("Socket address length too small: %d\n", *SockaddrLength);
        return WSAEFAULT;
    }

    RtlZeroMemory(Sockaddr, *SockaddrLength);

    Sockaddr->sa_family = AF_INET;
    *((PUINT)Sockaddr->sa_data) = INADDR_BROADCAST;

    /* *SockaddrLength = Size; */

    return NO_ERROR;
}


INT
EXPORT
WSHGetProviderGuid(
    IN  LPWSTR ProviderName,
    OUT LPGUID ProviderGuid)
{
    UNIMPLEMENTED;

    return NO_ERROR;
}


/*
Document from OSR how WSHGetSockaddrType works
http://www.osronline.com/ddkx/network/37wshfun_5lyq.htm
*/

INT
EXPORT
WSHGetSockaddrType(
    IN  PSOCKADDR Sockaddr,
    IN  DWORD SockaddrLength,
    OUT PSOCKADDR_INFO SockaddrInfo)
{
    PSOCKADDR_IN ipv4 = (PSOCKADDR_IN)Sockaddr;

    if (ipv4 && SockaddrInfo && ipv4->sin_family == AF_INET6)
    {
        PSOCKADDR_IN6 ipv6 = (PSOCKADDR_IN6)Sockaddr;

        if (SockaddrLength < sizeof(SOCKADDR_IN6))
            return WSAEFAULT;

        if (IN6_IS_ADDR_UNSPECIFIED(&ipv6->sin6_addr))
            SockaddrInfo->AddressInfo = SockaddrAddressInfoWildcard;
        else if (IN6_IS_ADDR_LOOPBACK(&ipv6->sin6_addr))
            SockaddrInfo->AddressInfo = SockaddrAddressInfoLoopback;
        else
            SockaddrInfo->AddressInfo = SockaddrAddressInfoNormal;

        if (ntohs(ipv6->sin6_port) == 0)
            SockaddrInfo->EndpointInfo = SockaddrEndpointInfoWildcard;
        else if (ntohs(ipv6->sin6_port) < IPPORT_RESERVED)
            SockaddrInfo->EndpointInfo = SockaddrEndpointInfoReserved;
        else
            SockaddrInfo->EndpointInfo = SockaddrEndpointInfoNormal;

        return NO_ERROR;
    }

    if (!ipv4 || !SockaddrInfo || SockaddrLength < sizeof(SOCKADDR_IN) ||
        ipv4->sin_family != AF_INET)
    {
        DPRINT1("Invalid parameter: %x %x %d %u\n", ipv4, SockaddrInfo, SockaddrLength, (ipv4 ? ipv4->sin_family : 0));
        return WSAEINVAL;
    }

    switch (ntohl(ipv4->sin_addr.s_addr))
    {
      case INADDR_ANY:
           SockaddrInfo->AddressInfo = SockaddrAddressInfoWildcard;
           break;

      case INADDR_BROADCAST:
           SockaddrInfo->AddressInfo = SockaddrAddressInfoBroadcast;
           break;

      case INADDR_LOOPBACK:
           SockaddrInfo->AddressInfo = SockaddrAddressInfoLoopback;
           break;

      default:
           SockaddrInfo->AddressInfo = SockaddrAddressInfoNormal;
	   break;
    }

    if (ntohs(ipv4->sin_port) == 0)
 	SockaddrInfo->EndpointInfo = SockaddrEndpointInfoWildcard;
    else if (ntohs(ipv4->sin_port) < IPPORT_RESERVED)
	SockaddrInfo->EndpointInfo = SockaddrEndpointInfoReserved;
    else
        SockaddrInfo->EndpointInfo = SockaddrEndpointInfoNormal;

    return NO_ERROR;
}

static
void
GetTdiTypeId(
    _In_ INT Level,
    _In_ INT OptionName,
    _Out_ PULONG TdiType,
    _Out_ PULONG TdiId)
{
    switch (Level)
    {
       case SOL_SOCKET:
          *TdiType = INFO_TYPE_CONNECTION;
          switch (OptionName)
          {
             case SO_KEEPALIVE:
                *TdiId = TCP_SOCKET_KEEPALIVE;
                return;
             default:
                break;
          }
          break;

       case IPPROTO_IP:
          *TdiType = INFO_TYPE_ADDRESS_OBJECT;
          switch (OptionName)
          {
             case IP_TTL:
                *TdiId = AO_OPTION_TTL;
                return;

             case IP_MULTICAST_IF:
                *TdiId = AO_OPTION_MCASTIF;
                return;

             case IP_MULTICAST_TTL:
                *TdiId = AO_OPTION_MCASTTTL;
                return;

             case IP_ADD_MEMBERSHIP:
                *TdiId = AO_OPTION_ADD_MCAST;
                return;

             case IP_DROP_MEMBERSHIP:
                *TdiId = AO_OPTION_DEL_MCAST;
                return;

             case IP_UNICAST_IF:
                *TdiId = AO_OPTION_IP_UCASTIF;
                return;

             case IP_DONTFRAGMENT:
                 *TdiId = AO_OPTION_IP_DONTFRAGMENT;
                 return;

#if 0
             case IP_RECEIVE_BROADCAST:
                 *TdiId = AO_OPTION_BROADCAST;
                 return;
#endif

             case IP_HDRINCL:
                 *TdiId = AO_OPTION_IP_HDRINCL;
                 return;

             case IP_PKTINFO:
                 *TdiId = AO_OPTION_IP_PKTINFO;
                 return;

             case IP_RECVTTL:
                 *TdiId = AO_OPTION_IP_RECVTTL;
                 return;

             case IP_RECVTOS:
                 *TdiId = AO_OPTION_IP_RECVTOS;
                 return;

             case IP_RECVECN:
                 *TdiId = AO_OPTION_IP_RECVECN;
                 return;

             case IP_RECVDSTADDR:
                 *TdiId = AO_OPTION_IP_RECVDSTADDR;
                 return;

             default:
                break;
          }
          break;

       case IPPROTO_IPV6:
          *TdiType = INFO_TYPE_ADDRESS_OBJECT;
          switch (OptionName)
          {
             case IPV6_ADD_MEMBERSHIP:
                *TdiId = AO_OPTION_ADD_MCAST;
                return;

             case IPV6_DROP_MEMBERSHIP:
                *TdiId = AO_OPTION_DEL_MCAST;
                return;

             default:
                break;
          }
          break;

       case IPPROTO_UDP:
          *TdiType = INFO_TYPE_ADDRESS_OBJECT;
          switch (OptionName)
          {
             case UDP_NOCHECKSUM:
                *TdiId = AO_OPTION_XSUM;
                return;

             case UDP_SEND_MSG_SIZE:
                *TdiId = AO_OPTION_UDP_SEND_MSG_SIZE;
                return;

             default:
                break;
          }
          break;

       case IPPROTO_TCP:
          *TdiType = INFO_TYPE_CONNECTION;
          switch (OptionName)
          {
             case TCP_NODELAY:
                 *TdiId = TCP_SOCKET_NODELAY;
                 return;
             default:
                 break;
          }

       default:
          break;
    }

    DPRINT1("Unknown level/option name: %d %d\n", Level, OptionName);
    *TdiType = 0;
    *TdiId = 0;
}

static ULONG
RecvInfoOptionBit(
    _In_ INT OptionName)
{
    switch (OptionName)
    {
        case IP_PKTINFO:
            return 0x1;
        case IP_RECVTTL:
            return 0x2;
        case IP_RECVTOS:
            return 0x4;
        case IP_RECVECN:
            return 0x8;
        case IP_RECVDSTADDR:
            return 0x10;
        default:
            return 0;
    }
}

static BOOLEAN
IsWindowsIpOption(
    _In_ INT OptionName)
{
    switch (OptionName)
    {
        case IP_OPTIONS:
        case IP_HDRINCL:
        case IP_TOS:
        case IP_TTL:
        case IP_MULTICAST_IF:
        case IP_MULTICAST_TTL:
        case IP_MULTICAST_LOOP:
        case IP_ADD_MEMBERSHIP:
        case IP_DROP_MEMBERSHIP:
        case IP_DONTFRAGMENT:
        case IP_ADD_SOURCE_MEMBERSHIP:
        case IP_DROP_SOURCE_MEMBERSHIP:
        case IP_BLOCK_SOURCE:
        case IP_UNBLOCK_SOURCE:
        case IP_PKTINFO:
        case IP_RECVTTL:
        case IP_RECEIVE_BROADCAST:
        case IP_RECVIF:
        case IP_RECVDSTADDR:
        case IP_IFLIST:
        case IP_ADD_IFLIST:
        case IP_DEL_IFLIST:
        case IP_UNICAST_IF:
        case IP_RTHDR:
        case IP_RECVRTHDR:
        case IP_TCLASS:
        case IP_RECVTOS:
        case IP_ORIGINAL_ARRIVAL_IF:
        case IP_RECVECN:
            return TRUE;
        default:
            return FALSE;
    }
}

INT
EXPORT
WSHGetSocketInformation(
    IN  PVOID HelperDllSocketContext,
    IN  SOCKET SocketHandle,
    IN  HANDLE TdiAddressObjectHandle,
    IN  HANDLE TdiConnectionObjectHandle,
    IN  INT Level,
    IN  INT OptionName,
    OUT PCHAR OptionValue,
    OUT LPINT OptionLength)
{
    PSOCKET_CONTEXT Context = HelperDllSocketContext;
    if (Level == IPPROTO_IP && (OptionName == IP_MULTICAST_IF || OptionName == IP_MULTICAST_TTL))
    {
        if (Context->SocketType != SOCK_DGRAM && Context->SocketType != SOCK_RAW)
            return WSAENOPROTOOPT;
        if (!OptionLength || !OptionValue || *OptionLength < sizeof(ULONG))
            return WSAEFAULT;
        memcpy(OptionValue, OptionName == IP_MULTICAST_IF ? &Context->MulticastInterface :
                                                         &Context->MulticastTTL, sizeof(ULONG));
        *OptionLength = sizeof(ULONG);
        return NO_ERROR;
    }
    if (Level == IPPROTO_IP && OptionName == IP_UNICAST_IF)
    {
        if (!OptionLength || !OptionValue || *OptionLength < sizeof(ULONG))
            return WSAEFAULT;
        memcpy(OptionValue, &Context->UnicastInterface, sizeof(ULONG));
        *OptionLength = sizeof(ULONG);
        return NO_ERROR;
    }
    if (Level == IPPROTO_UDP)
    {
        if (Context->SocketType != SOCK_DGRAM)
            return WSAEINVAL;
        if (!OptionLength || !OptionValue)
            return WSAEFAULT;
        switch (OptionName)
        {
            case UDP_NOCHECKSUM:
                if (*OptionLength < sizeof(BOOLEAN))
                    return WSAEFAULT;
                *(BOOLEAN *)OptionValue = Context->UdpNoChecksum;
                *OptionLength = sizeof(BOOLEAN);
                return NO_ERROR;

            case UDP_SEND_MSG_SIZE:
            case UDP_RECV_MAX_COALESCED_SIZE:
                if (*OptionLength < sizeof(DWORD))
                    return WSAEFAULT;
                memcpy(OptionValue,
                       OptionName == UDP_SEND_MSG_SIZE ? &Context->UdpSendMsgSize : &Context->UdpRecvMaxCoalescedSize,
                       sizeof(DWORD));
                *OptionLength = sizeof(DWORD);
                return NO_ERROR;

            default:
                return WSAEINVAL;
        }
    }
    if (Level == IPPROTO_IP && OptionName == IP_TTL)
    {
        if (!OptionLength || !OptionValue || *OptionLength < 1)
            return WSAEFAULT;
        if (*OptionLength >= sizeof(DWORD))
        {
            *(DWORD *)OptionValue = Context->Ttl;
            *OptionLength = sizeof(DWORD);
        }
        else
        {
            *(PUCHAR)OptionValue = (UCHAR)Context->Ttl;
            *OptionLength = sizeof(UCHAR);
        }
        return NO_ERROR;
    }
    if ((Level == IPPROTO_IP && OptionName == IP_DONTFRAGMENT) ||
        (Level == IPPROTO_IPV6 && OptionName == IPV6_DONTFRAG))
    {
        if (!OptionLength || !OptionValue || *OptionLength < sizeof(DWORD))
            return WSAEFAULT;
        *(DWORD *)OptionValue = Context->DontFragment ? 1 : 0;
        *OptionLength = sizeof(DWORD);
        return NO_ERROR;
    }
    if (Level == IPPROTO_IP && RecvInfoOptionBit(OptionName))
    {
        if (!OptionLength || !OptionValue || *OptionLength < sizeof(DWORD))
            return WSAEFAULT;
        *(DWORD *)OptionValue = (Context->RecvInfoOptions & RecvInfoOptionBit(OptionName)) ? 1 : 0;
        *OptionLength = sizeof(DWORD);
        return NO_ERROR;
    }
    if (Level == IPPROTO_IP && OptionName == IP_HDRINCL && Context->SocketType != SOCK_RAW)
        return WSAEINVAL;
    if (Level == IPPROTO_IPV6 && OptionName == IPV6_V6ONLY)
    {
        if (!OptionLength || !OptionValue || *OptionLength < sizeof(DWORD))
            return WSAEFAULT;
        *(DWORD *)OptionValue = Context->V6Only ? 1 : 0;
        *OptionLength = sizeof(DWORD);
        return NO_ERROR;
    }
    if (Level == SOL_SOCKET)
    {
        switch (OptionName)
        {
            case SO_DONTROUTE:
            case SO_KEEPALIVE:
                if (!OptionLength || !OptionValue || *OptionLength < sizeof(BOOL))
                    return WSAEFAULT;
                *(BOOL *)OptionValue = (OptionName == SO_DONTROUTE) ? Context->DontRoute : Context->KeepAlive;
                *OptionLength = sizeof(BOOL);
                return NO_ERROR;

            case SO_MAX_MSG_SIZE:
                if (Context->SocketType != SOCK_DGRAM && Context->SocketType != SOCK_STREAM)
                    return WSAENOPROTOOPT;
                if (!OptionLength || !OptionValue || *OptionLength < sizeof(DWORD))
                    return WSAEFAULT;
                if (Context->SocketType == SOCK_STREAM)
                    *(DWORD *)OptionValue = 0xFFFFFFFF;
                else
                    *(DWORD *)OptionValue = Context->AddressFamily == AF_INET6 ? 0xFFFF - 8 : 0xFFFF - 20 - 8;
                *OptionLength = sizeof(DWORD);
                return NO_ERROR;

            default:
                return WSAENOPROTOOPT;
        }
    }
    if (Level == IPPROTO_IP || Level == IPPROTO_TCP || Level == IPPROTO_IPV6)
        return WSAENOPROTOOPT;

    DPRINT1("Get: Unknown level/option name: %d %d\n", Level, OptionName);
    return WSAEINVAL;
}


INT
EXPORT
WSHGetWildcardSockaddr(
    IN  PVOID HelperDllSocketContext,
    OUT PSOCKADDR Sockaddr,
    OUT PINT SockaddrLength)
{
    INT Size = 2 * sizeof(UINT);
    PSOCKET_CONTEXT Context = HelperDllSocketContext;

    if (Context && Context->AddressFamily == AF_INET6)
    {
        if (*SockaddrLength < (INT)sizeof(SOCKADDR_IN6))
            return WSAEFAULT;

        RtlZeroMemory(Sockaddr, sizeof(SOCKADDR_IN6));
        Sockaddr->sa_family = AF_INET6;
        *SockaddrLength = sizeof(SOCKADDR_IN6);
        return NO_ERROR;
    }

    if (*SockaddrLength < Size)
    {
        DPRINT1("Socket address length too small: %d\n", *SockaddrLength);
        return WSAEFAULT;
    }

    RtlZeroMemory(Sockaddr, *SockaddrLength);

    Sockaddr->sa_family = AF_INET;
    *((PUINT)Sockaddr->sa_data) = INADDR_ANY;

    /* *SockaddrLength = Size; */

    return NO_ERROR;
}


DWORD
EXPORT
WSHGetWinsockMapping(
    OUT PWINSOCK_MAPPING Mapping,
    IN  DWORD MappingLength)
{
    DWORD Rows = 6;
    DWORD Columns = 3;
    DWORD Size = 2 * sizeof(DWORD) + Columns * Rows * sizeof(DWORD);

    if (MappingLength < Size)
    {
        DPRINT1("Mapping length too small: %d\n", MappingLength);
        return Size;
    }

    Mapping->Rows = Rows;
    Mapping->Columns = Columns;

    Mapping->Mapping[0].AddressFamily = AF_INET;
    Mapping->Mapping[0].SocketType = SOCK_STREAM;
    Mapping->Mapping[0].Protocol = 0;

    Mapping->Mapping[1].AddressFamily = AF_INET;
    Mapping->Mapping[1].SocketType = SOCK_STREAM;
    Mapping->Mapping[1].Protocol = IPPROTO_TCP;

    Mapping->Mapping[2].AddressFamily = AF_INET;
    Mapping->Mapping[2].SocketType = SOCK_DGRAM;
    Mapping->Mapping[2].Protocol = 0;

    Mapping->Mapping[3].AddressFamily = AF_INET;
    Mapping->Mapping[3].SocketType = SOCK_DGRAM;
    Mapping->Mapping[3].Protocol = IPPROTO_UDP;

    Mapping->Mapping[4].AddressFamily = AF_INET;
    Mapping->Mapping[4].SocketType = SOCK_RAW;
    Mapping->Mapping[4].Protocol = 0;

    Mapping->Mapping[5].AddressFamily = AF_INET;
    Mapping->Mapping[5].SocketType = SOCK_RAW;
    Mapping->Mapping[5].Protocol = IPPROTO_ICMP;

    return NO_ERROR;
}


INT
EXPORT
WSHGetWSAProtocolInfo(
    IN  LPWSTR ProviderName,
    OUT LPWSAPROTOCOL_INFOW *ProtocolInfo,
    OUT LPDWORD ProtocolInfoEntries)
{
    UNIMPLEMENTED;

    return NO_ERROR;
}


typedef DWORD (WINAPI *PGET_BEST_ROUTE2)(NET_LUID *, NET_IFINDEX, const SOCKADDR_INET *,
                                         const SOCKADDR_INET *, ULONG, PMIB_IPFORWARD_ROW2, SOCKADDR_INET *);

static
INT
WSHIoctl_RoutingInterfaceQuery(
    IN  PSOCKET_CONTEXT Context,
    IN  LPVOID InputBuffer,
    IN  DWORD InputBufferLength,
    IN  LPVOID OutputBuffer,
    IN  DWORD OutputBufferLength,
    OUT LPDWORD NumberOfBytesReturned)
{
    static PGET_BEST_ROUTE2 pGetBestRoute2;
    SOCKADDR_INET Destination, Source;
    MIB_IPFORWARD_ROW2 Route;
    DWORD AddressLength;
    HMODULE Iphlpapi;

    if (!InputBuffer || InputBufferLength < sizeof(USHORT))
        return WSAEFAULT;

    RtlZeroMemory(&Destination, sizeof(Destination));
    Destination.si_family = ((PSOCKADDR)InputBuffer)->sa_family;
    if (Destination.si_family != Context->AddressFamily)
        return WSAEAFNOSUPPORT;

    if (Destination.si_family == AF_INET)
    {
        AddressLength = sizeof(SOCKADDR_IN);
        if (InputBufferLength < AddressLength)
            return WSAEFAULT;
        Destination.Ipv4 = *(PSOCKADDR_IN)InputBuffer;
        if (Destination.Ipv4.sin_addr.s_addr == INADDR_ANY)
            return WSAEINVAL;
    }
    else if (Destination.si_family == AF_INET6)
    {
        AddressLength = sizeof(SOCKADDR_IN6);
        if (InputBufferLength < AddressLength)
            return WSAEFAULT;
        Destination.Ipv6 = *(PSOCKADDR_IN6)InputBuffer;
        if (IN6_IS_ADDR_UNSPECIFIED(&Destination.Ipv6.sin6_addr))
            return WSAEINVAL;
    }
    else
    {
        return WSAEAFNOSUPPORT;
    }

    *NumberOfBytesReturned = AddressLength;
    if (!OutputBuffer || OutputBufferLength < AddressLength)
        return WSAEFAULT;

    if (!pGetBestRoute2)
    {
        Iphlpapi = LoadLibraryW(L"iphlpapi.dll");
        if (Iphlpapi)
            pGetBestRoute2 = (PGET_BEST_ROUTE2)GetProcAddress(Iphlpapi, "GetBestRoute2");
        if (!pGetBestRoute2)
            return WSAENETUNREACH;
    }

    RtlZeroMemory(&Source, sizeof(Source));
    if (pGetBestRoute2(NULL, 0, NULL, &Destination, 0, &Route, &Source) != NO_ERROR)
        return WSAENETUNREACH;

    if (Source.si_family == AF_INET)
        Source.Ipv4.sin_port = 0;
    else
        Source.Ipv6.sin6_port = 0;

    RtlCopyMemory(OutputBuffer, &Source, AddressLength);
    return NO_ERROR;
}

INT
EXPORT
WSHIoctl(
    IN  PVOID HelperDllSocketContext,
    IN  SOCKET SocketHandle,
    IN  HANDLE TdiAddressObjectHandle,
    IN  HANDLE TdiConnectionObjectHandle,
    IN  DWORD IoControlCode,
    IN  LPVOID InputBuffer,
    IN  DWORD InputBufferLength,
    IN  LPVOID OutputBuffer,
    IN  DWORD OutputBufferLength,
    OUT LPDWORD NumberOfBytesReturned,
    IN  LPWSAOVERLAPPED Overlapped,
    IN  LPWSAOVERLAPPED_COMPLETION_ROUTINE CompletionRoutine,
    OUT LPBOOL NeedsCompletion)
{
    INT res;
    PSOCKET_CONTEXT Context = HelperDllSocketContext;
    PTCP_REQUEST_SET_INFORMATION_EX Info;
    PQUEUED_REQUEST Queued, NextQueued;

    if (IoControlCode == SIO_GET_INTERFACE_LIST)
    {
        res = WSHIoctl_GetInterfaceList(
            OutputBuffer,
            OutputBufferLength,
            NumberOfBytesReturned,
            NeedsCompletion);
        return res;
    }
    else if (IoControlCode == SIO_ROUTING_INTERFACE_QUERY)
    {
        return WSHIoctl_RoutingInterfaceQuery(Context,
                                              InputBuffer,
                                              InputBufferLength,
                                              OutputBuffer,
                                              OutputBufferLength,
                                              NumberOfBytesReturned);
    }
    else if (IoControlCode == SIO_KEEPALIVE_VALS)
    {
        if (!InputBuffer || InputBufferLength != sizeof(struct tcp_keepalive))
        {
            return WSAEINVAL;
        }

        Info = HeapAlloc(GetProcessHeap(), 0, sizeof(*Info) + InputBufferLength);
        if (!Info)
            return WSAENOBUFS;

        Info->ID.toi_entity.tei_entity = Context->AddrFileEntityType;
        Info->ID.toi_entity.tei_instance = Context->AddrFileInstance;
        Info->ID.toi_class = INFO_CLASS_PROTOCOL;
        Info->ID.toi_type = INFO_TYPE_CONNECTION;
        Info->ID.toi_id = TCP_SOCKET_KEEPALIVEVALS;
        Info->BufferSize = InputBufferLength;
        memcpy(Info->Buffer, InputBuffer, InputBufferLength);

        if (Context->SocketState == SocketStateCreated)
        {
            if (Context->RequestQueue)
            {
                Queued = Context->RequestQueue;
                while ((NextQueued = Queued->Next))
                {
                    Queued = NextQueued;
                }

                Queued->Next = HeapAlloc(GetProcessHeap(), 0, sizeof(QUEUED_REQUEST));
                if (!Queued->Next)
                {
                    HeapFree(GetProcessHeap(), 0, Info);
                    return WSAENOBUFS;
                }

                NextQueued = Queued->Next;
                NextQueued->Next = NULL;
                NextQueued->Info = Info;
            }
            else
            {
                Context->RequestQueue = HeapAlloc(GetProcessHeap(), 0, sizeof(QUEUED_REQUEST));
                if (!Context->RequestQueue)
                {
                    HeapFree(GetProcessHeap(), 0, Info);
                    return WSAENOBUFS;
                }

                Context->RequestQueue->Next = NULL;
                Context->RequestQueue->Info = Info;
            }

            return 0;
        }

        res = SendRequest(Info, sizeof(*Info) + Info->BufferSize, IOCTL_TCP_SET_INFORMATION_EX, TdiAddressObjectHandle);

        HeapFree(GetProcessHeap(), 0, Info);
        return res;
    }

    UNIMPLEMENTED;

    DPRINT1("Ioctl: Unknown IOCTL code: %x\n", IoControlCode);

    return WSAEINVAL;
}


INT
EXPORT
WSHJoinLeaf(
    IN  PVOID HelperDllSocketContext,
    IN  SOCKET SocketHandle,
    IN  HANDLE TdiAddressObjectHandle,
    IN  HANDLE TdiConnectionObjectHandle,
    IN  PVOID LeafHelperDllSocketContext,
    IN  SOCKET LeafSocketHandle,
    IN  PSOCKADDR Sockaddr,
    IN  DWORD SockaddrLength,
    IN  LPWSABUF CallerData,
    IN  LPWSABUF CalleeData,
    IN  LPQOS SocketQOS,
    IN  LPQOS GroupQOS,
    IN  DWORD Flags)
{
    UNIMPLEMENTED;

    return NO_ERROR;
}

INT
SendRequest(
    IN PVOID Request,
    IN DWORD RequestSize,
    IN DWORD IOCTL,
    IN HANDLE TdiAddressObjectHandle)
{
    BOOLEAN Success;
    HANDLE TcpCC;
    DWORD BytesReturned;

    if (TdiAddressObjectHandle)
        TcpCC = TdiAddressObjectHandle;
    else if (openTcpFile(&TcpCC, FILE_READ_DATA | FILE_WRITE_DATA) != STATUS_SUCCESS)
        return WSAEINVAL;

    Success = DeviceIoControl(TcpCC,
                              IOCTL,
                              Request,
                              RequestSize,
                              NULL,
                              0,
                              &BytesReturned,
                              NULL);

    if (TcpCC != TdiAddressObjectHandle)
        closeTcpFile(TcpCC);

    DPRINT("DeviceIoControl: %ld\n", (Success ? NO_ERROR : GetLastError()));

    if (!Success)
        return WSAEINVAL;

    return NO_ERROR;
}

INT
EXPORT
WSHNotify(
    IN  PVOID HelperDllSocketContext,
    IN  SOCKET SocketHandle,
    IN  HANDLE TdiAddressObjectHandle,
    IN  HANDLE TdiConnectionObjectHandle,
    IN  DWORD NotifyEvent)
{
    PSOCKET_CONTEXT Context = HelperDllSocketContext;
    NTSTATUS Status;
    HANDLE TcpCC;
    TDIEntityID *EntityIDs;
    DWORD EntityCount, i;
    PQUEUED_REQUEST QueuedRequest, NextQueuedRequest;

    switch (NotifyEvent)
    {
        case WSH_NOTIFY_CLOSE:
            DPRINT("WSHNotify: WSH_NOTIFY_CLOSE\n");
            QueuedRequest = Context->RequestQueue;
            while (QueuedRequest)
            {
                NextQueuedRequest = QueuedRequest->Next;

                HeapFree(GetProcessHeap(), 0, QueuedRequest->Info);
                HeapFree(GetProcessHeap(), 0, QueuedRequest);

                QueuedRequest = NextQueuedRequest;
            }
            HeapFree(GetProcessHeap(), 0, HelperDllSocketContext);
            break;


        case WSH_NOTIFY_BIND:
            DPRINT("WSHNotify: WSH_NOTIFY_BIND\n");
            Status = openTcpFile(&TcpCC, 0);
            if (Status != STATUS_SUCCESS)
                return WSAEINVAL;

            Status = tdiGetEntityIDSet(TcpCC,
                                       &EntityIDs,
                                       &EntityCount);

            closeTcpFile(TcpCC);

            if (Status != STATUS_SUCCESS)
                return WSAEINVAL;

            for (i = 0; i < EntityCount; i++)
            {
                if (EntityIDs[i].tei_entity == CO_TL_ENTITY ||
                    EntityIDs[i].tei_entity == CL_TL_ENTITY ||
                    EntityIDs[i].tei_entity == ER_ENTITY)
                {
                    Context->AddrFileInstance = EntityIDs[i].tei_instance;
                    Context->AddrFileEntityType = EntityIDs[i].tei_entity;
                }
            }

            DPRINT("Instance: %lx Type: %lx\n", Context->AddrFileInstance, Context->AddrFileEntityType);

            tdiFreeThingSet(EntityIDs);

            Context->SocketState = SocketStateBound;

            QueuedRequest = Context->RequestQueue;
            while (QueuedRequest)
            {
                QueuedRequest->Info->ID.toi_entity.tei_entity = Context->AddrFileEntityType;
                QueuedRequest->Info->ID.toi_entity.tei_instance = Context->AddrFileInstance;

                SendRequest(QueuedRequest->Info,
                            sizeof(*QueuedRequest->Info) + QueuedRequest->Info->BufferSize,
                            IOCTL_TCP_SET_INFORMATION_EX,
                            TdiAddressObjectHandle);

                NextQueuedRequest = QueuedRequest->Next;

                HeapFree(GetProcessHeap(), 0, QueuedRequest->Info);
                HeapFree(GetProcessHeap(), 0, QueuedRequest);

                QueuedRequest = NextQueuedRequest;
            }
            Context->RequestQueue = NULL;
            break;

        default:
            DPRINT1("Unwanted notification received! (%ld)\n", NotifyEvent);
            break;
    }

    return NO_ERROR;
}


INT
EXPORT
WSHOpenSocket(
    IN OUT  PINT AddressFamily,
    IN OUT  PINT SocketType,
    IN OUT  PINT Protocol,
    OUT     PUNICODE_STRING TransportDeviceName,
    OUT     PVOID HelperDllSocketContext,
    OUT     PDWORD NotificationEvents)
/*
 * FUNCTION: Opens a socket
 */
{
    return WSHOpenSocket2(AddressFamily,
                          SocketType,
                          Protocol,
                          0,
                          0,
                          TransportDeviceName,
                          HelperDllSocketContext,
                          NotificationEvents);
}


INT
EXPORT
WSHOpenSocket2(
    OUT PINT AddressFamily,
    IN  OUT PINT SocketType,
    IN  OUT PINT Protocol,
    IN  GROUP Group,
    IN  DWORD Flags,
    OUT PUNICODE_STRING TransportDeviceName,
    OUT PVOID *HelperDllSocketContext,
    OUT PDWORD NotificationEvents)
/*
 * FUNCTION: Opens a socket
 * ARGUMENTS:
 *     AddressFamily          = Address of buffer with address family (updated)
 *     SocketType             = Address of buffer with type of socket (updated)
 *     Protocol               = Address of buffer with protocol number (updated)
 *     Group                  = Socket group
 *     Flags                  = Socket flags
 *     TransportDeviceName    = Address of buffer to place name of transport device
 *     HelperDllSocketContext = Address of buffer to place socket context pointer
 *     NotificationEvents     = Address of buffer to place flags for event notification
 * RETURNS:
 *     Status of operation
 * NOTES:
 *     Mapping tripple is returned in an canonicalized form
 */
{
    PSOCKET_CONTEXT Context;
    UNICODE_STRING String;
    UNICODE_STRING TcpDeviceName = RTL_CONSTANT_STRING(DD_TCP_DEVICE_NAME);
    UNICODE_STRING UdpDeviceName = RTL_CONSTANT_STRING(DD_UDP_DEVICE_NAME);
    UNICODE_STRING RawDeviceName = RTL_CONSTANT_STRING(DD_RAW_IP_DEVICE_NAME);
    UNICODE_STRING Tcp6DeviceName = RTL_CONSTANT_STRING(DD_TCPV6_DEVICE_NAME);
    UNICODE_STRING Udp6DeviceName = RTL_CONSTANT_STRING(DD_UDPV6_DEVICE_NAME);

    DPRINT("WSHOpenSocket2 called\n");

    if (*AddressFamily == AF_INET6 && *SocketType != SOCK_STREAM && *SocketType != SOCK_DGRAM)
        return WSAESOCKTNOSUPPORT;

    switch (*SocketType) {
    case SOCK_STREAM:
        String = (*AddressFamily == AF_INET6) ? Tcp6DeviceName : TcpDeviceName;
        break;

    case SOCK_DGRAM:
        String = (*AddressFamily == AF_INET6) ? Udp6DeviceName : UdpDeviceName;
        break;

    case SOCK_RAW:
        if ((*Protocol < 0) || (*Protocol > 255))
          return WSAEINVAL;

        String = RawDeviceName;
        break;

    default:
        return WSAEINVAL;
    }

    RtlInitUnicodeString(TransportDeviceName, NULL);

    TransportDeviceName->MaximumLength = String.Length +        /* Transport device name */
                                         (4 * sizeof(WCHAR) +   /* Separator and protocol */
                                         sizeof(UNICODE_NULL)); /* Terminating null */

    TransportDeviceName->Buffer = HeapAlloc(
        GetProcessHeap(),
        0,
        TransportDeviceName->MaximumLength);

    if (!TransportDeviceName->Buffer)
        return WSAENOBUFS;

    /* Append the transport device name */
    RtlAppendUnicodeStringToString(TransportDeviceName, &String);

    if (*SocketType == SOCK_RAW) {
        /* Append a separator */
        TransportDeviceName->Buffer[TransportDeviceName->Length / sizeof(WCHAR)] = OBJ_NAME_PATH_SEPARATOR;
        TransportDeviceName->Length += sizeof(WCHAR);
        TransportDeviceName->Buffer[TransportDeviceName->Length / sizeof(WCHAR)] = UNICODE_NULL;

        /* Append the protocol number */
        String.Buffer = TransportDeviceName->Buffer + (TransportDeviceName->Length / sizeof(WCHAR));
        String.Length = 0;
        String.MaximumLength = TransportDeviceName->MaximumLength - TransportDeviceName->Length;

        RtlIntegerToUnicodeString((ULONG)*Protocol, 10, &String);

        TransportDeviceName->Length += String.Length;
    }

    /* Setup a socket context area */

    Context = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(SOCKET_CONTEXT));
    if (!Context) {
        RtlFreeUnicodeString(TransportDeviceName);
        return WSAENOBUFS;
    }

    Context->AddressFamily = *AddressFamily;
    Context->SocketType    = *SocketType;
    Context->Protocol      = *Protocol;
    Context->Flags         = Flags;
    Context->SocketState   = SocketStateCreated;
    Context->MulticastTTL  = 1;
    Context->Ttl           = 128;
    Context->V6Only        = TRUE;

    *HelperDllSocketContext = Context;
    *NotificationEvents = WSH_NOTIFY_CLOSE | WSH_NOTIFY_BIND;

    return NO_ERROR;
}

static INT ValidateMulticastInterface(ULONG Selector)
{
    HANDLE TcpFile;
    TDIEntityID *Entities;
    DWORD Count, i, j, AddressCount;
    IPAddrEntry *Addresses;
    NTSTATUS Status;
    INT Result = WSAEADDRNOTAVAIL;
    ULONG HostSelector = ntohl(Selector);

    if (!Selector) return NO_ERROR;
    Status = openTcpFile(&TcpFile, 0);
    if (!NT_SUCCESS(Status)) return WSAENETDOWN;
    Status = tdiGetEntityIDSet(TcpFile, &Entities, &Count);
    if (!NT_SUCCESS(Status))
    {
        closeTcpFile(TcpFile);
        return WSAENETDOWN;
    }
    for (i = 0; i < Count && Result; ++i)
    {
        if (Entities[i].tei_entity != CL_NL_ENTITY) continue;
        Status = tdiGetSetOfThings(TcpFile, INFO_CLASS_PROTOCOL, INFO_TYPE_PROVIDER,
                                  IP_MIB_ADDRTABLE_ENTRY_ID, Entities[i].tei_entity,
                                  Entities[i].tei_instance, 0, sizeof(*Addresses),
                                  (PVOID *)&Addresses, &AddressCount);
        if (!NT_SUCCESS(Status)) continue;
        for (j = 0; j < AddressCount; ++j)
            if ((HostSelector < 0x01000000 && Addresses[j].iae_index == HostSelector) ||
                (HostSelector >= 0x01000000 && Addresses[j].iae_addr == Selector))
            {
                Result = NO_ERROR;
                break;
            }
        tdiFreeThingSet(Addresses);
    }
    tdiFreeThingSet(Entities);
    closeTcpFile(TcpFile);
    return Result;
}

INT
EXPORT
WSHSetSocketInformation(
    IN  PVOID HelperDllSocketContext,
    IN  SOCKET SocketHandle,
    IN  HANDLE TdiAddressObjectHandle,
    IN  HANDLE TdiConnectionObjectHandle,
    IN  INT Level,
    IN  INT OptionName,
    IN  PCHAR OptionValue,
    IN  INT OptionLength)
{
    PSOCKET_CONTEXT Context = HelperDllSocketContext;
    ULONG TdiType, TdiId;
    INT Status;
    PTCP_REQUEST_SET_INFORMATION_EX Info;
    PQUEUED_REQUEST Queued, NextQueued;
    ULONG MulticastValue;
    ULONG InterfaceIndex = 0;
    ULONG UdpValue = 0;
    ULONG RecvValue = 0;

    DPRINT("WSHSetSocketInformation\n");

    /* FIXME: We only handle address file object here */

    switch (Level)
    {
        case SOL_SOCKET:
            switch (OptionName)
            {
                case SO_DONTROUTE:
                    if (OptionLength < sizeof(BOOL))
                    {
                        return WSAEFAULT;
                    }
                    Context->DontRoute = *(BOOL*)OptionValue;
                    /* This is silently ignored on Windows */
                    return 0;

                case SO_KEEPALIVE:
                    if (OptionLength < sizeof(DWORD))
                    {
                        return WSAEFAULT;
                    }
                    break;

                default:
                    /* Invalid option */
                    DPRINT1("Set: Received unexpected SOL_SOCKET option %d\n", OptionName);
                    return WSAENOPROTOOPT;
            }
            break;

        case IPPROTO_IP:
            switch (OptionName)
            {
                case IP_MULTICAST_IF:
                    if (Context->SocketType != SOCK_DGRAM && Context->SocketType != SOCK_RAW)
                        return WSAENOPROTOOPT;
                    if (!OptionValue || OptionLength < sizeof(ULONG))
                        return WSAEFAULT;
                    Status = ValidateMulticastInterface(*(ULONG *)OptionValue);
                    if (Status) return Status;
                    OptionLength = sizeof(ULONG);
                    break;

                case IP_MULTICAST_TTL:
                    if (Context->SocketType != SOCK_DGRAM && Context->SocketType != SOCK_RAW)
                        return WSAENOPROTOOPT;
                    if (!OptionValue || OptionLength < 1)
                        return WSAEFAULT;
                    MulticastValue = 0;
                    memcpy(&MulticastValue, OptionValue, min(OptionLength, (INT)sizeof(ULONG)));
                    if (MulticastValue > 255)
                        return WSAEINVAL;
                    OptionValue = (PCHAR)&MulticastValue;
                    OptionLength = sizeof(ULONG);
                    break;

                case IP_UNICAST_IF:
                    if (!OptionValue || OptionLength < sizeof(ULONG))
                        return WSAEFAULT;
                    memcpy(&InterfaceIndex, OptionValue, sizeof(ULONG));
                    if (ntohl(InterfaceIndex) >= 0x01000000)
                        return WSAEADDRNOTAVAIL;
                    Status = ValidateMulticastInterface(InterfaceIndex);
                    if (Status == WSAEADDRNOTAVAIL)
                        return WSAEINVAL;
                    if (Status)
                        return Status;
                    OptionValue = (PCHAR)&InterfaceIndex;
                    OptionLength = sizeof(ULONG);
                    break;

                case IP_HDRINCL:
                    if (Context->SocketType != SOCK_RAW)
                        return WSAEINVAL;
                    break;

                case IP_TTL:
                    if (!OptionValue || OptionLength < 1)
                        return WSAEFAULT;
                    break;

                case IP_ADD_MEMBERSHIP:
                case IP_DROP_MEMBERSHIP:
                    if (!OptionValue || OptionLength < sizeof(struct ip_mreq))
                        return WSAEFAULT;
                    OptionLength = sizeof(struct ip_mreq);
                    break;

                case IP_DONTFRAGMENT:
                case IP_PKTINFO:
                case IP_RECVTTL:
                case IP_RECVTOS:
                case IP_RECVECN:
                case IP_RECVDSTADDR:
                    if (!OptionValue || OptionLength < 1)
                        return WSAEFAULT;
                    memcpy(&RecvValue, OptionValue, min(OptionLength, (INT)sizeof(ULONG)));
                    RecvValue = (RecvValue != 0);
                    OptionValue = (PCHAR)&RecvValue;
                    OptionLength = sizeof(ULONG);
                    break;

                default:
                    if (!IsWindowsIpOption(OptionName))
                        return WSAENOPROTOOPT;
                    DPRINT1("Set: Received unsupported IPPROTO_IP option %d\n", OptionName);
                    return 0;
            }
            break;

        case IPPROTO_TCP:
            switch (OptionName)
            {
                case TCP_NODELAY:
                    if (OptionLength < sizeof(CHAR))
                    {
                        return WSAEFAULT;
                    }
                    break;

                default:
                    /* Invalid option */
                    DPRINT1("Set: Received unexpected IPPROTO_TCP option %d\n", OptionName);
                    return 0;
            }
            break;

        case IPPROTO_IPV6:
            if (OptionName == IPV6_DONTFRAG)
            {
                if (!OptionValue || OptionLength < 1)
                    return WSAEFAULT;
                memcpy(&RecvValue, OptionValue, min(OptionLength, (INT)sizeof(ULONG)));
                RecvValue = (RecvValue != 0);
                OptionValue = (PCHAR)&RecvValue;
                OptionLength = sizeof(ULONG);
                Level = IPPROTO_IP;
                OptionName = IP_DONTFRAGMENT;
                break;
            }
            if (OptionName == IPV6_ADD_MEMBERSHIP || OptionName == IPV6_DROP_MEMBERSHIP)
            {
                if (!OptionValue || OptionLength < sizeof(struct ipv6_mreq))
                    return WSAEFAULT;
                OptionLength = sizeof(struct ipv6_mreq);
                break;
            }
            if (OptionName != IPV6_V6ONLY)
                return 0;
            if (!OptionValue || OptionLength < sizeof(DWORD))
                return WSAEFAULT;
            if (Context->SocketState != SocketStateCreated)
                return WSAEINVAL;
            Context->V6Only = *(DWORD *)OptionValue != 0;
            return 0;

        case IPPROTO_UDP:
            if (Context->SocketType != SOCK_DGRAM)
                return WSAEINVAL;
            switch (OptionName)
            {
                case UDP_NOCHECKSUM:
                    if (!OptionValue || OptionLength < sizeof(BOOLEAN))
                        return WSAEFAULT;
                    if (OptionLength >= sizeof(DWORD))
                        memcpy(&UdpValue, OptionValue, sizeof(DWORD));
                    else
                        UdpValue = *(PUCHAR)OptionValue;
                    UdpValue = (UdpValue == 0);
                    OptionValue = (PCHAR)&UdpValue;
                    OptionLength = sizeof(ULONG);
                    break;

                case UDP_SEND_MSG_SIZE:
                    if (!OptionValue || OptionLength < sizeof(DWORD))
                        return WSAEFAULT;
                    memcpy(&UdpValue, OptionValue, sizeof(DWORD));
                    OptionValue = (PCHAR)&UdpValue;
                    OptionLength = sizeof(ULONG);
                    break;

                case UDP_RECV_MAX_COALESCED_SIZE:
                    if (!OptionValue || OptionLength < sizeof(DWORD))
                        return WSAEFAULT;
                    memcpy(&Context->UdpRecvMaxCoalescedSize, OptionValue, sizeof(DWORD));
                    return 0;

                default:
                    return WSAEINVAL;
            }
            break;

        default:
            DPRINT1("Set: Received unexpected %d option %d\n", Level, OptionName);
            return WSAEINVAL;
    }

    /* If we get here, GetAddressOption must return something valid */
    GetTdiTypeId(Level, OptionName, &TdiType, &TdiId);
    ASSERT((TdiId != 0) && (TdiType != 0));

    Info = HeapAlloc(GetProcessHeap(), 0, sizeof(*Info) + OptionLength);
    if (!Info)
        return WSAENOBUFS;

    Info->ID.toi_entity.tei_entity = Context->AddrFileEntityType;
    Info->ID.toi_entity.tei_instance = Context->AddrFileInstance;
    Info->ID.toi_class = INFO_CLASS_PROTOCOL;
    Info->ID.toi_type = TdiType;
    Info->ID.toi_id = TdiId;
    Info->BufferSize = OptionLength;
    memcpy(Info->Buffer, OptionValue, OptionLength);

    if (Context->SocketState == SocketStateCreated)
    {
        if (Context->RequestQueue)
        {
            Queued = Context->RequestQueue;
            while ((NextQueued = Queued->Next))
            {
               Queued = NextQueued;
            }

            Queued->Next = HeapAlloc(GetProcessHeap(), 0, sizeof(QUEUED_REQUEST));
            if (!Queued->Next)
            {
                HeapFree(GetProcessHeap(), 0, Info);
                return WSAENOBUFS;
            }

            NextQueued = Queued->Next;
            NextQueued->Next = NULL;
            NextQueued->Info = Info;
        }
        else
        {
            Context->RequestQueue = HeapAlloc(GetProcessHeap(), 0, sizeof(QUEUED_REQUEST));
            if (!Context->RequestQueue)
            {
                HeapFree(GetProcessHeap(), 0, Info);
                return WSAENOBUFS;
            }

            Context->RequestQueue->Next = NULL;
            Context->RequestQueue->Info = Info;
        }

        if (Level == IPPROTO_IP && OptionName == IP_MULTICAST_IF)
            memcpy(&Context->MulticastInterface, OptionValue, sizeof(ULONG));
        if (Level == IPPROTO_IP && OptionName == IP_MULTICAST_TTL)
            memcpy(&Context->MulticastTTL, OptionValue, sizeof(ULONG));
        if (Level == IPPROTO_IP && OptionName == IP_UNICAST_IF)
            Context->UnicastInterface = ntohl(InterfaceIndex);
        if (Level == IPPROTO_UDP && OptionName == UDP_NOCHECKSUM)
            Context->UdpNoChecksum = (UdpValue == 0);
        if (Level == IPPROTO_UDP && OptionName == UDP_SEND_MSG_SIZE)
            Context->UdpSendMsgSize = UdpValue;
        if (Level == IPPROTO_IP && RecvInfoOptionBit(OptionName))
        {
            if (RecvValue)
                Context->RecvInfoOptions |= RecvInfoOptionBit(OptionName);
            else
                Context->RecvInfoOptions &= ~RecvInfoOptionBit(OptionName);
        }
        if (Level == IPPROTO_IP && OptionName == IP_TTL)
            Context->Ttl = *(PUCHAR)OptionValue;
        if (Level == IPPROTO_IP && OptionName == IP_DONTFRAGMENT)
            Context->DontFragment = (RecvValue != 0);
        if (Level == SOL_SOCKET && OptionName == SO_KEEPALIVE)
            Context->KeepAlive = (*(DWORD *)OptionValue != 0);
        return 0;
    }

    Status = SendRequest(Info, sizeof(*Info) + Info->BufferSize, IOCTL_TCP_SET_INFORMATION_EX, TdiAddressObjectHandle);
    if (!Status && Level == IPPROTO_IP && OptionName == IP_MULTICAST_IF)
        memcpy(&Context->MulticastInterface, OptionValue, sizeof(ULONG));
    if (!Status && Level == IPPROTO_IP && OptionName == IP_MULTICAST_TTL)
        memcpy(&Context->MulticastTTL, OptionValue, sizeof(ULONG));
    if (!Status && Level == IPPROTO_IP && OptionName == IP_UNICAST_IF)
        Context->UnicastInterface = ntohl(InterfaceIndex);
    if (!Status && Level == IPPROTO_UDP && OptionName == UDP_NOCHECKSUM)
        Context->UdpNoChecksum = (UdpValue == 0);
    if (!Status && Level == IPPROTO_UDP && OptionName == UDP_SEND_MSG_SIZE)
        Context->UdpSendMsgSize = UdpValue;
    if (!Status && Level == IPPROTO_IP && RecvInfoOptionBit(OptionName))
    {
        if (RecvValue)
            Context->RecvInfoOptions |= RecvInfoOptionBit(OptionName);
        else
            Context->RecvInfoOptions &= ~RecvInfoOptionBit(OptionName);
    }
    if (!Status && Level == IPPROTO_IP && OptionName == IP_TTL)
        Context->Ttl = *(PUCHAR)OptionValue;
    if (!Status && Level == IPPROTO_IP && OptionName == IP_DONTFRAGMENT)
        Context->DontFragment = (RecvValue != 0);
    if (!Status && Level == SOL_SOCKET && OptionName == SO_KEEPALIVE)
        Context->KeepAlive = (*(DWORD *)OptionValue != 0);

    HeapFree(GetProcessHeap(), 0, Info);

    return Status;
}


INT
EXPORT
WSHStringToAddress(
    IN      LPWSTR AddressString,
    IN      DWORD AddressFamily,
    IN      LPWSAPROTOCOL_INFOW ProtocolInfo    OPTIONAL,
    OUT     LPSOCKADDR Address,
    IN OUT  LPDWORD AddressStringLength)
{
    UNIMPLEMENTED;

    return NO_ERROR;
}

/* EOF */
