/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that the kernel debugger's PCI device keeps its identifiers and reports code 53
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <windows.h>
#include <setupapi.h>
#include <cfgmgr32.h>

#define MAX_DEBUG_DEVICES 8

typedef struct _DEBUG_LOCATION
{
    ULONG Bus;
    ULONG Device;
    ULONG Function;
    BOOL Found;
} DEBUG_LOCATION;

static ULONG
ReadDebugLocations(DEBUG_LOCATION *Locations)
{
    WCHAR Name[32];
    ULONG Count = 0;
    DWORD Index, NameLength, Bus, Slot, Size;
    HKEY DebugKey, EntryKey;

    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Services\\PCI\\Debug", 0, KEY_READ, &DebugKey) != ERROR_SUCCESS)
        return 0;
    for (Index = 0; Count < MAX_DEBUG_DEVICES; Index++)
    {
        NameLength = ARRAYSIZE(Name);
        if (RegEnumKeyExW(DebugKey, Index, Name, &NameLength, NULL, NULL, NULL, NULL) != ERROR_SUCCESS)
            break;
        if (RegOpenKeyExW(DebugKey, Name, 0, KEY_READ, &EntryKey) != ERROR_SUCCESS)
            continue;
        Size = sizeof(Bus);
        if (RegQueryValueExW(EntryKey, L"Bus", NULL, NULL, (BYTE *)&Bus, &Size) == ERROR_SUCCESS)
        {
            Size = sizeof(Slot);
            if (RegQueryValueExW(EntryKey, L"Slot", NULL, NULL, (BYTE *)&Slot, &Size) == ERROR_SUCCESS)
            {
                Locations[Count].Bus = Bus;
                Locations[Count].Device = Slot & 0x1F;
                Locations[Count].Function = (Slot >> 5) & 0x7;
                Locations[Count].Found = FALSE;
                Count++;
            }
        }
        RegCloseKey(EntryKey);
    }
    RegCloseKey(DebugKey);
    return Count;
}

static void
CheckDebuggerDevice(DEVINST Node, const WCHAR *Id, ULONG Status, ULONG Problem)
{
    WCHAR Hardware[1024], Compatible[2048];
    const WCHAR *Entry;
    ULONG Vendor, Device, Subsystem, Revision, Size, Type;
    BOOL HaveClass = FALSE;

    ok(Status & DN_HAS_PROBLEM, "%ls: status 0x%08lx has no problem\n", Id, Status);
    ok(Problem == CM_PROB_USED_BY_DEBUGGER, "%ls: problem %lu, expected %u\n", Id, Problem, CM_PROB_USED_BY_DEBUGGER);
    ok(!(Status & DN_STARTED), "%ls: started, status 0x%08lx\n", Id, Status);
    ok(!(Status & DN_DRIVER_LOADED), "%ls: driver loaded, status 0x%08lx\n", Id, Status);

    ZeroMemory(Hardware, sizeof(Hardware));
    Size = sizeof(Hardware) - 2 * sizeof(WCHAR);
    ok(CM_Get_DevNode_Registry_PropertyW(Node, CM_DRP_HARDWAREID, &Type, Hardware, &Size, 0) == CR_SUCCESS,
       "%ls: no hardware IDs\n", Id);
    ok(swscanf(Hardware, L"PCI\\VEN_%4x&DEV_%4x&SUBSYS_%8x&REV_%2x", &Vendor, &Device, &Subsystem, &Revision) == 4,
       "%ls: first hardware ID '%ls'\n", Id, Hardware);
    ok(!(Vendor == 0xDEAD && Device == 0xBEEF), "%ls: substitute hardware ID '%ls'\n", Id, Hardware);
    ok(_wcsnicmp(Id, L"PCI\\VEN_DEAD&DEV_BEEF", 21) != 0, "%ls: substitute instance ID\n", Id);

    ZeroMemory(Compatible, sizeof(Compatible));
    Size = sizeof(Compatible) - 2 * sizeof(WCHAR);
    CM_Get_DevNode_Registry_PropertyW(Node, CM_DRP_COMPATIBLEIDS, &Type, Compatible, &Size, 0);
    for (Entry = Compatible; *Entry; Entry += wcslen(Entry) + 1)
    {
        if (_wcsnicmp(Entry, L"PCI\\CC_", 7) == 0)
            HaveClass = TRUE;
    }
    ok(HaveClass, "%ls: no class code compatible ID\n", Id);
}

static void
CheckAfterRescan(const WCHAR *Id)
{
    DEVINST Node, Parent;
    ULONG Status = 0, Problem = 0;
    CONFIGRET Result;

    if (CM_Locate_DevNodeW(&Node, (DEVINSTID_W)Id, CM_LOCATE_DEVNODE_NORMAL) != CR_SUCCESS ||
        CM_Get_Parent(&Parent, Node, 0) != CR_SUCCESS)
    {
        ok(FALSE, "%ls: not found before the rescan\n", Id);
        return;
    }
    Result = CM_Reenumerate_DevNode(Parent, CM_REENUMERATE_SYNCHRONOUS);
    if (Result == CR_ACCESS_DENIED)
    {
        skip("Rescanning the bus needs administrator rights\n");
        return;
    }
    ok(Result == CR_SUCCESS, "%ls: rescanning the parent bus returned %lu\n", Id, Result);

    ok(CM_Locate_DevNodeW(&Node, (DEVINSTID_W)Id, CM_LOCATE_DEVNODE_NORMAL) == CR_SUCCESS,
       "%ls: gone after the rescan\n", Id);
    ok(CM_Get_DevNode_Status(&Status, &Problem, Node, 0) == CR_SUCCESS, "%ls: no status after the rescan\n", Id);
    ok((Status & DN_HAS_PROBLEM) && Problem == CM_PROB_USED_BY_DEBUGGER,
       "%ls: status 0x%08lx problem %lu after the rescan\n", Id, Status, Problem);
}

START_TEST(PciDebuggerDevice)
{
    WCHAR DebuggerId[MAX_DEVICE_ID_LEN] = L"";
    DEBUG_LOCATION Locations[MAX_DEBUG_DEVICES];
    SP_DEVINFO_DATA Data = { sizeof(Data) };
    WCHAR Id[MAX_DEVICE_ID_LEN];
    ULONG LocationCount, CodeCount = 0, Status, Problem, Bus, Address, Size, Type, i;
    HDEVINFO Set;
    DWORD Index;

    LocationCount = ReadDebugLocations(Locations);

    Set = SetupDiGetClassDevsW(NULL, L"PCI", NULL, DIGCF_ALLCLASSES | DIGCF_PRESENT);
    ok(Set != INVALID_HANDLE_VALUE, "SetupDiGetClassDevsW failed %lu\n", GetLastError());
    if (Set == INVALID_HANDLE_VALUE)
        return;

    for (Index = 0; SetupDiEnumDeviceInfo(Set, Index, &Data); Index++)
    {
        BOOL Debugger = FALSE;

        if (CM_Get_Device_IDW(Data.DevInst, Id, ARRAYSIZE(Id), 0) != CR_SUCCESS)
            continue;
        Status = Problem = 0;
        if (CM_Get_DevNode_Status(&Status, &Problem, Data.DevInst, 0) != CR_SUCCESS)
            continue;

        Size = sizeof(Bus);
        if (CM_Get_DevNode_Registry_PropertyW(Data.DevInst, CM_DRP_BUSNUMBER, &Type, &Bus, &Size, 0) == CR_SUCCESS)
        {
            Size = sizeof(Address);
            if (CM_Get_DevNode_Registry_PropertyW(Data.DevInst, CM_DRP_ADDRESS, &Type, &Address, &Size, 0) == CR_SUCCESS)
            {
                for (i = 0; i < LocationCount; i++)
                {
                    if (Locations[i].Bus == Bus &&
                        Locations[i].Device == (Address >> 16) &&
                        Locations[i].Function == (Address & 0xFFFF))
                    {
                        ok(!Locations[i].Found, "%ls: second device at the debugger's location\n", Id);
                        Locations[i].Found = TRUE;
                        Debugger = TRUE;
                    }
                }
            }
        }

        if ((Status & DN_HAS_PROBLEM) && Problem == CM_PROB_USED_BY_DEBUGGER)
        {
            CodeCount++;
            ok(Debugger, "%ls: code 53 on a device the debugger does not use\n", Id);
        }
        if (Debugger)
        {
            CheckDebuggerDevice(Data.DevInst, Id, Status, Problem);
            wcscpy(DebuggerId, Id);
        }
    }
    SetupDiDestroyDeviceInfoList(Set);

    for (i = 0; i < LocationCount; i++)
    {
        ok(Locations[i].Found, "No device at the debugger's location %lu:%lu.%lu\n",
           Locations[i].Bus, Locations[i].Device, Locations[i].Function);
    }
    ok(CodeCount == LocationCount, "%lu devices report code 53, the debugger uses %lu\n", CodeCount, LocationCount);
    if (DebuggerId[0])
        CheckAfterRescan(DebuggerId);
    if (LocationCount == 0)
        skip("No PCI kernel debugger device\n");
}
