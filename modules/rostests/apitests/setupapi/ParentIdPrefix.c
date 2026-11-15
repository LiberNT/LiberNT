/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that a parent ID prefix names its parent's depth and is kept in the parent's instance key
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <windows.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <wchar.h>

static ULONG
DevNodeDepth(DEVINST Node)
{
    DEVINST Parent;
    ULONG Depth = 0;

    while (Depth < 64 && CM_Get_Parent(&Parent, Node, 0) == CR_SUCCESS)
    {
        Node = Parent;
        Depth++;
    }
    return Depth;
}

static BOOL
ParseIdPrefix(PCWSTR Component, ULONG *Depth, WCHAR *Prefix, SIZE_T PrefixChars)
{
    PCWSTR Cursor = Component;
    ULONG Field;

    for (Field = 0; Field < 3; Field++)
    {
        if (Field != 0)
        {
            if (*Cursor != L'&')
                return FALSE;
            Cursor++;
        }
        if (!iswxdigit(*Cursor))
            return FALSE;
        while (iswxdigit(*Cursor))
            Cursor++;
    }
    if (*Cursor != L'&' && *Cursor != UNICODE_NULL)
        return FALSE;
    if ((SIZE_T)(Cursor - Component) >= PrefixChars)
        return FALSE;

    wcsncpy(Prefix, Component, Cursor - Component);
    Prefix[Cursor - Component] = UNICODE_NULL;
    *Depth = wcstoul(Component, NULL, 16);
    return TRUE;
}

static VOID
CheckPrefixedDevice(DEVINST DevInst, ULONG *Checked)
{
    WCHAR Id[MAX_DEVICE_ID_LEN], ParentId[MAX_DEVICE_ID_LEN], Prefix[64], Stored[64], Path[MAX_DEVICE_ID_LEN + 64];
    DEVINST Parent;
    PCWSTR Component;
    ULONG Depth, ParentDepth;
    DWORD Size, Type;
    HKEY Key;
    LONG Error;

    if (CM_Get_Device_IDW(DevInst, Id, ARRAYSIZE(Id), 0) != CR_SUCCESS)
        return;
    Component = wcsrchr(Id, L'\\');
    if (Component == NULL || !ParseIdPrefix(Component + 1, &Depth, Prefix, ARRAYSIZE(Prefix)))
        return;
    if (CM_Get_Parent(&Parent, DevInst, 0) != CR_SUCCESS ||
        CM_Get_Device_IDW(Parent, ParentId, ARRAYSIZE(ParentId), 0) != CR_SUCCESS)
    {
        ok(FALSE, "%ls: no parent\n", Id);
        return;
    }

    (*Checked)++;
    ParentDepth = DevNodeDepth(Parent);
    ok(Depth == ParentDepth, "%ls: prefix depth %lu, parent %ls is at depth %lu\n", Id, Depth, ParentId, ParentDepth);

    _snwprintf(Path, ARRAYSIZE(Path) - 1, L"System\\CurrentControlSet\\Enum\\%s", ParentId);
    Path[ARRAYSIZE(Path) - 1] = UNICODE_NULL;
    Error = RegOpenKeyExW(HKEY_LOCAL_MACHINE, Path, 0, KEY_QUERY_VALUE, &Key);
    ok(Error == ERROR_SUCCESS, "%ls: cannot open %ls (error %ld)\n", Id, ParentId, Error);
    if (Error != ERROR_SUCCESS)
        return;

    Size = sizeof(Stored) - sizeof(WCHAR);
    ZeroMemory(Stored, sizeof(Stored));
    Error = RegQueryValueExW(Key, L"ParentIdPrefix", NULL, &Type, (BYTE *)Stored, &Size);
    RegCloseKey(Key);
    ok(Error == ERROR_SUCCESS, "%ls: parent %ls has no ParentIdPrefix (error %ld)\n", Id, ParentId, Error);
    if (Error != ERROR_SUCCESS)
        return;
    ok(Type == REG_SZ, "%ls: ParentIdPrefix of %ls has type %lu\n", Id, ParentId, Type);
    ok(!_wcsicmp(Stored, Prefix), "%ls: parent %ls keeps ParentIdPrefix '%ls', the instance uses '%ls'\n",
       Id, ParentId, Stored, Prefix);
}

START_TEST(ParentIdPrefix)
{
    SP_DEVINFO_DATA Data;
    HDEVINFO Set;
    ULONG Index, Checked = 0;

    Set = SetupDiGetClassDevsW(NULL, NULL, NULL, DIGCF_ALLCLASSES | DIGCF_PRESENT);
    ok(Set != INVALID_HANDLE_VALUE, "SetupDiGetClassDevsW failed with %lu\n", GetLastError());
    if (Set == INVALID_HANDLE_VALUE)
        return;

    Data.cbSize = sizeof(Data);
    for (Index = 0; SetupDiEnumDeviceInfo(Set, Index, &Data); Index++)
        CheckPrefixedDevice(Data.DevInst, &Checked);

    SetupDiDestroyDeviceInfoList(Set);
    ok(Checked != 0, "no device instance uses a parent ID prefix\n");
    trace("%lu device instances use a parent ID prefix\n", Checked);
}
