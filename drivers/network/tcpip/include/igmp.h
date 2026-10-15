/*
 * PROJECT:     LiberNT TCP/IP protocol driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     IPv4 multicast group membership and IGMPv3 host reports
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

NTSTATUS IGMPStartup(VOID);
NTSTATUS IGMPShutdown(VOID);
NTSTATUS IGMPJoinGroup(PVOID Owner, ULONG InterfaceIndex, IPv4_RAW_ADDRESS Group);
NTSTATUS IGMPLeaveGroup(PVOID Owner, ULONG InterfaceIndex, IPv4_RAW_ADDRESS Group);
VOID IGMPLeaveAllGroups(PVOID Owner);
BOOLEAN IGMPIsMember(PVOID Owner, ULONG InterfaceIndex, IPv4_RAW_ADDRESS Group);
VOID IGMPTimeout(VOID);
NTSTATUS LibIP6ChangeMembership(PVOID Owner, ULONG Index, const UCHAR *Group, BOOLEAN Join);
VOID LibIP6LeaveAllGroups(PVOID Owner);
