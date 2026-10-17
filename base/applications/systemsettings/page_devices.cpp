/*
 * PROJECT:     LiberNT Settings
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Bluetooth and devices pages
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "settings.h"
#include <initguid.h>
#include <bthdef.h>
#include <bluetoothapis.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <devguid.h>
#include <winspool.h>

typedef INT_PTR (WINAPI *PFN_DEVICEPROPERTIES)(HWND, LPCWSTR, LPCWSTR, BOOL);

static void
ShowDeviceProperties(HWND owner, const std::wstring &instance)
{
    HMODULE module = LoadLibraryW(L"devmgr.dll");
    PFN_DEVICEPROPERTIES properties;

    if (!module)
        return;
    properties = (PFN_DEVICEPROPERTIES)GetProcAddress(module, "DevicePropertiesW");
    if (properties)
        properties(owner, NULL, instance.c_str(), FALSE);
    FreeLibrary(module);
}

static HANDLE
OpenRadio(BLUETOOTH_RADIO_INFO *info)
{
    BLUETOOTH_FIND_RADIO_PARAMS params = { sizeof(params) };
    HBLUETOOTH_RADIO_FIND find;
    HANDLE radio = NULL;

    find = BluetoothFindFirstRadio(&params, &radio);
    if (!find)
        return NULL;
    BluetoothFindRadioClose(find);
    if (info)
    {
        ZeroMemory(info, sizeof(*info));
        info->dwSize = sizeof(*info);
        if (BluetoothGetRadioInfo(radio, info) != ERROR_SUCCESS)
            info->szName[0] = UNICODE_NULL;
    }
    return radio;
}

static std::wstring
BluetoothAddressText(const BLUETOOTH_ADDRESS &address)
{
    return Printf(L"%02X:%02X:%02X:%02X:%02X:%02X", address.rgBytes[5], address.rgBytes[4], address.rgBytes[3],
                  address.rgBytes[2], address.rgBytes[1], address.rgBytes[0]);
}

static LPCWSTR
BluetoothIcon(ULONG classOfDevice)
{
    switch (GET_COD_MAJOR(classOfDevice))
    {
        case COD_MAJOR_COMPUTER: return L"laptop_20_regular";
        case COD_MAJOR_PHONE: return L"phone_laptop_20_regular";
        case COD_MAJOR_AUDIO: return L"headphones_20_regular";
        case COD_MAJOR_PERIPHERAL: return L"keyboard_20_regular";
        case COD_MAJOR_IMAGING: return L"print_20_regular";
        default: return L"bluetooth_20_regular";
    }
}

static std::vector<BLUETOOTH_DEVICE_INFO> FoundDevices;
static bool Searching;
static bool Searched;

static std::vector<BLUETOOTH_DEVICE_INFO>
BluetoothDevices(HANDLE radio, bool inquiry)
{
    std::vector<BLUETOOTH_DEVICE_INFO> devices;
    BLUETOOTH_DEVICE_SEARCH_PARAMS params = { sizeof(params) };
    BLUETOOTH_DEVICE_INFO info = { sizeof(info) };
    HBLUETOOTH_DEVICE_FIND find;

    params.fReturnAuthenticated = !inquiry;
    params.fReturnRemembered = !inquiry;
    params.fReturnConnected = !inquiry;
    params.fReturnUnknown = inquiry;
    params.fIssueInquiry = inquiry;
    params.cTimeoutMultiplier = inquiry ? 6 : 0;
    params.hRadio = radio;
    find = BluetoothFindFirstDevice(&params, &info);
    if (!find)
        return devices;
    do
    {
        devices.push_back(info);
        info.dwSize = sizeof(info);
    } while (BluetoothFindNextDevice(find, &info));
    BluetoothFindDeviceClose(find);
    return devices;
}

struct PairContext
{
    HANDLE Radio;
    HWND Owner;
};

static BOOL CALLBACK
PairCallback(void *parameter, BLUETOOTH_AUTHENTICATION_CALLBACK_PARAMS *request)
{
    PairContext *context = (PairContext *)parameter;
    BLUETOOTH_AUTHENTICATE_RESPONSE response;
    std::wstring name = request->deviceInfo.szName;

    ZeroMemory(&response, sizeof(response));
    response.bthAddressRemote = request->deviceInfo.Address;
    response.authMethod = request->authenticationMethod;
    switch (request->authenticationMethod)
    {
        case BLUETOOTH_AUTHENTICATION_METHOD_NUMERIC_COMPARISON:
        case BLUETOOTH_AUTHENTICATION_METHOD_PASSKEY_NOTIFICATION:
            response.numericCompInfo.NumericValue = request->Numeric_Value;
            response.negativeResponse = !Confirm(context->Owner, Format(IDS_PAIR_CODE, name.c_str()),
                                                  Printf(L"%06lu", request->Numeric_Value), Str(IDS_PAIR));
            break;
        case BLUETOOTH_AUTHENTICATION_METHOD_PASSKEY:
        {
            std::wstring passkey;
            response.negativeResponse = !InputBox(context->Owner, Str(IDS_PAIR), Format(IDS_PAIR_PASSKEY, name.c_str()),
                                                  passkey);
            response.passkeyInfo.passkey = wcstoul(passkey.c_str(), NULL, 10);
            break;
        }
        case BLUETOOTH_AUTHENTICATION_METHOD_LEGACY:
        {
            std::wstring pin;
            response.negativeResponse = !InputBox(context->Owner, Str(IDS_PAIR), Format(IDS_PAIR_PIN, name.c_str()), pin);
            for (size_t i = 0; i < pin.length() && i < BTH_MAX_PIN_SIZE; i++)
                response.pinInfo.pin[i] = (UCHAR)pin[i];
            response.pinInfo.pinLength = (UCHAR)min(pin.length(), (size_t)BTH_MAX_PIN_SIZE);
            break;
        }
        default:
            response.negativeResponse = TRUE;
            break;
    }
    BluetoothSendAuthenticationResponseEx(context->Radio, &response);
    return TRUE;
}

static void
PairDevice(SettingsHost *host, BLUETOOTH_DEVICE_INFO device)
{
    DWORD *result = new DWORD(ERROR_SUCCESS);

    host->RunJob([host, device, result]() mutable
                 {
                     PairContext context;
                     HBLUETOOTH_AUTHENTICATION_REGISTRATION registration = NULL;
                     context.Owner = host->Window();
                     context.Radio = OpenRadio(NULL);
                     if (!context.Radio)
                     {
                         *result = ERROR_DEVICE_NOT_CONNECTED;
                         return;
                     }
                     BluetoothRegisterForAuthenticationEx(&device, &registration, PairCallback, &context);
                     *result = BluetoothAuthenticateDeviceEx(context.Owner, context.Radio, &device, NULL,
                                                             MITMProtectionNotRequiredGeneralBonding);
                     if (registration)
                         BluetoothUnregisterAuthentication(registration);
                     CloseHandle(context.Radio);
                 },
                 [host, result]()
                 {
                     if (*result != ERROR_SUCCESS)
                         ShowError(host->Window(), *result);
                     delete result;
                     FoundDevices.clear();
                     Searched = false;
                     host->Refresh();
                 });
}

static SRWLOCK DiscoveredLock = SRWLOCK_INIT;
static std::vector<BLUETOOTH_DEVICE_INFO> Discovered;
static HCMNOTIFICATION DiscoveryNotify;
static HANDLE DiscoveryRadio;

static void
AddFoundDevice(const BLUETOOTH_DEVICE_INFO &device)
{
    for (size_t i = 0; i < FoundDevices.size(); i++)
    {
        if (FoundDevices[i].Address.ullLong == device.Address.ullLong)
        {
            if (device.szName[0] || !FoundDevices[i].szName[0])
                FoundDevices[i] = device;
            return;
        }
    }
    FoundDevices.push_back(device);
}

static DWORD CALLBACK
DeviceInRange(HCMNOTIFICATION notify, PVOID context, CM_NOTIFY_ACTION action, PCM_NOTIFY_EVENT_DATA data, DWORD size)
{
    const BTH_RADIO_IN_RANGE *range;
    BLUETOOTH_DEVICE_INFO device = { sizeof(device) };

    if (action != CM_NOTIFY_ACTION_DEVICECUSTOMEVENT ||
        size < FIELD_OFFSET(CM_NOTIFY_EVENT_DATA, u.DeviceHandle.Data) + sizeof(BTH_RADIO_IN_RANGE) ||
        !IsEqualGUID(data->u.DeviceHandle.EventGuid, GUID_BLUETOOTH_RADIO_IN_RANGE) ||
        data->u.DeviceHandle.DataSize < sizeof(BTH_RADIO_IN_RANGE))
        return ERROR_SUCCESS;
    range = (const BTH_RADIO_IN_RANGE *)data->u.DeviceHandle.Data;
    if (!(range->deviceInfo.flags & BDIF_ADDRESS) || (range->deviceInfo.flags & BDIF_PAIRED))
        return ERROR_SUCCESS;
    device.Address.ullLong = range->deviceInfo.address;
    device.ulClassofDevice = range->deviceInfo.classOfDevice;
    device.fConnected = (range->deviceInfo.flags & BDIF_CONNECTED) != 0;
    device.fRemembered = (range->deviceInfo.flags & BDIF_PERSONAL) != 0;
    if (range->deviceInfo.flags & BDIF_NAME)
        MultiByteToWideChar(CP_UTF8, 0, range->deviceInfo.name, -1, device.szName, BLUETOOTH_MAX_NAME_SIZE);
    AcquireSRWLockExclusive(&DiscoveredLock);
    Discovered.push_back(device);
    ReleaseSRWLockExclusive(&DiscoveredLock);
    PostMessageW((HWND)context, WM_SETTINGS_REFRESH, 0, 0);
    return ERROR_SUCCESS;
}

static void
WatchDiscovery(SettingsHost *host, bool on)
{
    if (on && !DiscoveryNotify)
    {
        CM_NOTIFY_FILTER filter = { sizeof(filter) };

        DiscoveryRadio = OpenRadio(NULL);
        if (!DiscoveryRadio)
            return;
        filter.FilterType = CM_NOTIFY_FILTER_TYPE_DEVICEHANDLE;
        filter.u.DeviceHandle.hTarget = DiscoveryRadio;
        if (CM_Register_Notification(&filter, host->Window(), DeviceInRange, &DiscoveryNotify) != CR_SUCCESS)
            DiscoveryNotify = NULL;
    }
    else if (!on)
    {
        if (DiscoveryNotify)
            CM_Unregister_Notification(DiscoveryNotify);
        DiscoveryNotify = NULL;
        AcquireSRWLockExclusive(&DiscoveredLock);
        Discovered.clear();
        ReleaseSRWLockExclusive(&DiscoveredLock);
    }
    if (!DiscoveryNotify && DiscoveryRadio)
    {
        CloseHandle(DiscoveryRadio);
        DiscoveryRadio = NULL;
    }
}

static void
SearchDevices(SettingsHost *host)
{
    std::vector<BLUETOOTH_DEVICE_INFO> *found = new std::vector<BLUETOOTH_DEVICE_INFO>();

    Searching = true;
    FoundDevices.clear();
    WatchDiscovery(host, true);
    host->Refresh();
    host->RunJob([found]()
                 {
                     HANDLE radio = OpenRadio(NULL);
                     if (radio)
                     {
                         *found = BluetoothDevices(radio, true);
                         CloseHandle(radio);
                     }
                 },
                 [host, found]()
                 {
                     WatchDiscovery(host, false);
                     for (size_t i = 0; i < found->size(); i++)
                         AddFoundDevice((*found)[i]);
                     delete found;
                     Searching = false;
                     Searched = true;
                     host->Refresh();
                 });
}

void
BuildBluetooth(PageBuilder &page)
{
    SettingsHost *host = page.Host;
    BLUETOOTH_RADIO_INFO info;
    HANDLE radio;
    std::vector<BLUETOOTH_DEVICE_INFO> devices;
    BOOL discoverable, connectable;

    if (page.Indexing)
    {
        page.Group();
        page.Toggle(Str(IDS_BT_DISCOVERABLE), Str(IDS_BT_DISCOVERABLE_DESC), L"eye_20_regular", false, nullptr);
        page.Toggle(Str(IDS_BT_CONNECTABLE), Str(IDS_BT_CONNECTABLE_DESC), L"bluetooth_20_regular", false, nullptr);
        page.Button(Str(IDS_BT_ADD), Str(IDS_BT_ADD_DESC), L"add_circle_20_regular", Str(IDS_ADD_DEVICE), nullptr)
            .Keywords = Str(IDS_BT_KEYWORDS);
        return;
    }
    AcquireSRWLockExclusive(&DiscoveredLock);
    for (size_t i = 0; i < Discovered.size(); i++)
        AddFoundDevice(Discovered[i]);
    Discovered.clear();
    ReleaseSRWLockExclusive(&DiscoveredLock);
    radio = OpenRadio(&info);
    if (!radio)
    {
        page.Hero(Str(IDS_PAGE_BLUETOOTH), Str(IDS_BT_NO_RADIO), L"bluetooth_20_regular");
        return;
    }
    discoverable = BluetoothIsDiscoverable(radio);
    connectable = BluetoothIsConnectable(radio);
    page.Hero(info.szName[0] ? info.szName : Str(IDS_PAGE_BLUETOOTH),
              Format(discoverable ? IDS_BT_RADIO_DISCOVERABLE : IDS_BT_RADIO, BluetoothAddressText(info.address).c_str()),
              L"bluetooth_20_regular");
    page.Group();
    page.Toggle(Str(IDS_BT_DISCOVERABLE), Str(IDS_BT_DISCOVERABLE_DESC), L"eye_20_regular", discoverable != FALSE,
                [](int on)
                {
                    HANDLE radio = OpenRadio(NULL);
                    if (radio)
                    {
                        BluetoothEnableDiscovery(radio, on != 0);
                        CloseHandle(radio);
                    }
                });
    page.Toggle(Str(IDS_BT_CONNECTABLE), Str(IDS_BT_CONNECTABLE_DESC), L"bluetooth_20_regular", connectable != FALSE,
                [](int on)
                {
                    HANDLE radio = OpenRadio(NULL);
                    if (radio)
                    {
                        BluetoothEnableIncomingConnections(radio, on != 0);
                        CloseHandle(radio);
                    }
                });

    devices = BluetoothDevices(radio, false);
    CloseHandle(radio);
    page.Group(Str(IDS_BT_DEVICES));
    if (devices.empty())
        page.Text(Str(IDS_BT_NO_DEVICES));
    for (size_t i = 0; i < devices.size(); i++)
    {
        BLUETOOTH_DEVICE_INFO device = devices[i];
        UINT state = device.fConnected ? IDS_BT_CONNECTED : (device.fAuthenticated ? IDS_BT_PAIRED : IDS_BT_REMEMBERED);
        SettingRow &row = page.Button(device.szName[0] ? device.szName : BluetoothAddressText(device.Address),
                                      Str(state), BluetoothIcon(device.ulClassofDevice), Str(IDS_REMOVE),
                                      [host, device](int)
                                      {
                                          BLUETOOTH_ADDRESS address = device.Address;
                                          if (!Confirm(host->Window(), Format(IDS_BT_REMOVE_TITLE, device.szName),
                                                       Str(IDS_BT_REMOVE_TEXT), Str(IDS_REMOVE)))
                                              return;
                                          if (BluetoothRemoveDevice(&address) != ERROR_SUCCESS)
                                              ShowError(host->Window(), ERROR_NOT_FOUND);
                                      });
        UNREFERENCED_PARAMETER(row);
    }

    page.Group(Str(IDS_BT_ADD_GROUP));
    {
        SettingRow &row = page.Button(Str(IDS_BT_ADD), Str(Searching ? IDS_BT_SEARCHING : IDS_BT_ADD_DESC),
                                      L"add_circle_20_regular", Str(IDS_SEARCH_DEVICES),
                                      [host](int) { SearchDevices(host); });
        row.Enabled = !Searching;
        row.Accent = true;
        row.Keywords = Str(IDS_BT_KEYWORDS);
    }
    for (size_t i = 0; i < FoundDevices.size(); i++)
    {
        BLUETOOTH_DEVICE_INFO device = FoundDevices[i];
        bool known = false;
        for (size_t j = 0; j < devices.size() && !known; j++)
            known = devices[j].Address.ullLong == device.Address.ullLong;
        if (known)
            continue;
        page.Button(device.szName[0] ? device.szName : BluetoothAddressText(device.Address),
                    BluetoothAddressText(device.Address), BluetoothIcon(device.ulClassofDevice), Str(IDS_PAIR),
                    [host, device](int) { PairDevice(host, device); });
    }
    if (Searched && !Searching && FoundDevices.empty())
        page.Text(Str(IDS_BT_NOTHING_FOUND));
}

static const GUID *DeviceClasses[] =
{
    &GUID_DEVCLASS_MOUSE, &GUID_DEVCLASS_KEYBOARD, &GUID_DEVCLASS_MONITOR, &GUID_DEVCLASS_MEDIA,
    &GUID_DEVCLASS_IMAGE, &GUID_DEVCLASS_PRINTER, &GUID_DEVCLASS_BLUETOOTH, &GUID_DEVCLASS_DISKDRIVE,
};

static std::wstring
DeviceText(HDEVINFO set, SP_DEVINFO_DATA *data, DWORD property)
{
    WCHAR text[256];

    if (!SetupDiGetDeviceRegistryPropertyW(set, data, property, NULL, (BYTE *)text, sizeof(text), NULL))
        return std::wstring();
    return text;
}

static void
AddDeviceList(PageBuilder &page)
{
    SettingsHost *host = page.Host;
    HDEVINFO set = SetupDiGetClassDevsW(NULL, NULL, NULL, DIGCF_PRESENT | DIGCF_ALLCLASSES);
    SP_DEVINFO_DATA data = { sizeof(data) };

    if (set == INVALID_HANDLE_VALUE)
        return;
    for (UINT c = 0; c < _countof(DeviceClasses); c++)
    {
        WCHAR className[128] = L"";
        bool header = false;

        SetupDiGetClassDescriptionW(DeviceClasses[c], className, _countof(className), NULL);
        for (DWORD i = 0; SetupDiEnumDeviceInfo(set, i, &data); i++, data.cbSize = sizeof(data))
        {
            WCHAR instance[MAX_DEVICE_ID_LEN];
            std::wstring name;
            HICON icon = NULL;

            if (!IsEqualGUID(data.ClassGuid, *DeviceClasses[c]))
                continue;
            name = DeviceText(set, &data, SPDRP_FRIENDLYNAME);
            if (name.empty())
                name = DeviceText(set, &data, SPDRP_DEVICEDESC);
            if (name.empty() || !SetupDiGetDeviceInstanceIdW(set, &data, instance, _countof(instance), NULL))
                continue;
            if (!header)
            {
                page.Group(className);
                header = true;
            }
            SetupDiLoadClassIcon(DeviceClasses[c], &icon, NULL);
            {
                std::wstring id = instance;
                SettingRow &row = page.Link(name, std::wstring(), NULL,
                                            [host, id](int) { ShowDeviceProperties(host->Window(), id); });
                row.Image = icon;
            }
        }
    }
    SetupDiDestroyDeviceInfoList(set);
}

void
BuildDevices(PageBuilder &page)
{
    std::wstring bluetooth;

    if (!page.Indexing)
    {
        HANDLE radio = OpenRadio(NULL);
        bluetooth = Str(radio ? IDS_BT_AVAILABLE : IDS_BT_NO_RADIO);
        if (radio)
            CloseHandle(radio);
    }
    page.Group();
    page.Navigate(Str(IDS_PAGE_BLUETOOTH), bluetooth.empty() ? Str(IDS_BT_DESC) : bluetooth, L"bluetooth_20_regular",
                  PageId::Bluetooth).Keywords = Str(IDS_BT_KEYWORDS);
    page.Navigate(Str(IDS_PAGE_MOUSE), Str(IDS_MOUSE_DESC), L"cursor_20_regular", PageId::Mouse);
    page.Navigate(Str(IDS_PAGE_TYPING), Str(IDS_TYPING_DESC), L"keyboard_20_regular", PageId::Typing);
    page.Navigate(Str(IDS_PAGE_PRINTERS), Str(IDS_PRINTERS_DESC), L"print_20_regular", PageId::Printers);
    if (!page.Indexing)
        AddDeviceList(page);
}

static void
SetSpiValue(UINT action, UINT uiParam, PVOID pvParam)
{
    SystemParametersInfoW(action, uiParam, pvParam, SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);
}

void
BuildMouse(PageBuilder &page)
{
    std::vector<std::wstring> buttons, wheel;
    int speed = 10, mouse[3] = { 0, 0, 0 };
    UINT lines = 3;
    BOOL swapped = GetSystemMetrics(SM_SWAPBUTTON) != 0;

    if (!page.Indexing)
    {
        SystemParametersInfoW(SPI_GETMOUSESPEED, 0, &speed, 0);
        SystemParametersInfoW(SPI_GETMOUSE, 0, mouse, 0);
        SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0);
    }
    buttons.push_back(Str(IDS_LEFT));
    buttons.push_back(Str(IDS_RIGHT));
    page.Group(Str(IDS_PAGE_MOUSE));
    page.Choice(Str(IDS_PRIMARY_BUTTON), std::wstring(), L"cursor_click_20_regular", buttons, swapped ? 1 : 0,
                [](int index)
                {
                    SwapMouseButton(index == 1);
                    SetSpiValue(SPI_SETMOUSEBUTTONSWAP, index == 1, NULL);
                });
    {
        SettingRow &row = page.Slider(Str(IDS_POINTER_SPEED), std::wstring(), L"cursor_20_regular", 1, 20, speed,
                                      [](int value) { SetSpiValue(SPI_SETMOUSESPEED, 0, (PVOID)(INT_PTR)value); });
        row.Live = true;
        row.Label = [](int value) { return Printf(L"%d", value); };
    }
    page.Toggle(Str(IDS_ENHANCE_PRECISION), Str(IDS_ENHANCE_PRECISION_DESC), L"cursor_20_regular", mouse[2] != 0,
                [](int on)
                {
                    int values[3] = { on ? 6 : 0, on ? 10 : 0, on ? 1 : 0 };
                    SetSpiValue(SPI_SETMOUSE, 0, values);
                });

    page.Group(Str(IDS_SCROLLING));
    wheel.push_back(Str(IDS_SCROLL_LINES));
    wheel.push_back(Str(IDS_SCROLL_SCREEN));
    page.Choice(Str(IDS_WHEEL_SCROLLS), std::wstring(), NULL, wheel, lines == WHEEL_PAGESCROLL ? 1 : 0,
                [](int index) { SetSpiValue(SPI_SETWHEELSCROLLLINES, index == 1 ? WHEEL_PAGESCROLL : 3, NULL); });
    {
        SettingRow &row = page.Slider(Str(IDS_SCROLL_LINE_COUNT), std::wstring(), NULL, 1, 100,
                                      lines == WHEEL_PAGESCROLL ? 3 : (int)lines,
                                      [](int value) { SetSpiValue(SPI_SETWHEELSCROLLLINES, (UINT)value, NULL); });
        row.Enabled = lines != WHEEL_PAGESCROLL;
        row.Label = [](int value) { return Printf(L"%d", value); };
    }
    {
        SettingRow &row = page.Slider(Str(IDS_DOUBLE_CLICK), Str(IDS_DOUBLE_CLICK_DESC), NULL, 200, 900,
                                      (int)GetDoubleClickTime(),
                                      [](int value) { SetSpiValue(SPI_SETDOUBLECLICKTIME, (UINT)value, NULL); });
        row.Label = [](int value) { return Format(IDS_MILLISECONDS, value); };
    }
}

void
BuildTyping(PageBuilder &page)
{
    int delay = 1, speed = 31;
    UINT blink = GetCaretBlinkTime();

    if (!page.Indexing)
    {
        SystemParametersInfoW(SPI_GETKEYBOARDDELAY, 0, &delay, 0);
        SystemParametersInfoW(SPI_GETKEYBOARDSPEED, 0, &speed, 0);
    }
    page.Group(Str(IDS_KEYBOARD_REPEAT));
    {
        SettingRow &row = page.Slider(Str(IDS_REPEAT_DELAY), Str(IDS_REPEAT_DELAY_DESC), L"keyboard_20_regular", 0, 3,
                                      3 - delay, [](int value) { SetSpiValue(SPI_SETKEYBOARDDELAY, (UINT)(3 - value), NULL); });
        row.Label = [](int value) { return Str(value <= 0 ? IDS_LONG : (value >= 3 ? IDS_SHORT : IDS_MEDIUM)); };
    }
    {
        SettingRow &row = page.Slider(Str(IDS_REPEAT_RATE), Str(IDS_REPEAT_RATE_DESC), L"keyboard_20_regular", 0, 31,
                                      speed, [](int value) { SetSpiValue(SPI_SETKEYBOARDSPEED, (UINT)value, NULL); });
        row.Label = [](int value) { return Str(value < 10 ? IDS_SLOW : (value > 22 ? IDS_FAST : IDS_MEDIUM)); };
    }
    page.Group(Str(IDS_TEXT_CURSOR));
    {
        SettingRow &row = page.Slider(Str(IDS_CURSOR_BLINK), Str(IDS_CURSOR_BLINK_DESC), NULL, 0, 10,
                                      blink == INFINITE ? 0 : max(1, 11 - (int)(blink / 100)),
                                      [](int value)
                                      {
                                          UINT time = value ? (UINT)(11 - value) * 100 : INFINITE;
                                          SetCaretBlinkTime(time);
                                          WriteString(HKEY_CURRENT_USER, L"Control Panel\\Desktop", L"CursorBlinkRate",
                                                      value ? Printf(L"%u", time) : std::wstring(L"-1"));
                                      });
        row.Label = [](int value) { return value ? Printf(L"%d", value) : Str(IDS_NONE); };
    }
    page.Group(Str(IDS_RELATED));
    page.Navigate(Str(IDS_INPUT_LANGUAGES), Str(IDS_INPUT_LANGUAGES_DESC), L"local_language_20_regular",
                  PageId::Language);
}

void
BuildPrinters(PageBuilder &page)
{
    SettingsHost *host = page.Host;
    DWORD needed = 0, count = 0;
    WCHAR defaultName[512] = L"";
    DWORD defaultSize = _countof(defaultName);
    std::vector<BYTE> buffer;

    page.Group(Str(IDS_PRINTERS));
    if (page.Indexing)
        return;
    GetDefaultPrinterW(defaultName, &defaultSize);
    EnumPrintersW(PRINTER_ENUM_LOCAL | PRINTER_ENUM_CONNECTIONS, NULL, 2, NULL, 0, &needed, &count);
    if (needed)
    {
        buffer.resize(needed);
        if (!EnumPrintersW(PRINTER_ENUM_LOCAL | PRINTER_ENUM_CONNECTIONS, NULL, 2, buffer.data(), needed, &needed,
                           &count))
            count = 0;
    }
    if (!count)
        page.Text(Str(IDS_NO_PRINTERS));
    for (DWORD i = 0; i < count; i++)
    {
        PRINTER_INFO_2W *printer = &((PRINTER_INFO_2W *)buffer.data())[i];
        std::wstring name = printer->pPrinterName;
        bool isDefault = !lstrcmpiW(printer->pPrinterName, defaultName);
        SettingRow &row = page.Button(name, Str(isDefault ? IDS_DEFAULT_PRINTER : (printer->Status ? IDS_PRINTER_BUSY :
                                                                                  IDS_PRINTER_READY)),
                                      L"print_20_regular", std::wstring(),
                                      [host, name, isDefault](int action)
                                      {
                                          if (!isDefault && action == 0)
                                          {
                                              if (!SetDefaultPrinterW(name.c_str()))
                                                  ShowError(host->Window(), GetLastError());
                                              return;
                                          }
                                          if (Confirm(host->Window(), Format(IDS_REMOVE_PRINTER_TITLE, name.c_str()),
                                                      Str(IDS_REMOVE_PRINTER_TEXT), Str(IDS_REMOVE)))
                                          {
                                              HANDLE handle = NULL;
                                              PRINTER_DEFAULTSW defaults = { NULL, NULL, PRINTER_ALL_ACCESS };
                                              if (!OpenPrinterW((LPWSTR)name.c_str(), &handle, &defaults) ||
                                                  !DeletePrinter(handle))
                                                  ShowError(host->Window(), GetLastError());
                                              if (handle)
                                                  ClosePrinter(handle);
                                          }
                                      });
        if (!isDefault)
            row.Actions.push_back(Str(IDS_SET_DEFAULT));
        row.Actions.push_back(Str(IDS_REMOVE));
    }
}
