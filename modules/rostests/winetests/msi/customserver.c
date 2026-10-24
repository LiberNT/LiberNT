/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Custom actions after their custom action server exits
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <stdio.h>

#include <windows.h>
#include <tlhelp32.h>
#include <msiquery.h>
#include <msi.h>

#include "wine/test.h"
#include "utils.h"

static const char *msifile = "msitest.msi";

static const char cs_property_dat[] =
    "Property\tValue\n"
    "s72\tl0\n"
    "Property\tProperty\n"
    "ProductCode\t{8C6E1C55-7D0E-4D7A-9C55-2B7F1E4A6D31}\n"
    "ProductLanguage\t1033\n"
    "ProductName\tMSITEST\n"
    "ProductVersion\t1.1.1\n"
    "TEST_RETVAL\t0\n";

static const char cs_custom_action_dat[] =
    "Action\tType\tSource\tTarget\n"
    "s72\ti2\tS64\tS0\n"
    "CustomAction\tAction\n"
    "blocker\t65\tcustom.dll\tasync1\n"
    "testretval\t1\tcustom.dll\ttest_retval\n";

static const char cs_install_exec_seq_dat[] =
    "Action\tCondition\tSequence\n"
    "s72\tS255\tI2\n"
    "InstallExecuteSequence\tAction\n"
    "blocker\tBLOCK\t100\n"
    "testretval\t\t200\n";

static const msi_table cs_tables[] =
{
    ADD_TABLE(cs_property),
    ADD_TABLE(cs_custom_action),
    ADD_TABLE(cs_install_exec_seq),
};

static DWORD terminate_child_servers(void)
{
    PROCESSENTRY32 entry;
    HANDLE snapshot, process;
    DWORD count = 0;

    snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return 0;

    entry.dwSize = sizeof(entry);
    if (Process32First(snapshot, &entry))
    {
        do
        {
            if (entry.th32ParentProcessID != GetCurrentProcessId() || _stricmp(entry.szExeFile, "msiexec.exe"))
                continue;
            process = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, entry.th32ProcessID);
            if (!process)
                continue;
            if (TerminateProcess(process, 0xdead) && WaitForSingleObject(process, 5000) == WAIT_OBJECT_0)
                count++;
            CloseHandle(process);
        } while (Process32Next(snapshot, &entry));
    }

    CloseHandle(snapshot);
    return count;
}

static DWORD WINAPI server_killer(void *arg)
{
    DWORD *killed = arg;
    HANDLE event = NULL;
    DWORD i;

    for (i = 0; i < 200 && !event; i++)
    {
        event = OpenEventA(EVENT_MODIFY_STATE, FALSE, "wine_msi_async_test");
        if (!event)
            Sleep(50);
    }
    if (!event)
        return 0;

    *killed = terminate_child_servers();
    if (!*killed)
        SetEvent(event);
    CloseHandle(event);
    return 0;
}

static void test_server_exit(void)
{
    DWORD killed = 0;
    HANDLE thread;
    UINT r;

    create_database(msifile, cs_tables, ARRAY_SIZE(cs_tables));
    MsiSetInternalUI(INSTALLUILEVEL_NONE, NULL);

    thread = CreateThread(NULL, 0, server_killer, &killed, 0, NULL);
    ok(thread != NULL, "CreateThread failed, error %lu\n", GetLastError());
    r = MsiInstallProductA(msifile, "BLOCK=1");
    ok(r == ERROR_SUCCESS, "custom action after the server exited: got %u\n", r);
    WaitForSingleObject(thread, INFINITE);
    CloseHandle(thread);
    if (!killed)
        skip("custom action servers are not child processes\n");

    r = MsiInstallProductA(msifile, NULL);
    ok(r == ERROR_SUCCESS, "next package: got %u\n", r);

    DeleteFileA(msifile);
}

START_TEST(customserver)
{
    char temp_path[MAX_PATH], prev_path[MAX_PATH];
    DWORD len;

    if (!is_process_elevated()) restart_as_admin_elevated();

    GetCurrentDirectoryA(MAX_PATH, prev_path);
    GetTempPathA(MAX_PATH, temp_path);
    SetCurrentDirectoryA(temp_path);

    lstrcpyA(CURR_DIR, temp_path);
    len = lstrlenA(CURR_DIR);
    if (len && CURR_DIR[len - 1] == '\\')
        CURR_DIR[len - 1] = 0;

    test_server_exit();

    SetCurrentDirectoryA(prev_path);
}
