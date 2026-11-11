/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for the package identity queries of a process without package identity
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

#include "precomp.h"
#include <appmodel.h>

typedef LONG (WINAPI *PFN_CURRENT_STRING)(UINT32 *, PWSTR);
typedef LONG (WINAPI *PFN_CURRENT_ID)(UINT32 *, BYTE *);
typedef LONG (WINAPI *PFN_CURRENT_INFO)(const UINT32, UINT32 *, BYTE *, UINT32 *);
typedef LONG (WINAPI *PFN_PROCESS_STRING)(HANDLE, UINT32 *, PWSTR);
typedef HMODULE (WINAPI *PFN_LOADPACKAGEDLIBRARY)(LPCWSTR, DWORD);

static
VOID
CheckCurrentString(
    _In_ HMODULE Module,
    _In_ PCSTR ModuleName,
    _In_ PCSTR Name,
    _In_ LONG Expected)
{
    PFN_CURRENT_STRING Function;
    WCHAR Buffer[32];
    UINT32 Length;
    LONG Result;

    Function = (PFN_CURRENT_STRING)GetProcAddress(Module, Name);
    ok(Function != NULL, "%s does not export %s\n", ModuleName, Name);
    if (!Function)
        return;

    Length = 0;
    SetLastError(0x33333333);
    Result = Function(&Length, NULL);
    ok(Result == Expected, "%s!%s(0, NULL) returned %ld\n", ModuleName, Name, Result);
    ok(Length == 0, "%s!%s length %u\n", ModuleName, Name, Length);
    ok(GetLastError() == 0x33333333, "%s!%s last error %lu\n", ModuleName, Name, GetLastError());

    wcscpy(Buffer, L"unchanged");
    Length = _countof(Buffer);
    SetLastError(0x33333333);
    Result = Function(&Length, Buffer);
    ok(Result == Expected, "%s!%s(buffer) returned %ld\n", ModuleName, Name, Result);
    ok(Length == _countof(Buffer), "%s!%s length %u\n", ModuleName, Name, Length);
    ok(!wcscmp(Buffer, L"unchanged"), "%s!%s wrote %ls\n", ModuleName, Name, Buffer);
    ok(GetLastError() == 0x33333333, "%s!%s last error %lu\n", ModuleName, Name, GetLastError());

    SetLastError(0x33333333);
    Result = Function(NULL, NULL);
    ok(Result == ERROR_INVALID_PARAMETER, "%s!%s(NULL, NULL) returned %ld\n", ModuleName, Name, Result);
    ok(GetLastError() == 0x33333333, "%s!%s last error %lu\n", ModuleName, Name, GetLastError());
}

static
VOID
CheckCurrentPackageId(
    _In_ HMODULE Module,
    _In_ PCSTR ModuleName)
{
    PFN_CURRENT_ID Function;
    BYTE Buffer[64];
    UINT32 Length;
    LONG Result;

    Function = (PFN_CURRENT_ID)GetProcAddress(Module, "GetCurrentPackageId");
    ok(Function != NULL, "%s does not export GetCurrentPackageId\n", ModuleName);
    if (!Function)
        return;

    Length = 0;
    SetLastError(0x33333333);
    Result = Function(&Length, NULL);
    ok(Result == APPMODEL_ERROR_NO_PACKAGE, "%s: GetCurrentPackageId(0, NULL) returned %ld\n", ModuleName, Result);
    ok(Length == 0, "%s: length %u\n", ModuleName, Length);
    ok(GetLastError() == 0x33333333, "%s: last error %lu\n", ModuleName, GetLastError());

    memset(Buffer, 0x5a, sizeof(Buffer));
    Length = sizeof(Buffer);
    SetLastError(0x33333333);
    Result = Function(&Length, Buffer);
    ok(Result == APPMODEL_ERROR_NO_PACKAGE, "%s: GetCurrentPackageId(buffer) returned %ld\n", ModuleName, Result);
    ok(Length == sizeof(Buffer), "%s: length %u\n", ModuleName, Length);
    ok(Buffer[0] == 0x5a && Buffer[sizeof(Buffer) - 1] == 0x5a, "%s: buffer was written\n", ModuleName);
    ok(GetLastError() == 0x33333333, "%s: last error %lu\n", ModuleName, GetLastError());

    SetLastError(0x33333333);
    Result = Function(NULL, NULL);
    ok(Result == ERROR_INVALID_PARAMETER, "%s: GetCurrentPackageId(NULL, NULL) returned %ld\n", ModuleName, Result);
    ok(GetLastError() == 0x33333333, "%s: last error %lu\n", ModuleName, GetLastError());
}

static
VOID
CheckCurrentPackageInfo(
    _In_ HMODULE Module,
    _In_ PCSTR ModuleName)
{
    PFN_CURRENT_INFO Function;
    UINT32 Length, Count;
    LONG Result;

    Function = (PFN_CURRENT_INFO)GetProcAddress(Module, "GetCurrentPackageInfo");
    ok(Function != NULL, "%s does not export GetCurrentPackageInfo\n", ModuleName);
    if (!Function)
        return;

    Length = 0;
    Count = 0xdeadbeef;
    SetLastError(0x33333333);
    Result = Function(PACKAGE_FILTER_HEAD, &Length, NULL, &Count);
    ok(Result == APPMODEL_ERROR_NO_PACKAGE, "%s: GetCurrentPackageInfo returned %ld\n", ModuleName, Result);
    ok(Length == 0, "%s: length %u\n", ModuleName, Length);
    ok(Count == 0xdeadbeef, "%s: count %u\n", ModuleName, Count);
    ok(GetLastError() == 0x33333333, "%s: last error %lu\n", ModuleName, GetLastError());

    SetLastError(0x33333333);
    Result = Function(PACKAGE_FILTER_HEAD, NULL, NULL, NULL);
    ok(Result == ERROR_INVALID_PARAMETER, "%s: GetCurrentPackageInfo(NULL) returned %ld\n", ModuleName, Result);
    ok(GetLastError() == 0x33333333, "%s: last error %lu\n", ModuleName, GetLastError());
}

static
VOID
CheckProcessString(
    _In_ HMODULE Module,
    _In_ PCSTR ModuleName,
    _In_ PCSTR Name)
{
    PFN_PROCESS_STRING Function;
    WCHAR Buffer[32];
    UINT32 Length;
    HANDLE Process;
    LONG Result;

    Function = (PFN_PROCESS_STRING)GetProcAddress(Module, Name);
    ok(Function != NULL, "%s does not export %s\n", ModuleName, Name);
    if (!Function)
        return;

    Length = 0;
    SetLastError(0x33333333);
    Result = Function(GetCurrentProcess(), &Length, NULL);
    ok(Result == APPMODEL_ERROR_NO_PACKAGE, "%s!%s(self) returned %ld\n", ModuleName, Name, Result);
    ok(Length == 0, "%s!%s length %u\n", ModuleName, Name, Length);
    ok(GetLastError() == 0x33333333, "%s!%s last error %lu\n", ModuleName, Name, GetLastError());

    Length = _countof(Buffer);
    SetLastError(0x33333333);
    Result = Function(NULL, &Length, Buffer);
    ok(Result == ERROR_INVALID_PARAMETER, "%s!%s(NULL process) returned %ld\n", ModuleName, Name, Result);
    Result = Function(GetCurrentProcess(), NULL, NULL);
    ok(Result == ERROR_INVALID_PARAMETER, "%s!%s(NULL length) returned %ld\n", ModuleName, Name, Result);
    ok(GetLastError() == 0x33333333, "%s!%s last error %lu\n", ModuleName, Name, GetLastError());

    Process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, GetCurrentProcessId());
    ok(Process != NULL, "OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION) failed with %lu\n", GetLastError());
    if (Process)
    {
        wcscpy(Buffer, L"unchanged");
        Length = _countof(Buffer);
        Result = Function(Process, &Length, Buffer);
        ok(Result == APPMODEL_ERROR_NO_PACKAGE, "%s!%s(limited handle) returned %ld\n", ModuleName, Name, Result);
        ok(Length == _countof(Buffer), "%s!%s length %u\n", ModuleName, Name, Length);
        ok(!wcscmp(Buffer, L"unchanged"), "%s!%s wrote %ls\n", ModuleName, Name, Buffer);
        CloseHandle(Process);
    }

    Process = OpenProcess(SYNCHRONIZE, FALSE, GetCurrentProcessId());
    ok(Process != NULL, "OpenProcess(SYNCHRONIZE) failed with %lu\n", GetLastError());
    if (Process)
    {
        Length = _countof(Buffer);
        Result = Function(Process, &Length, Buffer);
        ok(Result == ERROR_ACCESS_DENIED, "%s!%s(SYNCHRONIZE handle) returned %ld\n", ModuleName, Name, Result);
        CloseHandle(Process);
    }
}

static
VOID
CheckLoadPackagedLibrary(
    _In_ HMODULE Module,
    _In_ PCSTR ModuleName)
{
    PFN_LOADPACKAGEDLIBRARY Function;
    HMODULE Loaded;

    Function = (PFN_LOADPACKAGEDLIBRARY)GetProcAddress(Module, "LoadPackagedLibrary");
    ok(Function != NULL, "%s does not export LoadPackagedLibrary\n", ModuleName);
    if (!Function)
        return;

    SetLastError(0xdeadbeef);
    Loaded = Function(L"kernel32.dll", 0);
    ok(Loaded == NULL, "%s: LoadPackagedLibrary returned %p\n", ModuleName, Loaded);
    ok(GetLastError() == APPMODEL_ERROR_NO_PACKAGE, "%s: last error %lu\n", ModuleName, GetLastError());
}

static
VOID
CheckModule(
    _In_ HMODULE Module,
    _In_ PCSTR ModuleName)
{
    CheckCurrentString(Module, ModuleName, "GetCurrentApplicationUserModelId", APPMODEL_ERROR_NO_APPLICATION);
    CheckCurrentString(Module, ModuleName, "GetCurrentPackageFamilyName", APPMODEL_ERROR_NO_PACKAGE);
    CheckCurrentString(Module, ModuleName, "GetCurrentPackagePath", APPMODEL_ERROR_NO_PACKAGE);
    CheckCurrentPackageId(Module, ModuleName);
    CheckCurrentPackageInfo(Module, ModuleName);
    CheckProcessString(Module, ModuleName, "GetPackageFamilyName");
    CheckProcessString(Module, ModuleName, "GetPackageFullName");
    CheckLoadPackagedLibrary(Module, ModuleName);
}

START_TEST(PackageIdentity)
{
    HMODULE KernelBase;

    CheckModule(GetModuleHandleW(L"kernel32.dll"), "kernel32");

    KernelBase = GetModuleHandleW(L"kernelbase.dll");
    ok(KernelBase != NULL, "kernelbase.dll is not loaded\n");
    if (KernelBase)
        CheckModule(KernelBase, "kernelbase");
}
