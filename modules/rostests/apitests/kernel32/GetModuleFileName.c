/*
 * PROJECT:         ReactOS api tests
 * LICENSE:         GPLv2+ - See COPYING in the top level directory
 * PURPOSE:         Test for GetModuleFileName
 * PROGRAMMER:      Thomas Faber <thomas.faber@reactos.org>
 */

#include "precomp.h"

#include <shlwapi.h>

static
VOID
StartChild(char **argv)
{
    BOOL Success;
    WCHAR Path[MAX_PATH];
    PWSTR FileName;
    PWSTR Slash;
    WCHAR CommandLine[MAX_PATH];
    STARTUPINFOW StartupInfo;
    PROCESS_INFORMATION ProcessInfo;
    DWORD Ret;
    int Length;

    Length = MultiByteToWideChar(CP_ACP,
                                 0,
                                 argv[0],
                                 -1,
                                 Path,
                                 sizeof(Path) / sizeof(WCHAR));
    ok(Length > 0, "Length = %d\n", Length);

    FileName = wcsrchr(Path, '\\');
    Slash = wcsrchr(Path, L'/');
    if (Slash && (!FileName || Slash > FileName))
        FileName = Slash;

    if (FileName)
    {
        /* It's an absolute path. Set it as current dir and get the file name */
        FileName++;
        FileName[-1] = L'\0';

        Success = SetCurrentDirectoryW(Path);
        ok(Success == TRUE, "SetCurrentDirectory failed for path '%ls'\n", Path);

        trace("Starting '%ls' in path '%ls'\n", FileName, Path);
    }
    else
    {
        FileName = Path;
        trace("Starting '%ls', which is already relative\n", FileName);
    }

    _swprintf(CommandLine, L"\"%ls\" GetModuleFileName relative", FileName);

    RtlZeroMemory(&StartupInfo, sizeof(StartupInfo));
    StartupInfo.cb = sizeof(StartupInfo);

    Success = CreateProcessW(FileName,
                             CommandLine,
                             NULL,
                             NULL,
                             FALSE,
                             0,
                             NULL,
                             NULL,
                             &StartupInfo,
                             &ProcessInfo);
    if (!Success)
    {
        skip("CreateProcess failed with %lu\n", GetLastError());
        return;
    }
    CloseHandle(ProcessInfo.hThread);
    Ret = WaitForSingleObject(ProcessInfo.hProcess, 30 * 1000);
    ok(Ret == WAIT_OBJECT_0, "WaitForSingleObject returns %lu\n", Ret);
    CloseHandle(ProcessInfo.hProcess);
}

static
VOID
TestGetModuleFileNameA(VOID)
{
    CHAR Buffer[MAX_PATH];
    DWORD Length;
    BOOL Relative;

    Length = GetModuleFileNameA(NULL, Buffer, sizeof(Buffer));
    ok(Length != 0, "Length = %lu\n", Length);
    ok(Length < sizeof(Buffer), "Length = %lu\n", Length);
    ok(Buffer[Length] == 0, "Buffer not null terminated\n");
    Relative = PathIsRelativeA(Buffer);
    ok(Relative == FALSE, "GetModuleFileNameA returned relative path: %s\n", Buffer);
}

static
VOID
TestGetModuleFileNameW(VOID)
{
    WCHAR Buffer[MAX_PATH];
    DWORD Length;
    BOOL Relative;

    Length = GetModuleFileNameW(NULL, Buffer, sizeof(Buffer) / sizeof(WCHAR));
    ok(Length != 0, "Length = %lu\n", Length);
    ok(Length < sizeof(Buffer) / sizeof(WCHAR), "Length = %lu\n", Length);
    ok(Buffer[Length] == 0, "Buffer not null terminated\n");
    Relative = PathIsRelativeW(Buffer);
    ok(Relative == FALSE, "GetModuleFileNameW returned relative path: %ls\n", Buffer);
}

static
VOID
TestWow64DllName(VOID)
{
    WCHAR Source[MAX_PATH], Expected[MAX_PATH], Reported[MAX_PATH];
    HMODULE Module;
    BOOL Wow64;

    if (!IsWow64Process(GetCurrentProcess(), &Wow64) || !Wow64)
        return;

    if (!GetSystemWow64DirectoryW(Source, RTL_NUMBER_OF(Source)) ||
        !GetSystemDirectoryW(Expected, RTL_NUMBER_OF(Expected)) ||
        FAILED(StringCchCatW(Source, RTL_NUMBER_OF(Source), L"\\cabinet.dll")) ||
        FAILED(StringCchCatW(Expected, RTL_NUMBER_OF(Expected), L"\\cabinet.dll")))
    {
        ok(FALSE, "Could not construct system DLL paths\n");
        return;
    }

    Module = LoadLibraryW(Source);
    ok(Module != NULL, "LoadLibraryW(%ls) failed with %lu\n", Source, GetLastError());
    if (!Module)
        return;

    if (GetModuleFileNameW(Module, Reported, RTL_NUMBER_OF(Reported)))
        ok(!_wcsicmp(Reported, Expected), "Reported %ls, expected %ls\n", Reported, Expected);
    else
        ok(FALSE, "GetModuleFileNameW failed with %lu\n", GetLastError());
    ok(GetModuleHandleW(Source) == Module, "Physical name did not identify the loaded module\n");
    ok(GetModuleHandleW(Expected) == Module, "Logical name did not identify the loaded module\n");
    FreeLibrary(Module);
}

START_TEST(GetModuleFileName)
{
    int argc;
    char **argv;

    argc = winetest_get_mainargs(&argv);
    if (argc < 3)
    {
        TestWow64DllName();
        StartChild(argv);
    }
    else
    {
        TestGetModuleFileNameA();
        TestGetModuleFileNameW();
    }
}
