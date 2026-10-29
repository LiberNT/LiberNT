/*
 * PROJECT:     LiberNT TCP/IP protocol driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     lwIP hooks implemented by the TCP/IP glue
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#pragma once

#include "lwip/err.h"

struct tcp_pcb;
struct tcp_hdr;

struct netif *LibIP6RouteHook(const ip6_addr_t *Source, const ip6_addr_t *Destination);
const ip6_addr_t *LibIP6GatewayHook(struct netif *Netif, const ip6_addr_t *Destination);
u32_t LibIP6ReachableTime(struct netif *Netif, u32_t Default);
u16_t LibIP6DadTransmits(struct netif *Netif, u16_t Default);
u32_t LibIP6RetransTime(struct netif *Netif, u32_t Default);
u8_t LibIP6RouterAllowed(struct netif *Netif);
u32_t LibIP6PmtuTimeout(const ip6_addr_t *NextHop);
u8_t LibIP6ForwardAllowed(struct netif *Netif);
u8_t LibIP6WeakReceive(struct netif *Input, struct netif *Owner);
u8_t LibIP6WeakSend(const ip6_addr_t *Source);
u8_t LibIP6AddressIsManaged(struct netif *Netif, s8_t Slot);
void LibIP6StableAddress(struct netif *Netif, const ip6_addr_t *Prefix, ip6_addr_t *Address);
s8_t LibIP6SourceRank(struct netif *Netif, s8_t Slot);
u16_t LibIP4PathMtu(const ip4_addr_t *Destination, struct netif *Netif);
void LibIP6PathOutput(struct netif *Netif, const ip6_addr_t *Source, const ip6_addr_t *Destination, u8_t NextHeader,
                      struct pbuf *p);
void TiSignalChange(void);
err_t LibTCPWfpInPacket(struct tcp_pcb *pcb, struct tcp_hdr *hdr);
