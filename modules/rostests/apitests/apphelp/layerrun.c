/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that a process started with a compatibility layer runs with its shims
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <windows.h>
#include <stdio.h>
#include "wine/test.h"

static DWORD run_layered_child(const char *layer)
{
    char path[MAX_PATH], cmdline[MAX_PATH + 2];
    STARTUPINFOA si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    DWORD code = 0xdeadbeef;
    char *name;
    BOOL ret;

    GetModuleFileNameA(NULL, path, MAX_PATH);
    name = strrchr(path, '\\');
    name = name ? name + 1 : path;
    strcpy(name, "apphelp_layerchild.exe");
    sprintf(cmdline, "\"%s\"", path);

    SetEnvironmentVariableA("__COMPAT_LAYER", layer);
    ret = CreateProcessA(NULL, cmdline, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi);
    SetEnvironmentVariableA("__COMPAT_LAYER", NULL);
    ok(ret, "CreateProcess failed with %lu for %s\n", GetLastError(), layer);
    if (!ret)
        return code;

    ok(WaitForSingleObject(pi.hProcess, 30000) == WAIT_OBJECT_0, "Child with %s did not exit\n", layer);
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return code;
}

START_TEST(layerrun)
{
    ok_hex(run_layered_child("WINXPSP3"), 0x501);
    ok_hex(run_layered_child("WIN7RTM"), 0x601);
}
