/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     A device disabled persistently stays down when its parent enumerates it again and after a restart
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 */

#include <apitest.h>
#include <winreg.h>
#include <cfgmgr32.h>
#include <regstr.h>

#define NET_CLASS_GUID L"{4d36e972-e325-11ce-bfc1-08002be10318}"
#define MARKER_NAME L"C:\\DeviceDisablePersist.txt"

static DEVINST DevInst;
static WCHAR InstanceId[MAX_DEVICE_ID_LEN];

static BOOL IsStartedNetAdapter(DEVINST Candidate)
{
    WCHAR ClassGuid[64];
    ULONG Size = sizeof(ClassGuid);
    ULONG Status, Problem;

    if (CM_Get_DevNode_Status(&Status, &Problem, Candidate, 0) != CR_SUCCESS || !(Status & DN_STARTED))
        return FALSE;
    if (CM_Get_DevNode_Registry_PropertyW(Candidate, CM_DRP_CLASSGUID, NULL, ClassGuid, &Size, 0) != CR_SUCCESS)
        return FALSE;
    return _wcsicmp(ClassGuid, NET_CLASS_GUID) == 0;
}

static BOOL FindNetAdapter(void)
{
    static WCHAR List[65536];
    DEVINST Candidate;
    PCWSTR Id;

    if (CM_Get_Device_ID_ListW(L"PCI", List, _countof(List),
                               CM_GETIDLIST_FILTER_ENUMERATOR | CM_GETIDLIST_FILTER_PRESENT) != CR_SUCCESS)
    {
        return FALSE;
    }
    for (Id = List; *Id; Id += wcslen(Id) + 1)
    {
        if (CM_Locate_DevNodeW(&Candidate, (DEVINSTID_W)Id, CM_LOCATE_DEVNODE_NORMAL) == CR_SUCCESS &&
            IsStartedNetAdapter(Candidate))
        {
            DevInst = Candidate;
            wcsncpy(InstanceId, Id, _countof(InstanceId) - 1);
            return TRUE;
        }
    }
    return FALSE;
}

static void EnableAgain(void)
{
    ULONG Status = 0, Problem = 0, Try;

    ok_int(CM_Enable_DevNode(DevInst, 0), CR_SUCCESS);
    for (Try = 0; Try < 50; Try++)
    {
        if (CM_Get_DevNode_Status(&Status, &Problem, DevInst, 0) == CR_SUCCESS && (Status & DN_STARTED))
            break;
        Sleep(20);
    }
    ok(Status & DN_STARTED, "the enabled device did not start again (status 0x%lx problem %lu)\n", Status, Problem);
}

/* Run with DEVICE_DISABLE_ACROSS_RESTART to leave the adapter disabled, then again after a restart to check it */
static BOOL CheckAfterRestart(void)
{
    ULONG Status = 0, Problem = 0, Flags = 0, Size = sizeof(Flags);
    DWORD Read = 0;
    HANDLE Marker;

    Marker = CreateFileW(MARKER_NAME, GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (Marker == INVALID_HANDLE_VALUE)
        return FALSE;
    ZeroMemory(InstanceId, sizeof(InstanceId));
    ReadFile(Marker, InstanceId, sizeof(InstanceId) - sizeof(WCHAR), &Read, NULL);
    CloseHandle(Marker);
    DeleteFileW(MARKER_NAME);
    trace("Device disabled before the restart: %S\n", InstanceId);
    ok_int(CM_Locate_DevNodeW(&DevInst, InstanceId, CM_LOCATE_DEVNODE_NORMAL), CR_SUCCESS);
    ok_int(CM_Get_DevNode_Registry_PropertyW(DevInst, CM_DRP_CONFIGFLAGS, NULL, &Flags, &Size, 0), CR_SUCCESS);
    ok(Flags & CONFIGFLAG_DISABLED, "the persistent disable was not kept (ConfigFlags 0x%lx)\n", Flags);
    ok_int(CM_Get_DevNode_Status(&Status, &Problem, DevInst, 0), CR_SUCCESS);
    ok(!(Status & DN_STARTED), "the restart started the disabled device (status 0x%lx)\n", Status);
    ok((Status & DN_HAS_PROBLEM) && Problem == CM_PROB_DISABLED,
       "after the restart the device reports status 0x%lx problem %lu\n", Status, Problem);
    EnableAgain();
    return TRUE;
}

START_TEST(DeviceDisablePersist)
{
    ULONG Status, Problem;
    DEVINST Parent;
    CONFIGRET Result;
    HANDLE Marker;
    DWORD Written;

    if (CheckAfterRestart())
        return;
    if (!FindNetAdapter())
    {
        skip("No started PCI network adapter\n");
        return;
    }
    trace("Test device %S\n", InstanceId);
    ok_int(CM_Get_Parent(&Parent, DevInst, 0), CR_SUCCESS);

    Result = CM_Disable_DevNode(DevInst, CM_DISABLE_PERSIST | CM_DISABLE_UI_NOT_OK);
    if (Result == CR_ACCESS_DENIED)
    {
        skip("Disabling a device needs administrator rights\n");
        return;
    }
    ok_int(Result, CR_SUCCESS);
    if (Result != CR_SUCCESS)
        return;

    ok_int(CM_Get_DevNode_Status(&Status, &Problem, DevInst, 0), CR_SUCCESS);
    ok(!(Status & DN_STARTED), "the disabled device is started (status 0x%lx)\n", Status);
    ok(Problem == CM_PROB_DISABLED, "the disabled device reports problem %lu\n", Problem);

    ok_int(CM_Reenumerate_DevNode(Parent, CM_REENUMERATE_SYNCHRONOUS), CR_SUCCESS);
    ok_int(CM_Get_DevNode_Status(&Status, &Problem, DevInst, 0), CR_SUCCESS);
    ok(!(Status & DN_STARTED), "the parent's enumeration started the disabled device (status 0x%lx)\n", Status);
    ok((Status & DN_HAS_PROBLEM) && Problem == CM_PROB_DISABLED,
       "after the parent's enumeration the device reports status 0x%lx problem %lu\n", Status, Problem);

    if (GetEnvironmentVariableW(L"DEVICE_DISABLE_ACROSS_RESTART", NULL, 0) == 0)
    {
        EnableAgain();
        return;
    }
    Marker = CreateFileW(MARKER_NAME, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_FLAG_WRITE_THROUGH, NULL);
    ok(Marker != INVALID_HANDLE_VALUE, "cannot record the disabled device (%lu)\n", GetLastError());
    if (Marker != INVALID_HANDLE_VALUE)
    {
        WriteFile(Marker, InstanceId, (DWORD)(wcslen(InstanceId) * sizeof(WCHAR)), &Written, NULL);
        CloseHandle(Marker);
    }
}
