/*
 * PROJECT:     LiberNT SAS Library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Software generation of the secure attention sequence
 * COPYRIGHT:   Copyright 2026 LuRy <lury@lury.cz>
 */

#include <windef.h>
#include <winbase.h>
#include <winreg.h>
#include <sas.h>
#include <strsafe.h>
#include <winlogon_sas.h>

#define NDEBUG
#include <reactos/debug.h>

#define SAS_GENERATION_SERVICES       0x1
#define SAS_GENERATION_EASE_OF_ACCESS 0x2

static
BOOL
IsSasGenerationAllowed(
    _In_ BOOL AsUser)
{
    DWORD Policy = 0, Size = sizeof(Policy);
    LSTATUS Status;

    Status = RegGetValueW(HKEY_LOCAL_MACHINE,
                          L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Policies\\System",
                          L"SoftwareSASGeneration",
                          RRF_RT_REG_DWORD,
                          NULL,
                          &Policy,
                          &Size);
    if (Status != ERROR_SUCCESS)
        return FALSE;

    /* TODO: SAS_GENERATION_EASE_OF_ACCESS needs a Winlogon event that uiAccess callers may signal */
    if (AsUser)
    {
        DPRINT1("SendSAS for Ease of Access applications is not supported\n");
        return FALSE;
    }

    return (Policy & SAS_GENERATION_SERVICES) != 0;
}

VOID
WINAPI
SendSAS(
    _In_ BOOL AsUser)
{
    WCHAR EventName[64];
    DWORD SessionId, Size;
    HANDLE hEvent = NULL, hThreadToken;

    if (!OpenThreadToken(GetCurrentThread(), TOKEN_QUERY | TOKEN_IMPERSONATE, TRUE, &hThreadToken))
    {
        if (GetLastError() != ERROR_NO_TOKEN)
            return;
        hThreadToken = NULL;
        if (!ProcessIdToSessionId(GetCurrentProcessId(), &SessionId))
            return;
    }
    else if (!GetTokenInformation(hThreadToken, TokenSessionId, &SessionId, sizeof(SessionId), &Size) ||
             !RevertToSelf())
    {
        CloseHandle(hThreadToken);
        return;
    }

    if (!IsSasGenerationAllowed(AsUser))
    {
        DPRINT1("SAS generation is disabled by policy\n");
    }
    else
    {
        if (SessionId == 0)
            SessionId = WTSGetActiveConsoleSessionId();
        if (SessionId != 0xFFFFFFFF)
        {
            StringCchPrintfW(EventName, ARRAYSIZE(EventName), WINLOGON_SOFTWARE_SAS_EVENT, SessionId);
            hEvent = OpenEventW(EVENT_MODIFY_STATE, FALSE, EventName);
            if (!hEvent)
                DPRINT1("OpenEventW(%S) failed (%lu)\n", EventName, GetLastError());
        }
    }

    if (hThreadToken)
    {
        if (!SetThreadToken(NULL, hThreadToken))
            DPRINT1("Restoring the impersonation failed (%lu)\n", GetLastError());
        CloseHandle(hThreadToken);
    }

    if (hEvent)
    {
        SetEvent(hEvent);
        CloseHandle(hEvent);
    }
}
