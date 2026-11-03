/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The 64-bit rundll32 running an entry point of a 32-bit DLL
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arifing193@gmail.com>
 */

#include <apitest.h>
#include <windows.h>
#include <strsafe.h>

#define TEST_KEY L"Software\\LiberNT Tests\\rundll32"
#define NOT_EXITED 0xffffffff

static BOOL write_inf(PCWSTR path, PCSTR value)
{
    char text[512];
    DWORD written;
    HANDLE file;
    BOOL res;
    int length;

    StringCbPrintfA(text, sizeof(text),
                       "[Version]\r\nSignature=\"$Windows NT$\"\r\n\r\n"
                       "[DefaultInstall]\r\nAddReg=LntReg\r\n\r\n"
                       "[LntReg]\r\nHKCU,\"Software\\LiberNT Tests\\rundll32\",%s,0x00010001,1\r\n", value);
    length = lstrlenA(text);
    file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE)
        return FALSE;
    res = WriteFile(file, text, length, &written, NULL) && written == (DWORD)length;
    CloseHandle(file);
    return res;
}

/* The exit code, or NOT_EXITED when rundll32 had to be stopped after the time limit. */
static DWORD run_rundll32(PCWSTR rundll32, PCWSTR dll, PCWSTR inf)
{
    WCHAR command[3 * MAX_PATH];
    PROCESS_INFORMATION info;
    STARTUPINFOW startup = { sizeof(startup) };
    DWORD code = NOT_EXITED;

    StringCbPrintfW(command, sizeof(command), L"\"%ls\" %ls,InstallHinfSection DefaultInstall 128 %ls", rundll32, dll, inf);
    if (!CreateProcessW(rundll32, command, NULL, NULL, FALSE, 0, NULL, NULL, &startup, &info))
    {
        ok(0, "CreateProcessW(%ls) failed: %lu\n", command, GetLastError());
        return NOT_EXITED;
    }
    if (WaitForSingleObject(info.hProcess, 15000) == WAIT_OBJECT_0)
        GetExitCodeProcess(info.hProcess, &code);
    else
        TerminateProcess(info.hProcess, 1);
    CloseHandle(info.hThread);
    CloseHandle(info.hProcess);
    return code;
}

static BOOL value_written(PCWSTR value)
{
    DWORD data = 0, size = sizeof(data);
    HKEY key;
    LONG res;

    if (RegOpenKeyExW(HKEY_CURRENT_USER, TEST_KEY, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS)
        return FALSE;
    res = RegQueryValueExW(key, value, NULL, NULL, (BYTE *)&data, &size);
    RegCloseKey(key);
    return res == ERROR_SUCCESS && data == 1;
}

START_TEST(wow64)
{
    WCHAR system[MAX_PATH], wow64[MAX_PATH], rundll32[MAX_PATH], dll[MAX_PATH], temp[MAX_PATH], inf[MAX_PATH];
    DWORD code;

    if (sizeof(void *) < 8 || !GetSystemWow64DirectoryW(wow64, ARRAYSIZE(wow64)))
    {
        skip("no WoW64 directory for a 64-bit process\n");
        return;
    }
    GetSystemDirectoryW(system, ARRAYSIZE(system));
    GetTempPathW(ARRAYSIZE(temp), temp);
    StringCbPrintfW(rundll32, sizeof(rundll32), L"%ls\\rundll32.exe", system);
    StringCbPrintfW(inf, sizeof(inf), L"%lslntrundll32.inf", temp);
    RegDeleteKeyW(HKEY_CURRENT_USER, TEST_KEY);

    ok(write_inf(inf, "Native"), "cannot write %ls\n", inf);
    StringCbPrintfW(dll, sizeof(dll), L"%ls\\setupapi.dll", system);
    code = run_rundll32(rundll32, dll, inf);
    ok(code == 0, "native DLL: exit code %lu\n", code);
    ok(value_written(L"Native"), "native DLL entry point did not run\n");

    ok(write_inf(inf, "Wow64"), "cannot write %ls\n", inf);
    StringCbPrintfW(dll, sizeof(dll), L"%ls\\setupapi.dll", wow64);
    code = run_rundll32(rundll32, dll, inf);
    ok(code == 0, "32-bit DLL: exit code %lu\n", code);
    ok(value_written(L"Wow64"), "32-bit DLL entry point had not run when rundll32 exited\n");

    RegDeleteKeyW(HKEY_CURRENT_USER, TEST_KEY);
    DeleteFileW(inf);
}
