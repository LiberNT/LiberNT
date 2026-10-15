#include <rosdhcp.h>
#include <ws2ipdef.h>

SOCKET ServerSocket;

void SocketInit() {
    ServerSocket = socket( AF_INET, SOCK_DGRAM, 0 );
}

ssize_t send_packet( struct interface_info *ip,
                     struct dhcp_packet *p,
                     size_t size,
                     struct in_addr addr,
                     struct sockaddr_in *broadcast,
                     struct hardware *hardware ) {
    PDHCP_ADAPTER Adapter = CONTAINING_RECORD(ip, DHCP_ADAPTER, DhclientInfo);
    DWORD InterfaceIndex = htonl(Adapter->IfMib.dwIndex);
    int result;

    if (size > INT_MAX)
        return WSAEMSGSIZE;

    if (setsockopt(ip->wfdesc, IPPROTO_IP, IP_UNICAST_IF,
                   (const char *)&InterfaceIndex, sizeof(InterfaceIndex)) == SOCKET_ERROR)
        return SOCKET_ERROR;

    result =
        sendto( ip->wfdesc, (char *)p, (int)size, 0,
                (struct sockaddr *)broadcast, sizeof(*broadcast) );

    if (result < 0) {
        note ("send_packet: %x", result);
        if (result == WSAENETUNREACH)
            note ("send_packet: please consult README file%s",
                  " regarding broadcast address.");
    }

    return result;
}

ssize_t receive_packet(struct interface_info *ip,
                       unsigned char *packet_data,
                       size_t packet_len,
                       struct sockaddr_in *dest,
                       struct hardware *hardware ) {
    int recv_addr_size = sizeof(*dest);
    int result;

    if (packet_len > INT_MAX)
        return WSAEMSGSIZE;

    result =
        recvfrom (ip -> rfdesc, (char *)packet_data, (int)packet_len, 0,
                  (struct sockaddr *)dest, &recv_addr_size );
    return result;
}
