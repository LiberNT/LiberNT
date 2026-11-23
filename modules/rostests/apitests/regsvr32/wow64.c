/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     regsvr32 hands 32-bit DLLs to the WoW64 regsvr32
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arifing193@gmail.com>
 */

#include <apitest.h>
#include <windows.h>
#include <strsafe.h>

#define EXITCODE_LOADERROR 3
#define EXITCODE_NOENTRY 4
#define NOT_EXITED 0xffffffff

/* The exit code, or NOT_EXITED when regsvr32 had to be stopped after the time limit. */
static DWORD run_regsvr32(PCWSTR regsvr32, PCWSTR options, PCWSTR dll)
{
    WCHAR command[3 * MAX_PATH];
    PROCESS_INFORMATION info;
    STARTUPINFOW startup = { sizeof(startup) };
    DWORD code = NOT_EXITED;

    StringCbPrintfW(command, sizeof(command), L"\"%ls\" %ls \"%ls\"", regsvr32, options, dll);
    if (!CreateProcessW(regsvr32, command, NULL, NULL, FALSE, 0, NULL, NULL, &startup, &info))
    {
        ok(0, "CreateProcessW(%ls) failed: %lu\n", command, GetLastError());
        return NOT_EXITED;
    }
    if (WaitForSingleObject(info.hProcess, 10000) == WAIT_OBJECT_0)
        GetExitCodeProcess(info.hProcess, &code);
    else
        TerminateProcess(info.hProcess, NOT_EXITED);
    CloseHandle(info.hThread);
    CloseHandle(info.hProcess);
    return code;
}

static BOOL write_file(PCWSTR path, const void *data, DWORD size)
{
    DWORD written;
    HANDLE file;
    BOOL res;

    file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE)
        return FALSE;
    res = WriteFile(file, data, size, &written, NULL) && written == size;
    CloseHandle(file);
    return res;
}

START_TEST(wow64)
{
    static const char not_an_image[] = "MZ not a portable executable";
    WCHAR system[MAX_PATH], wow64[MAX_PATH], regsvr32[MAX_PATH], wow64_regsvr32[MAX_PATH], dll[MAX_PATH];
    WCHAR temp[MAX_PATH], bogus[MAX_PATH];
    DWORD code;

    if (sizeof(void *) < 8 || !GetSystemWow64DirectoryW(wow64, ARRAYSIZE(wow64)))
    {
        skip("no WoW64 directory for a 64-bit process\n");
        return;
    }
    StringCbPrintfW(wow64_regsvr32, sizeof(wow64_regsvr32), L"%ls\\regsvr32.exe", wow64);
    StringCbPrintfW(dll, sizeof(dll), L"%ls\\kernel32.dll", wow64);
    if (GetFileAttributesW(wow64_regsvr32) == INVALID_FILE_ATTRIBUTES || GetFileAttributesW(dll) == INVALID_FILE_ATTRIBUTES)
    {
        skip("no WoW64 regsvr32 or kernel32\n");
        return;
    }
    GetSystemDirectoryW(system, ARRAYSIZE(system));
    StringCbPrintfW(regsvr32, sizeof(regsvr32), L"%ls\\regsvr32.exe", system);

    code = run_regsvr32(regsvr32, L"/s", dll);
    ok(code == EXITCODE_NOENTRY, "32-bit DLL without DllRegisterServer: exit code %lu\n", code);
    code = run_regsvr32(regsvr32, L"/s /u", dll);
    ok(code == EXITCODE_NOENTRY, "32-bit DLL without DllUnregisterServer: exit code %lu\n", code);
    code = run_regsvr32(regsvr32, L"/s /n /i:x", dll);
    ok(code == EXITCODE_NOENTRY, "32-bit DLL without DllInstall: exit code %lu\n", code);

    StringCbPrintfW(dll, sizeof(dll), L"%ls\\kernel32.dll", system);
    code = run_regsvr32(regsvr32, L"/s", dll);
    ok(code == EXITCODE_NOENTRY, "native DLL without DllRegisterServer: exit code %lu\n", code);

    GetTempPathW(ARRAYSIZE(temp), temp);
    StringCbPrintfW(bogus, sizeof(bogus), L"%lslntregsvr32.dll", temp);
    ok(write_file(bogus, not_an_image, sizeof(not_an_image)), "cannot write %ls\n", bogus);
    code = run_regsvr32(regsvr32, L"/s", bogus);
    ok(code == EXITCODE_LOADERROR, "file that is not an image: exit code %lu\n", code);
    DeleteFileW(bogus);
}
