/*
 * PROJECT:     LiberNT API Tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Native process creation from an x86 WoW64 caller
 */

#include "precomp.h"

START_TEST(wow64_native_process)
{
#ifdef _M_IX86
    static const struct
    {
        PCWSTR Directory;
        BOOL DisableRedirection;
        BOOL PassApplicationName;
    } Cases[] =
    {
        { L"sysnative", FALSE, TRUE },
        { L"system32", TRUE, TRUE },
        { L"sysnative", FALSE, FALSE },
        { L"system32", TRUE, FALSE },
    };
    STARTUPINFOW Startup = { sizeof(Startup) };
    PROCESS_INFORMATION Process;
    WCHAR Windows[MAX_PATH], Image[MAX_PATH], Command[MAX_PATH + 64];
    PVOID Redirection;
    BOOL Wow64 = FALSE, Created, Disabled;
    DWORD Error, Wait, ExitCode, Resume;
    ULONG i, Length;

    if (!IsWow64Process(GetCurrentProcess(), &Wow64) || !Wow64)
    {
        skip("Requires an x86 process under WoW64\n");
        return;
    }
    Length = GetWindowsDirectoryW(Windows, ARRAYSIZE(Windows));
    ok(Length && Length < ARRAYSIZE(Windows), "Invalid Windows directory length %lu\n", Length);
    if (!Length || Length >= ARRAYSIZE(Windows)) return;

    for (i = 0; i < ARRAYSIZE(Cases); i++)
    {
        StringCbPrintfW(Image, sizeof(Image), L"%ls\\%ls\\cmd.exe", Windows, Cases[i].Directory);
        StringCbPrintfW(Command, sizeof(Command), L"\"%ls\" /d /c exit 42", Image);
        Disabled = FALSE;
        if (Cases[i].DisableRedirection)
        {
            Disabled = Wow64DisableWow64FsRedirection(&Redirection);
            ok(Disabled, "Disable redirection failed: %lu\n", GetLastError());
            if (!Disabled) continue;
        }
        Created = CreateProcessW(Cases[i].PassApplicationName ? Image : NULL, Command, NULL, NULL, FALSE, CREATE_SUSPENDED | CREATE_NO_WINDOW,
                                 NULL, Windows, &Startup, &Process);
        Error = GetLastError();
        if (Disabled) ok(Wow64RevertWow64FsRedirection(Redirection), "Restore redirection failed: %lu\n", GetLastError());
        ok(Created, "Native CreateProcess via %ls (case %lu) failed: %lu\n", Cases[i].Directory, i, Error);
        if (!Created) continue;

        Wow64 = TRUE;
        ok(IsWow64Process(Process.hProcess, &Wow64), "Query child architecture failed: %lu\n", GetLastError());
        ok(!Wow64, "Case %lu created an x86 child instead of a native child\n", i);
        Resume = ResumeThread(Process.hThread);
        ok(Resume == 1, "Wrong initial suspend count %lu\n", Resume);
        Wait = WaitForSingleObject(Process.hProcess, 30000);
        ok_hex(Wait, WAIT_OBJECT_0);
        if (Wait == WAIT_OBJECT_0)
        {
            ExitCode = STILL_ACTIVE;
            ok(GetExitCodeProcess(Process.hProcess, &ExitCode), "Query child exit failed: %lu\n", GetLastError());
            ok_hex(ExitCode, 42);
        }
        else
        {
            TerminateProcess(Process.hProcess, 1);
            WaitForSingleObject(Process.hProcess, 30000);
        }
        CloseHandle(Process.hThread);
        CloseHandle(Process.hProcess);
    }
#else
    skip("Requires an x86 build\n");
#endif
}
