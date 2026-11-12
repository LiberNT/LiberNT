
#define STANDALONE
#include <apitest.h>

extern void func_BufferedRecv(void);
extern void func_BusyLoopback(void);
extern void func_CloseCompletion(void);
extern void func_MulticastInterface(void);
extern void func_ReuseDelivery(void);
extern void func_addresslistsort(void);
extern void func_afunix(void);
extern void func_bind(void);
extern void func_broadcast(void);
extern void func_close(void);
extern void func_connect_any(void);
extern void func_connect_select(void);
extern void func_dualstack(void);
extern void func_dualstack_mapped(void);
extern void func_getaddrinfo(void);
extern void func_gethostname(void);
extern void func_getnameinfo(void);
extern void func_getservbyname(void);
extern void func_getservbyport(void);
extern void func_ioctlsocket(void);
extern void func_nonblocking(void);
extern void func_nostartup(void);
extern void func_open_osfhandle(void);
extern void func_pollable_event(void);
extern void func_loopback_pingpong(void);
extern void func_recv(void);
extern void func_send(void);
extern void func_sendto_neighbor(void);
extern void func_udp_queue(void);
extern void func_udp_quic(void);
extern void func_WSAAsync(void);
extern void func_WSAIoctl(void);
extern void func_WSARecv(void);
extern void func_WSABufferArray(void);
extern void func_WSAStartup(void);

const struct test winetest_testlist[] =
{
    { "BufferedRecv", func_BufferedRecv },
    { "BusyLoopback", func_BusyLoopback },
    { "CloseCompletion", func_CloseCompletion },
    { "MulticastInterface", func_MulticastInterface },
    { "ReuseDelivery", func_ReuseDelivery },
    { "addresslistsort", func_addresslistsort },
    { "afunix", func_afunix },
    { "bind", func_bind },
    { "broadcast", func_broadcast },
    { "close", func_close },
    { "connect_any", func_connect_any },
    { "connect_select", func_connect_select },
    { "dualstack", func_dualstack },
    { "dualstack_mapped", func_dualstack_mapped },
    { "getaddrinfo", func_getaddrinfo },
    { "gethostname", func_gethostname },
    { "getnameinfo", func_getnameinfo },
    { "getservbyname", func_getservbyname },
    { "getservbyport", func_getservbyport },
    { "ioctlsocket", func_ioctlsocket },
    { "nonblocking", func_nonblocking },
    { "nostartup", func_nostartup },
    { "open_osfhandle", func_open_osfhandle },
    { "pollable_event", func_pollable_event },
    { "loopback_pingpong", func_loopback_pingpong },
    { "recv", func_recv },
    { "send", func_send },
    { "sendto_neighbor", func_sendto_neighbor },
    { "udp_queue", func_udp_queue },
    { "udp_quic", func_udp_quic },
    { "WSAAsync", func_WSAAsync },
    { "WSAIoctl", func_WSAIoctl },
    { "WSARecv", func_WSARecv },
    { "WSABufferArray", func_WSABufferArray },
    { "WSAStartup", func_WSAStartup },
    { 0, 0 }
};

