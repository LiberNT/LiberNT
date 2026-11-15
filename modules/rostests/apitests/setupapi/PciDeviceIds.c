/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test the instance, hardware and compatible identifiers of PCI devices
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <windows.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <stdio.h>

#define ID_CHARS 2048

static BOOL
GetDevFn(DEVINST Node, ULONG *DevFn)
{
    ULONG Address, Size = sizeof(Address), Type;

    if (CM_Get_DevNode_Registry_PropertyW(Node, CM_DRP_ADDRESS, &Type, &Address, &Size, 0) != CR_SUCCESS)
        return FALSE;
    *DevFn = ((Address >> 16) << 3) | (Address & 0xFFFF);
    return TRUE;
}

static BOOL
CheckList(const WCHAR *Id, const WCHAR *Kind, const WCHAR *Actual, const WCHAR *Expected)
{
    for (; *Expected; Expected += wcslen(Expected) + 1, Actual += wcslen(Actual) + 1)
    {
        ok(_wcsicmp(Actual, Expected) == 0, "%ls: %ls '%ls', expected '%ls'\n", Id, Kind, Actual, Expected);
        if (!*Actual)
            return FALSE;
    }
    ok(*Actual == UNICODE_NULL, "%ls: extra %ls '%ls'\n", Id, Kind, Actual);
    return TRUE;
}

static ULONG
Add(WCHAR *List, ULONG Used, const WCHAR *Format, ...)
{
    va_list Arguments;
    int Written;

    va_start(Arguments, Format);
    Written = _vsnwprintf(List + Used, ID_CHARS - Used - 2, Format, Arguments);
    va_end(Arguments);
    if (Written < 0)
        return Used;
    List[Used + Written] = UNICODE_NULL;
    List[Used + Written + 1] = UNICODE_NULL;
    return Used + Written + 1;
}

static void
CheckDevice(DEVINST Node, const WCHAR *Id)
{
    static WCHAR Hardware[ID_CHARS], Compatible[ID_CHARS], ExpectedHw[ID_CHARS], ExpectedCompat[ID_CHARS];
    WCHAR Expected[128], Parent[MAX_DEVICE_ID_LEN];
    const WCHAR *Last, *Suffix, *Entry;
    ULONG Vendor, Device, Subsystem, Revision, Class = 0, Size, Type, Used, DevFn, Chars = 0;
    DEVINST Current, Up;
    BOOL HaveClass = FALSE;

    ZeroMemory(Hardware, sizeof(Hardware));
    ZeroMemory(Compatible, sizeof(Compatible));
    Size = sizeof(Hardware) - 4;
    if (CM_Get_DevNode_Registry_PropertyW(Node, CM_DRP_HARDWAREID, &Type, Hardware, &Size, 0) != CR_SUCCESS)
        return;
    Size = sizeof(Compatible) - 4;
    CM_Get_DevNode_Registry_PropertyW(Node, CM_DRP_COMPATIBLEIDS, &Type, Compatible, &Size, 0);
    if (swscanf(Hardware, L"PCI\\VEN_%4x&DEV_%4x&SUBSYS_%8x&REV_%2x", &Vendor, &Device, &Subsystem, &Revision) != 4)
    {
        ok(FALSE, "%ls: first hardware ID '%ls' is not VEN&DEV&SUBSYS&REV\n", Id, Hardware);
        return;
    }
    for (Entry = Hardware; *Entry; Entry += wcslen(Entry) + 1)
    {
        ULONG Scanned;

        if (wcslen(Entry) == 31 && swscanf(Entry, L"PCI\\VEN_%*4x&DEV_%*4x&CC_%6x", &Scanned) == 1)
        {
            Class = Scanned;
            HaveClass = TRUE;
        }
    }
    ok(HaveClass, "%ls: no VEN&DEV&CC hardware ID\n", Id);
    if (!HaveClass)
        return;

    Used = Add(ExpectedHw, 0, L"PCI\\VEN_%04X&DEV_%04X&SUBSYS_%08X&REV_%02X", Vendor, Device, Subsystem, Revision);
    Used = Add(ExpectedHw, Used, L"PCI\\VEN_%04X&DEV_%04X&SUBSYS_%08X", Vendor, Device, Subsystem);
    if (Subsystem == 0)
    {
        Used = Add(ExpectedHw, Used, L"PCI\\VEN_%04X&DEV_%04X&REV_%02X", Vendor, Device, Revision);
        Used = Add(ExpectedHw, Used, L"PCI\\VEN_%04X&DEV_%04X", Vendor, Device);
    }
    Used = Add(ExpectedHw, Used, L"PCI\\VEN_%04X&DEV_%04X&CC_%06X", Vendor, Device, Class);
    Add(ExpectedHw, Used, L"PCI\\VEN_%04X&DEV_%04X&CC_%04X", Vendor, Device, Class >> 8);

    Used = 0;
    ExpectedCompat[0] = ExpectedCompat[1] = UNICODE_NULL;
    if (Subsystem != 0)
    {
        Used = Add(ExpectedCompat, Used, L"PCI\\VEN_%04X&DEV_%04X&REV_%02X", Vendor, Device, Revision);
        Used = Add(ExpectedCompat, Used, L"PCI\\VEN_%04X&DEV_%04X", Vendor, Device);
    }
    Used = Add(ExpectedCompat, Used, L"PCI\\VEN_%04X&CC_%06X", Vendor, Class);
    Used = Add(ExpectedCompat, Used, L"PCI\\VEN_%04X&CC_%04X", Vendor, Class >> 8);
    Used = Add(ExpectedCompat, Used, L"PCI\\VEN_%04X", Vendor);
    Used = Add(ExpectedCompat, Used, L"PCI\\CC_%06X", Class);
    Add(ExpectedCompat, Used, L"PCI\\CC_%04X", Class >> 8);

    CheckList(Id, L"hardware ID", Hardware, ExpectedHw);
    CheckList(Id, L"compatible ID", Compatible, ExpectedCompat);

    /* The instance suffix is the DevFn of the device, then of each PCI bridge above it */
    Current = Node;
    Entry = Id;
    while (!_wcsnicmp(Entry, L"PCI\\", 4) && Chars + 3 < ARRAYSIZE(Expected))
    {
        if (!GetDevFn(Current, &DevFn))
        {
            ok(FALSE, "%ls: no address on %ls\n", Id, Entry);
            return;
        }
        Chars += _snwprintf(Expected + Chars, ARRAYSIZE(Expected) - Chars - 1, L"%02X", DevFn);
        if (CM_Get_Parent(&Up, Current, 0) != CR_SUCCESS ||
            CM_Get_Device_IDW(Up, Parent, ARRAYSIZE(Parent), 0) != CR_SUCCESS)
        {
            break;
        }
        Current = Up;
        Entry = Parent;
    }

    Last = wcsrchr(Id, L'\\');
    Suffix = Last ? wcsrchr(Last, L'&') : NULL;
    ok(Suffix != NULL && _wcsicmp(Suffix + 1, Expected) == 0, "%ls: instance suffix '%ls', expected '%ls'\n",
       Id, Suffix ? Suffix + 1 : L"", Expected);
}

START_TEST(PciDeviceIds)
{
    WCHAR Id[MAX_DEVICE_ID_LEN];
    SP_DEVINFO_DATA Data;
    HDEVINFO Set;
    ULONG Checked = 0;
    DWORD Index;

    Set = SetupDiGetClassDevsW(NULL, L"PCI", NULL, DIGCF_ALLCLASSES | DIGCF_PRESENT);
    ok(Set != INVALID_HANDLE_VALUE, "SetupDiGetClassDevsW failed with %lu\n", GetLastError());
    if (Set == INVALID_HANDLE_VALUE)
        return;

    Data.cbSize = sizeof(Data);
    for (Index = 0; SetupDiEnumDeviceInfo(Set, Index, &Data); Index++)
    {
        if (!SetupDiGetDeviceInstanceIdW(Set, &Data, Id, ARRAYSIZE(Id), NULL))
            continue;
        CheckDevice(Data.DevInst, Id);
        Checked++;
    }

    ok(Checked > 0, "no PCI device found\n");
    trace("%lu PCI devices checked\n", Checked);
    SetupDiDestroyDeviceInfoList(Set);
}
