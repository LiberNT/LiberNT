/*
 * PROJECT:     LiberNT DHCP Client Service
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     RFC 2131 section 4.1 DHCPDISCOVER/DHCPREQUEST retransmission schedule
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#pragma once

#include <time.h>

#define DHCP_RETRANSMIT_INITIAL 4
#define DHCP_RETRANSMIT_MAX 64
#define DHCP_RETRANSMIT_JITTER_MS 1000
#define DHCP_DISCOVER_RESTART 32

time_t dhcp_retransmit_interval(time_t interval);
unsigned long dhcp_retransmit_delay_ms(time_t interval, unsigned long random);
int dhcp_discover_restarts(time_t interval);
int dhcp_discover_broadcast(unsigned int cycle);
