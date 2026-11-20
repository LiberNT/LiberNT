/*
 * PROJECT:     LiberNT RDP console server
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Accept RDP connections in session 0 and hand each one to the server of its session
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#pragma once

#include <winsock2.h>
#include <windows.h>

#define BROKER_PIPE_FORMAT L"\\\\.\\pipe\\LiberNT-RDPServer-%lu"
#define BROKER_DISPLAY_PREFIX L"RdpVdd_RemoteDisplay&NewSession_"

typedef void (*BROKER_LOG)(const char *fmt, ...);
typedef HANDLE (*BROKER_START_AGENT)(DWORD session, const WCHAR *display);
typedef BOOL (*BROKER_ALLOWED)(void);

typedef struct BROKER_HANDOFF
{
    WSAPROTOCOL_INFOW Socket;
} BROKER_HANDOFF;

BOOL BrokerStart(BROKER_LOG log, BROKER_START_AGENT start, BROKER_ALLOWED allowed);
void BrokerStop(void);
void BrokerSetConsoleAgent(HANDLE process, DWORD session);
BOOL BrokerCookieUser(const BYTE *data, int length, WCHAR *user, DWORD count);
