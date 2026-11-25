/*
 * PROJECT:     LiberNT network tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Installs the NetAdapterCx and WiFiCx test adapters and pings their simulated link peer
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193.001@gmail.com>
 */

#include <winsock2.h>
#include <ws2ipdef.h>
#include <windows.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <newdev.h>
#include <iphlpapi.h>
#include <icmpapi.h>
#include <netioapi.h>
#include <stdarg.h>
#include <stdio.h>

static const UCHAR AdapterAddress[6] = { 0x02, 0x00, 0x4C, 0x4F, 0x4F, 0x50 };
static const UCHAR LightweightAddress[6] = { 0x02, 0x00, 0x4C, 0x4F, 0x4F, 0x52 };
static const UCHAR WifiAddress[6] = { 0x02, 0x00, 0x4C, 0x4F, 0x4F, 0x57 };
static const WCHAR LightweightInterface[] = L"{4C4F4F50-4C57-4D00-8000-4C4F4F504C57}";

static int Failures;
static WCHAR DeviceInstanceId[MAX_DEVICE_ID_LEN];
static WCHAR WifiDeviceInstanceId[MAX_DEVICE_ID_LEN];

static
void
Report(const char *Format, ...)
{
    char Buffer[512];
    va_list Args;

    va_start(Args, Format);
    _vsnprintf(Buffer, sizeof(Buffer) - 2, Format, Args);
    va_end(Args);
    Buffer[sizeof(Buffer) - 2] = 0;
    strcat(Buffer, "\n");
    fputs(Buffer, stdout);
    OutputDebugStringA(Buffer);
}

static
void
Check(BOOL Condition, const char *What)
{
    if (!Condition)
        Failures++;
    Report("NETCXTEST: %s %s", Condition ? "ok" : "FAIL", What);
}

static
BOOL
SetValue(HKEY Root, const WCHAR *Path, const WCHAR *Name, DWORD Type, const void *Data, DWORD Size)
{
    HKEY Key;
    LONG Error;

    Error = RegCreateKeyExW(Root, Path, 0, NULL, 0, KEY_SET_VALUE, NULL, &Key, NULL);
    if (Error == ERROR_SUCCESS)
    {
        if (Name != NULL || Data != NULL)
            Error = RegSetValueExW(Key, Name, 0, Type, Data, Size);
        RegCloseKey(Key);
    }
    if (Error != ERROR_SUCCESS)
        Report("NETCXTEST: registry write '%ls' '%ls' error %ld", Path, Name ? Name : L"", Error);
    return Error == ERROR_SUCCESS;
}

static
BOOL
ProvisionLightweightInterface(void)
{
    WCHAR Interface[160];
    WCHAR Path[256];
    WCHAR Configuration[128];
    BYTE Property[8 * sizeof(DWORD) + sizeof(Configuration)];
    DWORD Header[8] = { 2, 7, 4, 24, 18, 0, 16, 0 };
    DWORD ConfigurationSize;
    DWORD IfType = 6;
    DWORD MediaType = 0;
    DWORD NetLuidIndex = 0x8100;

    _snwprintf(Interface, ARRAYSIZE(Interface), L"SYSTEM\\CurrentControlSet\\Control\\NetworkSetup2\\Interfaces\\%ls", LightweightInterface);
    _snwprintf(Configuration, ARRAYSIZE(Configuration), L"Control\\Network\\Interfaces\\%ls\\Configuration", LightweightInterface);
    ConfigurationSize = (DWORD)((wcslen(Configuration) + 1) * sizeof(WCHAR));
    Header[5] = ConfigurationSize;
    memcpy(Property, Header, sizeof(Header));
    memcpy(Property + sizeof(Header), Configuration, ConfigurationSize);

    _snwprintf(Path, ARRAYSIZE(Path), L"%ls\\Kernel", Interface);
    if (!SetValue(HKEY_LOCAL_MACHINE, Path, L"IfType", REG_DWORD, &IfType, sizeof(IfType)) ||
        !SetValue(HKEY_LOCAL_MACHINE, Path, L"MediaType", REG_DWORD, &MediaType, sizeof(MediaType)) ||
        !SetValue(HKEY_LOCAL_MACHINE, Path, L"NetLuidIndex", REG_DWORD, &NetLuidIndex, sizeof(NetLuidIndex)))
        return FALSE;

    _snwprintf(Path, ARRAYSIZE(Path), L"%ls\\Properties\\{a111f1f4-5923-47c0-9a68-d0bafb577901}\\0024", Interface);
    if (!SetValue(HKEY_LOCAL_MACHINE, Path, L"", 0xffff0082, Property, sizeof(Header) + ConfigurationSize))
        return FALSE;

    _snwprintf(Path, ARRAYSIZE(Path), L"SYSTEM\\CurrentControlSet\\%ls", Configuration);
    return SetValue(HKEY_LOCAL_MACHINE, Path, NULL, REG_NONE, NULL, 0);
}

static
BOOL
InstallDevice(const WCHAR *InfPath, const WCHAR *HardwareId, WCHAR *InstanceId)
{
    WCHAR ClassName[MAX_CLASS_NAME_LEN];
    SP_DEVINFO_DATA Info;
    HDEVINFO Set;
    GUID ClassGuid;
    BOOL Reboot = FALSE;
    BOOL Result;

    if (!SetupDiGetINFClassW(InfPath, &ClassGuid, ClassName, ARRAYSIZE(ClassName), NULL))
    {
        Report("NETCXTEST: SetupDiGetINFClass error %lu", GetLastError());
        return FALSE;
    }

    Set = SetupDiCreateDeviceInfoList(&ClassGuid, NULL);
    if (Set == INVALID_HANDLE_VALUE)
    {
        Report("NETCXTEST: SetupDiCreateDeviceInfoList error %lu", GetLastError());
        return FALSE;
    }

    Info.cbSize = sizeof(Info);
    Result = SetupDiCreateDeviceInfoW(Set, ClassName, &ClassGuid, NULL, NULL, DICD_GENERATE_ID, &Info) &&
             SetupDiSetDeviceRegistryPropertyW(Set, &Info, SPDRP_HARDWAREID, (const BYTE *)HardwareId,
                                               (lstrlenW(HardwareId) + 2) * sizeof(WCHAR)) &&
             SetupDiCallClassInstaller(DIF_REGISTERDEVICE, Set, &Info) &&
             SetupDiGetDeviceInstanceIdW(Set, &Info, InstanceId, MAX_DEVICE_ID_LEN, NULL);
    if (!Result)
    {
        Report("NETCXTEST: device registration error %lu", GetLastError());
        SetupDiDestroyDeviceInfoList(Set);
        return FALSE;
    }

    Result = UpdateDriverForPlugAndPlayDevicesW(NULL, HardwareId, InfPath, INSTALLFLAG_FORCE, &Reboot);
    if (!Result)
        Report("NETCXTEST: UpdateDriverForPlugAndPlayDevices error %lu", GetLastError());
    else
        Report("NETCXTEST: driver installed, reboot %d", Reboot);
    SetupDiDestroyDeviceInfoList(Set);
    return Result;
}

static
BOOL
FindInterface(const UCHAR *Mac, MIB_IF_ROW2 *Row)
{
    MIB_IF_TABLE2 *Table;
    BOOL Found = FALSE;
    ULONG i;

    if (GetIfTable2(&Table) != NO_ERROR)
        return FALSE;
    for (i = 0; i < Table->NumEntries; i++)
    {
        if (Table->Table[i].PhysicalAddressLength == sizeof(AdapterAddress) &&
            !memcmp(Table->Table[i].PhysicalAddress, Mac, sizeof(AdapterAddress)))
        {
            *Row = Table->Table[i];
            Found = TRUE;
            break;
        }
    }
    FreeMibTable(Table);
    return Found;
}

static
void
ReportInterfaceTable(void)
{
    MIB_IF_TABLE2 *Table;
    ULONG i;

    if (GetIfTable2(&Table) != NO_ERROR)
    {
        Report("NETCXTEST: GetIfTable2 failed");
        return;
    }
    for (i = 0; i < Table->NumEntries; i++)
    {
        const MIB_IF_ROW2 *Entry = &Table->Table[i];

        Report("NETCXTEST: if %lu type %lu oper %d len %lu mac %02x:%02x:%02x:%02x:%02x:%02x '%ls'",
               Entry->InterfaceIndex, Entry->Type, Entry->OperStatus, Entry->PhysicalAddressLength,
               Entry->PhysicalAddress[0], Entry->PhysicalAddress[1], Entry->PhysicalAddress[2],
               Entry->PhysicalAddress[3], Entry->PhysicalAddress[4], Entry->PhysicalAddress[5],
               Entry->Description);
    }
    FreeMibTable(Table);
}

static
void
ReportInterface(const char *When, const MIB_IF_ROW2 *Row)
{
    Report("NETCXTEST: %s: index %lu type %lu oper %d media %d mtu %lu speed %I64u '%ls'",
           When, Row->InterfaceIndex, Row->Type, Row->OperStatus, Row->MediaConnectState,
           Row->Mtu, Row->TransmitLinkSpeed, Row->Description);
    Report("NETCXTEST: %s: in %I64u/%I64u pkts %I64u bytes, out %I64u/%I64u pkts %I64u bytes, in discards %I64u errors %I64u, out errors %I64u",
           When, Row->InUcastPkts, Row->InNUcastPkts, Row->InOctets,
           Row->OutUcastPkts, Row->OutNUcastPkts, Row->OutOctets,
           Row->InDiscards, Row->InErrors, Row->OutErrors);
}

static
BOOL
AddAddress(NET_LUID Luid, const char *Ip)
{
    MIB_UNICASTIPADDRESS_ROW Address;
    DWORD Status;
    int i;

    InitializeUnicastIpAddressEntry(&Address);
    Address.InterfaceLuid = Luid;
    Address.Address.Ipv4.sin_family = AF_INET;
    Address.Address.Ipv4.sin_addr.s_addr = inet_addr(Ip);
    Address.OnLinkPrefixLength = 24;
    Status = CreateUnicastIpAddressEntry(&Address);
    Report("NETCXTEST: CreateUnicastIpAddressEntry %lu", Status);
    if (Status != NO_ERROR && Status != ERROR_OBJECT_ALREADY_EXISTS)
        return FALSE;

    for (i = 0; i < 50; i++)
    {
        if (GetUnicastIpAddressEntry(&Address) == NO_ERROR && Address.DadState == IpDadStatePreferred)
            break;
        if (i % 10 == 9)
            Report("NETCXTEST: waiting for the address, %d ms", (i + 1) * 100);
        Sleep(100);
    }
    Report("NETCXTEST: address DAD state %d", Address.DadState);
    return Address.DadState == IpDadStatePreferred;
}

static
int
PingPeer(const char *Ip, int Count)
{
    UCHAR Reply[sizeof(ICMP_ECHO_REPLY) + 64 + 8];
    char Data[32];
    HANDLE Icmp;
    DWORD Status;
    int Replies = 0;
    int i;

    Icmp = IcmpCreateFile();
    if (Icmp == INVALID_HANDLE_VALUE)
    {
        Report("NETCXTEST: IcmpCreateFile error %lu", GetLastError());
        return 0;
    }
    memset(Data, 'L', sizeof(Data));
    for (i = 0; i < Count; i++)
    {
        PICMP_ECHO_REPLY Echo = (PICMP_ECHO_REPLY)Reply;

        Status = IcmpSendEcho(Icmp, inet_addr(Ip), Data, sizeof(Data), NULL, Reply, sizeof(Reply), 1000);
        if (Status && Echo->Status == IP_SUCCESS && Echo->DataSize == sizeof(Data) &&
            !memcmp(Echo->Data, Data, sizeof(Data)))
            Replies++;
        Report("NETCXTEST: echo %d: count %lu status %lu rtt %lu size %u error %lu",
               i, Status, Status ? Echo->Status : 0, Status ? Echo->RoundTripTime : 0,
               Status ? Echo->DataSize : 0, Status ? 0 : GetLastError());
    }
    IcmpCloseHandle(Icmp);
    return Replies;
}

static
BOOL
IsDeviceStarted(const WCHAR *InstanceId)
{
    DEVINST DevInst;
    CONFIGRET Result;
    ULONG Status = 0, Problem = 0;
    int i;

    for (i = 0; i < 50; i++)
    {
        Result = CM_Locate_DevNodeW(&DevInst, (DEVINSTID_W)InstanceId, CM_LOCATE_DEVNODE_NORMAL);
        if (Result == CR_SUCCESS)
            Result = CM_Get_DevNode_Status(&Status, &Problem, DevInst, 0);
        if (Result == CR_SUCCESS && ((Status & DN_STARTED) || (Status & DN_HAS_PROBLEM)))
            break;
        Sleep(100);
    }
    Report("NETCXTEST: '%ls' result %lu status 0x%08lx problem %lu", InstanceId, Result, Status, Problem);
    return Result == CR_SUCCESS && (Status & DN_STARTED) && !(Status & DN_HAS_PROBLEM);
}

static
BOOL
SetDeviceEnabled(BOOL Enable)
{
    DEVINST DevInst;
    CONFIGRET Result;

    Result = CM_Locate_DevNodeW(&DevInst, DeviceInstanceId, CM_LOCATE_DEVNODE_PHANTOM);
    if (Result == CR_SUCCESS)
        Result = Enable ? CM_Enable_DevNode(DevInst, 0) : CM_Disable_DevNode(DevInst, 0);
    Report("NETCXTEST: %s '%ls' result %lu", Enable ? "enable" : "disable", DeviceInstanceId, Result);
    return Result == CR_SUCCESS;
}

static
BOOL
WaitForInterface(const UCHAR *Mac, BOOL Present, MIB_IF_ROW2 *Row)
{
    int i;

    for (i = 0; i < 100; i++)
    {
        if (FindInterface(Mac, Row) == Present &&
            (!Present || Row->OperStatus == IfOperStatusUp))
            return TRUE;
        if (i % 10 == 9)
            Report("NETCXTEST: waiting for the interface to be %s, %d ms", Present ? "up" : "gone", (i + 1) * 100);
        Sleep(100);
    }
    return FALSE;
}

static
void
TestWifi(const WCHAR *WifiInfPath)
{
    MIB_IF_ROW2 Row;

    Check(InstallDevice(WifiInfPath, L"ROOT\\WIFILOOP\0", WifiDeviceInstanceId), "wifi install");
    Check(IsDeviceStarted(WifiDeviceInstanceId), "wifi device started");
    if (WaitForInterface(WifiAddress, TRUE, &Row))
    {
        Check(TRUE, "wifi interface up");
        ReportInterface("wifi", &Row);
        Check(AddAddress(Row.InterfaceLuid, "10.233.0.1"), "wifi address preferred");
        Check(PingPeer("10.233.0.2", 2) >= 1, "wifi peer answers echo");
    }
    else
    {
        Check(FALSE, "wifi interface up");
        ReportInterfaceTable();
    }
}

int
wmain(int argc, WCHAR **argv)
{
    WCHAR InfPath[MAX_PATH];
    WCHAR WifiInfPath[MAX_PATH];
    MIB_IF_ROW2 Row;
    WCHAR *Slash;
    int i;

    if (argc > 1)
    {
        lstrcpynW(InfPath, argv[1], ARRAYSIZE(InfPath));
    }
    else
    {
        GetModuleFileNameW(NULL, InfPath, ARRAYSIZE(InfPath));
        Slash = wcsrchr(InfPath, L'\\');
        if (Slash)
            Slash[1] = 0;
        lstrcatW(InfPath, L"netcxloop.inf");
    }

    lstrcpynW(WifiInfPath, InfPath, ARRAYSIZE(WifiInfPath));
    Slash = wcsrchr(WifiInfPath, L'\\');
    if (Slash)
        Slash[1] = 0;
    lstrcatW(WifiInfPath, L"wifiloop.inf");

    if (argc > 2 && !_wcsicmp(argv[2], L"wifi"))
    {
        TestWifi(WifiInfPath);
        Report("NETCXTEST: DONE %d failures", Failures);
        return Failures ? 1 : 0;
    }

    Check(ProvisionLightweightInterface(), "lightweight interface provisioned");
    Check(InstallDevice(InfPath, L"ROOT\\NETCXLOOP\0", DeviceInstanceId), "install");

    for (i = 0; i < 100 && !FindInterface(AdapterAddress, &Row); i++)
    {
        if (i % 10 == 9)
            Report("NETCXTEST: waiting for the interface, %d ms", (i + 1) * 100);
        Sleep(100);
    }
    Check(i < 100, "interface present");
    if (i == 100)
    {
        ReportInterfaceTable();
        Report("NETCXTEST: DONE %d failures", Failures);
        return 1;
    }

    for (i = 0; i < 50; i++)
    {
        if (GetIfEntry2(&Row) == NO_ERROR && Row.OperStatus == IfOperStatusUp)
            break;
        if (i % 10 == 9)
            Report("NETCXTEST: waiting for oper status up, %d ms", (i + 1) * 100);
        Sleep(100);
    }
    ReportInterface("start", &Row);
    Check(Row.OperStatus == IfOperStatusUp, "oper status up");
    Check(Row.MediaConnectState == MediaConnectStateConnected, "media connected");
    Check(Row.Type == IF_TYPE_ETHERNET_CSMACD, "ethernet type");
    Check(Row.Mtu == 1500, "mtu 1500");
    Check(Row.TransmitLinkSpeed == 1000000000ULL, "link speed 1 Gbps");

    Check(AddAddress(Row.InterfaceLuid, "10.231.0.1"), "address preferred");
    Check(PingPeer("10.231.0.2", 4) >= 3, "peer answers echo");

    if (GetIfEntry2(&Row) == NO_ERROR)
        ReportInterface("end", &Row);
    Check(Row.InOctets > 0 && Row.OutOctets > 0, "traffic counted");

    if (WaitForInterface(LightweightAddress, TRUE, &Row))
    {
        Check(TRUE, "lightweight interface up");
        ReportInterface("lightweight", &Row);
        Check(Row.Type == IF_TYPE_ETHERNET_CSMACD, "lightweight ethernet type");
        Check(AddAddress(Row.InterfaceLuid, "10.232.0.1"), "lightweight address preferred");
        Check(PingPeer("10.232.0.2", 2) >= 1, "lightweight peer answers echo");
    }
    else
    {
        Check(FALSE, "lightweight interface up");
        ReportInterfaceTable();
    }

    Check(SetDeviceEnabled(FALSE), "disable");
    Check(WaitForInterface(AdapterAddress, FALSE, &Row), "interface removed after disable");
    Check(SetDeviceEnabled(TRUE), "enable");
    Check(WaitForInterface(AdapterAddress, TRUE, &Row), "interface up after enable");
    Check(AddAddress(Row.InterfaceLuid, "10.231.0.1"), "address preferred after enable");
    Check(PingPeer("10.231.0.2", 2) >= 1, "peer answers echo after enable");

    Report("NETCXTEST: DONE %d failures", Failures);
    return Failures ? 1 : 0;
}
