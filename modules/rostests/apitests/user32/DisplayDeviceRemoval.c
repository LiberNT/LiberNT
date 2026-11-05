/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test the runtime removal of a display adapter that is not on the desktop
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include "precomp.h"
#include <winreg.h>
#include <cfgmgr32.h>

#define MAX_TEST_DEVICES 16

static BOOL
FindDisplayDevice(
    _In_opt_ PCWSTR DeviceName,
    _In_opt_ PCWSTR DeviceId,
    _In_ BOOL Detached,
    _Out_ PDISPLAY_DEVICEW Found)
{
    DISPLAY_DEVICEW Device;
    ULONG Index;

    for (Index = 0; Index < MAX_TEST_DEVICES; Index++)
    {
        ZeroMemory(&Device, sizeof(Device));
        Device.cb = sizeof(Device);
        if (!EnumDisplayDevicesW(NULL, Index, &Device, 0))
            break;
        if (Device.StateFlags & DISPLAY_DEVICE_MIRRORING_DRIVER)
            continue;
        if (DeviceName && lstrcmpiW(Device.DeviceName, DeviceName))
            continue;
        if (DeviceId && lstrcmpiW(Device.DeviceID, DeviceId))
            continue;
        if (Detached && (Device.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP))
            continue;
        *Found = Device;
        return TRUE;
    }
    return FALSE;
}

static BOOL
FindPrimary(
    _Out_ PDISPLAY_DEVICEW Primary)
{
    DISPLAY_DEVICEW Device;
    ULONG Index;

    for (Index = 0; Index < MAX_TEST_DEVICES; Index++)
    {
        ZeroMemory(&Device, sizeof(Device));
        Device.cb = sizeof(Device);
        if (!EnumDisplayDevicesW(NULL, Index, &Device, 0))
            break;
        if (Device.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE)
        {
            *Primary = Device;
            return TRUE;
        }
    }
    return FALSE;
}

static BOOL
FindDevNode(
    _In_ PCWSTR HardwareId,
    _Out_ DEVINST *DevInst)
{
    WCHAR Class[64], Ids[512];
    PWSTR List, Id;
    ULONG Size = 0, Length;
    BOOL Found = FALSE;

    if (CM_Get_Device_ID_List_SizeW(&Size, NULL, CM_GETIDLIST_FILTER_PRESENT) != CR_SUCCESS || !Size)
        return FALSE;
    List = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, Size * sizeof(WCHAR));
    if (!List)
        return FALSE;
    if (CM_Get_Device_ID_ListW(NULL, List, Size, CM_GETIDLIST_FILTER_PRESENT) == CR_SUCCESS)
    {
        for (Id = List; *Id && !Found; Id += wcslen(Id) + 1)
        {
            if (CM_Locate_DevNodeW(DevInst, Id, CM_LOCATE_DEVNODE_NORMAL) != CR_SUCCESS)
                continue;
            Length = sizeof(Class);
            if (CM_Get_DevNode_Registry_PropertyW(*DevInst, CM_DRP_CLASS, NULL, Class, &Length, 0) != CR_SUCCESS ||
                lstrcmpiW(Class, L"Display"))
            {
                continue;
            }
            ZeroMemory(Ids, sizeof(Ids));
            Length = sizeof(Ids) - sizeof(WCHAR);
            if (CM_Get_DevNode_Registry_PropertyW(*DevInst, CM_DRP_HARDWAREID, NULL, Ids, &Length, 0) == CR_SUCCESS &&
                !lstrcmpiW(Ids, HardwareId))
            {
                Found = TRUE;
            }
        }
    }
    HeapFree(GetProcessHeap(), 0, List);
    return Found;
}

static BOOL
WaitForDisplayDevice(
    _In_opt_ PCWSTR DeviceName,
    _In_opt_ PCWSTR DeviceId,
    _In_ BOOL Present,
    _Out_ PDISPLAY_DEVICEW Found)
{
    DWORD Start = GetTickCount();

    for (;;)
    {
        if (FindDisplayDevice(DeviceName, DeviceId, FALSE, Found) == Present)
            return TRUE;
        if (GetTickCount() - Start > 10000)
            return FALSE;
        Sleep(100);
    }
}

START_TEST(DisplayDeviceRemoval)
{
    DISPLAY_DEVICEW Detached, Primary, PrimaryAfter, Device;
    PNP_VETO_TYPE VetoType;
    WCHAR VetoName[MAX_PATH];
    DEVMODEW Before, After;
    DEVINST DevInst;
    CONFIGRET cr;

    if (!FindDisplayDevice(NULL, NULL, TRUE, &Detached) || !Detached.DeviceID[0])
    {
        skip("No display device is detached from the desktop\n");
        return;
    }
    ok(FindPrimary(&Primary), "No primary display device\n");
    trace("Removing %ls '%ls' (%ls), primary %ls\n",
          Detached.DeviceName, Detached.DeviceString, Detached.DeviceID, Primary.DeviceName);

    if (!FindDevNode(Detached.DeviceID, &DevInst))
    {
        skip("No device node for %ls\n", Detached.DeviceID);
        return;
    }

    ZeroMemory(&Before, sizeof(Before));
    Before.dmSize = sizeof(Before);
    ok(EnumDisplaySettingsW(NULL, ENUM_CURRENT_SETTINGS, &Before), "EnumDisplaySettingsW failed\n");

    VetoType = PNP_VetoTypeUnknown;
    VetoName[0] = UNICODE_NULL;
    cr = CM_Query_And_Remove_SubTreeW(DevInst, &VetoType, VetoName, ARRAYSIZE(VetoName), CM_REMOVE_NO_RESTART);
    ok(cr == CR_SUCCESS, "CM_Query_And_Remove_SubTreeW: %lu, veto %d '%ls'\n", cr, VetoType, VetoName);
    if (cr != CR_SUCCESS)
        return;

    ok(WaitForDisplayDevice(Detached.DeviceName, NULL, FALSE, &Device), "%ls is still listed\n", Detached.DeviceName);
    ok(!FindDisplayDevice(NULL, Detached.DeviceID, FALSE, &Device), "%ls is still listed as %ls\n",
       Detached.DeviceID, Device.DeviceName);
    ok(FindPrimary(&PrimaryAfter), "No primary display device after the removal\n");
    ok(!lstrcmpiW(PrimaryAfter.DeviceName, Primary.DeviceName), "Primary changed from %ls to %ls\n",
       Primary.DeviceName, PrimaryAfter.DeviceName);
    ok(PrimaryAfter.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP, "Primary %ls is detached\n", PrimaryAfter.DeviceName);
    ok(GetSystemMetrics(SM_CMONITORS) >= 1, "SM_CMONITORS is %d\n", GetSystemMetrics(SM_CMONITORS));

    ZeroMemory(&After, sizeof(After));
    After.dmSize = sizeof(After);
    ok(EnumDisplaySettingsW(NULL, ENUM_CURRENT_SETTINGS, &After), "EnumDisplaySettingsW failed after the removal\n");
    ok(After.dmPelsWidth == Before.dmPelsWidth && After.dmPelsHeight == Before.dmPelsHeight,
       "Desktop changed from %lux%lu to %lux%lu\n", Before.dmPelsWidth, Before.dmPelsHeight,
       After.dmPelsWidth, After.dmPelsHeight);

    cr = CM_Setup_DevNode(DevInst, CM_SETUP_DEVNODE_READY);
    ok(cr == CR_SUCCESS, "CM_Setup_DevNode: %lu\n", cr);
    ok(WaitForDisplayDevice(NULL, Detached.DeviceID, TRUE, &Device), "%ls did not come back\n", Detached.DeviceID);
    ok(!(Device.StateFlags & (DISPLAY_DEVICE_ATTACHED_TO_DESKTOP | DISPLAY_DEVICE_PRIMARY_DEVICE)),
       "%ls came back as %ls with flags 0x%lx\n", Detached.DeviceID, Device.DeviceName, Device.StateFlags);
    ok(FindPrimary(&PrimaryAfter) && !lstrcmpiW(PrimaryAfter.DeviceName, Primary.DeviceName),
       "Primary changed from %ls to %ls\n", Primary.DeviceName, PrimaryAfter.DeviceName);
}
