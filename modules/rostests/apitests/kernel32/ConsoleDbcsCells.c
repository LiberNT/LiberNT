/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test the cells of a new console on a DBCS code page
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include "precomp.h"
#include <strsafe.h>

static void Child(void)
{
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    CONSOLE_FONT_INFOEX cfi;
    HANDLE hOut;
    DWORD dwWritten;
    SHORT xStart;
    DWORD dwResult;

    hOut = CreateFileW(L"CONOUT$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                       OPEN_EXISTING, 0, NULL);
    GetConsoleScreenBufferInfo(hOut, &csbi);
    xStart = csbi.dwCursorPosition.X;
    WriteConsoleW(hOut, L"ab\x65e5", 3, &dwWritten, NULL);
    GetConsoleScreenBufferInfo(hOut, &csbi);
    ZeroMemory(&cfi, sizeof(cfi));
    cfi.cbSize = sizeof(cfi);
    GetCurrentConsoleFontEx(hOut, FALSE, &cfi);
    dwResult = (DWORD)(csbi.dwCursorPosition.X - xStart) & 0xFF;
    dwResult |= ((DWORD)cfi.dwFontSize.X & 0xFF) << 8;
    dwResult |= ((DWORD)cfi.dwFontSize.Y & 0xFF) << 16;
    dwResult |= (GetConsoleOutputCP() == 932) << 24;
    ExitProcess(dwResult);
}

START_TEST(ConsoleDbcsCells)
{
    WCHAR szExe[MAX_PATH], szCmd[MAX_PATH + 32];
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    CPINFO cpinfo;
    DWORD dwResult = 0;
    char **argv;

    if (winetest_get_mainargs(&argv) >= 3)
    {
        Child();
        return;
    }
    if (!GetCPInfo(GetOEMCP(), &cpinfo) || cpinfo.MaxCharSize != 2)
    {
        skip("The OEM code page %u is not a DBCS code page\n", GetOEMCP());
        return;
    }

    GetModuleFileNameW(NULL, szExe, _countof(szExe));
    StringCchPrintfW(szCmd, _countof(szCmd), L"\"%s\" ConsoleDbcsCells child", szExe);
    if (!CreateProcessW(NULL, szCmd, NULL, NULL, FALSE, CREATE_NEW_CONSOLE, NULL, NULL, &si, &pi))
    {
        skip("CreateProcessW failed %lu\n", GetLastError());
        return;
    }
    ok(WaitForSingleObject(pi.hProcess, 30000) == WAIT_OBJECT_0, "the child did not exit\n");
    GetExitCodeProcess(pi.hProcess, &dwResult);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    trace("code page 932 %lu, cell %lux%lu\n", dwResult >> 24, (dwResult >> 8) & 0xFF, (dwResult >> 16) & 0xFF);
    ok_long(dwResult & 0xFF, 4);
    ok(((dwResult >> 8) & 0xFF) * 2 <= ((dwResult >> 16) & 0xFF), "the cell is not half width\n");
}
