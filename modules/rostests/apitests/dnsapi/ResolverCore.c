/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The DNS servers each attempt of a query asks, its timeouts and the server priorities
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>

#define WIN32_NO_STATUS
#include <windef.h>
#include <winbase.h>

#include <resolver_core.h>

static
ULONG
AskedMask(
    _In_ ULONG Attempt,
    _In_ ULONG AdapterRank,
    _In_ ULONG ServerCount)
{
    ULONG Rank, Mask = 0;

    for (Rank = 0; Rank < ServerCount; Rank++)
    {
        if (DnsResolverCoreAttemptAsksServer(Attempt, AdapterRank, Rank, ServerCount))
            Mask |= 1 << Rank;
    }
    return Mask;
}

static
VOID
TestTimeouts(VOID)
{
    static const ULONG Expected[] = {1000, 1000, 2000, 4000, 2000};
    ULONG Attempt, Total = 0;

    ok_int(DNS_RESOLVER_CORE_ATTEMPTS, 5);
    for (Attempt = 0; Attempt < ARRAYSIZE(Expected); Attempt++)
    {
        ok(DnsResolverCoreAttemptTimeout(Attempt) == Expected[Attempt], "Attempt %lu waits %lu ms\n",
           Attempt, DnsResolverCoreAttemptTimeout(Attempt));
        Total += DnsResolverCoreAttemptTimeout(Attempt);
    }
    ok_int(Total, 10000);
    ok_int(DnsResolverCoreAttemptTimeout(DNS_RESOLVER_CORE_ATTEMPTS), 0);
}

static
VOID
TestOneAdapter(VOID)
{
    static const ULONG One[] = {0x01, 0x01, 0x01, 0x01, 0x01};
    static const ULONG Two[] = {0x01, 0x02, 0x02, 0x03, 0x03};
    static const ULONG Three[] = {0x01, 0x02, 0x04, 0x07, 0x07};
    static const ULONG Five[] = {0x01, 0x02, 0x04, 0x1f, 0x1f};
    ULONG Attempt;

    for (Attempt = 0; Attempt < DNS_RESOLVER_CORE_ATTEMPTS; Attempt++)
    {
        ok(AskedMask(Attempt, 0, 1) == One[Attempt], "One server, attempt %lu asks %#lx\n", Attempt, AskedMask(Attempt, 0, 1));
        ok(AskedMask(Attempt, 0, 2) == Two[Attempt], "Two servers, attempt %lu asks %#lx\n", Attempt, AskedMask(Attempt, 0, 2));
        ok(AskedMask(Attempt, 0, 3) == Three[Attempt], "Three servers, attempt %lu asks %#lx\n", Attempt, AskedMask(Attempt, 0, 3));
        ok(AskedMask(Attempt, 0, 5) == Five[Attempt], "Five servers, attempt %lu asks %#lx\n", Attempt, AskedMask(Attempt, 0, 5));
    }

    ok_int(DnsResolverCoreAttemptAsksServer(0, 0, 0, 0), FALSE);
    ok_int(DnsResolverCoreAttemptAsksServer(4, 0, 2, 2), FALSE);
}

static
VOID
TestSeveralAdapters(VOID)
{
    static const ULONG Counts[] = {4, 1, 3, 2};
    static const ULONG Expected[DNS_RESOLVER_CORE_ATTEMPTS][4] =
    {
        {0x1, 0x0, 0x0, 0x0},
        {0x2, 0x1, 0x1, 0x1},
        {0x4, 0x1, 0x2, 0x2},
        {0xf, 0x1, 0x7, 0x3},
        {0xf, 0x1, 0x7, 0x3},
    };
    ULONG Attempt, Adapter;

    for (Attempt = 0; Attempt < DNS_RESOLVER_CORE_ATTEMPTS; Attempt++)
    {
        for (Adapter = 0; Adapter < ARRAYSIZE(Counts); Adapter++)
        {
            ULONG Mask = AskedMask(Attempt, Adapter, Counts[Adapter]);

            ok(Mask == Expected[Attempt][Adapter], "Attempt %lu asks %#lx on adapter %lu\n", Attempt, Mask, Adapter);
        }
    }
}

static
VOID
TestPriorities(VOID)
{
    LONG Equal[] = {DNS_RESOLVER_CORE_PRIORITY_DEFAULT, DNS_RESOLVER_CORE_PRIORITY_DEFAULT, DNS_RESOLVER_CORE_PRIORITY_DEFAULT};
    LONG FirstSilent[] = {DNS_RESOLVER_CORE_PRIORITY_REDUCED, DNS_RESOLVER_CORE_PRIORITY_DEFAULT, DNS_RESOLVER_CORE_PRIORITY_DEFAULT};
    LONG AllSilent[] = {DNS_RESOLVER_CORE_PRIORITY_REDUCED, DNS_RESOLVER_CORE_PRIORITY_REDUCED, DNS_RESOLVER_CORE_PRIORITY_REDUCED};
    LONG MiddleSilent[] = {DNS_RESOLVER_CORE_PRIORITY_DEFAULT, DNS_RESOLVER_CORE_PRIORITY_REDUCED, DNS_RESOLVER_CORE_PRIORITY_DEFAULT};

    ok(DNS_RESOLVER_CORE_PRIORITY_REDUCED < DNS_RESOLVER_CORE_PRIORITY_DEFAULT, "A silent server must rank below the others\n");
    ok_int(DnsResolverCorePriorityAfter(TRUE), DNS_RESOLVER_CORE_PRIORITY_DEFAULT);
    ok_int(DnsResolverCorePriorityAfter(FALSE), DNS_RESOLVER_CORE_PRIORITY_REDUCED);

    ok_int(DnsResolverCoreServerRank(Equal, 3, 0), 0);
    ok_int(DnsResolverCoreServerRank(Equal, 3, 1), 1);
    ok_int(DnsResolverCoreServerRank(Equal, 3, 2), 2);

    ok_int(DnsResolverCoreServerRank(FirstSilent, 3, 0), 2);
    ok_int(DnsResolverCoreServerRank(FirstSilent, 3, 1), 0);
    ok_int(DnsResolverCoreServerRank(FirstSilent, 3, 2), 1);
    ok_int(DnsResolverCoreServerRank(FirstSilent, 2, 0), 1);
    ok_int(DnsResolverCoreServerRank(FirstSilent, 2, 1), 0);

    ok_int(DnsResolverCoreServerRank(AllSilent, 3, 0), 0);
    ok_int(DnsResolverCoreServerRank(AllSilent, 3, 1), 1);
    ok_int(DnsResolverCoreServerRank(AllSilent, 3, 2), 2);

    ok_int(DnsResolverCoreServerRank(MiddleSilent, 3, 0), 0);
    ok_int(DnsResolverCoreServerRank(MiddleSilent, 3, 1), 2);
    ok_int(DnsResolverCoreServerRank(MiddleSilent, 3, 2), 1);
}

static
VOID
TestPriorityTimeLimit(VOID)
{
    ok_int(DNS_RESOLVER_CORE_PRIORITY_TIME_LIMIT, 900);
    ok_int(DnsResolverCorePrioritiesExpired(1000000, 1000000, 900), FALSE);
    ok_int(DnsResolverCorePrioritiesExpired(1899999, 1000000, 900), FALSE);
    ok_int(DnsResolverCorePrioritiesExpired(1900000, 1000000, 900), TRUE);
    ok_int(DnsResolverCorePrioritiesExpired(1000000, 1000000, 0), TRUE);
    ok_int(DnsResolverCorePrioritiesExpired(0xFFFFFFFFULL * 1000 - 1, 0, 0xFFFFFFFF), FALSE);
    ok_int(DnsResolverCorePrioritiesExpired(0xFFFFFFFFULL * 1000, 0, 0xFFFFFFFF), TRUE);
}

START_TEST(ResolverCore)
{
    TestTimeouts();
    TestOneAdapter();
    TestSeveralAdapters();
    TestPriorities();
    TestPriorityTimeLimit();
}
