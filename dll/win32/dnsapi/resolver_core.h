/*
 * PROJECT:     LiberNT DNS API
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Which DNS servers each attempt of a query asks, and how long it waits
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#define DNS_RESOLVER_CORE_ATTEMPTS 5
#define DNS_RESOLVER_CORE_SINGLE_ATTEMPTS 3
#define DNS_RESOLVER_CORE_PRIORITY_DEFAULT 0
#define DNS_RESOLVER_CORE_PRIORITY_REDUCED (-1)
#define DNS_RESOLVER_CORE_PRIORITY_TIME_LIMIT 900

/* Microsoft "DNS client resolution timeouts": queries leave at 0, 1, 2, 4 and 8 s and the client gives up at 10 s */
FORCEINLINE
ULONG
DnsResolverCoreAttemptTimeout(_In_ ULONG Attempt)
{
    static const ULONG Timeouts[DNS_RESOLVER_CORE_ATTEMPTS] = {1000, 1000, 2000, 4000, 2000};

    return Attempt < DNS_RESOLVER_CORE_ATTEMPTS ? Timeouts[Attempt] : 0;
}

FORCEINLINE
ULONG
DnsResolverCoreServerRank(_In_reads_(Count) const LONG *Priorities, _In_ ULONG Count, _In_ ULONG Index)
{
    ULONG i, Rank = 0;

    for (i = 0; i < Count; i++)
    {
        if (Priorities[i] > Priorities[Index] || (Priorities[i] == Priorities[Index] && i < Index))
            Rank++;
    }
    return Rank;
}

FORCEINLINE
BOOLEAN
DnsResolverCoreAttemptAsksServer(_In_ ULONG Attempt, _In_ ULONG AdapterRank, _In_ ULONG ServerRank, _In_ ULONG ServerCount)
{
    ULONG Step;

    if (ServerCount == 0 || ServerRank >= ServerCount)
        return FALSE;
    if (Attempt >= DNS_RESOLVER_CORE_SINGLE_ATTEMPTS)
        return TRUE;
    if (AdapterRank != 0 && Attempt == 0)
        return FALSE;

    Step = AdapterRank != 0 ? Attempt - 1 : Attempt;
    if (Step > ServerCount - 1)
        Step = ServerCount - 1;
    return ServerRank == Step;
}

FORCEINLINE
LONG
DnsResolverCorePriorityAfter(_In_ BOOLEAN Answered)
{
    return Answered ? DNS_RESOLVER_CORE_PRIORITY_DEFAULT : DNS_RESOLVER_CORE_PRIORITY_REDUCED;
}

FORCEINLINE
BOOLEAN
DnsResolverCorePrioritiesExpired(_In_ ULONGLONG Now, _In_ ULONGLONG LastReset, _In_ ULONG TimeLimitSeconds)
{
    return Now - LastReset >= (ULONGLONG)TimeLimitSeconds * 1000;
}
