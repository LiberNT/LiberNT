/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test USB keyboards, mice and tablets, behind a hub too, through HID and raw input
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>

#include <windows.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <hidsdi.h>

#define QEMU_VID            0x0627
#define QEMU_HID_PID        0x0001
#define HUB_VID             0x0409
#define HUB_PID             0x55aa
#define TABLET_X1           0x2000
#define TABLET_Y1           0x1000
#define TABLET_X2           0x4000
#define TABLET_Y2           0x3000
#define REL_X               7
#define REL_Y               (-3)
#define INPUT_WAIT_MS       20000

typedef enum _HID_KIND
{
    HidKeyboard,
    HidMouse,
    HidTablet,
    HidKinds
} HID_KIND;

typedef struct _HID_EXPECTED
{
    LPCWSTR Product;
    LPCWSTR Serial;
    LPCWSTR CollectionIds;
    LPCWSTR UsbIds;
    LPCWSTR UsbCompatibleIds;
    LPCWSTR ClassGuid;
    LPCWSTR Service;
    LPCWSTR RawGuid;
    DWORD RawType;
    USHORT Usage;
    USHORT InputLength;
    USHORT OutputLength;
    USHORT LinkNodes;
    USHORT InputButtonCaps;
    USHORT InputValueCaps;
    USHORT InputDataIndices;
    USHORT OutputButtonCaps;
    USHORT OutputDataIndices;
    ULONG Port;
    BOOLEAN BehindHub;
} HID_EXPECTED;

typedef struct _HID_FOUND
{
    WCHAR Path[MAX_PATH];
    DEVINST Collection;
    DEVINST Device;
    HANDLE Raw;
    ULONG Count;
    ULONG HubNumber;
} HID_FOUND;

static const HID_EXPECTED Expected[HidKinds] =
{
    {
        L"QEMU USB Keyboard", L"68284-",
        L"HID\\VID_0627&PID_0001&REV_0000\0HID\\VID_0627&PID_0001\0HID\\VID_0627&UP:0001_U:0006\0"
        L"HID_DEVICE_SYSTEM_KEYBOARD\0HID_DEVICE_UP:0001_U:0006\0HID_DEVICE\0",
        L"USB\\VID_0627&PID_0001&REV_0000\0USB\\VID_0627&PID_0001\0",
        L"USB\\COMPAT_VID_0627&Class_03&SubClass_01&Prot_01\0USB\\COMPAT_VID_0627&Class_03&SubClass_01\0"
        L"USB\\COMPAT_VID_0627&Class_03\0USB\\Class_03&SubClass_01&Prot_01\0USB\\Class_03&SubClass_01\0"
        L"USB\\Class_03\0",
        L"{4d36e96b-e325-11ce-bfc1-08002be10318}", L"kbdhid",
        L"{884b96c3-56ef-11d1-bc8c-00a0c91405dd}", RIM_TYPEKEYBOARD,
        0x06, 9, 2, 1, 2, 0, 264, 1, 5, 5, FALSE
    },
    {
        L"QEMU USB Mouse", L"89126-",
        L"HID\\VID_0627&PID_0001&REV_0000\0HID\\VID_0627&PID_0001\0HID\\VID_0627&UP:0001_U:0002\0"
        L"HID_DEVICE_SYSTEM_MOUSE\0HID_DEVICE_UP:0001_U:0002\0HID_DEVICE\0",
        L"USB\\VID_0627&PID_0001&REV_0000\0USB\\VID_0627&PID_0001\0",
        L"USB\\COMPAT_VID_0627&Class_03&SubClass_01&Prot_02\0USB\\COMPAT_VID_0627&Class_03&SubClass_01\0"
        L"USB\\COMPAT_VID_0627&Class_03\0USB\\Class_03&SubClass_01&Prot_02\0USB\\Class_03&SubClass_01\0"
        L"USB\\Class_03\0",
        L"{4d36e96f-e325-11ce-bfc1-08002be10318}", L"mouhid",
        L"{378de44c-56ef-11d1-bc8c-00a0c91405dd}", RIM_TYPEMOUSE,
        0x02, 5, 0, 2, 1, 3, 8, 0, 0, 1, TRUE
    },
    {
        L"QEMU USB Tablet", L"28754-",
        L"HID\\VID_0627&PID_0001&REV_0000\0HID\\VID_0627&PID_0001\0HID\\VID_0627&UP:0001_U:0002\0"
        L"HID_DEVICE_SYSTEM_MOUSE\0HID_DEVICE_UP:0001_U:0002\0HID_DEVICE\0",
        L"USB\\VID_0627&PID_0001&REV_0000\0USB\\VID_0627&PID_0001\0",
        L"USB\\COMPAT_VID_0627&Class_03&SubClass_00&Prot_00\0USB\\COMPAT_VID_0627&Class_03&SubClass_00\0"
        L"USB\\COMPAT_VID_0627&Class_03\0USB\\Class_03&SubClass_00&Prot_00\0USB\\Class_03&SubClass_00\0"
        L"USB\\Class_03\0",
        L"{4d36e96f-e325-11ce-bfc1-08002be10318}", L"mouhid",
        L"{378de44c-56ef-11d1-bc8c-00a0c91405dd}", RIM_TYPEMOUSE,
        0x02, 7, 0, 2, 1, 3, 8, 0, 0, 6, FALSE
    },
};

static HID_FOUND Found[HidKinds];

static const WCHAR HidClassGuid[] = L"{745a17a0-74d3-11d0-b6fe-00a0c90f57da}";
static const WCHAR UsbClassGuid[] = L"{36fc9e60-c465-11cf-8056-444553540000}";

static
VOID
MultiSzToText(
    LPCWSTR List,
    PWSTR Text,
    SIZE_T Size)
{
    SIZE_T Used = 0, Length;

    Text[0] = 0;
    for (; *List && Used + 2 < Size; List += wcslen(List) + 1)
    {
        Length = min(wcslen(List), Size - Used - 2);
        if (Used)
            Text[Used++] = L'|';
        memcpy(Text + Used, List, Length * sizeof(WCHAR));
        Used += Length;
        Text[Used] = 0;
    }
}

static
BOOL
GetProperty(
    DEVINST Dev,
    ULONG Property,
    PWSTR Buffer,
    ULONG Size)
{
    ULONG Length = Size - 2 * sizeof(WCHAR);

    ZeroMemory(Buffer, Size);
    return CM_Get_DevNode_Registry_PropertyW(Dev, Property, NULL, Buffer, &Length, 0) == CR_SUCCESS;
}

static
VOID
CheckMultiSz(
    DEVINST Dev,
    ULONG Property,
    LPCWSTR Wanted,
    LPCSTR What)
{
    WCHAR Actual[1024], Text[1024], WantedText[1024];
    LPCWSTR Left, Right;
    BOOL Same = TRUE;

    GetProperty(Dev, Property, Actual, sizeof(Actual));
    MultiSzToText(Actual, Text, RTL_NUMBER_OF(Text));
    MultiSzToText(Wanted, WantedText, RTL_NUMBER_OF(WantedText));
    for (Left = Actual, Right = Wanted; *Left || *Right; Left += wcslen(Left) + 1, Right += wcslen(Right) + 1)
    {
        if (!*Left || !*Right || wcscmp(Left, Right))
        {
            Same = FALSE;
            break;
        }
    }
    ok(Same, "%s are '%S', expected '%S'\n", What, Text, WantedText);
}

static
VOID
CheckString(
    DEVINST Dev,
    ULONG Property,
    LPCWSTR Wanted,
    LPCSTR What)
{
    WCHAR Actual[512];

    GetProperty(Dev, Property, Actual, sizeof(Actual));
    ok(!_wcsicmp(Actual, Wanted), "%s is '%S', expected '%S'\n", What, Actual, Wanted);
}

static
VOID
TraceDevice(
    DEVINST Dev,
    LPCSTR What)
{
    WCHAR Id[MAX_DEVICE_ID_LEN], Service[128], Location[256], Ids[512], Compatible[512], Text[1024], Text2[1024];
    ULONG Status = 0, Problem = 0;

    Id[0] = 0;
    CM_Get_Device_IDW(Dev, Id, RTL_NUMBER_OF(Id), 0);
    GetProperty(Dev, CM_DRP_SERVICE, Service, sizeof(Service));
    GetProperty(Dev, CM_DRP_LOCATION_INFORMATION, Location, sizeof(Location));
    GetProperty(Dev, CM_DRP_HARDWAREID, Ids, sizeof(Ids));
    GetProperty(Dev, CM_DRP_COMPATIBLEIDS, Compatible, sizeof(Compatible));
    MultiSzToText(Ids, Text, RTL_NUMBER_OF(Text));
    MultiSzToText(Compatible, Text2, RTL_NUMBER_OF(Text2));
    CM_Get_DevNode_Status(&Status, &Problem, Dev, 0);
    trace("%s %S service '%S' location '%S' status %08lx problem %lu ids '%S' compatible '%S'\n", What, Id, Service,
          Location, Status, Problem, Text, Text2);
}

static
BOOL
GetLocation(
    DEVINST Dev,
    ULONG *Port,
    ULONG *Hub)
{
    WCHAR Location[256], Check[64] = { 0 };

    *Port = *Hub = 0;
    GetProperty(Dev, CM_DRP_LOCATION_INFORMATION, Location, sizeof(Location));
    if (swscanf(Location, L"Port_#%4lu.Hub_#%4lu", Port, Hub) != 2)
        return FALSE;
    _snwprintf(Check, RTL_NUMBER_OF(Check) - 1, L"Port_#%04lu.Hub_#%04lu", *Port, *Hub);
    return !wcscmp(Check, Location);
}

static
VOID
CheckStarted(
    DEVINST Dev,
    LPCSTR What)
{
    ULONG Status = 0, Problem = 0;

    ok_int(CM_Get_DevNode_Status(&Status, &Problem, Dev, 0), CR_SUCCESS);
    ok((Status & DN_STARTED) && !(Status & DN_HAS_PROBLEM), "%s is not started: status %08lx problem %lu\n", What,
       Status, Problem);
}

static
VOID
FindCollections(VOID)
{
    SP_DEVICE_INTERFACE_DATA Interface;
    PSP_DEVICE_INTERFACE_DETAIL_DATA_W Detail;
    SP_DEVINFO_DATA Info;
    HIDD_ATTRIBUTES Attributes;
    WCHAR Product[128];
    HDEVINFO Set;
    HANDLE File;
    GUID HidGuid;
    DWORD Index;
    ULONG Kind;
    BYTE Buffer[sizeof(*Detail) + MAX_PATH * sizeof(WCHAR)];

    HidD_GetHidGuid(&HidGuid);
    Set = SetupDiGetClassDevsW(&HidGuid, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    ok(Set != INVALID_HANDLE_VALUE, "SetupDiGetClassDevs failed: %lu\n", GetLastError());
    if (Set == INVALID_HANDLE_VALUE)
        return;
    Detail = (PSP_DEVICE_INTERFACE_DETAIL_DATA_W)Buffer;
    Interface.cbSize = sizeof(Interface);
    for (Index = 0; SetupDiEnumDeviceInterfaces(Set, NULL, &HidGuid, Index, &Interface); ++Index)
    {
        Detail->cbSize = sizeof(*Detail);
        Info.cbSize = sizeof(Info);
        if (!SetupDiGetDeviceInterfaceDetailW(Set, &Interface, Detail, sizeof(Buffer), NULL, &Info))
            continue;
        File = CreateFileW(Detail->DevicePath, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
        trace("HID interface %S: open with no access %s (%lu)\n", Detail->DevicePath,
              File != INVALID_HANDLE_VALUE ? "works" : "fails", GetLastError());
        if (File == INVALID_HANDLE_VALUE)
            continue;
        Attributes.Size = sizeof(Attributes);
        Product[0] = 0;
        if (HidD_GetAttributes(File, &Attributes) && Attributes.VendorID == QEMU_VID &&
            Attributes.ProductID == QEMU_HID_PID && HidD_GetProductString(File, Product, sizeof(Product)))
        {
            for (Kind = 0; Kind < HidKinds; ++Kind)
            {
                if (wcscmp(Product, Expected[Kind].Product))
                    continue;
                if (!Found[Kind].Count++)
                {
                    wcsncpy(Found[Kind].Path, Detail->DevicePath, MAX_PATH - 1);
                    Found[Kind].Collection = Info.DevInst;
                }
            }
        }
        CloseHandle(File);
    }
    SetupDiDestroyDeviceInfoList(Set);
}

static
VOID
TestDenied(
    ULONG Kind,
    DWORD Access,
    LPCSTR What)
{
    HANDLE File;
    DWORD Error;

    SetLastError(0xdeadbeef);
    File = CreateFileW(Found[Kind].Path, Access, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    Error = GetLastError();
    ok(File == INVALID_HANDLE_VALUE, "%S: open for %s worked\n", Expected[Kind].Product, What);
    ok(Error == ERROR_ACCESS_DENIED, "%S: open for %s failed with %lu\n", Expected[Kind].Product, What, Error);
    if (File != INVALID_HANDLE_VALUE)
        CloseHandle(File);
}

static
VOID
TestWriteHandle(
    ULONG Kind)
{
    const HID_EXPECTED *Want = &Expected[Kind];
    BYTE Report[64];
    DWORD Done, Error;
    HANDLE File;
    BOOL Result;

    SetLastError(0xdeadbeef);
    File = CreateFileW(Found[Kind].Path, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0,
                       NULL);
    ok(File != INVALID_HANDLE_VALUE, "%S: open for writing failed: %lu\n", Want->Product, GetLastError());
    if (File == INVALID_HANDLE_VALUE)
        return;

    ZeroMemory(Report, sizeof(Report));
    SetLastError(0xdeadbeef);
    Done = 0xdeadbeef;
    Result = ReadFile(File, Report, Want->InputLength, &Done, NULL);
    Error = GetLastError();
    ok(!Result && Error == ERROR_ACCESS_DENIED, "%S: read on a write handle gave %d, error %lu\n", Want->Product,
       Result, Error);
    ok_int(Done, 0);
    SetLastError(0xdeadbeef);
    Result = HidD_GetInputReport(File, Report, Want->InputLength);
    Error = GetLastError();
    ok(!Result && Error == ERROR_INVALID_FUNCTION, "%S: input report on a write handle gave %d, error %lu\n",
       Want->Product, Result, Error);
    ZeroMemory(Report, sizeof(Report));
    SetLastError(0xdeadbeef);
    Result = HidD_SetOutputReport(File, Report, max(Want->OutputLength, 2));
    Error = GetLastError();
    ok(!Result && Error == ERROR_INVALID_FUNCTION, "%S: output report on a write handle gave %d, error %lu\n",
       Want->Product, Result, Error);
    SetLastError(0xdeadbeef);
    Done = 0xdeadbeef;
    Result = WriteFile(File, Report, max(Want->OutputLength, 2), &Done, NULL);
    Error = GetLastError();
    ok(!Result && Error == ERROR_INVALID_FUNCTION, "%S: write on a write handle gave %d, error %lu\n",
       Want->Product, Result, Error);
    ok_int(Done, 0);
    SetLastError(0xdeadbeef);
    Result = HidD_GetFeature(File, Report, 2);
    Error = GetLastError();
    ok(!Result && Error == ERROR_INVALID_FUNCTION, "%S: feature report on a write handle gave %d, error %lu\n",
       Want->Product, Result, Error);
    SetLastError(0xdeadbeef);
    Result = HidD_SetFeature(File, Report, 2);
    Error = GetLastError();
    ok(!Result && Error == ERROR_INVALID_FUNCTION, "%S: setting a feature report on a write handle gave %d, error %lu\n",
       Want->Product, Result, Error);
    CloseHandle(File);
}

static
VOID
TestHidApi(
    ULONG Kind)
{
    const HID_EXPECTED *Want = &Expected[Kind];
    PHIDP_PREPARSED_DATA Preparsed = NULL;
    HIDD_ATTRIBUTES Attributes;
    HIDP_CAPS Caps;
    WCHAR String[128];
    BYTE Report[64];
    ULONG Buffers = 0;
    DWORD Read, Error;
    HANDLE File;
    BOOLEAN Result;

    File = CreateFileW(Found[Kind].Path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    ok(File != INVALID_HANDLE_VALUE, "%S: open with no access failed: %lu\n", Want->Product, GetLastError());
    if (File == INVALID_HANDLE_VALUE)
        return;

    ZeroMemory(&Attributes, sizeof(Attributes));
    Attributes.Size = sizeof(Attributes);
    ok(HidD_GetAttributes(File, &Attributes), "%S: HidD_GetAttributes failed: %lu\n", Want->Product, GetLastError());
    ok_int(Attributes.Size, sizeof(Attributes));
    ok_hex(Attributes.VendorID, QEMU_VID);
    ok_hex(Attributes.ProductID, QEMU_HID_PID);
    ok_hex(Attributes.VersionNumber, 0);

    String[0] = 0;
    ok(HidD_GetManufacturerString(File, String, sizeof(String)), "%S: no manufacturer: %lu\n", Want->Product,
       GetLastError());
    ok(!wcscmp(String, L"QEMU"), "%S: manufacturer '%S'\n", Want->Product, String);
    String[0] = 0;
    ok(HidD_GetProductString(File, String, sizeof(String)), "%S: no product: %lu\n", Want->Product, GetLastError());
    ok(!wcscmp(String, Want->Product), "Product '%S', expected '%S'\n", String, Want->Product);
    String[0] = 0;
    ok(HidD_GetSerialNumberString(File, String, sizeof(String)), "%S: no serial number: %lu\n", Want->Product,
       GetLastError());
    trace("%S: serial number '%S'\n", Want->Product, String);
    ok(!wcsncmp(String, Want->Serial, wcslen(Want->Serial)), "%S: serial number '%S'\n", Want->Product, String);
    SetLastError(0xdeadbeef);
    Result = HidD_GetProductString(File, String, 4);
    Error = GetLastError();
    ok(!Result, "%S: product into 4 bytes worked\n", Want->Product);
    ok(Error == ERROR_INVALID_USER_BUFFER, "%S: product into 4 bytes failed with %lu\n", Want->Product, Error);

    ok(HidD_GetPreparsedData(File, &Preparsed), "%S: HidD_GetPreparsedData failed: %lu\n", Want->Product,
       GetLastError());
    if (Preparsed)
    {
        ZeroMemory(&Caps, sizeof(Caps));
        ok_hex(HidP_GetCaps(Preparsed, &Caps), HIDP_STATUS_SUCCESS);
        trace("%S: page %x usage %x input %u output %u feature %u nodes %u in %u/%u/%u out %u/%u/%u feat %u/%u/%u\n",
              Want->Product, Caps.UsagePage, Caps.Usage, Caps.InputReportByteLength, Caps.OutputReportByteLength,
              Caps.FeatureReportByteLength, Caps.NumberLinkCollectionNodes, Caps.NumberInputButtonCaps,
              Caps.NumberInputValueCaps, Caps.NumberInputDataIndices, Caps.NumberOutputButtonCaps,
              Caps.NumberOutputValueCaps, Caps.NumberOutputDataIndices, Caps.NumberFeatureButtonCaps,
              Caps.NumberFeatureValueCaps, Caps.NumberFeatureDataIndices);
        ok_hex(Caps.UsagePage, 0x01);
        ok_hex(Caps.Usage, Want->Usage);
        ok_int(Caps.InputReportByteLength, Want->InputLength);
        ok_int(Caps.OutputReportByteLength, Want->OutputLength);
        ok_int(Caps.FeatureReportByteLength, 0);
        ok_int(Caps.NumberLinkCollectionNodes, Want->LinkNodes);
        ok_int(Caps.NumberInputButtonCaps, Want->InputButtonCaps);
        ok_int(Caps.NumberInputValueCaps, Want->InputValueCaps);
        ok_int(Caps.NumberInputDataIndices, Want->InputDataIndices);
        ok_int(Caps.NumberOutputButtonCaps, Want->OutputButtonCaps);
        ok_int(Caps.NumberOutputValueCaps, 0);
        ok_int(Caps.NumberOutputDataIndices, Want->OutputDataIndices);
        ok_int(Caps.NumberFeatureButtonCaps, 0);
        ok_int(Caps.NumberFeatureValueCaps, 0);
        ok_int(Caps.NumberFeatureDataIndices, 0);
        ok(HidD_FreePreparsedData(Preparsed), "%S: HidD_FreePreparsedData failed\n", Want->Product);
    }

    ok(HidD_GetNumInputBuffers(File, &Buffers), "%S: HidD_GetNumInputBuffers failed: %lu\n", Want->Product,
       GetLastError());
    ok_int(Buffers, 32);
    ZeroMemory(Report, sizeof(Report));
    SetLastError(0xdeadbeef);
    Result = HidD_GetInputReport(File, Report, Want->InputLength);
    Error = GetLastError();
    ok(!Result, "%S: input report with no access worked\n", Want->Product);
    ok(Error == ERROR_INVALID_FUNCTION, "%S: input report with no access failed with %lu\n", Want->Product, Error);
    SetLastError(0xdeadbeef);
    Read = 0xdeadbeef;
    Result = (BOOLEAN)ReadFile(File, Report, Want->InputLength, &Read, NULL);
    Error = GetLastError();
    ok(!Result, "%S: read with no access worked\n", Want->Product);
    ok(Error == ERROR_ACCESS_DENIED, "%S: read with no access failed with %lu\n", Want->Product, Error);
    ok_int(Read, 0);
    CloseHandle(File);

    TestDenied(Kind, GENERIC_READ, "reading");
    TestDenied(Kind, GENERIC_READ | GENERIC_WRITE, "reading and writing");
    TestWriteHandle(Kind);
}

static
VOID
TestTree(
    ULONG Kind)
{
    const HID_EXPECTED *Want = &Expected[Kind];
    WCHAR Id[MAX_DEVICE_ID_LEN], Serial[128];
    DEVINST Hub = 0, Root = 0;
    ULONG Port, HubNumber;
    HANDLE File;
    PWSTR Instance;

    TraceDevice(Found[Kind].Collection, "Collection");
    CheckStarted(Found[Kind].Collection, "HID collection");
    CheckMultiSz(Found[Kind].Collection, CM_DRP_HARDWAREID, Want->CollectionIds, "Collection hardware IDs");
    CheckString(Found[Kind].Collection, CM_DRP_CLASSGUID, Want->ClassGuid, "Collection class");
    CheckString(Found[Kind].Collection, CM_DRP_SERVICE, Want->Service, "Collection service");

    ok_int(CM_Get_Parent(&Found[Kind].Device, Found[Kind].Collection, 0), CR_SUCCESS);
    TraceDevice(Found[Kind].Device, "USB device");
    CheckStarted(Found[Kind].Device, "USB HID device");
    CheckMultiSz(Found[Kind].Device, CM_DRP_HARDWAREID, Want->UsbIds, "USB device hardware IDs");
    CheckMultiSz(Found[Kind].Device, CM_DRP_COMPATIBLEIDS, Want->UsbCompatibleIds, "USB device compatible IDs");
    CheckString(Found[Kind].Device, CM_DRP_CLASSGUID, HidClassGuid, "USB device class");
    CheckString(Found[Kind].Device, CM_DRP_SERVICE, L"HidUsb", "USB device service");
    ok(GetLocation(Found[Kind].Device, &Port, &Found[Kind].HubNumber), "%S has no port location\n", Want->Product);
    ok_int(Port, Want->Port);

    Id[0] = Serial[0] = 0;
    CM_Get_Device_IDW(Found[Kind].Device, Id, RTL_NUMBER_OF(Id), 0);
    File = CreateFileW(Found[Kind].Path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (File != INVALID_HANDLE_VALUE)
    {
        HidD_GetSerialNumberString(File, Serial, sizeof(Serial));
        CloseHandle(File);
    }
    Instance = wcsrchr(Id, L'\\');
    ok(Instance && !_wcsicmp(Instance + 1, Serial), "USB device %S is not named after its serial number '%S'\n", Id,
       Serial);

    ok_int(CM_Get_Parent(&Hub, Found[Kind].Device, 0), CR_SUCCESS);
    TraceDevice(Hub, "Parent");
    CheckStarted(Hub, "USB hub");
    CheckString(Hub, CM_DRP_CLASSGUID, UsbClassGuid, "Hub class");
    if (Want->BehindHub)
    {
        CheckMultiSz(Hub, CM_DRP_HARDWAREID, L"USB\\VID_0409&PID_55AA&REV_0101\0USB\\VID_0409&PID_55AA\0",
                     "Hub hardware IDs");
        CheckMultiSz(Hub, CM_DRP_COMPATIBLEIDS, L"USB\\USB20_HUB\0", "Hub compatible IDs");
        Id[0] = 0;
        CM_Get_Device_IDW(Hub, Id, RTL_NUMBER_OF(Id), 0);
        ok(!wcsncmp(Id, L"USB\\VID_0409&PID_55AA\\MSFT20314159-", 35), "Hub instance %S\n", Id);
        ok(GetLocation(Hub, &Port, &HubNumber), "The hub has no port location\n");
        ok_int(Port, 5);
        ok(HubNumber != Found[Kind].HubNumber, "The hub and the device behind it are both on hub %lu\n", HubNumber);
        ok_int(CM_Get_Parent(&Root, Hub, 0), CR_SUCCESS);
        TraceDevice(Root, "Hub parent");
    }
    else
    {
        Root = Hub;
    }
    CheckMultiSz(Root, CM_DRP_HARDWAREID,
                 L"USB\\ROOT_HUB30&VID1B36&PID000D&REV0001\0USB\\ROOT_HUB30&VID1B36&PID000D\0USB\\ROOT_HUB30\0",
                 "Root hub hardware IDs");
    CheckMultiSz(Root, CM_DRP_COMPATIBLEIDS, L"\0", "Root hub compatible IDs");
    Id[0] = 0;
    CM_Get_Device_IDW(Root, Id, RTL_NUMBER_OF(Id), 0);
    ok(!wcsncmp(Id, L"USB\\ROOT_HUB30\\", 15), "Root hub instance %S\n", Id);
    CheckString(Root, CM_DRP_CLASSGUID, UsbClassGuid, "Root hub class");
    CheckStarted(Root, "Root hub");
}

static
BOOL
SameInstance(
    LPCWSTR RawName,
    LPCWSTR Path,
    LPCWSTR Guid)
{
    LPCWSTR RawEnd = wcsrchr(RawName, L'#'), PathEnd = wcsrchr(Path, L'#');

    if (!RawEnd || !PathEnd || RawEnd - RawName != PathEnd - Path)
        return FALSE;
    return !_wcsnicmp(RawName + 4, Path + 4, RawEnd - RawName - 4) && !_wcsicmp(RawEnd + 1, Guid);
}

static
VOID
TestRawDevices(VOID)
{
    PRAWINPUTDEVICELIST List;
    RID_DEVICE_INFO Info;
    WCHAR Name[MAX_PATH];
    UINT Count = 0, Size, Index, Result;
    ULONG Kind;

    Result = GetRawInputDeviceList(NULL, &Count, sizeof(*List));
    ok_int(Result, 0);
    ok_int(Count, HidKinds);
    List = HeapAlloc(GetProcessHeap(), 0, (Count + 4) * sizeof(*List));
    if (!List)
        return;
    Size = Count + 4;
    Result = GetRawInputDeviceList(List, &Size, sizeof(*List));
    ok_int(Result, Count);
    for (Index = 0; Index < Result && Index < Count; ++Index)
    {
        Size = RTL_NUMBER_OF(Name);
        Name[0] = 0;
        ok(GetRawInputDeviceInfoW(List[Index].hDevice, RIDI_DEVICENAME, Name, &Size) != (UINT)-1,
           "No name for raw input device %p\n", List[Index].hDevice);
        trace("Raw input device %p type %lu '%S'\n", List[Index].hDevice, List[Index].dwType, Name);
        for (Kind = 0; Kind < HidKinds; ++Kind)
        {
            if (!Found[Kind].Count || !SameInstance(Name, Found[Kind].Path, Expected[Kind].RawGuid))
                continue;
            ok(!Found[Kind].Raw, "%S has two raw input devices\n", Expected[Kind].Product);
            Found[Kind].Raw = List[Index].hDevice;
            ok_int(List[Index].dwType, Expected[Kind].RawType);

            Size = 0;
            ok_int(GetRawInputDeviceInfoW(List[Index].hDevice, RIDI_DEVICENAME, NULL, &Size), 0);
            ok_int(Size, wcslen(Name) + 1);

            ZeroMemory(&Info, sizeof(Info));
            Info.cbSize = sizeof(Info);
            Size = sizeof(Info);
            ok_int(GetRawInputDeviceInfoW(List[Index].hDevice, RIDI_DEVICEINFO, &Info, &Size), sizeof(Info));
            ok_int(Info.dwType, Expected[Kind].RawType);
            if (Info.dwType == RIM_TYPEKEYBOARD)
            {
                ok_int(Info.keyboard.dwType, 81);
                ok_int(Info.keyboard.dwSubType, 0);
                ok_int(Info.keyboard.dwKeyboardMode, 1);
                ok_int(Info.keyboard.dwNumberOfFunctionKeys, 12);
                ok_int(Info.keyboard.dwNumberOfIndicators, 3);
                ok_int(Info.keyboard.dwNumberOfKeysTotal, 264);
            }
            else
            {
                ok_int(Info.mouse.dwId, 256);
                ok_int(Info.mouse.dwNumberOfButtons, 5);
                ok_int(Info.mouse.dwSampleRate, 0);
                ok_int(Info.mouse.fHasHorizontalWheel, FALSE);
            }
            Size = 0;
            ok_int(GetRawInputDeviceInfoW(List[Index].hDevice, RIDI_PREPARSEDDATA, NULL, &Size), 0);
            ok_int(Size, 0);
            Result = Count;
        }
    }
    HeapFree(GetProcessHeap(), 0, List);
    for (Kind = 0; Kind < HidKinds; ++Kind)
        ok(Found[Kind].Raw != NULL, "%S has no raw input device\n", Expected[Kind].Product);
}

typedef struct _INPUT_SEEN
{
    ULONG Messages;
    ULONG Sink;
    BOOL KeyDown;
    BOOL KeyUp;
    BOOL Relative;
    BOOL Absolute;
    BOOL Wheel;
    BOOL SideDown;
    BOOL SideUp;
} INPUT_SEEN;

static INPUT_SEEN Seen;

static
VOID
CheckRawInput(
    WPARAM wParam,
    HRAWINPUT Handle)
{
    RAWINPUTHEADER Header;
    RAWINPUT Input;
    UINT Size;

    ++Seen.Messages;
    if (GET_RAWINPUT_CODE_WPARAM(wParam) == RIM_INPUTSINK)
        ++Seen.Sink;
    Size = sizeof(Header);
    ok_int(GetRawInputData(Handle, RID_HEADER, &Header, &Size, sizeof(Header)), sizeof(Header));
    Size = 0;
    ok_int(GetRawInputData(Handle, RID_INPUT, NULL, &Size, sizeof(Header)), 0);
    ok_int(Size, Header.dwSize);
    Size = sizeof(Input);
    ok_int(GetRawInputData(Handle, RID_INPUT, &Input, &Size, sizeof(Header)), Header.dwSize);
    ok(Input.header.hDevice == Header.hDevice, "Header device %p, input device %p\n", Header.hDevice,
       Input.header.hDevice);
    ok_hex(Input.header.wParam, wParam);

    if (Input.header.dwType == RIM_TYPEKEYBOARD)
    {
        ok_int(Header.dwSize, FIELD_OFFSET(RAWINPUT, data) + sizeof(RAWKEYBOARD));
        if (Input.header.hDevice != Found[HidKeyboard].Raw)
        {
            trace("Keyboard input from %p\n", Input.header.hDevice);
            return;
        }
        if (Seen.KeyDown && Seen.KeyUp)
            return;
        trace("Key make %x flags %x vkey %x message %x extra %lx wparam %Ix\n", Input.data.keyboard.MakeCode,
              Input.data.keyboard.Flags, Input.data.keyboard.VKey, Input.data.keyboard.Message,
              Input.data.keyboard.ExtraInformation, wParam);
        ok_hex(Input.data.keyboard.MakeCode, 0x46);
        ok_hex(Input.data.keyboard.VKey, VK_SCROLL);
        ok_hex(Input.data.keyboard.Reserved, 0);
        ok_hex(Input.data.keyboard.ExtraInformation, 0);
        if (Input.data.keyboard.Flags == RI_KEY_MAKE)
        {
            ok_hex(Input.data.keyboard.Message, WM_KEYDOWN);
            Seen.KeyDown = TRUE;
        }
        else
        {
            ok_hex(Input.data.keyboard.Flags, RI_KEY_BREAK);
            ok_hex(Input.data.keyboard.Message, WM_KEYUP);
            Seen.KeyUp = Seen.KeyDown;
        }
        return;
    }

    ok_int(Input.header.dwType, RIM_TYPEMOUSE);
    if (Input.header.dwType != RIM_TYPEMOUSE)
        return;
    ok_int(Header.dwSize, FIELD_OFFSET(RAWINPUT, data) + sizeof(RAWMOUSE));
    trace("Mouse %p flags %x buttons %x data %x raw %lx x %ld y %ld extra %lx wparam %Ix\n", Input.header.hDevice,
          Input.data.mouse.usFlags, Input.data.mouse.usButtonFlags, Input.data.mouse.usButtonData,
          Input.data.mouse.ulRawButtons, Input.data.mouse.lLastX, Input.data.mouse.lLastY,
          Input.data.mouse.ulExtraInformation, wParam);
    if (Input.header.hDevice == Found[HidMouse].Raw && Input.data.mouse.usFlags == MOUSE_MOVE_RELATIVE &&
        (Input.data.mouse.lLastX || Input.data.mouse.lLastY) && !Seen.Relative)
    {
        ok_int(Input.data.mouse.lLastX, REL_X);
        ok_int(Input.data.mouse.lLastY, REL_Y);
        ok_hex(Input.data.mouse.usButtonFlags, 0);
        Seen.Relative = TRUE;
    }
    if (Input.header.hDevice == Found[HidTablet].Raw && (Input.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE) &&
        !Seen.Absolute && !Input.data.mouse.usButtonFlags)
    {
        ok_hex(Input.data.mouse.usFlags, MOUSE_MOVE_ABSOLUTE);
        ok((Input.data.mouse.lLastX == 16384 && Input.data.mouse.lLastY == 8192) ||
           (Input.data.mouse.lLastX == 32768 && Input.data.mouse.lLastY == 24576),
           "Absolute move to %ld,%ld\n", Input.data.mouse.lLastX, Input.data.mouse.lLastY);
        Seen.Absolute = TRUE;
    }
    if ((Input.header.hDevice == Found[HidMouse].Raw || Input.header.hDevice == Found[HidTablet].Raw) &&
        (Input.data.mouse.usButtonFlags & RI_MOUSE_WHEEL) && !Seen.Wheel)
    {
        ok_hex(Input.data.mouse.usButtonData, WHEEL_DELTA);
        Seen.Wheel = TRUE;
    }
    if ((Input.header.hDevice == Found[HidMouse].Raw || Input.header.hDevice == Found[HidTablet].Raw) &&
        (Input.data.mouse.usButtonFlags & RI_MOUSE_BUTTON_4_DOWN))
        Seen.SideDown = TRUE;
    if (Seen.SideDown && (Input.header.hDevice == Found[HidMouse].Raw || Input.header.hDevice == Found[HidTablet].Raw) &&
        (Input.data.mouse.usButtonFlags & RI_MOUSE_BUTTON_4_UP))
        Seen.SideUp = TRUE;
}

static
LRESULT
CALLBACK
InputWindowProc(
    HWND hWnd,
    UINT Message,
    WPARAM wParam,
    LPARAM lParam)
{
    if (Message == WM_INPUT)
        CheckRawInput(wParam, (HRAWINPUT)lParam);
    return DefWindowProcW(hWnd, Message, wParam, lParam);
}

static
BOOL
AllSeen(VOID)
{
    return Seen.KeyDown && Seen.KeyUp && Seen.Relative && Seen.Absolute && Seen.Wheel && Seen.SideDown && Seen.SideUp;
}

static
VOID
TestInput(VOID)
{
    RAWINPUTDEVICE Devices[2];
    WNDCLASSW Class = { 0 };
    DWORD Start, Last = 0;
    HWND Window;
    MSG Msg;

    Class.lpfnWndProc = InputWindowProc;
    Class.hInstance = GetModuleHandleW(NULL);
    Class.lpszClassName = L"UsbClassHidInput";
    ok(RegisterClassW(&Class) != 0, "RegisterClass failed: %lu\n", GetLastError());
    Window = CreateWindowExW(0, Class.lpszClassName, L"usbclass", WS_OVERLAPPEDWINDOW, 0, 0, 100, 100, NULL, NULL,
                             Class.hInstance, NULL);
    ok(Window != NULL, "CreateWindow failed: %lu\n", GetLastError());
    if (!Window)
        return;

    Devices[0].usUsagePage = 0x01;
    Devices[0].usUsage = 0x06;
    Devices[0].dwFlags = RIDEV_INPUTSINK;
    Devices[0].hwndTarget = Window;
    Devices[1] = Devices[0];
    Devices[1].usUsage = 0x02;
    ok(RegisterRawInputDevices(Devices, 2, sizeof(Devices[0])), "RegisterRawInputDevices failed: %lu\n",
       GetLastError());

    Start = GetTickCount();
    while (!AllSeen() && GetTickCount() - Start < INPUT_WAIT_MS)
    {
        if (!Last || GetTickCount() - Last >= 1000)
        {
            Last = GetTickCount();
            OutputDebugStringA("usbclass: inject hid input\n");
            trace("Waiting for input: key %d/%d relative %d absolute %d wheel %d side %d/%d\n", Seen.KeyDown,
                  Seen.KeyUp, Seen.Relative, Seen.Absolute, Seen.Wheel, Seen.SideDown, Seen.SideUp);
            fflush(stdout);
        }
        MsgWaitForMultipleObjects(0, NULL, FALSE, 100, QS_ALLINPUT);
        while (PeekMessageW(&Msg, NULL, 0, 0, PM_REMOVE))
            DispatchMessageW(&Msg);
    }
    trace("%lu raw input messages, %lu while in the background, after %lu ms\n", Seen.Messages, Seen.Sink,
          GetTickCount() - Start);
    ok(Seen.KeyDown, "No key press from the USB keyboard\n");
    ok(Seen.KeyUp, "No key release from the USB keyboard\n");
    ok(Seen.Relative, "No relative move from the USB mouse behind the hub\n");
    ok(Seen.Absolute, "No absolute move from the USB tablet\n");
    ok(Seen.Wheel, "No wheel turn\n");
    ok(Seen.SideDown && Seen.SideUp, "No side button click\n");
    ok(Seen.Messages && Seen.Sink == Seen.Messages, "%lu of %lu raw input messages came as RIM_INPUTSINK\n",
       Seen.Sink, Seen.Messages);

    Devices[0].dwFlags = Devices[1].dwFlags = RIDEV_REMOVE;
    Devices[0].hwndTarget = Devices[1].hwndTarget = NULL;
    ok(RegisterRawInputDevices(Devices, 2, sizeof(Devices[0])), "Removing the raw input registration failed: %lu\n",
       GetLastError());
    DestroyWindow(Window);
    UnregisterClassW(Class.lpszClassName, Class.hInstance);
}

START_TEST(Hid)
{
    ULONG Kind;

    FindCollections();
    if (!Found[HidKeyboard].Count || !Found[HidMouse].Count || !Found[HidTablet].Count)
    {
        skip("The QEMU USB keyboard, tablet and mouse behind a hub are not all present\n");
        return;
    }
    for (Kind = 0; Kind < HidKinds; ++Kind)
    {
        ok(Found[Kind].Count == 1, "%lu HID collections for %S\n", Found[Kind].Count, Expected[Kind].Product);
        trace("%S: %S\n", Expected[Kind].Product, Found[Kind].Path);
    }
    for (Kind = 0; Kind < HidKinds; ++Kind)
    {
        TestTree(Kind);
        TestHidApi(Kind);
    }
    ok(Found[HidKeyboard].HubNumber && Found[HidKeyboard].HubNumber == Found[HidTablet].HubNumber,
       "The keyboard is on hub %lu, the tablet on hub %lu\n", Found[HidKeyboard].HubNumber,
       Found[HidTablet].HubNumber);
    ok(Found[HidMouse].HubNumber != Found[HidKeyboard].HubNumber, "The mouse shares hub %lu with the keyboard\n",
       Found[HidMouse].HubNumber);
    ok(!_wcsicmp(Found[HidKeyboard].Path + wcslen(Found[HidKeyboard].Path) - 4, L"\\kbd"),
       "The keyboard interface %S has no KBD reference string\n", Found[HidKeyboard].Path);
    TestRawDevices();
    TestInput();
}
