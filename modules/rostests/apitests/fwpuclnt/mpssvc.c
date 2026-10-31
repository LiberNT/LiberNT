/*
 * PROJECT:     LiberNT API Tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests Windows Firewall policy rule parsing and WFP enforcement
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#include <apitest.h>
#include <winsock2.h>
#include <rpc.h>
#include <fwpmu.h>
#include <fwpmk.h>

#include "mpssvc_policy.h"

static const GUID TestSubLayer =
    { 0xf57ffe69, 0xe542, 0x42e8, { 0xa6, 0xfe, 0x91, 0xe2, 0xd3, 0x48, 0x20, 0x68 } };

static DWORD
ParseRule(
    _In_ PCWSTR Text,
    _Out_ MPS_FIREWALL_RULE *Rule)
{
    return MpsParseFirewallRule(Text, wcslen(Text), Rule);
}

static HANDLE
OpenEngine(
    _In_ UINT32 Flags)
{
    FWPM_SESSION0 Session;
    HANDLE Engine = NULL;
    DWORD Error;

    ZeroMemory(&Session, sizeof(Session));
    Session.flags = Flags;
    Error = FwpmEngineOpen0(NULL, RPC_C_AUTHN_WINNT, NULL, &Session, &Engine);
    ok(Error == ERROR_SUCCESS, "FwpmEngineOpen0(0x%x) returned %lu\n", Flags, Error);
    return Error == ERROR_SUCCESS ? Engine : NULL;
}

static VOID
SetPortRule(
    _In_ HKEY Key,
    _In_ PCWSTR Name,
    _In_ PCWSTR Format,
    _In_ USHORT Port)
{
    WCHAR Text[256];
    DWORD Error;

    _snwprintf(Text, ARRAYSIZE(Text), Format, Port);
    Text[ARRAYSIZE(Text) - 1] = UNICODE_NULL;
    Error = RegSetValueExW(Key, Name, 0, REG_SZ, (const BYTE *)Text,
                           (DWORD)(wcslen(Text) + 1) * sizeof(WCHAR));
    ok(Error == ERROR_SUCCESS, "RegSetValueExW(%ls) returned %lu\n", Name, Error);
}

static DWORD
ApplyRule(
    _In_ HANDLE Engine,
    _In_ PCWSTR Text)
{
    MPS_FIREWALL_RULE Rule;
    DWORD Error;

    Error = ParseRule(Text, &Rule);
    ok(Error == ERROR_SUCCESS, "Failed to parse %ls: %lu\n", Text, Error);
    if (Error != ERROR_SUCCESS)
        return Error;
    return MpsApplyFirewallRule(Engine, &TestSubLayer, &Rule);
}

static BOOL
ConnectLoopback(
    _In_ SOCKET Listener,
    _In_ USHORT Port,
    _Out_ int *WsaError)
{
    struct sockaddr_in Remote;
    struct timeval Timeout = { 3, 0 };
    SOCKET Client, Server;
    fd_set Set;
    int Result;

    Client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    ZeroMemory(&Remote, sizeof(Remote));
    Remote.sin_family = AF_INET;
    Remote.sin_port = htons(Port);
    Remote.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    Result = connect(Client, (struct sockaddr *)&Remote, sizeof(Remote));
    *WsaError = Result == 0 ? 0 : WSAGetLastError();
    closesocket(Client);

    if (Result != 0)
        return FALSE;

    FD_ZERO(&Set);
    FD_SET(Listener, &Set);
    if (select(0, &Set, NULL, NULL, &Timeout) == 1)
    {
        Server = accept(Listener, NULL, NULL);
        if (Server != INVALID_SOCKET)
            closesocket(Server);
    }
    return TRUE;
}

START_TEST(mpssvc)
{
    static const WCHAR Supported[] =
        L"v2.10|Action=Block|Active=TRUE|Dir=In|Protocol=6|"
        L"Profile=Domain|Profile=Private|Profile=Public|"
        L"LPort=80|LPort2_10=443-445|RPort=50000|Name=web|";
    static const WCHAR Inactive[] = L"v2.10|Action=Allow|Dir=Out|";
    static const WCHAR PartialProfile[] =
        L"v2.10|Action=Allow|Active=TRUE|Dir=In|Protocol=17|Profile=Public|";
    static const WCHAR DocumentedExample[] =
        L"v2.10|Action=Allow|Active=TRUE|Dir=In|Protocol=6|"
        L"Profile=Public|LPort=RPC|RPort=49000|"
        L"LA4=192.168.1.0/255.255.255.0|RA4=LocalSubnet|"
        L"RA6=LocalSubnet|App=c:\\\\path\\\\foo.exe|"
        L"Name=Firewall Rule Test|Security=Authenticate|Security2_9=An-NoEncap|";
    static const WCHAR DuplicateAction[] =
        L"v2.10|Action=Allow|Action=Block|Dir=In|";
    static const WCHAR PortWithoutProtocol[] = L"v2.10|Action=Block|Dir=In|LPort=80|";
    static const WCHAR BadPortRange[] =
        L"v2.10|Action=Block|Dir=In|Protocol=6|LPort=65535-65536|";
    static const WCHAR MissingDelimiter[] = L"v2.10|Action=Block|Dir=In";
    static const WCHAR AllowRule[] =
        L"v2.10|Action=Allow|Active=TRUE|Dir=Out|Protocol=6|"
        L"Profile=Domain|Profile=Private|Profile=Public|RPort=9|";
    static const WCHAR ByPassRule[] =
        L"v2.10|Action=ByPass|Dir=In|";
    static const WCHAR ExplicitFalse[] =
        L"v2.10|Action=Block|Active=FALSE|Dir=In|";
    static const WCHAR UdpRule[] =
        L"v2.10|Action=Block|Active=TRUE|Dir=In|Protocol=17|"
        L"Profile=Domain|Profile=Private|Profile=Public|LPort=5353|";
    static const WCHAR SvcWildcard[] =
        L"v2.10|Action=Allow|Dir=In|Svc=*|";
    static const WCHAR SvcSpecific[] =
        L"v2.10|Action=Allow|Dir=In|Svc=dnscache|";
    static const WCHAR PortKeywordRpc[] =
        L"v2.10|Action=Allow|Dir=In|Protocol=6|LPort=RPC|";
    static const WCHAR BadVersion[] = L"v1.10|Action=Allow|Dir=In|";
    static const WCHAR NoVersion[] = L"Action=Allow|Dir=In|";
    static const WCHAR MissingAction[] = L"v2.10|Dir=In|";
    static const WCHAR MissingDir[] = L"v2.10|Action=Allow|";
    static const WCHAR DuplicateDir[] =
        L"v2.10|Action=Allow|Dir=In|Dir=Out|";
    static const WCHAR InvertedRange[] =
        L"v2.10|Action=Block|Dir=In|Protocol=6|LPort=445-443|";
    static const WCHAR BadAction[] = L"v2.10|Action=Deny|Dir=In|";
    static const WCHAR Protocol256Rule[] =
        L"v2.10|Action=Block|Active=TRUE|Dir=In|Protocol=256|";
    static const WCHAR EmptyDescRule[] =
        L"v2.10|Action=Block|Active=TRUE|Dir=In|Desc=|EmbedCtxt=|";
    static const WCHAR ExactAddressRule[] =
        L"v2.10|Action=Block|Active=TRUE|Dir=Out|Protocol=6|RA4=127.0.0.1|";
    static const WCHAR CidrAddressRule[] =
        L"v2.10|Action=Block|Active=TRUE|Dir=Out|Protocol=6|RA4=10.0.0.0/8|LA4=192.168.1.100/24|";
    static const WCHAR RepeatedAddressRule[] =
        L"v2.10|Action=Block|Active=TRUE|Dir=Out|Protocol=6|"
        L"RA4=10.0.0.1|RA4=172.16.0.0/12|LA4=192.168.1.1|LA4=192.168.2.0/24|";
    static const WCHAR AddressRangeRule[] =
        L"v2.10|Action=Block|Active=TRUE|Dir=Out|RA4=10.0.0.1-10.0.0.9|";
    static const WCHAR DottedMaskRule[] =
        L"v2.10|Action=Allow|Active=TRUE|Dir=In|Protocol=17|LA4=192.168.1.0/255.255.255.0|";
    static const WCHAR BadAddressRule[] =
        L"v2.10|Action=Block|Dir=In|LA4=192.168.1.256|";
    static const WCHAR AddressKeywordRule[] =
        L"v2.10|Action=Allow|Active=TRUE|Dir=In|RA4=LocalSubnet|";
    static const WCHAR MultiPortRule[] =
        L"v2.10|Action=Block|Active=TRUE|Dir=In|Protocol=6|"
        L"Profile=Domain|Profile=Private|Profile=Public|"
        L"LPort=80,443,8080-8090|RPort=1000,2000-2005|";
    static const WCHAR BadTrailingCommaPort[] =
        L"v2.10|Action=Block|Dir=In|Protocol=6|LPort=80,|";
    static const WCHAR BadEmptyCommaPort[] =
        L"v2.10|Action=Block|Dir=In|Protocol=6|LPort=80,,443|";

    MPS_FIREWALL_RULE Rule;
    HANDLE Engine, Owner, First, Second;
    HKEY TestKey;
    DWORD Disposition;
    WSADATA WsaData;
    SOCKET Listener;
    struct sockaddr_in Local;
    int LocalLength = sizeof(Local);
    USHORT Port;
    WCHAR Own[MAX_PATH];
    WCHAR Text[MPS_MAX_RULE_CHARS];
    DWORD Error, KeyError;
    int WsaError;

    ok(ParseRule(Supported, &Rule) == ERROR_SUCCESS, "Failed to parse supported rule\n");
    ok(Rule.Active, "Active rule was not marked active\n");
    ok(Rule.Action == MpsRuleActionBlock, "Unexpected action %u\n", (UINT32)Rule.Action);
    ok(Rule.Inbound, "Inbound rule was not recognized\n");
    ok(Rule.Protocol == 6, "Unexpected protocol %u\n", Rule.Protocol);
    ok(Rule.ProfileMask == 7, "Unexpected profile mask %u\n", Rule.ProfileMask);
    ok(!Rule.Unsupported, "Supported rule was marked unsupported\n");
    ok(Rule.LocalPortCount == 2, "Unexpected local port count %u\n", Rule.LocalPortCount);
    ok(Rule.LocalPorts[0].First == 80 && Rule.LocalPorts[0].Last == 80,
       "Single port was not parsed\n");
    ok(Rule.LocalPorts[1].First == 443 && Rule.LocalPorts[1].Last == 445,
       "Port range was not parsed\n");
    ok(Rule.RemotePortCount == 1 && Rule.RemotePorts[0].First == 50000,
       "Remote port was not parsed\n");

    ok(ParseRule(Inactive, &Rule) == ERROR_SUCCESS, "Failed to parse inactive rule\n");
    ok(!Rule.Active, "Active omitted from a rule must default to false\n");
    ok(Rule.Protocol == 256, "Protocol omitted from a rule must default to 256\n");
    ok(Rule.ProfileMask == 7, "Profile omitted from a rule must default to all\n");
    ok(!Rule.Inbound, "Outbound rule was not recognized\n");

    ok(ParseRule(PartialProfile, &Rule) == ERROR_SUCCESS, "Failed to parse profile rule\n");
    ok(Rule.Unsupported, "Profile-specific rule was not marked unsupported\n");

    ok(ParseRule(DocumentedExample, &Rule) == ERROR_SUCCESS,
       "Failed to parse the MS-GPFAS example rule\n");
    ok(Rule.Unsupported, "Unsupported conditions in the MS-GPFAS example were not detected\n");
    ok(Rule.HasApplication, "Application path was not parsed\n");

    ok(ParseRule(AllowRule, &Rule) == ERROR_SUCCESS, "Failed to parse allow rule\n");
    ok(Rule.Action == MpsRuleActionAllow, "Allow rule action was %u\n", (UINT32)Rule.Action);
    ok(Rule.Active, "Allow rule was not marked active\n");
    ok(!Rule.Inbound, "Outbound allow rule was marked inbound\n");
    ok(!Rule.Unsupported, "Supported allow rule was marked unsupported\n");

    ok(ParseRule(ByPassRule, &Rule) == ERROR_SUCCESS, "Failed to parse ByPass rule\n");
    ok(Rule.Action == MpsRuleActionBypass, "ByPass action was %u\n", (UINT32)Rule.Action);
    ok(Rule.Unsupported, "ByPass rule was not marked unsupported\n");

    ok(ParseRule(ExplicitFalse, &Rule) == ERROR_SUCCESS,
       "Failed to parse explicit FALSE rule\n");
    ok(!Rule.Active, "Explicit FALSE rule was marked active\n");

    ok(ParseRule(UdpRule, &Rule) == ERROR_SUCCESS, "Failed to parse UDP rule\n");
    ok(Rule.Protocol == 17, "UDP protocol was %u\n", Rule.Protocol);
    ok(Rule.LocalPortCount == 1, "UDP local port count was %u\n", Rule.LocalPortCount);
    ok(Rule.LocalPorts[0].First == 5353 && Rule.LocalPorts[0].Last == 5353,
       "UDP port was not parsed\n");

    ok(ParseRule(Protocol256Rule, &Rule) == ERROR_SUCCESS, "Failed to parse Protocol=256 rule\n");
    ok(Rule.Protocol == 256, "Protocol 256 was %u\n", Rule.Protocol);

    ok(ParseRule(EmptyDescRule, &Rule) == ERROR_SUCCESS, "Failed to parse rule with empty Desc/EmbedCtxt\n");

    ok(ParseRule(ExactAddressRule, &Rule) == ERROR_SUCCESS, "Failed to parse exact address rule\n");
    ok(Rule.RemoteAddressCount == 1, "Unexpected remote address count %u\n", Rule.RemoteAddressCount);
    ok(Rule.RemoteAddresses[0].Address == 0x7F000001, "Unexpected remote address 0x%08X\n", Rule.RemoteAddresses[0].Address);
    ok(Rule.RemoteAddresses[0].Mask == 0xFFFFFFFF, "Unexpected remote mask 0x%08X\n", Rule.RemoteAddresses[0].Mask);
    ok(!Rule.Unsupported, "Exact address rule was marked unsupported\n");

    ok(ParseRule(CidrAddressRule, &Rule) == ERROR_SUCCESS, "Failed to parse CIDR address rule\n");
    ok(Rule.RemoteAddressCount == 1 && Rule.LocalAddressCount == 1, "Addresses were not recognized\n");
    ok(Rule.RemoteAddresses[0].Address == 0x0A000000, "Unexpected remote CIDR address 0x%08X\n", Rule.RemoteAddresses[0].Address);
    ok(Rule.RemoteAddresses[0].Mask == 0xFF000000, "Unexpected remote CIDR mask 0x%08X\n", Rule.RemoteAddresses[0].Mask);
    ok(Rule.LocalAddresses[0].Address == 0xC0A80100, "Unexpected local CIDR address 0x%08X\n", Rule.LocalAddresses[0].Address);
    ok(Rule.LocalAddresses[0].Mask == 0xFFFFFF00, "Unexpected local CIDR mask 0x%08X\n", Rule.LocalAddresses[0].Mask);
    ok(!Rule.Unsupported, "CIDR address rule was marked unsupported\n");

    ok(ParseRule(DottedMaskRule, &Rule) == ERROR_SUCCESS, "Failed to parse dotted mask rule\n");
    ok(Rule.LocalAddressCount == 1, "Local address was not recognized\n");
    ok(Rule.LocalAddresses[0].Address == 0xC0A80100, "Unexpected local address 0x%08X\n", Rule.LocalAddresses[0].Address);
    ok(Rule.LocalAddresses[0].Mask == 0xFFFFFF00, "Unexpected local mask 0x%08X\n", Rule.LocalAddresses[0].Mask);

    ok(ParseRule(RepeatedAddressRule, &Rule) == ERROR_SUCCESS, "Failed to parse repeated address rule\n");
    ok(!Rule.Unsupported, "Repeated address rule was marked unsupported\n");
    ok(Rule.RemoteAddressCount == 2, "Unexpected remote address count %u\n", Rule.RemoteAddressCount);
    ok(Rule.RemoteAddresses[0].Address == 0x0A000001 && Rule.RemoteAddresses[0].Mask == 0xFFFFFFFF,
       "First remote address was not parsed\n");
    ok(Rule.RemoteAddresses[1].Address == 0xAC100000 && Rule.RemoteAddresses[1].Mask == 0xFFF00000,
       "Second remote address was not parsed\n");
    ok(Rule.LocalAddressCount == 2, "Unexpected local address count %u\n", Rule.LocalAddressCount);
    ok(Rule.LocalAddresses[1].Address == 0xC0A80200 && Rule.LocalAddresses[1].Mask == 0xFFFFFF00,
       "Second local address was not parsed\n");

    ok(ParseRule(AddressRangeRule, &Rule) == ERROR_SUCCESS, "Failed to parse address range rule\n");
    ok(Rule.Unsupported, "Address range rule was not marked unsupported\n");

    ok(ParseRule(BadAddressRule, &Rule) == ERROR_INVALID_DATA, "Out of range address octet was accepted\n");

    ok(ParseRule(AddressKeywordRule, &Rule) == ERROR_SUCCESS, "Failed to parse address keyword rule\n");
    ok(Rule.Unsupported, "Address keyword rule was not marked unsupported\n");

    ok(ParseRule(SvcWildcard, &Rule) == ERROR_SUCCESS, "Failed to parse Svc=* rule\n");
    ok(!Rule.Unsupported, "Svc=* was marked unsupported\n");

    ok(ParseRule(SvcSpecific, &Rule) == ERROR_SUCCESS, "Failed to parse Svc=dnscache rule\n");
    ok(Rule.Unsupported, "Svc=dnscache was not marked unsupported\n");

    ok(ParseRule(PortKeywordRpc, &Rule) == ERROR_SUCCESS,
       "Failed to parse RPC port keyword rule\n");
    ok(Rule.Unsupported, "RPC port keyword was not marked unsupported\n");

    ok(ParseRule(MultiPortRule, &Rule) == ERROR_SUCCESS, "Failed to parse multi-port rule\n");
    ok(Rule.LocalPortCount == 3, "Unexpected local port count %u\n", Rule.LocalPortCount);
    ok(Rule.LocalPorts[0].First == 80 && Rule.LocalPorts[0].Last == 80, "Port 80 not parsed\n");
    ok(Rule.LocalPorts[1].First == 443 && Rule.LocalPorts[1].Last == 443, "Port 443 not parsed\n");
    ok(Rule.LocalPorts[2].First == 8080 && Rule.LocalPorts[2].Last == 8090, "Port range not parsed\n");
    ok(Rule.RemotePortCount == 2, "Unexpected remote port count %u\n", Rule.RemotePortCount);
    ok(Rule.RemotePorts[0].First == 1000 && Rule.RemotePorts[0].Last == 1000, "Port 1000 not parsed\n");
    ok(Rule.RemotePorts[1].First == 2000 && Rule.RemotePorts[1].Last == 2005, "Port range 2000-2005 not parsed\n");
    ok(!Rule.Unsupported, "Multi-port rule was marked unsupported\n");

    ok(ParseRule(BadTrailingCommaPort, &Rule) == ERROR_INVALID_DATA, "Trailing comma was accepted\n");
    ok(ParseRule(BadEmptyCommaPort, &Rule) == ERROR_INVALID_DATA, "Empty comma port was accepted\n");

    ok(ParseRule(DuplicateAction, &Rule) == ERROR_INVALID_DATA,
       "Duplicate Action field was accepted\n");
    ok(ParseRule(PortWithoutProtocol, &Rule) == ERROR_INVALID_DATA,
       "Port without a preceding protocol was accepted\n");
    ok(ParseRule(BadPortRange, &Rule) == ERROR_INVALID_DATA,
       "Out-of-range port was accepted\n");
    ok(ParseRule(MissingDelimiter, &Rule) == ERROR_INVALID_DATA,
       "Missing trailing delimiter was accepted\n");
    ok(ParseRule(BadVersion, &Rule) == ERROR_INVALID_DATA,
       "Version 1 was accepted\n");
    ok(ParseRule(NoVersion, &Rule) == ERROR_INVALID_DATA,
       "Missing version was accepted\n");
    ok(ParseRule(MissingAction, &Rule) == ERROR_INVALID_DATA,
       "Missing Action was accepted\n");
    ok(ParseRule(MissingDir, &Rule) == ERROR_INVALID_DATA,
       "Missing Dir was accepted\n");
    ok(ParseRule(DuplicateDir, &Rule) == ERROR_INVALID_DATA,
       "Duplicate Dir was accepted\n");
    ok(ParseRule(InvertedRange, &Rule) == ERROR_INVALID_DATA,
       "Inverted port range was accepted\n");
    ok(ParseRule(BadAction, &Rule) == ERROR_INVALID_DATA,
       "Invalid action value was accepted\n");

    ok(MpsParseFirewallRule(Supported, MPS_MAX_RULE_CHARS + 1, &Rule) == ERROR_BUFFER_OVERFLOW,
       "Oversized rule was accepted\n");

    ok(MpsParseFirewallRule(NULL, 10, &Rule) == ERROR_INVALID_PARAMETER,
       "NULL text was accepted\n");
    ok(MpsParseFirewallRule(Supported, wcslen(Supported), NULL) == ERROR_INVALID_PARAMETER,
       "NULL rule was accepted\n");
    ok(MpsApplyFirewallRule(NULL, &TestSubLayer, &Rule) == ERROR_INVALID_PARAMETER,
       "NULL engine was accepted\n");
    ok(MpsAddFirewallSubLayer(NULL, &TestSubLayer) == ERROR_INVALID_PARAMETER,
       "NULL engine was accepted by MpsAddFirewallSubLayer\n");
    ok(MpsAddLoopbackPermit(NULL, &TestSubLayer) == ERROR_INVALID_PARAMETER,
       "NULL engine was accepted by MpsAddLoopbackPermit\n");
    ok(MpsLoadFirewallRulesFromKey(NULL, &TestSubLayer, NULL) == ERROR_INVALID_PARAMETER,
       "NULL parameters to MpsLoadFirewallRulesFromKey succeeded\n");

    Owner = OpenEngine(0);
    if (Owner == NULL)
        return;

    ok(MpsApplyFirewallRule(Owner, NULL, &Rule) == ERROR_INVALID_PARAMETER,
       "NULL sublayer was accepted\n");
    Error = MpsAddFirewallSubLayer(Owner, &TestSubLayer);
    ok(Error == ERROR_SUCCESS, "MpsAddFirewallSubLayer returned %lu\n", Error);
    ok(MpsAddFirewallSubLayer(Owner, &TestSubLayer) == ERROR_SUCCESS,
       "Adding an existing sublayer failed\n");

    Engine = OpenEngine(FWPM_SESSION_FLAG_DYNAMIC);
    if (Engine != NULL)
    {
        ok(ApplyRule(Engine, Inactive) == ERROR_SUCCESS, "Inactive rule was not skipped\n");
        ok(ApplyRule(Engine, PartialProfile) == ERROR_NOT_SUPPORTED,
           "Unsupported active rule did not return ERROR_NOT_SUPPORTED\n");
        ok(ApplyRule(Engine, ExactAddressRule) == ERROR_SUCCESS, "Failed to apply exact address rule\n");
        ok(ApplyRule(Engine, CidrAddressRule) == ERROR_SUCCESS, "Failed to apply CIDR address rule\n");
        ok(ApplyRule(Engine, RepeatedAddressRule) == ERROR_SUCCESS, "Failed to apply repeated address rule\n");
        ok(ApplyRule(Engine, MultiPortRule) == ERROR_SUCCESS, "Failed to apply multi-port rule\n");
        FwpmEngineClose0(Engine);
    }

    if (WSAStartup(MAKEWORD(2, 2), &WsaData) != 0)
    {
        ok(FALSE, "WSAStartup failed\n");
        FwpmSubLayerDeleteByKey0(Owner, &TestSubLayer);
        FwpmEngineClose0(Owner);
        return;
    }

    Listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    ZeroMemory(&Local, sizeof(Local));
    Local.sin_family = AF_INET;
    Local.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ok(bind(Listener, (struct sockaddr *)&Local, sizeof(Local)) == 0, "bind failed: %d\n", WSAGetLastError());
    ok(listen(Listener, 4) == 0, "listen failed: %d\n", WSAGetLastError());
    ok(getsockname(Listener, (struct sockaddr *)&Local, &LocalLength) == 0,
       "getsockname failed: %d\n", WSAGetLastError());
    Port = ntohs(Local.sin_port);
    GetModuleFileNameW(NULL, Own, ARRAYSIZE(Own));

    ok(ConnectLoopback(Listener, Port, &WsaError), "Unfiltered connect failed: %d\n", WsaError);

    Engine = OpenEngine(FWPM_SESSION_FLAG_DYNAMIC);
    if (Engine != NULL)
    {
        _snwprintf(Text, ARRAYSIZE(Text), L"v2.10|Action=Block|Active=TRUE|Dir=Out|Protocol=6|RPort=%u|", Port);
        ok(ApplyRule(Engine, Text) == ERROR_SUCCESS, "Failed to apply block rule\n");
        ok(!ConnectLoopback(Listener, Port, &WsaError), "Blocked connect succeeded\n");
        ok(WsaError == WSAEACCES || WsaError == WSAECONNREFUSED, "Blocked connect error %d\n", WsaError);
        FwpmEngineClose0(Engine);
    }

    ok(ConnectLoopback(Listener, Port, &WsaError), "Connect after the block session closed failed: %d\n", WsaError);

    Engine = OpenEngine(FWPM_SESSION_FLAG_DYNAMIC);
    if (Engine != NULL)
    {
        _snwprintf(Text,
                   ARRAYSIZE(Text),
                   L"v2.10|Action=Block|Active=TRUE|Dir=Out|Protocol=6|RPort=%u|"
                   L"LA4=127.0.0.1|RA4=127.0.0.1|App=%ls|",
                   Port, Own);
        ok(ApplyRule(Engine, Text) == ERROR_SUCCESS, "Failed to apply fully specific block rule\n");
        ok(!ConnectLoopback(Listener, Port, &WsaError), "Fully specific block rule was not enforced\n");
        FwpmEngineClose0(Engine);
    }

    ok(ConnectLoopback(Listener, Port, &WsaError), "Connect after the specific block session closed failed: %d\n", WsaError);

    Engine = OpenEngine(FWPM_SESSION_FLAG_DYNAMIC);
    if (Engine != NULL)
    {
        _snwprintf(Text,
                   ARRAYSIZE(Text),
                   L"v2.10|Action=Allow|Active=TRUE|Dir=Out|Protocol=6|RPort=%u|"
                   L"LA4=127.0.0.1|RA4=127.0.0.1|App=%ls|",
                   Port, Own);
        ok(ApplyRule(Engine, Text) == ERROR_SUCCESS, "Failed to apply specific allow rule\n");
        ok(ConnectLoopback(Listener, Port, &WsaError), "Allowed connect failed: %d\n", WsaError);
        ok(ApplyRule(Engine, L"v2.10|Action=Block|Active=TRUE|Dir=Out|") == ERROR_SUCCESS,
           "Failed to apply generic block rule\n");
        ok(!ConnectLoopback(Listener, Port, &WsaError), "A specific allow rule overrode a block rule\n");
        FwpmEngineClose0(Engine);
    }

    Engine = OpenEngine(FWPM_SESSION_FLAG_DYNAMIC);
    if (Engine != NULL)
    {
        Error = MpsAddLoopbackPermit(Engine, &TestSubLayer);
        ok(Error == ERROR_SUCCESS, "MpsAddLoopbackPermit returned %lu\n", Error);
        ok(ApplyRule(Engine, L"v2.10|Action=Block|Active=TRUE|Dir=Out|") == ERROR_SUCCESS,
           "Failed to apply generic block rule\n");
        ok(ConnectLoopback(Listener, Port, &WsaError), "A block rule overrode the loopback permit: %d\n", WsaError);
        FwpmEngineClose0(Engine);
    }

    First = OpenEngine(FWPM_SESSION_FLAG_DYNAMIC);
    Second = OpenEngine(FWPM_SESSION_FLAG_DYNAMIC);
    KeyError = RegCreateKeyExW(HKEY_CURRENT_USER,
                               L"Software\\LiberNT_MpsSvc_Test",
                               0,
                               NULL,
                               REG_OPTION_VOLATILE,
                               KEY_READ | KEY_WRITE,
                               NULL,
                               &TestKey,
                               &Disposition);
    ok(KeyError == ERROR_SUCCESS, "RegCreateKeyExW returned %lu\n", KeyError);
    if (KeyError == ERROR_SUCCESS && First != NULL && Second != NULL)
    {
        SetPortRule(TestKey, L"Inactive", L"v2.10|Action=Block|Dir=Out|Protocol=6|RPort=%u|", Port);
        SetPortRule(TestKey, L"Profile",
                    L"v2.10|Action=Block|Active=TRUE|Dir=Out|Protocol=6|Profile=Public|RPort=%u|", Port);
        SetPortRule(TestKey, L"Version", L"v1.10|Action=Block|Active=TRUE|Dir=Out|Protocol=6|RPort=%u|", Port);
        SetPortRule(TestKey, L"Range",
                    L"v2.10|Action=Block|Active=TRUE|Dir=Out|Protocol=6|RA4=127.0.0.1-127.0.0.2|RPort=%u|", Port);

        Error = MpsLoadFirewallRulesFromKey(First, &TestSubLayer, TestKey);
        ok(Error == ERROR_SUCCESS, "MpsLoadFirewallRulesFromKey returned %lu\n", Error);
        ok(ConnectLoopback(Listener, Port, &WsaError), "A skipped rule blocked the connect: %d\n", WsaError);

        SetPortRule(TestKey, L"Block", L"v2.10|Action=Block|Active=TRUE|Dir=Out|Protocol=6|RPort=%u|", Port);
        Error = MpsLoadFirewallRulesFromKey(Second, &TestSubLayer, TestKey);
        ok(Error == ERROR_SUCCESS, "Reload MpsLoadFirewallRulesFromKey returned %lu\n", Error);
        ok(!ConnectLoopback(Listener, Port, &WsaError), "Reloaded block rule was not enforced\n");

        FwpmEngineClose0(First);
        First = NULL;
        ok(!ConnectLoopback(Listener, Port, &WsaError), "Closing the old session removed the reloaded rule\n");

        FwpmEngineClose0(Second);
        Second = NULL;
        ok(ConnectLoopback(Listener, Port, &WsaError), "Closing the new session left the rule: %d\n", WsaError);
    }
    if (KeyError == ERROR_SUCCESS)
    {
        RegCloseKey(TestKey);
        RegDeleteKeyW(HKEY_CURRENT_USER, L"Software\\LiberNT_MpsSvc_Test");
    }
    if (Second != NULL)
        FwpmEngineClose0(Second);
    if (First != NULL)
        FwpmEngineClose0(First);
    Error = FwpmSubLayerDeleteByKey0(Owner, &TestSubLayer);
    ok(Error == ERROR_SUCCESS, "FwpmSubLayerDeleteByKey0 returned %lu\n", Error);
    FwpmEngineClose0(Owner);

    closesocket(Listener);
    WSACleanup();
}
