/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Task Manager Start Menu shortcut tests
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>

#define WIN32_NO_STATUS
#include <windows.h>
#include <setupapi.h>

static BOOL
FindShortcutTitle(HINF Inf, PCWSTR Program, PWSTR Title, DWORD TitleCount)
{
    INFCONTEXT Context;
    WCHAR Command[MAX_PATH];
    SIZE_T CommandLength, ProgramLength = wcslen(Program);
    BOOL More;

    for (More = SetupFindFirstLineW(Inf, L"SystemToolsShortcuts", NULL, &Context);
         More;
         More = SetupFindNextLine(&Context, &Context))
    {
        if (!SetupGetStringFieldW(&Context, 1, Command, _countof(Command), NULL))
            continue;

        CommandLength = wcslen(Command);
        if (CommandLength > ProgramLength &&
            Command[CommandLength - ProgramLength - 1] == L'\\' &&
            !_wcsicmp(Command + CommandLength - ProgramLength, Program))
        {
            return SetupGetStringFieldW(&Context, 2, Title, TitleCount, NULL);
        }
    }

    return FALSE;
}

START_TEST(shortcuts)
{
    WCHAR Classic[MAX_PATH] = L"";
    WCHAR Modern[MAX_PATH] = L"";
    HINF Inf = SetupOpenInfFileW(L"shortcuts.inf", NULL, INF_STYLE_WIN4, NULL);

    if (Inf == INVALID_HANDLE_VALUE)
    {
        skip("shortcuts.inf is not installed, error %lu\n", GetLastError());
        return;
    }

    ok(FindShortcutTitle(Inf, L"taskmgr.exe", Classic, _countof(Classic)),
       "taskmgr.exe has no System Tools shortcut\n");
    ok(FindShortcutTitle(Inf, L"taskmgr11.exe", Modern, _countof(Modern)),
       "taskmgr11.exe has no System Tools shortcut\n");
    ok(Classic[0] != UNICODE_NULL && Modern[0] != UNICODE_NULL,
       "a task manager shortcut has no name\n");
    ok(_wcsicmp(Classic, Modern) != 0,
       "both task managers install as \"%ls.lnk\", one replaces the other\n", Classic);

    SetupCloseInfFile(Inf);
}
