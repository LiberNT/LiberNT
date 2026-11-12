/*
 * PROJECT:     LiberNT DHCP Client Service
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     RFC 2131 sections 4.1 and 4.4.5 DHCPDISCOVER/DHCPREQUEST retransmission schedule
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#pragma once

#include <time.h>

#define DHCP_RETRANSMIT_INITIAL 4
#define DHCP_RETRANSMIT_MAX 64
#define DHCP_RETRANSMIT_JITTER_MS 1000
#define DHCP_DISCOVER_RESTART 32
#define DHCP_REQUEST_SENDS 4
#define DHCP_RENEW_SENDS 3
#define DHCP_RENEW_UNICAST_SENDS 2

time_t dhcp_retransmit_interval(time_t interval);
unsigned long dhcp_retransmit_delay_ms(time_t interval, unsigned long random);
int dhcp_discover_restarts(time_t interval);
int dhcp_discover_broadcast(unsigned int cycle);
unsigned long dhcp_request_timeout_ms(unsigned int send, unsigned long random);
unsigned long long dhcp_renew_next_attempt_ms(unsigned long long end, unsigned long long deadline);
