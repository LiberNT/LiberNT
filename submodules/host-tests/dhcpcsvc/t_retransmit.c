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

int
main(void)
{
    TestIntervals();
    TestJitter();
    TestRestart();
    TestBroadcastFlag();
    TestWindows11Schedule();
    printf("dhcpcsvc.retransmit: ok\n");
    return 0;
}
