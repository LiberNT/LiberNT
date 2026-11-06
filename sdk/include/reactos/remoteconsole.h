/*
 * PROJECT:     LiberNT Remote Desktop
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Private pipe between the RDP server and Winlogon for a session taken over remotely
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#pragma once

#define REMOTE_CONSOLE_PIPE_FORMAT L"\\\\.\\pipe\\LiberNT-RemoteConsole-%lu"

#define REMOTE_CONSOLE_CONNECT    1
#define REMOTE_CONSOLE_DISCONNECT 2
#define REMOTE_CONSOLE_TAKEOVER   3

#define REMOTE_CONSOLE_KEY L"SYSTEM\\CurrentControlSet\\Control\\Terminal Server\\RemoteSessions"

typedef struct _REMOTE_CONSOLE_MESSAGE
{
    ULONG Code;
    ULONG Status;
    ULONG Connection;
    ULONG TakenOver;
    WCHAR UserName[257];
    WCHAR Domain[256];
    WCHAR Password[256];
} REMOTE_CONSOLE_MESSAGE, *PREMOTE_CONSOLE_MESSAGE;
