/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Remote Desktop clients and accounts shared by the Remote Desktop server tests
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#pragma once


typedef struct _RDP_CLIENT
{
    const char *User;
    const char *RoutingUser;
    HANDLE Thread;
    HANDLE Stop;
    HANDLE Connected;
    void *Instance;
    volatile LONG Alive;
    volatile LONG Disconnected;
    volatile LONG Updates;
    UINT32 Error;
} RDP_CLIENT, *PRDP_CLIENT;

typedef struct _RDP_TEST_SETUP
{
    HKEY Key;
    DWORD Original;
    BOOL Started;
} RDP_TEST_SETUP, *PRDP_TEST_SETUP;

BOOL RdpTestEnableDebugPrivilege(VOID);
const char *RdpTestPassword(VOID);
BOOL RdpTestStart(PRDP_TEST_SETUP Setup);
VOID RdpTestEnd(PRDP_TEST_SETUP Setup);
BOOL RdpTestAddUser(PCWSTR Name);
BOOL RdpClientStart(PRDP_CLIENT Client, const char *User, const char *RoutingUser);
BOOL RdpClientWaitConnected(PRDP_CLIENT Client);
VOID RdpClientStop(PRDP_CLIENT Client);
