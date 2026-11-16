/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     DisplayConfig targets name the monitor the display drives
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"
#include <winreg.h>
#include <initguid.h>
#include <devpropdef.h>
#include <setupapi.h>

DEFINE_DEVPROPKEY(TestMonitorGpuLuid, 0xca085853, 0x16ce, 0x48aa, 0xb1, 0x14, 0xde, 0x9c, 0x72, 0x33, 0x42, 0x23, 1);
DEFINE_DEVPROPKEY(TestMonitorOutputId, 0xca085853, 0x16ce, 0x48aa, 0xb1, 0x14, 0xde, 0x9c, 0x72, 0x33, 0x42, 0x23, 2);
DEFINE_GUID(TestMonitorInterface, 0xe6f07b5f, 0xee97, 0x4a90, 0xb0, 0x76, 0x33, 0xf5, 0x7b, 0xf4, 0xea, 0xa7);

#define MAX_PATHS 16

static LONG (WINAPI *pQueryDisplayConfig)(UINT32, UINT32 *, DISPLAYCONFIG_PATH_INFO *, UINT32 *,
                                          DISPLAYCONFIG_MODE_INFO *, DISPLAYCONFIG_TOPOLOGY_ID *);
static LONG (WINAPI *pDisplayConfigGetDeviceInfo)(DISPLAYCONFIG_DEVICE_INFO_HEADER *);

static BOOL
EdidName(const BYTE *Edid, WCHAR *Name, UINT Capacity)
{
    UINT Descriptor, Length;

    for (Descriptor = 0; Descriptor < 4; Descriptor++)
    {
        const BYTE *Block = &Edid[54 + Descriptor * 18];

        if (Block[0] != 0 || Block[1] != 0 || Block[3] != 0xFC)
            continue;
        for (Length = 0; Length < 13 && Length + 1 < Capacity && Block[5 + Length] != 0 && Block[5 + Length] != 0x0A; Length++)
            Name[Length] = Block[5 + Length];
        while (Length != 0 && Name[Length - 1] == L' ')
            Length--;
        Name[Length] = 0;
        return Length != 0;
    }
    return FALSE;
}

static void
CheckMonitorDevice(const DISPLAYCONFIG_PATH_INFO *Path, const DISPLAYCONFIG_TARGET_DEVICE_NAME *Name)
{
    BYTE Buffer[sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W) + 512 * sizeof(WCHAR)];
    SP_DEVICE_INTERFACE_DETAIL_DATA_W *Detail = (SP_DEVICE_INTERFACE_DETAIL_DATA_W *)Buffer;
    SP_DEVICE_INTERFACE_DATA Iface = {sizeof(Iface)};
    SP_DEVINFO_DATA Device = {sizeof(Device)};
    BOOL Found = FALSE;
    HDEVINFO Set;
    DWORD Index;

    Set = SetupDiGetClassDevsW(&TestMonitorInterface, NULL, NULL, DIGCF_DEVICEINTERFACE | DIGCF_PRESENT);
    ok(Set != INVALID_HANDLE_VALUE, "SetupDiGetClassDevsW failed, error %lu\n", GetLastError());
    if (Set == INVALID_HANDLE_VALUE)
        return;
    for (Index = 0; !Found && SetupDiEnumDeviceInterfaces(Set, NULL, &TestMonitorInterface, Index, &Iface); Index++)
    {
        DEVPROPTYPE Type;
        LUID Luid;
        UINT32 OutputId;
        HKEY Key;

        Detail->cbSize = sizeof(*Detail);
        if (!SetupDiGetDeviceInterfaceDetailW(Set, &Iface, Detail, sizeof(Buffer), NULL, &Device) ||
            _wcsicmp(Detail->DevicePath, Name->monitorDevicePath) != 0)
        {
            continue;
        }
        Found = TRUE;

        ok(SetupDiGetDevicePropertyW(Set, &Device, &TestMonitorGpuLuid, &Type, (BYTE *)&Luid, sizeof(Luid), NULL, 0),
           "monitor GPU LUID missing, error %lu\n", GetLastError());
        ok(Luid.LowPart == Path->targetInfo.adapterId.LowPart && Luid.HighPart == Path->targetInfo.adapterId.HighPart,
           "monitor GPU LUID %08lx:%08lx, path adapter %08lx:%08lx\n", Luid.HighPart, Luid.LowPart,
           Path->targetInfo.adapterId.HighPart, Path->targetInfo.adapterId.LowPart);
        ok(SetupDiGetDevicePropertyW(Set, &Device, &TestMonitorOutputId, &Type, (BYTE *)&OutputId, sizeof(OutputId), NULL, 0),
           "monitor output id missing, error %lu\n", GetLastError());
        ok(OutputId == Path->targetInfo.id, "monitor output id %u, path target %u\n", OutputId, Path->targetInfo.id);

        Key = SetupDiOpenDevRegKey(Set, &Device, DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_READ);
        ok(Key != INVALID_HANDLE_VALUE, "monitor device key missing, error %lu\n", GetLastError());
        if (Key != INVALID_HANDLE_VALUE)
        {
            BYTE Edid[256];
            DWORD Length = sizeof(Edid);
            WCHAR EdidMonitor[14];

            if (RegQueryValueExW(Key, L"EDID", NULL, NULL, Edid, &Length) == ERROR_SUCCESS && Length >= 128)
            {
                ok(Name->flags.edidIdsValid, "flags %#x without edidIdsValid for a monitor with an EDID\n", Name->flags.value);
                ok(Name->edidManufactureId == (UINT16)(Edid[8] | (Edid[9] << 8)),
                   "manufacture id %04x, EDID bytes %02x %02x\n", Name->edidManufactureId, Edid[8], Edid[9]);
                ok(Name->edidProductCodeId == (UINT16)(Edid[10] | (Edid[11] << 8)),
                   "product code %04x, EDID bytes %02x %02x\n", Name->edidProductCodeId, Edid[10], Edid[11]);
                if (EdidName(Edid, EdidMonitor, ARRAYSIZE(EdidMonitor)))
                {
                    ok(Name->flags.friendlyNameFromEdid, "flags %#x without friendlyNameFromEdid\n", Name->flags.value);
                    ok(wcscmp(Name->monitorFriendlyDeviceName, EdidMonitor) == 0, "friendly name %ls, EDID name %ls\n",
                       Name->monitorFriendlyDeviceName, EdidMonitor);
                }
            }
            else
            {
                ok(!Name->flags.edidIdsValid, "flags %#x with edidIdsValid for a monitor without an EDID\n", Name->flags.value);
            }
            RegCloseKey(Key);
        }
    }
    SetupDiDestroyDeviceInfoList(Set);
    ok(Found, "no monitor interface %ls\n", Name->monitorDevicePath);
}

static void
CheckPath(const DISPLAYCONFIG_PATH_INFO *Path, const DISPLAYCONFIG_MODE_INFO *Modes, UINT32 ModeCount)
{
    DISPLAYCONFIG_SOURCE_DEVICE_NAME Source;
    DISPLAYCONFIG_TARGET_DEVICE_NAME Name;
    DISPLAYCONFIG_TARGET_BASE_TYPE Base;
    DISPLAY_DEVICEW Monitor;
    LONG Status;

    ok(Path->targetInfo.modeInfoIdx < ModeCount, "target mode index %u of %u\n", Path->targetInfo.modeInfoIdx, ModeCount);
    if (Path->targetInfo.modeInfoIdx < ModeCount)
        ok(Modes[Path->targetInfo.modeInfoIdx].id == Path->targetInfo.id, "target mode id %u, path target %u\n",
           Modes[Path->targetInfo.modeInfoIdx].id, Path->targetInfo.id);

    memset(&Source, 0, sizeof(Source));
    Source.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
    Source.header.size = sizeof(Source);
    Source.header.adapterId = Path->sourceInfo.adapterId;
    Source.header.id = Path->sourceInfo.id;
    Status = pDisplayConfigGetDeviceInfo(&Source.header);
    ok(Status == ERROR_SUCCESS, "source name: %ld\n", Status);

    memset(&Name, 0xCC, sizeof(Name));
    Name.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
    Name.header.size = sizeof(Name);
    Name.header.adapterId = Path->targetInfo.adapterId;
    Name.header.id = Path->targetInfo.id;
    Status = pDisplayConfigGetDeviceInfo(&Name.header);
    ok(Status == ERROR_SUCCESS, "target name: %ld\n", Status);
    if (Status != ERROR_SUCCESS)
        return;
    ok(Name.outputTechnology == Path->targetInfo.outputTechnology, "target name technology %d, path %d\n",
       Name.outputTechnology, Path->targetInfo.outputTechnology);
    ok((Name.flags.value & ~7u) == 0, "target name flags %#x\n", Name.flags.value);

    memset(&Base, 0xCC, sizeof(Base));
    Base.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_BASE_TYPE;
    Base.header.size = sizeof(Base);
    Base.header.adapterId = Path->targetInfo.adapterId;
    Base.header.id = Path->targetInfo.id;
    Status = pDisplayConfigGetDeviceInfo(&Base.header);
    ok(Status == ERROR_SUCCESS, "target base type: %ld\n", Status);
    if (Status == ERROR_SUCCESS)
        ok(Base.baseOutputTechnology == Path->targetInfo.outputTechnology, "base technology %d, path %d\n",
           Base.baseOutputTechnology, Path->targetInfo.outputTechnology);

    memset(&Monitor, 0, sizeof(Monitor));
    Monitor.cb = sizeof(Monitor);
    if (!EnumDisplayDevicesW(Source.viewGdiDeviceName, 0, &Monitor, EDD_GET_DEVICE_INTERFACE_NAME))
    {
        ok(Name.monitorDevicePath[0] == 0, "no monitor on %ls, target names %ls\n", Source.viewGdiDeviceName,
           Name.monitorDevicePath);
        ok(Name.flags.value == 2, "target without a monitor: flags %#x\n", Name.flags.value);
        ok(Name.monitorFriendlyDeviceName[0] == 0, "target without a monitor: name %ls\n", Name.monitorFriendlyDeviceName);
        return;
    }
    ok((Monitor.StateFlags & ~(DISPLAY_DEVICE_ACTIVE | DISPLAY_DEVICE_ATTACHED)) == 0, "monitor state %#lx\n", Monitor.StateFlags);
    ok(_wcsnicmp(Monitor.DeviceID, L"\\\\?\\DISPLAY#", 12) == 0, "monitor interface %ls\n", Monitor.DeviceID);
    ok(_wcsicmp(Monitor.DeviceID, Name.monitorDevicePath) == 0, "monitor interface %ls, target names %ls\n",
       Monitor.DeviceID, Name.monitorDevicePath);
    CheckMonitorDevice(Path, &Name);
}

START_TEST(DisplayConfigTarget)
{
    DISPLAYCONFIG_PATH_INFO Paths[MAX_PATHS];
    DISPLAYCONFIG_MODE_INFO Modes[MAX_PATHS * 2];
    DISPLAYCONFIG_TARGET_DEVICE_NAME Name;
    DISPLAYCONFIG_TARGET_BASE_TYPE Base;
    DISPLAYCONFIG_TARGET_PREFERRED_MODE Preferred;
    UINT32 PathCount = MAX_PATHS, ModeCount = MAX_PATHS * 2, Index;
    LONG Status;

    pQueryDisplayConfig = (void *)GetProcAddress(GetModuleHandleW(L"user32.dll"), "QueryDisplayConfig");
    pDisplayConfigGetDeviceInfo = (void *)GetProcAddress(GetModuleHandleW(L"user32.dll"), "DisplayConfigGetDeviceInfo");
    if (pQueryDisplayConfig == NULL || pDisplayConfigGetDeviceInfo == NULL)
    {
        skip("QueryDisplayConfig is not available\n");
        return;
    }

    Status = pQueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &PathCount, Paths, &ModeCount, Modes, NULL);
    ok(Status == ERROR_SUCCESS, "QueryDisplayConfig: %ld\n", Status);
    if (Status != ERROR_SUCCESS)
        return;
    ok(PathCount != 0, "no active path\n");
    for (Index = 0; Index < PathCount; Index++)
        CheckPath(&Paths[Index], Modes, ModeCount);
    if (PathCount == 0)
        return;

    memset(&Name, 0, sizeof(Name));
    Name.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
    Name.header.size = sizeof(Name);
    Name.header.adapterId = Paths[0].targetInfo.adapterId;
    Name.header.id = Paths[0].targetInfo.id + 1000;
    Status = pDisplayConfigGetDeviceInfo(&Name.header);
    ok(Status == ERROR_INVALID_PARAMETER, "target name of an unknown target: %ld\n", Status);

    memset(&Base, 0, sizeof(Base));
    Base.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_BASE_TYPE;
    Base.header.size = sizeof(Base);
    Base.header.adapterId = Paths[0].targetInfo.adapterId;
    Base.header.id = Paths[0].targetInfo.id + 1000;
    Status = pDisplayConfigGetDeviceInfo(&Base.header);
    ok(Status == ERROR_INVALID_PARAMETER, "base type of an unknown target: %ld\n", Status);

    memset(&Preferred, 0, sizeof(Preferred));
    Preferred.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_PREFERRED_MODE;
    Preferred.header.size = sizeof(Preferred);
    Preferred.header.adapterId = Paths[0].targetInfo.adapterId;
    Preferred.header.id = Paths[0].targetInfo.id + 1000;
    Status = pDisplayConfigGetDeviceInfo(&Preferred.header);
    ok(Status == ERROR_GEN_FAILURE, "preferred mode of an unknown target: %ld\n", Status);
}
