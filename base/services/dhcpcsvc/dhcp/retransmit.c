/*
 * PROJECT:     LiberNT DHCP Client Service
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     RFC 2131 sections 4.1 and 4.4.5 DHCPDISCOVER/DHCPREQUEST retransmission schedule
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

unsigned long
dhcp_request_timeout_ms(unsigned int send, unsigned long random)
{
    /* FIXME: the fourth timeout of the REQUESTING state comes from a single Windows 11 sample (8.9 s) */
    static const unsigned char timeouts[DHCP_REQUEST_SENDS] = { 2, 3, 5, 9 };
    unsigned long delay;

    assert(send < DHCP_REQUEST_SENDS);

    delay = (unsigned long)timeouts[send] * 1000 - DHCP_RETRANSMIT_JITTER_MS;
    delay += random % (2 * DHCP_RETRANSMIT_JITTER_MS + 1);

    assert(delay >= (unsigned long)timeouts[send] * 1000 - DHCP_RETRANSMIT_JITTER_MS);
    assert(delay <= (unsigned long)timeouts[send] * 1000 + DHCP_RETRANSMIT_JITTER_MS);
    return delay;
}

unsigned long long
dhcp_renew_next_attempt_ms(unsigned long long end, unsigned long long deadline)
{
    unsigned long long next;

    if (end >= deadline)
        return end;

    next = end + (deadline - end) / 2;
    assert(next >= end && next <= deadline);
    return next;
}
