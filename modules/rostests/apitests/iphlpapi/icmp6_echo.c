/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test ICMPv6 echo requests whose IPv6 packet does not fit in 65535 bytes
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#include <apitest.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <icmpapi.h>

static UCHAR RequestData[0x10000];
static UCHAR ReplyBuffer[sizeof(ICMPV6_ECHO_REPLY) + 0x10000 + 8];

static
BOOL
FindAdapterAddress6(
    _Out_ SOCKADDR_IN6 *Address)
{
    PIP_ADAPTER_ADDRESSES List, Adapter;
    PIP_ADAPTER_UNICAST_ADDRESS Unicast;
    ULONG Size = 0, Flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
    BOOL Found = FALSE;

    if (GetAdaptersAddresses(AF_INET6, Flags, NULL, NULL, &Size) != ERROR_BUFFER_OVERFLOW)
        return FALSE;
    List = HeapAlloc(GetProcessHeap(), 0, Size);
    if (List && !GetAdaptersAddresses(AF_INET6, Flags, NULL, List, &Size))
    {
        for (Adapter = List; Adapter && !Found; Adapter = Adapter->Next)
        {
            if (Adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK || Adapter->OperStatus != IfOperStatusUp)
                continue;
            for (Unicast = Adapter->FirstUnicastAddress; Unicast && !Found; Unicast = Unicast->Next)
            {
                const SOCKADDR_IN6 *Candidate = (const SOCKADDR_IN6 *)Unicast->Address.lpSockaddr;

                if (Candidate->sin6_family != AF_INET6 || IN6_IS_ADDR_LINKLOCAL(&Candidate->sin6_addr) ||
                    Unicast->DadState != IpDadStatePreferred)
                    continue;
                *Address = *Candidate;
                Found = TRUE;
            }
        }
    }
    HeapFree(GetProcessHeap(), 0, List);
    return Found;
}

static
void
ExpectEcho(
    _In_ HANDLE Icmp,
    _In_ PCSTR Tag,
    _In_ SOCKADDR_IN6 *Destination,
    _In_ ULONG Size,
    _In_ BOOL Replies)
{
    PICMPV6_ECHO_REPLY Reply = (PICMPV6_ECHO_REPLY)ReplyBuffer;
    SOCKADDR_IN6 Source;
    DWORD Count, Error;
    ULONG i;

    for (i = 0; i < Size; i++)
        RequestData[i] = (UCHAR)(i * 7 + i / 251);
    ZeroMemory(&Source, sizeof(Source));
    Source.sin6_family = AF_INET6;
    memset(ReplyBuffer, 0xCC, sizeof(ReplyBuffer));

    SetLastError(0xdeadbeef);
    Count = Icmp6SendEcho2(Icmp, NULL, NULL, NULL, &Source, Destination, RequestData, (WORD)Size, NULL,
                           ReplyBuffer, sizeof(ReplyBuffer), 4000);
    Error = GetLastError();
    if (!Replies)
    {
        ok(Count == 0, "%s: %lu replies, expected none\n", Tag, Count);
        ok(Error == IP_GENERAL_FAILURE, "%s: error %lu\n", Tag, Error);
        return;
    }

    ok(Count == 1, "%s: %lu replies, error %lu\n", Tag, Count, Error);
    if (Count != 1)
        return;
    ok(Reply->Status == IP_SUCCESS, "%s: reply status %lu\n", Tag, Reply->Status);
    ok(!memcmp(Reply->Address.sin6_addr, &Destination->sin6_addr, sizeof(Reply->Address.sin6_addr)),
       "%s: reply from another address\n", Tag);
    ok(!memcmp(Reply + 1, RequestData, Size), "%s: reply data differs from the request data\n", Tag);
    ok(ReplyBuffer[sizeof(*Reply) + Size] == 0xCC, "%s: reply data runs past the request size\n", Tag);
}

static
void
TestEchoSizes(
    _In_ HANDLE Icmp,
    _In_ PCSTR Path,
    _In_ SOCKADDR_IN6 *Destination)
{
    static const ULONG Sizes[] = { 65487, 65488, 65500, 65527, 65528, 65535 };
    CHAR Tag[64];
    ULONG i;

    for (i = 0; i < _countof(Sizes); i++)
    {
        sprintf(Tag, "%s, %lu bytes", Path, Sizes[i]);
        ExpectEcho(Icmp, Tag, Destination, Sizes[i], TRUE);
    }
}

START_TEST(icmp6_echo)
{
    SOCKADDR_IN6 Destination;
    HANDLE Icmp;

    Icmp = Icmp6CreateFile();
    if (Icmp == INVALID_HANDLE_VALUE)
    {
        skip("Icmp6CreateFile failed: %lu\n", GetLastError());
        return;
    }

    ZeroMemory(&Destination, sizeof(Destination));
    Destination.sin6_family = AF_INET6;
    Destination.sin6_addr.s6_addr[15] = 1;
    TestEchoSizes(Icmp, "loopback", &Destination);

    if (FindAdapterAddress6(&Destination))
    {
        TestEchoSizes(Icmp, "adapter", &Destination);

        ZeroMemory(&Destination.sin6_addr.s6_addr[8], 8);
        Destination.sin6_addr.s6_addr[15] = 2;
        ExpectEcho(Icmp, "neighbor, 65528 bytes", &Destination, 65528, FALSE);
        ExpectEcho(Icmp, "neighbor, 65535 bytes", &Destination, 65535, FALSE);
    }
    else
    {
        skip("no network adapter with a global IPv6 address\n");
    }

    IcmpCloseHandle(Icmp);
}
