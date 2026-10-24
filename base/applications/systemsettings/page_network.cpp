/*
 * PROJECT:     LiberNT Settings
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Network and internet pages
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "settings.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <wlanapi.h>
#include <wininet.h>
#include <netcfgx.h>
#include <netcfgx_undoc.h>
#include <netcfgn_undoc.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <algorithm>

#define EXPLORER_KEY L"Software\\ReactOS\\Explorer"

class WlanSession
{
public:
    WlanSession() : m_Handle(NULL), m_Interfaces(NULL)
    {
        DWORD version = 0;

        if (WlanOpenHandle(2, NULL, &version, &m_Handle) != ERROR_SUCCESS)
            m_Handle = NULL;
        else if (WlanEnumInterfaces(m_Handle, NULL, &m_Interfaces) != ERROR_SUCCESS)
            m_Interfaces = NULL;
    }

    ~WlanSession()
    {
        if (m_Interfaces)
            WlanFreeMemory(m_Interfaces);
        if (m_Handle)
            WlanCloseHandle(m_Handle, NULL);
    }

    bool Available() const { return m_Interfaces && m_Interfaces->dwNumberOfItems; }
    HANDLE Handle() const { return m_Handle; }
    const GUID *Interface() const { return Available() ? &m_Interfaces->InterfaceInfo[0].InterfaceGuid : NULL; }
    WLAN_INTERFACE_STATE State() const
    {
        return Available() ? m_Interfaces->InterfaceInfo[0].isState : wlan_interface_state_not_ready;
    }

    bool RadioOn()
    {
        PWLAN_RADIO_STATE state = NULL;
        DWORD size = 0;
        bool on = false;

        if (!Available() ||
            WlanQueryInterface(m_Handle, Interface(), wlan_intf_opcode_radio_state, NULL, &size, (PVOID *)&state,
                               NULL) != ERROR_SUCCESS || !state)
            return false;
        for (DWORD i = 0; i < state->dwNumberOfPhys && i < WLAN_MAX_PHY_INDEX; i++)
        {
            if (state->PhyRadioState[i].dot11SoftwareRadioState == dot11_radio_state_on &&
                state->PhyRadioState[i].dot11HardwareRadioState == dot11_radio_state_on)
                on = true;
        }
        WlanFreeMemory(state);
        return on;
    }

    void SetRadio(bool on)
    {
        WLAN_PHY_RADIO_STATE state;

        if (!Available())
            return;
        ZeroMemory(&state, sizeof(state));
        state.dot11SoftwareRadioState = on ? dot11_radio_state_on : dot11_radio_state_off;
        state.dot11HardwareRadioState = dot11_radio_state_on;
        WlanSetInterface(m_Handle, Interface(), wlan_intf_opcode_radio_state, sizeof(state), &state, NULL);
    }

private:
    HANDLE m_Handle;
    PWLAN_INTERFACE_INFO_LIST m_Interfaces;
};

static bool
AirplaneMode()
{
    return ReadDword(HKEY_CURRENT_USER, EXPLORER_KEY, L"AirplaneMode", 0) != 0;
}

static void
SetAirplaneMode(bool on)
{
    WlanSession wlan;
    bool restore = ReadDword(HKEY_CURRENT_USER, EXPLORER_KEY, L"WifiRestore", 1) != 0;

    if (on)
    {
        WriteDword(HKEY_CURRENT_USER, EXPLORER_KEY, L"WifiRestore", wlan.Available() && wlan.RadioOn());
        if (wlan.Available())
            wlan.SetRadio(false);
    }
    else if (wlan.Available())
    {
        wlan.SetRadio(restore);
    }
    WriteDword(HKEY_CURRENT_USER, EXPLORER_KEY, L"AirplaneMode", on);
}

static std::wstring
SsidText(const DOT11_SSID &ssid)
{
    WCHAR text[DOT11_SSID_MAX_LENGTH + 1];
    int length = MultiByteToWideChar(CP_UTF8, 0, (LPCSTR)ssid.ucSSID, (int)ssid.uSSIDLength, text, DOT11_SSID_MAX_LENGTH);

    text[max(0, length)] = UNICODE_NULL;
    return text;
}

static std::wstring
SocketAddressText(const SOCKET_ADDRESS &address)
{
    WCHAR text[64] = L"";
    const SOCKADDR *sockaddr = address.lpSockaddr;

    if (!sockaddr)
        return std::wstring();
    if (sockaddr->sa_family == AF_INET)
        InetNtopW(AF_INET, (PVOID)&((const SOCKADDR_IN *)sockaddr)->sin_addr, text, _countof(text));
    else if (sockaddr->sa_family == AF_INET6)
        InetNtopW(AF_INET6, (PVOID)&((const SOCKADDR_IN6 *)sockaddr)->sin6_addr, text, _countof(text));
    return text;
}

static std::vector<BYTE>
AdapterTable()
{
    std::vector<BYTE> buffer(16 * 1024);
    ULONG size = (ULONG)buffer.size();
    ULONG flags = GAA_FLAG_INCLUDE_GATEWAYS | GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST;
    ULONG result = GetAdaptersAddresses(AF_UNSPEC, flags, NULL, (PIP_ADAPTER_ADDRESSES)buffer.data(), &size);

    if (result == ERROR_BUFFER_OVERFLOW)
    {
        buffer.resize(size);
        result = GetAdaptersAddresses(AF_UNSPEC, flags, NULL, (PIP_ADAPTER_ADDRESSES)buffer.data(), &size);
    }
    if (result != ERROR_SUCCESS)
        buffer.clear();
    return buffer;
}

static bool
VisibleAdapter(const IP_ADAPTER_ADDRESSES *adapter)
{
    return adapter->IfType != IF_TYPE_SOFTWARE_LOOPBACK && adapter->IfType != IF_TYPE_TUNNEL;
}

static LPCWSTR
AdapterIcon(const IP_ADAPTER_ADDRESSES *adapter)
{
    return adapter->IfType == IF_TYPE_IEEE80211 ? L"wifi_1_20_regular" : L"plug_connected_20_regular";
}

static std::wstring
LinkSpeed(ULONG64 bits)
{
    if (!bits || bits == (ULONG64)-1)
        return std::wstring();
    if (bits >= 1000000000ULL)
        return Printf(L"%I64u Gbps", bits / 1000000000ULL);
    if (bits >= 1000000ULL)
        return Printf(L"%I64u Mbps", bits / 1000000ULL);
    return Printf(L"%I64u Kbps", bits / 1000ULL);
}

void
BuildNetwork(PageBuilder &page)
{
    SettingsHost *host = page.Host;
    std::vector<BYTE> table;
    const IP_ADAPTER_ADDRESSES *connected = NULL;
    bool hasWlan = false, wifiOn = false;

    if (!page.Indexing)
    {
        WlanSession wlan;
        hasWlan = wlan.Available();
        wifiOn = hasWlan && wlan.RadioOn();
        table = AdapterTable();
        for (const IP_ADAPTER_ADDRESSES *adapter = table.empty() ? NULL : (const IP_ADAPTER_ADDRESSES *)table.data();
             adapter; adapter = adapter->Next)
        {
            if (VisibleAdapter(adapter) && adapter->OperStatus == IfOperStatusUp && adapter->FirstGatewayAddress)
            {
                connected = adapter;
                break;
            }
        }
        if (connected)
        {
            std::wstring name = connected->AdapterName ? std::wstring(connected->AdapterName,
                                                                      connected->AdapterName +
                                                                      strlen(connected->AdapterName)) : std::wstring();
            SettingRow &row = page.Hero(connected->FriendlyName, Str(IDS_NET_CONNECTED), AdapterIcon(connected));
            row.Action = Str(IDS_PROPERTIES);
            row.Invoke = [host, name](int) { host->Open(PageId::Adapter, name); };
        }
        else
        {
            page.Hero(Str(IDS_NET_NOT_CONNECTED), Str(IDS_NET_NOT_CONNECTED_DESC), L"plug_connected_20_regular");
        }
    }
    page.Group();
    if (hasWlan || page.Indexing)
    {
        SettingRow &row = page.Navigate(Str(IDS_PAGE_WIFI), Str(IDS_WIFI_DESC), L"wifi_1_20_regular", PageId::WiFi);
        row.Value = page.Indexing ? std::wstring() : Str(wifiOn ? IDS_ON : IDS_OFF);
        row.Keywords = Str(IDS_WIFI_KEYWORDS);
    }
    for (const IP_ADAPTER_ADDRESSES *adapter = table.empty() ? NULL : (const IP_ADAPTER_ADDRESSES *)table.data();
         adapter; adapter = adapter->Next)
    {
        std::wstring name;
        if (!VisibleAdapter(adapter) || adapter->IfType == IF_TYPE_IEEE80211)
            continue;
        name = std::wstring(adapter->AdapterName, adapter->AdapterName + strlen(adapter->AdapterName));
        page.Navigate(adapter->FriendlyName, adapter->Description, AdapterIcon(adapter), PageId::Adapter, name).Value =
            Str(adapter->OperStatus == IfOperStatusUp ? IDS_NET_CONNECTED : IDS_NET_DISCONNECTED);
    }
    page.Toggle(Str(IDS_AIRPLANE), Str(IDS_AIRPLANE_DESC), L"airplane_20_regular", !page.Indexing && AirplaneMode(),
                [](int on) { SetAirplaneMode(on != 0); });
    page.Navigate(Str(IDS_PAGE_PROXY), Str(IDS_PROXY_DESC), L"globe_20_regular", PageId::Proxy);
    page.Navigate(Str(IDS_ADVANCED_NETWORK), Str(IDS_ADVANCED_NETWORK_DESC), L"network_check_20_regular",
                  PageId::NetworkAdvanced);
}

#define NETWORK_CONNECTIONS_KEY L"SYSTEM\\CurrentControlSet\\Control\\Network\\{4D36E972-E325-11CE-BFC1-08002BE10318}"

struct NetworkConnection
{
    std::wstring Guid;
    std::wstring Name;
    std::wstring Instance;
    std::wstring Description;
    bool Disabled;
};

static std::vector<NetworkConnection>
NetworkConnections()
{
    std::vector<NetworkConnection> list;
    WCHAR guid[64];
    HKEY key;

    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, NETWORK_CONNECTIONS_KEY, 0, KEY_READ, &key) != ERROR_SUCCESS)
        return list;
    for (DWORD i = 0;; i++)
    {
        DWORD length = _countof(guid);
        if (RegEnumKeyExW(key, i, guid, &length, NULL, NULL, NULL, NULL) != ERROR_SUCCESS)
            break;
        if (guid[0] != L'{')
            continue;

        std::wstring path = std::wstring(NETWORK_CONNECTIONS_KEY L"\\") + guid + L"\\Connection";
        NetworkConnection connection;
        DEVINST device;
        ULONG status = 0, problem = 0, size;
        WCHAR text[256];

        connection.Guid = guid;
        connection.Name = ReadString(HKEY_LOCAL_MACHINE, path.c_str(), L"Name");
        connection.Instance = ReadString(HKEY_LOCAL_MACHINE, path.c_str(), L"PnpInstanceID");
        if (connection.Instance.empty() ||
            CM_Locate_DevNodeW(&device, (DEVINSTID_W)connection.Instance.c_str(), CM_LOCATE_DEVNODE_NORMAL) != CR_SUCCESS)
            continue;
        connection.Disabled = CM_Get_DevNode_Status(&status, &problem, device, 0) == CR_SUCCESS &&
                              problem == CM_PROB_DISABLED;
        size = sizeof(text);
        if (CM_Get_DevNode_Registry_PropertyW(device, CM_DRP_FRIENDLYNAME, NULL, text, &size, 0) != CR_SUCCESS)
        {
            size = sizeof(text);
            if (CM_Get_DevNode_Registry_PropertyW(device, CM_DRP_DEVICEDESC, NULL, text, &size, 0) != CR_SUCCESS)
                text[0] = UNICODE_NULL;
        }
        connection.Description = text;
        if (connection.Name.empty())
            connection.Name = connection.Description;
        list.push_back(connection);
    }
    RegCloseKey(key);
    std::sort(list.begin(), list.end(),
              [](const NetworkConnection &a, const NetworkConnection &b) { return lstrcmpiW(a.Name.c_str(), b.Name.c_str()) < 0; });
    return list;
}

static DWORD
SetDeviceEnabled(const std::wstring &instance, bool enable)
{
    HDEVINFO set = SetupDiCreateDeviceInfoList(NULL, NULL);
    SP_DEVINFO_DATA data = { sizeof(data) };
    SP_PROPCHANGE_PARAMS params = {};
    DWORD error = ERROR_SUCCESS;

    if (set == INVALID_HANDLE_VALUE)
        return GetLastError();
    params.ClassInstallHeader.cbSize = sizeof(SP_CLASSINSTALL_HEADER);
    params.ClassInstallHeader.InstallFunction = DIF_PROPERTYCHANGE;
    params.StateChange = enable ? DICS_ENABLE : DICS_DISABLE;
    params.Scope = DICS_FLAG_GLOBAL;
    if (!SetupDiOpenDeviceInfoW(set, instance.c_str(), NULL, 0, &data) ||
        !SetupDiSetClassInstallParamsW(set, &data, &params.ClassInstallHeader, sizeof(params)) ||
        !SetupDiCallClassInstaller(DIF_PROPERTYCHANGE, set, &data))
        error = GetLastError();
    SetupDiDestroyDeviceInfoList(set);
    return error;
}

void
BuildNetworkAdvanced(PageBuilder &page)
{
    SettingsHost *host = page.Host;
    std::vector<NetworkConnection> connections;
    bool admin = IsAdmin();

    page.Group(Str(IDS_NETWORK_ADAPTERS));
    if (page.Indexing)
        return;
    connections = NetworkConnections();
    if (connections.empty())
        page.Text(Str(IDS_NO_ADAPTERS));
    for (size_t i = 0; i < connections.size(); i++)
    {
        NetworkConnection connection = connections[i];
        std::wstring detail = connection.Description;
        if (connection.Disabled)
            detail += L" \u00b7 " + Str(IDS_ADAPTER_DISABLED);
        SettingRow &row = page.Button(connection.Name, detail, L"plug_connected_20_regular", std::wstring(),
                                      [host, connection](int action)
                                      {
                                          if (action == 1)
                                          {
                                              host->Open(PageId::Adapter, connection.Guid);
                                              return;
                                          }
                                          DWORD *result = new DWORD(ERROR_SUCCESS);
                                          host->RunJob([connection, result]()
                                                       {
                                                           *result = SetDeviceEnabled(connection.Instance,
                                                                                      connection.Disabled);
                                                       },
                                                       [host, result]()
                                                       {
                                                           if (*result != ERROR_SUCCESS)
                                                               ShowError(host->Window(), *result);
                                                           delete result;
                                                           host->Refresh();
                                                       });
                                      });
        row.Actions.push_back(Str(connection.Disabled ? IDS_ENABLE : IDS_DISABLE));
        if (!connection.Disabled)
            row.Actions.push_back(Str(IDS_PROPERTIES));
        row.Enabled = admin;
    }
}

static void
XmlEscape(const std::wstring &text, std::wstring &out)
{
    out.clear();
    for (size_t i = 0; i < text.length(); i++)
    {
        switch (text[i])
        {
            case L'&': out += L"&amp;"; break;
            case L'<': out += L"&lt;"; break;
            case L'>': out += L"&gt;"; break;
            case L'"': out += L"&quot;"; break;
            case L'\'': out += L"&apos;"; break;
            default: out += text[i]; break;
        }
    }
}

static LPCWSTR
AuthName(DOT11_AUTH_ALGORITHM auth)
{
    switch (auth)
    {
        case DOT11_AUTH_ALGO_RSNA_PSK: return L"WPA2PSK";
        case DOT11_AUTH_ALGO_WPA_PSK: return L"WPAPSK";
        case DOT11_AUTH_ALGO_WPA3_SAE: return L"WPA3SAE";
        case DOT11_AUTH_ALGO_80211_SHARED_KEY: return L"shared";
        default: return L"open";
    }
}

static LPCWSTR
CipherName(DOT11_CIPHER_ALGORITHM cipher, bool secure)
{
    switch (cipher)
    {
        case DOT11_CIPHER_ALGO_CCMP: return L"AES";
        case DOT11_CIPHER_ALGO_TKIP: return L"TKIP";
        case DOT11_CIPHER_ALGO_WEP:
        case DOT11_CIPHER_ALGO_WEP40:
        case DOT11_CIPHER_ALGO_WEP104: return L"WEP";
        default: return secure ? L"AES" : L"none";
    }
}

struct WifiNetwork
{
    std::wstring Ssid;
    std::wstring Profile;
    ULONG Quality;
    bool Secure;
    bool Connected;
    DOT11_AUTH_ALGORITHM Auth;
    DOT11_CIPHER_ALGORITHM Cipher;
};

static DWORD
ConnectWifi(HWND owner, const WifiNetwork &network)
{
    WlanSession wlan;
    WLAN_CONNECTION_PARAMETERS params;
    std::wstring profile = network.Profile;
    DWORD result;

    if (!wlan.Available())
        return ERROR_NOT_FOUND;
    if (network.Auth == DOT11_AUTH_ALGO_RSNA || network.Auth == DOT11_AUTH_ALGO_WPA)
    {
        if (profile.empty())
            return ERROR_NOT_SUPPORTED;
    }
    if (profile.empty())
    {
        std::wstring ssid, key, xml, passphrase;
        DWORD reason = 0;

        if (network.Secure && !InputBox(owner, network.Ssid, Str(IDS_WIFI_PASSWORD), passphrase, true))
            return ERROR_CANCELLED;
        XmlEscape(network.Ssid, ssid);
        XmlEscape(passphrase, key);
        xml = L"<?xml version=\"1.0\"?><WLANProfile xmlns=\"http://www.microsoft.com/networking/WLAN/profile/v1\">"
              L"<name>" + ssid + L"</name><SSIDConfig><SSID><name>" + ssid + L"</name></SSID></SSIDConfig>"
              L"<connectionType>ESS</connectionType><connectionMode>auto</connectionMode><MSM><security>"
              L"<authEncryption><authentication>" + std::wstring(network.Secure ? AuthName(network.Auth) : L"open") +
              L"</authentication><encryption>" + CipherName(network.Cipher, network.Secure) +
              L"</encryption><useOneX>false</useOneX></authEncryption>";
        if (network.Secure)
            xml += L"<sharedKey><keyType>passPhrase</keyType><protected>false</protected><keyMaterial>" + key +
                   L"</keyMaterial></sharedKey>";
        xml += L"</security></MSM></WLANProfile>";
        result = WlanSetProfile(wlan.Handle(), wlan.Interface(), 0, xml.c_str(), NULL, TRUE, NULL, &reason);
        SecureZeroMemory(&xml[0], xml.length() * sizeof(WCHAR));
        SecureZeroMemory(&key[0], key.length() * sizeof(WCHAR));
        if (result != ERROR_SUCCESS)
            return result;
        profile = network.Ssid;
    }
    ZeroMemory(&params, sizeof(params));
    params.wlanConnectionMode = wlan_connection_mode_profile;
    params.strProfile = profile.c_str();
    params.dot11BssType = dot11_BSS_type_infrastructure;
    return WlanConnect(wlan.Handle(), wlan.Interface(), &params, NULL);
}

void
BuildWiFi(PageBuilder &page)
{
    SettingsHost *host = page.Host;
    std::vector<WifiNetwork> networks;
    PWLAN_AVAILABLE_NETWORK_LIST list = NULL;
    bool on;

    if (page.Indexing)
    {
        page.Group();
        page.Toggle(Str(IDS_PAGE_WIFI), std::wstring(), L"wifi_1_20_regular", false, nullptr);
        page.Navigate(Str(IDS_WIFI_KNOWN), Str(IDS_WIFI_KNOWN_DESC), L"wifi_1_20_regular", PageId::WiFiKnown);
        return;
    }
    {
        WlanSession wlan;
        if (!wlan.Available())
        {
            page.Group();
            page.Text(Str(IDS_WIFI_NONE));
            return;
        }
        on = wlan.RadioOn();
        if (on)
        {
            static ULONGLONG LastScan;
            ULONGLONG now = GetTickCount64();
            if (!LastScan || now - LastScan > 15000)
            {
                WlanScan(wlan.Handle(), wlan.Interface(), NULL, NULL, NULL);
                LastScan = now;
            }
        }
        if (on && WlanGetAvailableNetworkList(wlan.Handle(), wlan.Interface(), 0, NULL, &list) == ERROR_SUCCESS && list)
        {
            for (DWORD i = 0; i < list->dwNumberOfItems; i++)
            {
                const WLAN_AVAILABLE_NETWORK &entry = list->Network[i];
                WifiNetwork network;
                bool merged = false;

                if (entry.dot11BssType != dot11_BSS_type_infrastructure)
                    continue;
                network.Ssid = SsidText(entry.dot11Ssid);
                network.Profile = (entry.dwFlags & WLAN_AVAILABLE_NETWORK_HAS_PROFILE) ? entry.strProfileName : L"";
                network.Quality = entry.wlanSignalQuality;
                network.Secure = entry.bSecurityEnabled != FALSE;
                network.Connected = (entry.dwFlags & WLAN_AVAILABLE_NETWORK_CONNECTED) != 0;
                network.Auth = entry.dot11DefaultAuthAlgorithm;
                network.Cipher = entry.dot11DefaultCipherAlgorithm;
                for (size_t j = 0; j < networks.size() && !merged; j++)
                {
                    if (networks[j].Ssid != network.Ssid)
                        continue;
                    if (network.Connected || (!networks[j].Connected && !network.Profile.empty()))
                        networks[j] = network;
                    merged = true;
                }
                if (!merged)
                    networks.push_back(network);
            }
            WlanFreeMemory(list);
        }
    }
    std::sort(networks.begin(), networks.end(), [](const WifiNetwork &a, const WifiNetwork &b)
              { return a.Connected != b.Connected ? a.Connected : a.Quality > b.Quality; });

    page.Group();
    page.Toggle(Str(IDS_PAGE_WIFI), Str(on ? IDS_ON : IDS_OFF), L"wifi_1_20_regular", on,
                [host](int value)
                {
                    WlanSession wlan;
                    wlan.SetRadio(value != 0);
                    host->SetTimerRefresh(1500);
                });
    page.Navigate(Str(IDS_WIFI_KNOWN), Str(IDS_WIFI_KNOWN_DESC), L"wifi_1_20_regular", PageId::WiFiKnown);

    if (!on)
        return;
    page.Group(Str(IDS_WIFI_AVAILABLE));
    {
        SettingRow &row = page.Button(Str(IDS_WIFI_SCAN), Str(IDS_WIFI_SCAN_DESC), L"arrow_clockwise_16_regular",
                                      Str(IDS_REFRESH),
                                      [host](int)
                                      {
                                          WlanSession wlan;
                                          if (wlan.Available())
                                              WlanScan(wlan.Handle(), wlan.Interface(), NULL, NULL, NULL);
                                          host->SetTimerRefresh(4000);
                                      });
        UNREFERENCED_PARAMETER(row);
    }
    if (networks.empty())
        page.Text(Str(IDS_WIFI_NO_NETWORKS));
    for (size_t i = 0; i < networks.size(); i++)
    {
        WifiNetwork network = networks[i];
        std::wstring detail = Format(network.Secure ? IDS_WIFI_SECURED : IDS_WIFI_OPEN, network.Quality);

        if (network.Connected)
            detail = Str(IDS_NET_CONNECTED) + L" \x00B7 " + detail;
        SettingRow &row = page.Button(network.Ssid.empty() ? Str(IDS_WIFI_HIDDEN) : network.Ssid, detail,
                                      network.Connected ? L"wifi_1_20_regular" : (network.Secure ? L"lock_closed_20_regular" :
                                                                                  L"wifi_1_20_regular"),
                                      Str(network.Connected ? IDS_DISCONNECT : IDS_CONNECT),
                                      [host, network](int)
                                      {
                                          DWORD result = ERROR_SUCCESS;
                                          if (network.Connected)
                                          {
                                              WlanSession wlan;
                                              result = WlanDisconnect(wlan.Handle(), wlan.Interface(), NULL);
                                          }
                                          else
                                          {
                                              result = ConnectWifi(host->Window(), network);
                                          }
                                          if (result != ERROR_SUCCESS && result != ERROR_CANCELLED)
                                              ShowError(host->Window(), result);
                                          host->SetTimerRefresh(3000);
                                      });
        row.Accent = network.Connected ? false : true;
        row.Enabled = !network.Ssid.empty();
    }
}

void
BuildWiFiKnown(PageBuilder &page)
{
    SettingsHost *host = page.Host;
    PWLAN_PROFILE_INFO_LIST profiles = NULL;
    WlanSession wlan;

    page.Group();
    if (page.Indexing || !wlan.Available())
        return;
    if (WlanGetProfileList(wlan.Handle(), wlan.Interface(), NULL, &profiles) != ERROR_SUCCESS || !profiles ||
        !profiles->dwNumberOfItems)
    {
        page.Text(Str(IDS_WIFI_NO_KNOWN));
        if (profiles)
            WlanFreeMemory(profiles);
        return;
    }
    for (DWORD i = 0; i < profiles->dwNumberOfItems; i++)
    {
        std::wstring name = profiles->ProfileInfo[i].strProfileName;
        page.Button(name, std::wstring(), L"wifi_1_20_regular", Str(IDS_FORGET),
                    [host, name](int)
                    {
                        WlanSession wlan;
                        DWORD result = wlan.Available() ? WlanDeleteProfile(wlan.Handle(), wlan.Interface(), name.c_str(),
                                                                            NULL) : ERROR_NOT_FOUND;
                        if (result != ERROR_SUCCESS)
                            ShowError(host->Window(), result);
                    });
    }
    WlanFreeMemory(profiles);
}

struct TcpipConfig
{
    bool Dhcp;
    std::wstring Address;
    std::wstring Mask;
    std::wstring Parameters;
};

class TcpipSession
{
public:
    TcpipSession() : m_Config(NULL), m_Lock(NULL), m_Component(NULL), m_Properties(NULL), m_Locked(false),
                     m_Initialized(false)
    {
        INetCfgComponentPrivate *component = NULL;

        m_Result = CoCreateInstance(CLSID_CNetCfg, NULL, CLSCTX_INPROC_SERVER, IID_INetCfg, (void **)&m_Config);
        if (m_Result == S_OK)
            m_Result = m_Config->QueryInterface(IID_INetCfgLock, (void **)&m_Lock);
        if (m_Result == S_OK)
            m_Result = m_Lock->AcquireWriteLock(5000, L"Settings", NULL);
        m_Locked = m_Result == S_OK;
        if (m_Locked)
            m_Result = m_Config->Initialize(NULL);
        m_Initialized = m_Locked && m_Result == S_OK;
        if (m_Initialized)
            m_Result = m_Config->FindComponent(L"MS_TCPIP", &m_Component);
        if (m_Result == S_OK)
            m_Result = m_Component->QueryInterface(IID_INetCfgComponentPrivate, (void **)&component);
        if (m_Result == S_OK)
        {
            m_Result = component->Unknown1(IID_ITcpipProperties, (void **)&m_Properties);
            component->Release();
        }
        if (m_Result == S_FALSE)
            m_Result = HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    }

    ~TcpipSession()
    {
        if (m_Properties)
            m_Properties->Release();
        if (m_Component)
            m_Component->Release();
        if (m_Initialized)
            m_Config->Uninitialize();
        if (m_Locked)
            m_Lock->ReleaseWriteLock();
        if (m_Lock)
            m_Lock->Release();
        if (m_Config)
            m_Config->Release();
    }

    template <class Change>
    HRESULT Update(GUID adapter, Change change)
    {
        PTCPIP_PROPERTIES current = NULL;
        TCPIP_PROPERTIES next;
        TcpipConfig config;
        HRESULT hr = m_Result;

        if (hr == S_OK)
            hr = m_Properties->Unknown1(&adapter, &current);
        if (hr != S_OK)
            return FAILED(hr) ? hr : E_FAIL;
        config.Dhcp = current->dwDhcp != 0;
        config.Address = current->pszIpAddress ? current->pszIpAddress : L"";
        config.Mask = current->pszSubnetMask ? current->pszSubnetMask : L"";
        config.Parameters = current->pszParameters ? current->pszParameters : L"";
        CoTaskMemFree(current);
        change(config);
        next.dwDhcp = config.Dhcp ? MAXDWORD : 0;
        next.pszIpAddress = (PWSTR)config.Address.c_str();
        next.pszSubnetMask = (PWSTR)config.Mask.c_str();
        next.pszParameters = (PWSTR)config.Parameters.c_str();
        hr = m_Properties->Unknown2(&adapter, &next);
        if (hr == S_OK)
            hr = m_Config->Apply();
        return FAILED(hr) ? hr : S_OK;
    }

private:
    INetCfg *m_Config;
    INetCfgLock *m_Lock;
    INetCfgComponent *m_Component;
    ITcpipProperties *m_Properties;
    bool m_Locked;
    bool m_Initialized;
    HRESULT m_Result;
};

static std::wstring
WithParameter(const std::wstring &parameters, LPCWSTR name, const std::wstring &value)
{
    std::wstring key = std::wstring(name) + L"=", result;
    size_t start = 0, end;
    bool found = false;

    while (start < parameters.size())
    {
        end = parameters.find(L';', start);
        if (end == std::wstring::npos)
            end = parameters.size();
        std::wstring item = parameters.substr(start, end - start);
        if (!item.compare(0, key.size(), key))
        {
            item = key + value;
            found = true;
        }
        if (!item.empty())
            result += item + L";";
        start = end + 1;
    }
    if (!found)
        result += key + value + L";";
    return result;
}

static bool
ValidIpv4(const std::wstring &text, bool optional)
{
    IN_ADDR address;

    if (text.empty())
        return optional;
    return InetPtonW(AF_INET, text.c_str(), &address) == 1;
}

static std::wstring
PrefixMask(UINT8 length)
{
    ULONG mask = length ? (length >= 32 ? 0xFFFFFFFF : ~(0xFFFFFFFFu >> length)) : 0;

    return Printf(L"%u.%u.%u.%u", (mask >> 24) & 0xFF, (mask >> 16) & 0xFF, (mask >> 8) & 0xFF, mask & 0xFF);
}

template <class Change>
static void
ApplyTcpip(SettingsHost *host, const std::wstring &adapterName, Change change)
{
    HRESULT *result;
    GUID adapter;

    if (FAILED(CLSIDFromString(adapterName.c_str(), &adapter)))
    {
        ShowError(host->Window(), ERROR_NOT_FOUND);
        return;
    }
    result = new HRESULT(E_FAIL);
    host->RunJob([result, adapter, change]()
                 {
                     HRESULT init = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
                     {
                         TcpipSession session;
                         *result = session.Update(adapter, change);
                     }
                     if (SUCCEEDED(init))
                         CoUninitialize();
                 },
                 [host, result]()
                 {
                     if (FAILED(*result))
                         ShowError(host->Window(), (DWORD)*result);
                     delete result;
                     host->Refresh();
                 });
}

void
BuildAdapter(PageBuilder &page)
{
    SettingsHost *host = page.Host;
    std::vector<BYTE> table;
    const IP_ADAPTER_ADDRESSES *adapter = NULL;
    std::vector<std::pair<std::wstring, std::wstring>> rows;
    std::wstring ipv4, ipv6, gateway, dns, text;

    if (page.Indexing)
        return;
    table = AdapterTable();
    for (const IP_ADAPTER_ADDRESSES *entry = table.empty() ? NULL : (const IP_ADAPTER_ADDRESSES *)table.data(); entry;
         entry = entry->Next)
    {
        std::wstring name(entry->AdapterName, entry->AdapterName + strlen(entry->AdapterName));
        if (name == page.Param || (page.Param.empty() && VisibleAdapter(entry)))
        {
            adapter = entry;
            break;
        }
    }
    if (!adapter)
    {
        page.Group();
        page.Text(Str(IDS_ADAPTER_GONE));
        return;
    }
    page.Title = adapter->FriendlyName;
    for (const IP_ADAPTER_UNICAST_ADDRESS *address = adapter->FirstUnicastAddress; address; address = address->Next)
    {
        std::wstring &target = address->Address.lpSockaddr->sa_family == AF_INET ? ipv4 : ipv6;
        target += (target.empty() ? L"" : L", ") + SocketAddressText(address->Address);
    }
    for (const IP_ADAPTER_GATEWAY_ADDRESS_LH *address = adapter->FirstGatewayAddress; address; address = address->Next)
        gateway += (gateway.empty() ? L"" : L", ") + SocketAddressText(address->Address);
    for (const IP_ADAPTER_DNS_SERVER_ADDRESS *address = adapter->FirstDnsServerAddress; address; address = address->Next)
        dns += (dns.empty() ? L"" : L", ") + SocketAddressText(address->Address);
    rows.push_back(std::make_pair(Str(IDS_STATUS), Str(adapter->OperStatus == IfOperStatusUp ? IDS_NET_CONNECTED :
                                                       IDS_NET_DISCONNECTED)));
    rows.push_back(std::make_pair(Str(IDS_DESCRIPTION), adapter->Description));
    rows.push_back(std::make_pair(Str(IDS_LINK_SPEED), LinkSpeed(adapter->ReceiveLinkSpeed)));
    rows.push_back(std::make_pair(Str(IDS_IPV4), ipv4));
    rows.push_back(std::make_pair(Str(IDS_IPV6), ipv6));
    rows.push_back(std::make_pair(Str(IDS_GATEWAY), gateway));
    rows.push_back(std::make_pair(Str(IDS_DNS_SERVERS), dns));
    rows.push_back(std::make_pair(Str(IDS_DNS_SUFFIX), adapter->DnsSuffix ? adapter->DnsSuffix : L""));
    if (adapter->PhysicalAddressLength)
    {
        std::wstring mac;
        for (ULONG i = 0; i < adapter->PhysicalAddressLength; i++)
            mac += Printf(i ? L"-%02X" : L"%02X", adapter->PhysicalAddress[i]);
        rows.push_back(std::make_pair(Str(IDS_MAC_ADDRESS), mac));
    }
    {
        std::wstring name(adapter->AdapterName, adapter->AdapterName + strlen(adapter->AdapterName));
        std::wstring key = L"SYSTEM\\CurrentControlSet\\Services\\Tcpip\\Parameters\\Interfaces\\" + name;
        std::wstring firstIp, mask, firstGateway, dns1, dns2;
        std::wstring manualDns = ReadString(HKEY_LOCAL_MACHINE, key.c_str(), L"NameServer");
        bool dhcp = ReadDword(HKEY_LOCAL_MACHINE, key.c_str(), L"EnableDHCP", adapter->Dhcpv4Enabled) != 0;
        bool admin = IsAdmin();

        for (const IP_ADAPTER_UNICAST_ADDRESS *address = adapter->FirstUnicastAddress; address; address = address->Next)
        {
            if (address->Address.lpSockaddr->sa_family == AF_INET)
            {
                firstIp = SocketAddressText(address->Address);
                mask = PrefixMask(address->OnLinkPrefixLength);
                break;
            }
        }
        for (const IP_ADAPTER_GATEWAY_ADDRESS_LH *address = adapter->FirstGatewayAddress; address; address = address->Next)
        {
            if (address->Address.lpSockaddr->sa_family == AF_INET)
            {
                firstGateway = SocketAddressText(address->Address);
                break;
            }
        }
        for (const IP_ADAPTER_DNS_SERVER_ADDRESS *address = adapter->FirstDnsServerAddress; address; address = address->Next)
        {
            if (address->Address.lpSockaddr->sa_family != AF_INET)
                continue;
            if (dns1.empty())
                dns1 = SocketAddressText(address->Address);
            else if (dns2.empty())
                dns2 = SocketAddressText(address->Address);
        }

        page.Group();
        page.Button(Str(IDS_IP_ASSIGNMENT), Str(dhcp ? IDS_DHCP : IDS_MANUAL), L"edit_20_regular", Str(IDS_EDIT),
                    [host, name, dhcp, firstIp, mask, firstGateway](int)
                    {
                        std::vector<std::wstring> options;
                        std::vector<std::wstring> labels;
                        std::vector<std::wstring> values;
                        int choice;

                        options.push_back(Str(IDS_DHCP));
                        options.push_back(Str(IDS_MANUAL));
                        labels.push_back(Str(IDS_IP_ADDRESS));
                        labels.push_back(Str(IDS_SUBNET_MASK));
                        labels.push_back(Str(IDS_GATEWAY));
                        values.push_back(firstIp);
                        values.push_back(mask);
                        values.push_back(firstGateway);
                        choice = PickFromMenu(host->Window(), options, dhcp ? 0 : 1);
                        if (choice < 0)
                            return;
                        if (choice == 1)
                        {
                            if (!InputFields(host->Window(), Str(IDS_EDIT_IP), labels, values,
                                             std::vector<bool>(labels.size(), false)))
                                return;
                            if (!ValidIpv4(values[0], false) || !ValidIpv4(values[1], false) ||
                                !ValidIpv4(values[2], true))
                            {
                                ShowError(host->Window(), ERROR_INVALID_PARAMETER);
                                return;
                            }
                        }
                        ApplyTcpip(host, name,
                                   [choice, values](TcpipConfig &config)
                                   {
                                       config.Dhcp = choice == 0;
                                       if (!config.Dhcp)
                                       {
                                           config.Address = values[0];
                                           config.Mask = values[1];
                                       }
                                       config.Parameters = WithParameter(config.Parameters, L"DefGw",
                                                                         config.Dhcp ? L"" : values[2]);
                                       config.Parameters = WithParameter(config.Parameters, L"GwMetric",
                                                                         config.Dhcp || values[2].empty() ? L"" : L"0");
                                   });
                    }).Enabled = admin;
        page.Button(Str(IDS_DNS_ASSIGNMENT), Str(manualDns.empty() ? IDS_DHCP : IDS_MANUAL), L"edit_20_regular",
                    Str(IDS_EDIT),
                    [host, name, manualDns, dns1, dns2](int)
                    {
                        std::vector<std::wstring> options;
                        std::vector<std::wstring> labels;
                        std::vector<std::wstring> values;
                        std::wstring list;
                        int choice;

                        options.push_back(Str(IDS_DHCP));
                        options.push_back(Str(IDS_MANUAL));
                        labels.push_back(Str(IDS_PREFERRED_DNS));
                        labels.push_back(Str(IDS_ALTERNATE_DNS));
                        values.push_back(dns1);
                        values.push_back(dns2);
                        choice = PickFromMenu(host->Window(), options, manualDns.empty() ? 0 : 1);
                        if (choice < 0)
                            return;
                        if (choice == 1)
                        {
                            if (!InputFields(host->Window(), Str(IDS_EDIT_DNS), labels, values,
                                             std::vector<bool>(labels.size(), false)))
                                return;
                            if (!ValidIpv4(values[0], false) || !ValidIpv4(values[1], true))
                            {
                                ShowError(host->Window(), ERROR_INVALID_PARAMETER);
                                return;
                            }
                            list = values[1].empty() ? values[0] : values[0] + L"," + values[1];
                        }
                        ApplyTcpip(host, name,
                                   [list](TcpipConfig &config)
                                   {
                                       config.Parameters = WithParameter(config.Parameters, L"DNS", list);
                                   });
                    }).Enabled = admin;
    }

    page.Group(Str(IDS_PROPERTIES));
    for (size_t i = 0; i < rows.size(); i++)
    {
        if (!rows[i].second.empty())
        {
            page.Info(rows[i].first, rows[i].second);
            text += rows[i].first + L":\t" + rows[i].second + L"\r\n";
        }
    }
    page.Button(Str(IDS_COPY_PROPERTIES), std::wstring(), L"copy_20_regular", Str(IDS_COPY),
                [host, text](int) { CopyToClipboard(host->Window(), text); });
}

static bool
QueryProxy(DWORD *flags, std::wstring &server, std::wstring &bypass, std::wstring &script)
{
    INTERNET_PER_CONN_OPTIONW options[4];
    INTERNET_PER_CONN_OPTION_LISTW list = { sizeof(list) };
    DWORD size = sizeof(list);

    options[0].dwOption = INTERNET_PER_CONN_FLAGS;
    options[1].dwOption = INTERNET_PER_CONN_PROXY_SERVER;
    options[2].dwOption = INTERNET_PER_CONN_PROXY_BYPASS;
    options[3].dwOption = INTERNET_PER_CONN_AUTOCONFIG_URL;
    list.dwOptionCount = _countof(options);
    list.pOptions = options;
    if (!InternetQueryOptionW(NULL, INTERNET_OPTION_PER_CONNECTION_OPTION, &list, &size))
        return false;
    *flags = options[0].Value.dwValue;
    server = options[1].Value.pszValue ? options[1].Value.pszValue : L"";
    bypass = options[2].Value.pszValue ? options[2].Value.pszValue : L"";
    script = options[3].Value.pszValue ? options[3].Value.pszValue : L"";
    for (UINT i = 1; i < _countof(options); i++)
    {
        if (options[i].Value.pszValue)
            GlobalFree(options[i].Value.pszValue);
    }
    return true;
}

static void
StoreProxy(HWND owner, DWORD flags, const std::wstring &server, const std::wstring &bypass, const std::wstring &script)
{
    INTERNET_PER_CONN_OPTIONW options[4];
    INTERNET_PER_CONN_OPTION_LISTW list = { sizeof(list) };

    options[0].dwOption = INTERNET_PER_CONN_FLAGS;
    options[0].Value.dwValue = flags;
    options[1].dwOption = INTERNET_PER_CONN_PROXY_SERVER;
    options[1].Value.pszValue = (LPWSTR)server.c_str();
    options[2].dwOption = INTERNET_PER_CONN_PROXY_BYPASS;
    options[2].Value.pszValue = (LPWSTR)bypass.c_str();
    options[3].dwOption = INTERNET_PER_CONN_AUTOCONFIG_URL;
    options[3].Value.pszValue = (LPWSTR)script.c_str();
    list.dwOptionCount = _countof(options);
    list.pOptions = options;
    if (!InternetSetOptionW(NULL, INTERNET_OPTION_PER_CONNECTION_OPTION, &list, sizeof(list)))
    {
        ShowError(owner, GetLastError());
        return;
    }
    InternetSetOptionW(NULL, INTERNET_OPTION_SETTINGS_CHANGED, NULL, 0);
    InternetSetOptionW(NULL, INTERNET_OPTION_REFRESH, NULL, 0);
}

void
BuildProxy(PageBuilder &page)
{
    SettingsHost *host = page.Host;
    DWORD flags = PROXY_TYPE_DIRECT;
    std::wstring server, bypass, script;
    bool local;

    if (!page.Indexing)
        QueryProxy(&flags, server, bypass, script);
    local = bypass.find(L"<local>") != std::wstring::npos;

    page.Group(Str(IDS_PROXY_AUTOMATIC));
    page.Toggle(Str(IDS_PROXY_DETECT), Str(IDS_PROXY_DETECT_DESC), L"globe_20_regular",
                (flags & PROXY_TYPE_AUTO_DETECT) != 0,
                [host, flags, server, bypass, script](int on)
                {
                    StoreProxy(host->Window(), on ? (flags | PROXY_TYPE_AUTO_DETECT) : (flags & ~PROXY_TYPE_AUTO_DETECT),
                               server, bypass, script);
                });
    page.Toggle(Str(IDS_PROXY_SCRIPT), script.empty() ? Str(IDS_PROXY_SCRIPT_DESC) : script, L"code_20_regular",
                (flags & PROXY_TYPE_AUTO_PROXY_URL) != 0,
                [host, flags, server, bypass, script](int on)
                {
                    std::wstring address = script;
                    if (on && address.empty() &&
                        !InputBox(host->Window(), Str(IDS_PROXY_SCRIPT), Str(IDS_PROXY_SCRIPT_ADDRESS), address))
                        return;
                    StoreProxy(host->Window(),
                               on ? (flags | PROXY_TYPE_AUTO_PROXY_URL) : (flags & ~PROXY_TYPE_AUTO_PROXY_URL), server,
                               bypass, address);
                });
    page.Button(Str(IDS_PROXY_SCRIPT_ADDRESS), script, NULL, Str(IDS_EDIT),
                [host, flags, server, bypass, script](int)
                {
                    std::wstring address = script;
                    if (InputBox(host->Window(), Str(IDS_PROXY_SCRIPT), Str(IDS_PROXY_SCRIPT_ADDRESS), address))
                        StoreProxy(host->Window(), flags, server, bypass, address);
                });

    page.Group(Str(IDS_PROXY_MANUAL));
    page.Toggle(Str(IDS_PROXY_USE), Str(IDS_PROXY_USE_DESC), L"globe_shield_20_regular", (flags & PROXY_TYPE_PROXY) != 0,
                [host, flags, server, bypass, script](int on)
                {
                    std::wstring address = server;
                    if (on && address.empty() &&
                        !InputBox(host->Window(), Str(IDS_PROXY_MANUAL), Str(IDS_PROXY_SERVER_PROMPT), address))
                        return;
                    StoreProxy(host->Window(), on ? (flags | PROXY_TYPE_PROXY) : (flags & ~PROXY_TYPE_PROXY), address,
                               bypass, script);
                });
    page.Button(Str(IDS_PROXY_SERVER), server.empty() ? Str(IDS_NOT_SET) : server, NULL, Str(IDS_EDIT),
                [host, flags, server, bypass, script](int)
                {
                    std::wstring address = server;
                    if (InputBox(host->Window(), Str(IDS_PROXY_MANUAL), Str(IDS_PROXY_SERVER_PROMPT), address))
                        StoreProxy(host->Window(), flags, address, bypass, script);
                });
    page.Button(Str(IDS_PROXY_EXCEPTIONS), bypass.empty() ? Str(IDS_NOT_SET) : bypass, NULL, Str(IDS_EDIT),
                [host, flags, server, bypass, script](int)
                {
                    std::wstring list = bypass;
                    if (InputBox(host->Window(), Str(IDS_PROXY_MANUAL), Str(IDS_PROXY_EXCEPTIONS_PROMPT), list))
                        StoreProxy(host->Window(), flags, server, list, script);
                });
    page.Toggle(Str(IDS_PROXY_LOCAL), std::wstring(), NULL, local,
                [host, flags, server, bypass, script](int on)
                {
                    std::wstring list = bypass;
                    size_t at = list.find(L"<local>");
                    if (on && at == std::wstring::npos)
                        list += (list.empty() ? L"" : L";") + std::wstring(L"<local>");
                    else if (!on && at != std::wstring::npos)
                    {
                        list.erase(at, 7);
                        while (!list.empty() && list[list.size() - 1] == L';')
                            list.pop_back();
                        if (!list.empty() && list[0] == L';')
                            list.erase(0, 1);
                    }
                    StoreProxy(host->Window(), flags, server, list, script);
                });
}
