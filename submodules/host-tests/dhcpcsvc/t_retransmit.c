/*
 * PROJECT:     LiberNT host-native tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     DHCP client RFC 2131 retransmission schedule against Windows 11
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#undef NDEBUG
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include "retransmit.h"

/* Gaps between the DHCPDISCOVERs of one xid sent by Windows 11 with no DHCP server, two xids. */
static const unsigned long Win11GapsMs[][3] =
{
    { 3999, 7183, 16990 },
    { 4999, 8248, 16812 },
};

/* Gap between the last DHCPDISCOVER of one xid and the first of the next on Windows 11. */
static const unsigned long Win11RestartGapMs = 32960;

/* Windows 11 timers fire this late at most in the captures below. */
#define WIN11_LATENCY_MS 100

/* Gaps after each DHCPREQUEST of a REQUESTING sequence on Windows 11 (the last one ends with a
   new DHCPDISCOVER), two sequences; 0 where the capture ended. */
static const unsigned long Win11RequestingGapsMs[][DHCP_REQUEST_SENDS] =
{
    { 2002, 3017, 5000, 8915 },
    { 2924, 2997, 5225, 0 },
};

/* Gaps between the three DHCPREQUESTs of each RENEWING attempt, then of the REBINDING attempt,
   on Windows 11 with a 240 s lease that the server never renews. */
static const unsigned long Win11RenewGapsMs[][DHCP_RENEW_SENDS - 1] =
{
    { 2008, 3007 }, { 2000, 3001 }, { 1000, 2001 }, { 2003, 2002 }, { 3001, 3000 },
};

/* Same capture, ms on the server clock: T2, the lease expiry, and for each attempt the last
   DHCPREQUEST and the first DHCPREQUEST of the next attempt. */
static const unsigned long long Win11T2Ms = 272006;
static const unsigned long long Win11ExpiryMs = 302006;
static const unsigned long long Win11AttemptsMs[][2] =
{
    { 187021, 232024 }, { 237025, 257029 }, { 260030, 269037 }, { 273042, 290055 },
};

static void
TestIntervals(void)
{
    static const time_t Expected[] = { 4, 8, 16, 32, 64, 64, 64 };
    time_t Interval = 0;
    unsigned Index;

    for (Index = 0; Index < sizeof(Expected) / sizeof(Expected[0]); Index++)
    {
        Interval = dhcp_retransmit_interval(Interval);
        assert(Interval == Expected[Index]);
    }
    assert(dhcp_retransmit_interval(5) == 10);
    assert(dhcp_retransmit_interval(33) == DHCP_RETRANSMIT_MAX);
}

static void
TestJitter(void)
{
    time_t Interval;

    for (Interval = DHCP_RETRANSMIT_INITIAL; Interval <= DHCP_RETRANSMIT_MAX; Interval++)
    {
        unsigned long Base = (unsigned long)Interval * 1000;

        assert(dhcp_retransmit_delay_ms(Interval, 0) == Base - 1000);
        assert(dhcp_retransmit_delay_ms(Interval, 1000) == Base);
        assert(dhcp_retransmit_delay_ms(Interval, 2000) == Base + 1000);
        assert(dhcp_retransmit_delay_ms(Interval, 2001) == Base - 1000);
        assert(dhcp_retransmit_delay_ms(Interval, ULONG_MAX) >= Base - 1000);
        assert(dhcp_retransmit_delay_ms(Interval, ULONG_MAX) <= Base + 1000);
    }
}

static void
TestRestart(void)
{
    time_t Interval = 0;
    unsigned Sends;

    for (Sends = 1; Sends <= 3; Sends++)
    {
        Interval = dhcp_retransmit_interval(Interval);
        assert(!dhcp_discover_restarts(Interval));
    }
    Interval = dhcp_retransmit_interval(Interval);
    assert(Interval == DHCP_DISCOVER_RESTART);
    assert(dhcp_discover_restarts(Interval));
    assert(Win11RestartGapMs >= dhcp_retransmit_delay_ms(Interval, 0));
    assert(Win11RestartGapMs <= dhcp_retransmit_delay_ms(Interval, 2000));
    assert(!dhcp_discover_restarts(0));
}

static void
TestBroadcastFlag(void)
{
    /* Windows 11 sends its first discovery cycle without BOOTP_BROADCAST and toggles it on each new xid. */
    static const int Expected[] = { 0, 1, 0, 1 };
    unsigned Cycle;

    for (Cycle = 0; Cycle < sizeof(Expected) / sizeof(Expected[0]); Cycle++)
        assert(dhcp_discover_broadcast(Cycle) == Expected[Cycle]);
    assert(dhcp_discover_broadcast(UINT_MAX) == 1);
}

static void
TestWindows11Schedule(void)
{
    unsigned Cycle, Index;

    for (Cycle = 0; Cycle < sizeof(Win11GapsMs) / sizeof(Win11GapsMs[0]); Cycle++)
    {
        time_t Interval = 0;

        for (Index = 0; Index < 3; Index++)
        {
            Interval = dhcp_retransmit_interval(Interval);
            assert(Win11GapsMs[Cycle][Index] >= dhcp_retransmit_delay_ms(Interval, 0));
            assert(Win11GapsMs[Cycle][Index] <= dhcp_retransmit_delay_ms(Interval, 2000));
        }
    }
}

static void
TestRequestTimeouts(void)
{
    static const unsigned long BaseMs[DHCP_REQUEST_SENDS] = { 2000, 3000, 5000, 9000 };
    unsigned Send, Cycle;

    assert(DHCP_RENEW_SENDS == 3 && DHCP_RENEW_UNICAST_SENDS == 2 && DHCP_REQUEST_SENDS == 4);
    for (Send = 0; Send < DHCP_REQUEST_SENDS; Send++)
    {
        assert(dhcp_request_timeout_ms(Send, 0) == BaseMs[Send] - 1000);
        assert(dhcp_request_timeout_ms(Send, 1000) == BaseMs[Send]);
        assert(dhcp_request_timeout_ms(Send, 2000) == BaseMs[Send] + 1000);
        assert(dhcp_request_timeout_ms(Send, ULONG_MAX) <= BaseMs[Send] + 1000);
    }

    for (Cycle = 0; Cycle < sizeof(Win11RequestingGapsMs) / sizeof(Win11RequestingGapsMs[0]); Cycle++)
    {
        for (Send = 0; Send < DHCP_REQUEST_SENDS; Send++)
        {
            unsigned long Gap = Win11RequestingGapsMs[Cycle][Send];

            if (Gap == 0)
                continue;
            assert(Gap >= dhcp_request_timeout_ms(Send, 0));
            assert(Gap <= dhcp_request_timeout_ms(Send, 2000) + WIN11_LATENCY_MS);
        }
    }

    for (Cycle = 0; Cycle < sizeof(Win11RenewGapsMs) / sizeof(Win11RenewGapsMs[0]); Cycle++)
    {
        for (Send = 0; Send < DHCP_RENEW_SENDS - 1; Send++)
        {
            unsigned long Gap = Win11RenewGapsMs[Cycle][Send];

            assert(Gap >= dhcp_request_timeout_ms(Send, 0));
            assert(Gap <= dhcp_request_timeout_ms(Send, 2000) + WIN11_LATENCY_MS);
        }
    }
}

static void
TestRenewSchedule(void)
{
    unsigned Attempt;

    assert(dhcp_renew_next_attempt_ms(10000, 20000) == 15000);
    assert(dhcp_renew_next_attempt_ms(19999, 20000) == 19999);
    assert(dhcp_renew_next_attempt_ms(20000, 20000) == 20000);
    assert(dhcp_renew_next_attempt_ms(25000, 20000) == 25000);
    assert(dhcp_renew_next_attempt_ms(ULLONG_MAX - 2, ULLONG_MAX) == ULLONG_MAX - 1);

    for (Attempt = 0; Attempt < sizeof(Win11AttemptsMs) / sizeof(Win11AttemptsMs[0]); Attempt++)
    {
        unsigned long long Last = Win11AttemptsMs[Attempt][0];
        unsigned long long Next = Win11AttemptsMs[Attempt][1];
        unsigned long long EarlyEnd = Last + dhcp_request_timeout_ms(DHCP_RENEW_SENDS - 1, 0);
        unsigned long long LateEnd = Last + dhcp_request_timeout_ms(DHCP_RENEW_SENDS - 1, 2000) + WIN11_LATENCY_MS;
        unsigned long long Deadline = EarlyEnd < Win11T2Ms ? Win11T2Ms : Win11ExpiryMs;

        assert((LateEnd < Win11T2Ms) == (EarlyEnd < Win11T2Ms));
        assert(Next >= dhcp_renew_next_attempt_ms(EarlyEnd, Deadline));
        assert(Next <= dhcp_renew_next_attempt_ms(LateEnd, Deadline));
    }
}

int
main(void)
{
    TestIntervals();
    TestJitter();
    TestRestart();
    TestBroadcastFlag();
    TestRequestTimeouts();
    TestRenewSchedule();
    TestWindows11Schedule();
    printf("dhcpcsvc.retransmit: ok\n");
    return 0;
}
