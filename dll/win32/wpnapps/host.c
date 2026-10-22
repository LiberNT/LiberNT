/*
 * PROJECT:     LiberNT Windows Runtime
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Per-session notification host that shows popups and delivers scheduled toasts
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "private.h"
#include <sddl.h>

static BOOL host_mutex_name(WCHAR *name, DWORD size)
{
    BYTE buffer[sizeof(TOKEN_USER) + SECURITY_MAX_SID_SIZE];
    TOKEN_USER *user = (TOKEN_USER *)buffer;
    WCHAR *sid = NULL;
    HANDLE token;
    DWORD length;
    BOOL result = FALSE;

    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        return FALSE;
    if (GetTokenInformation(token, TokenUser, user, sizeof(buffer), &length) &&
        ConvertSidToStringSidW(user->User.Sid, &sid))
    {
        swprintf(name, size, L"Local\\LiberNT.NotificationHost.%ls", sid);
        LocalFree(sid);
        result = TRUE;
    }
    CloseHandle(token);
    return result;
}

void host_ensure(void)
{
    WCHAR name[192], directory[MAX_PATH], command[MAX_PATH * 2];
    STARTUPINFOW startup = {sizeof(startup)};
    PROCESS_INFORMATION process;
    HANDLE mutex;

    if (!host_mutex_name(name, ARRAYSIZE(name)))
        return;
    if ((mutex = OpenMutexW(SYNCHRONIZE, FALSE, name)))
    {
        CloseHandle(mutex);
        return;
    }
    if (!GetSystemDirectoryW(directory, ARRAYSIZE(directory)))
        return;
    swprintf(command, ARRAYSIZE(command), L"\"%ls\\rundll32.exe\" wpnapps.dll,NotificationHost", directory);
    if (CreateProcessW(NULL, command, NULL, NULL, FALSE, CREATE_NO_WINDOW | DETACHED_PROCESS, NULL, directory,
                       &startup, &process))
    {
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
    }
}

void WINAPI NotificationHost(HWND window, HINSTANCE instance, char *command, int show)
{
    WCHAR name[192];
    HANDLE mutex;

    if (!host_mutex_name(name, ARRAYSIZE(name)))
        return;
    mutex = CreateMutexW(NULL, FALSE, name);
    if (!mutex || GetLastError() == ERROR_ALREADY_EXISTS)
    {
        if (mutex)
            CloseHandle(mutex);
        return;
    }
    presenter_run();
    CloseHandle(mutex);
}
