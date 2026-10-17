/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for the module path strings of RegInstall
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>

#include <windows.h>
#include <advpub.h>
#include <stdio.h>

static
VOID
ReadProbe(
    _In_ PCSTR Name,
    _Out_writes_(Size) PSTR Value,
    _In_ DWORD Size)
{
    HKEY Key;
    DWORD Type, Length = Size;

    Value[0] = 0;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\LiberNTRegInstallProbe", 0, KEY_QUERY_VALUE, &Key) != ERROR_SUCCESS)
        return;
    if (RegQueryValueExA(Key, Name, NULL, &Type, (PBYTE)Value, &Length) != ERROR_SUCCESS)
        Value[0] = 0;
    Value[Size - 1] = 0;
    RegCloseKey(Key);
}

START_TEST(RegInstall)
{
    CHAR Module[MAX_PATH], SystemRoot[MAX_PATH], ProgramFiles[MAX_PATH], ModPath[MAX_PATH + 4];
    CHAR SysModPath[MAX_PATH + 4], Expected[MAX_PATH + 32];
    HRESULT hr;

    GetModuleFileNameA(NULL, Module, sizeof(Module));
    SystemRoot[0] = ProgramFiles[0] = 0;
    GetEnvironmentVariableA("SystemRoot", SystemRoot, sizeof(SystemRoot));
    GetEnvironmentVariableA("ProgramFiles", ProgramFiles, sizeof(ProgramFiles));
    RegDeleteKeyA(HKEY_CURRENT_USER, "Software\\LiberNTRegInstallProbe");

    hr = RegInstall(GetModuleHandleA(NULL), "Probe", NULL);
    ok(hr == S_OK, "RegInstall returned 0x%lx\n", hr);
    ReadProbe("ModPath", ModPath, sizeof(ModPath));
    ReadProbe("SysModPath", SysModPath, sizeof(SysModPath));
    ok(lstrcmpiA(ModPath, Module) == 0, "_MOD_PATH is [%s], module [%s]\n", ModPath, Module);
    if (SystemRoot[0] && _strnicmp(Module, SystemRoot, strlen(SystemRoot)) == 0)
        sprintf(Expected, "%%SystemRoot%%%s", Module + strlen(SystemRoot));
    else if (ProgramFiles[0] && _strnicmp(Module, ProgramFiles, strlen(ProgramFiles)) == 0)
        sprintf(Expected, "%%ProgramFiles%%%s", Module + strlen(ProgramFiles));
    else
        strcpy(Expected, Module);
    ok(lstrcmpiA(SysModPath, Expected) == 0, "_SYS_MOD_PATH is [%s], expected [%s]\n", SysModPath, Expected);
    RegDeleteKeyA(HKEY_CURRENT_USER, "Software\\LiberNTRegInstallProbe");
}
