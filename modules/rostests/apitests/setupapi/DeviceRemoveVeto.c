/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for removing a USB stick through the PnP manager, with and without open files
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <windows.h>
#include <cfgmgr32.h>

static HANDLE
OpenStickFile(WCHAR *Name)
{
    WCHAR Letter;

    for (Letter = L'D'; Letter <= L'Z'; Letter++)
    {
        HANDLE File;

        wsprintfW(Name, L"%c:\\HELLO.TXT", Letter);
        File = CreateFileW(Name, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
        if (File != INVALID_HANDLE_VALUE)
            return File;
    }
    return INVALID_HANDLE_VALUE;
}

static HANDLE
WaitForStickFile(WCHAR *Name, DWORD TimeoutMs)
{
    DWORD Start = GetTickCount();
    HANDLE File;

    do
    {
        File = OpenStickFile(Name);
        if (File != INVALID_HANDLE_VALUE)
            return File;
        Sleep(200);
    } while (GetTickCount() - Start < TimeoutMs);
    return INVALID_HANDLE_VALUE;
}

START_TEST(DeviceRemoveVeto)
{
    DEVINST Device = 0, Root = 0;
    PNP_VETO_TYPE Veto;
    WCHAR Name[32], VetoName[MAX_PATH];
    HANDLE File;
    CONFIGRET Result;

    if (CM_Locate_DevNodeW(&Device, (DEVINSTID_W)L"USB\\VID_46F4&PID_0001\\STICK01", CM_LOCATE_DEVNODE_NORMAL) != CR_SUCCESS)
    {
        skip("No USB stick with the serial number STICK01\n");
        return;
    }

    File = WaitForStickFile(Name, 5000);
    if (File == INVALID_HANDLE_VALUE)
    {
        skip("The stick has no HELLO.TXT\n");
        return;
    }

    Veto = PNP_VetoTypeUnknown;
    VetoName[0] = 0;
    Result = CM_Query_And_Remove_SubTreeW(Device, &Veto, VetoName, ARRAYSIZE(VetoName), 0);
    trace("MEASURE open file: cr %lu veto %d name '%ls'\n", Result, Veto, VetoName);
    ok(Result == CR_REMOVE_VETOED, "Removing with a file open: cr %lu\n", Result);
    CloseHandle(File);
    if (Result == CR_SUCCESS)
        return;

    File = WaitForStickFile(Name, 3000);
    ok(File != INVALID_HANDLE_VALUE, "The file went away after the veto\n");
    if (File != INVALID_HANDLE_VALUE)
        CloseHandle(File);

    Veto = PNP_VetoTypeUnknown;
    VetoName[0] = 0;
    Result = CM_Query_And_Remove_SubTreeW(Device, &Veto, VetoName, ARRAYSIZE(VetoName), 0);
    trace("MEASURE no open file: cr %lu veto %d name '%ls'\n", Result, Veto, VetoName);
    ok(Result == CR_SUCCESS, "Removing with no file open: cr %lu veto %d name '%ls'\n", Result, Veto, VetoName);

    if (Result == CR_SUCCESS)
    {
        File = OpenStickFile(Name);
        ok(File == INVALID_HANDLE_VALUE, "The removed stick still serves '%ls'\n", Name);
        if (File != INVALID_HANDLE_VALUE)
            CloseHandle(File);

        ok(CM_Locate_DevNodeW(&Root, NULL, CM_LOCATE_DEVNODE_NORMAL) == CR_SUCCESS, "CM_Locate_DevNodeW failed\n");
        ok(CM_Reenumerate_DevNode(Root, CM_REENUMERATE_SYNCHRONOUS) == CR_SUCCESS, "CM_Reenumerate_DevNode failed\n");

        File = WaitForStickFile(Name, 5000);
        ok(File != INVALID_HANDLE_VALUE, "The stick did not come back\n");
        if (File != INVALID_HANDLE_VALUE)
            CloseHandle(File);
    }
}
