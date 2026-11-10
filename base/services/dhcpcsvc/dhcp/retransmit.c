/*
 * PROJECT:     LiberNT DHCP Client Service
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     RFC 2131 section 4.1 DHCPDISCOVER/DHCPREQUEST retransmission schedule
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#include <assert.h>
#include "retransmit.h"

time_t
dhcp_retransmit_interval(time_t interval)
{
    assert(interval >= 0);

    if (interval == 0)
        return DHCP_RETRANSMIT_INITIAL;
    if (interval >= DHCP_RETRANSMIT_MAX / 2)
        return DHCP_RETRANSMIT_MAX;
    return interval * 2;
}

unsigned long
dhcp_retransmit_delay_ms(time_t interval, unsigned long random)
{
    unsigned long delay;

    assert(interval >= DHCP_RETRANSMIT_INITIAL && interval <= DHCP_RETRANSMIT_MAX);

    delay = (unsigned long)interval * 1000 - DHCP_RETRANSMIT_JITTER_MS;
    delay += random % (2 * DHCP_RETRANSMIT_JITTER_MS + 1);

    assert(delay >= (unsigned long)interval * 1000 - DHCP_RETRANSMIT_JITTER_MS);
    assert(delay <= (unsigned long)interval * 1000 + DHCP_RETRANSMIT_JITTER_MS);
    return delay;
}

int
dhcp_discover_restarts(time_t interval)
{
    assert(interval >= 0);

    return interval >= DHCP_DISCOVER_RESTART;
}

int
dhcp_discover_broadcast(unsigned int cycle)
{
    return (cycle & 1) != 0;
}
