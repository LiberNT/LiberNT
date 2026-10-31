/*
 * PROJECT:     LiberNT Windows Firewall service
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Firewall policy rule and WFP translation interface
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#pragma once

#include <windows.h>

#define MPS_MAX_RULE_CHARS 4096
#define MPS_MAX_PORTS 16
#define MPS_MAX_APP_PATH 1024
#define MPS_MAX_ADDRESSES 16

typedef struct _MPS_PORT_RANGE
{
    USHORT First;
    USHORT Last;
} MPS_PORT_RANGE;

typedef struct _MPS_V4_SUBNET
{
    UINT32 Address;
    UINT32 Mask;
} MPS_V4_SUBNET;

typedef enum _MPS_RULE_ACTION
{
    MpsRuleActionAllow,
    MpsRuleActionBlock,
    MpsRuleActionBypass
} MPS_RULE_ACTION;

typedef struct _MPS_FIREWALL_RULE
{
    BOOL Active;
    BOOL Inbound;
    BOOL HasAction;
    BOOL HasDirection;
    BOOL HasActive;
    BOOL HasProtocol;
    BOOL HasProfile;
    BOOL HasApplication;
    BOOL HasService;
    BOOL Unsupported;
    MPS_RULE_ACTION Action;
    UINT32 Protocol;
    UINT32 ProfileMask;
    UINT32 LocalAddressCount;
    UINT32 RemoteAddressCount;
    MPS_V4_SUBNET LocalAddresses[MPS_MAX_ADDRESSES];
    MPS_V4_SUBNET RemoteAddresses[MPS_MAX_ADDRESSES];
    UINT32 LocalPortCount;
    UINT32 RemotePortCount;
    MPS_PORT_RANGE LocalPorts[MPS_MAX_PORTS];
    MPS_PORT_RANGE RemotePorts[MPS_MAX_PORTS];
    WCHAR Application[MPS_MAX_APP_PATH];
} MPS_FIREWALL_RULE;

DWORD
MpsParseFirewallRule(
    _In_reads_(TextLength) PCWSTR Text,
    _In_ SIZE_T TextLength,
    _Out_ MPS_FIREWALL_RULE *Rule);

DWORD
MpsApplyFirewallRule(
    _In_ HANDLE Engine,
    _In_ const GUID *SubLayerKey,
    _In_ const MPS_FIREWALL_RULE *Rule);

DWORD
MpsAddFirewallSubLayer(
    _In_ HANDLE Engine,
    _In_ const GUID *SubLayerKey);

DWORD
MpsAddLoopbackPermit(
    _In_ HANDLE Engine,
    _In_ const GUID *SubLayerKey);

DWORD
MpsLoadFirewallRulesFromKey(
    _In_ HANDLE Engine,
    _In_ const GUID *SubLayerKey,
    _In_ HKEY Key);
