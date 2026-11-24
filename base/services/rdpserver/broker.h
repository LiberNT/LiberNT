/*
 * PROJECT:     LiberNT RDP console server
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Accept RDP connections in session 0 and give each signed-in user the agent of its session
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#pragma once

#include <winsock2.h>
#include <windows.h>

#define BROKER_DISPLAY_PREFIX L"RdpVdd_RemoteDisplay&NewSession_"
#define BROKER_SESSIONS_KEY L"SYSTEM\\CurrentControlSet\\Services\\LiberNTRDPServer\\Sessions"
#define AGENT_TIMEOUT 10000
#define AGENT_ATTACH_TIMEOUT 90000
#define WINLOGON_START_TIMEOUT 60000

typedef struct AGENT AGENT;
typedef void (*BROKER_LOG)(const char *fmt, ...);
typedef HANDLE (*BROKER_START_AGENT)(DWORD session, const WCHAR *pipe, const WCHAR *display);
typedef BOOL (*BROKER_ALLOWED)(void);
typedef BOOL (*BROKER_ACCEPTED)(SOCKET client);

BOOL BrokerStart(BROKER_LOG log, BROKER_START_AGENT start, BROKER_ALLOWED allowed, BROKER_ACCEPTED accepted);
void BrokerClose(void);
void BrokerStop(void);
void BrokerSetConsoleSession(DWORD session);
AGENT *BrokerConnect(const BYTE *sid, const WCHAR *user);
void BrokerDisconnect(AGENT *agent);

BOOL AgentEnter(AGENT *agent, LONG owner);
void AgentLeave(AGENT *agent);
LONG AgentTakeOver(AGENT *agent);
BOOL AgentOwnedBy(AGENT *agent, LONG owner);
BOOL AgentBroken(AGENT *agent);
DWORD AgentSession(AGENT *agent);
BOOL AgentWrite(AGENT *agent, const void *message, DWORD size);
BOOL AgentRead(AGENT *agent, void *buffer, DWORD capacity, DWORD *size, DWORD timeout);
void AgentAbandon(AGENT *agent);
HANDLE AgentDuplicate(AGENT *agent, ULONG64 handle, DWORD access);
