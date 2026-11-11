/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test WinUSB enumeration, descriptors, interface queries and policies
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "winusb_test.h"
#include <devpkey.h>

static VOID JoinMultiSz(PCWSTR MultiSz, PWSTR Out, ULONG OutChars)
{
    ULONG Used = 0;

    Out[0] = UNICODE_NULL;
    while (*MultiSz && Used + 2 < OutChars)
    {
        if (Used)
            Out[Used++] = L';';
        while (*MultiSz && Used + 1 < OutChars)
            Out[Used++] = *MultiSz++;
        MultiSz++;
    }
    Out[Used] = UNICODE_NULL;
}

static BOOL GetDeviceString(HDEVINFO Set, PSP_DEVINFO_DATA Info, DWORD Property, PWSTR Out, ULONG OutChars)
{
    WCHAR Buffer[1024];
    DWORD Type = 0;

    ZeroMemory(Buffer, sizeof(Buffer));
    Out[0] = UNICODE_NULL;
    if (!SetupDiGetDeviceRegistryPropertyW(Set, Info, Property, &Type, (PBYTE)Buffer,
                                           sizeof(Buffer) - 2 * sizeof(WCHAR), NULL))
        return FALSE;
    if (Type == REG_MULTI_SZ)
        JoinMultiSz(Buffer, Out, OutChars);
    else
        lstrcpynW(Out, Buffer, OutChars);
    return TRUE;
}

static VOID TestOneFunction(const GUID *Guid, PCWSTR ExpectedGuid, PCWSTR ExpectedHardwareIds,
                            PCWSTR ExpectedCompatibleIds, PCWSTR ExpectedInstancePrefix, PCWSTR ExpectedBusDescription)
{
    SP_DEVICE_INTERFACE_DATA Interface;
    SP_DEVINFO_DATA Info;
    BYTE DetailBuffer[sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W) + 1024];
    PSP_DEVICE_INTERFACE_DETAIL_DATA_W Detail = (PSP_DEVICE_INTERFACE_DETAIL_DATA_W)DetailBuffer;
    WCHAR Text[1024], Value[512];
    DEVPROPTYPE PropType;
    ULONG Status, Problem, Count;
    DWORD Type, Size;
    HDEVINFO Set;
    HKEY Key;
    LONG Error;

    Set = SetupDiGetClassDevsW(Guid, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    ok(Set != INVALID_HANDLE_VALUE, "SetupDiGetClassDevs failed: %lu\n", GetLastError());
    if (Set == INVALID_HANDLE_VALUE)
        return;
    Interface.cbSize = sizeof(Interface);
    for (Count = 0; SetupDiEnumDeviceInterfaces(Set, NULL, Guid, Count, &Interface); Count++)
        ;
    ok(Count == 1, "%S: %lu device interfaces\n", ExpectedGuid, Count);
    if (!SetupDiEnumDeviceInterfaces(Set, NULL, Guid, 0, &Interface))
    {
        SetupDiDestroyDeviceInfoList(Set);
        return;
    }
    Detail->cbSize = sizeof(*Detail);
    Info.cbSize = sizeof(Info);
    ok(SetupDiGetDeviceInterfaceDetailW(Set, &Interface, Detail, sizeof(DetailBuffer), NULL, &Info),
       "SetupDiGetDeviceInterfaceDetail failed: %lu\n", GetLastError());
    trace("Interface path %S\n", Detail->DevicePath);
    lstrcpynW(Text, Detail->DevicePath, ARRAYSIZE(Text));
    _wcslwr(Text);
    ok(wcsstr(Text, ExpectedGuid) != NULL, "Path %S lacks %S\n", Detail->DevicePath, ExpectedGuid);
    ok(Interface.Flags == SPINT_ACTIVE, "Interface flags %lx\n", Interface.Flags);

    ok(SetupDiGetDeviceInstanceIdW(Set, &Info, Text, ARRAYSIZE(Text), NULL), "No instance ID: %lu\n", GetLastError());
    trace("Instance %S\n", Text);
    ok(!wcsncmp(Text, ExpectedInstancePrefix, wcslen(ExpectedInstancePrefix)), "Instance %S, expected %S...\n", Text,
       ExpectedInstancePrefix);

    ok(GetDeviceString(Set, &Info, SPDRP_HARDWAREID, Text, ARRAYSIZE(Text)), "No hardware IDs\n");
    ok(!wcscmp(Text, ExpectedHardwareIds), "Hardware IDs %S\n", Text);
    ok(GetDeviceString(Set, &Info, SPDRP_COMPATIBLEIDS, Text, ARRAYSIZE(Text)), "No compatible IDs\n");
    ok(!wcscmp(Text, ExpectedCompatibleIds), "Compatible IDs %S\n", Text);
    ok(GetDeviceString(Set, &Info, SPDRP_SERVICE, Text, ARRAYSIZE(Text)), "No service\n");
    ok(!wcscmp(Text, L"WINUSB"), "Service %S\n", Text);
    ok(GetDeviceString(Set, &Info, SPDRP_CLASS, Text, ARRAYSIZE(Text)), "No class\n");
    ok(!wcscmp(Text, L"USBDevice"), "Class %S\n", Text);
    ok(GetDeviceString(Set, &Info, SPDRP_CLASSGUID, Text, ARRAYSIZE(Text)), "No class GUID\n");
    ok(!_wcsicmp(Text, L"{88bae032-5a81-49f0-bc3d-a4ff138216d6}"), "Class GUID %S\n", Text);
    ok(GetDeviceString(Set, &Info, SPDRP_DEVICEDESC, Text, ARRAYSIZE(Text)), "No description\n");
    ok(!wcscmp(Text, L"WinUsb Device"), "Description %S\n", Text);
    ok(GetDeviceString(Set, &Info, SPDRP_MFG, Text, ARRAYSIZE(Text)), "No manufacturer\n");
    ok(!wcscmp(Text, L"WinUsb Device"), "Manufacturer %S\n", Text);
    ok(GetDeviceString(Set, &Info, SPDRP_ENUMERATOR_NAME, Text, ARRAYSIZE(Text)), "No enumerator\n");
    ok(!wcscmp(Text, L"USB"), "Enumerator %S\n", Text);
    if (GetDeviceString(Set, &Info, SPDRP_LOCATION_INFORMATION, Text, ARRAYSIZE(Text)))
        trace("Location %S\n", Text);

    ZeroMemory(Text, sizeof(Text));
    PropType = 0;
    ok(SetupDiGetDevicePropertyW(Set, &Info, &DEVPKEY_Device_BusReportedDeviceDesc, &PropType, (PBYTE)Text,
                                 sizeof(Text) - sizeof(WCHAR), NULL, 0),
       "No bus reported description: %lu\n", GetLastError());
    ok(PropType == DEVPROP_TYPE_STRING, "Bus description type %lx\n", PropType);
    ok(!wcscmp(Text, ExpectedBusDescription), "Bus description %S\n", Text);

    Status = Problem = 0;
    ok_int(CM_Get_DevNode_Status(&Status, &Problem, Info.DevInst, 0), CR_SUCCESS);
    ok((Status & DN_STARTED) && !(Status & DN_HAS_PROBLEM), "Status %08lx problem %lu\n", Status, Problem);

    Key = SetupDiOpenDevRegKey(Set, &Info, DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_READ);
    ok(Key != INVALID_HANDLE_VALUE, "No device key: %lu\n", GetLastError());
    if (Key != INVALID_HANDLE_VALUE)
    {
        ZeroMemory(Value, sizeof(Value));
        Size = sizeof(Value) - 2 * sizeof(WCHAR);
        Type = 0;
        if (RegQueryValueExW(Key, L"DeviceInterfaceGUID", NULL, NULL, NULL, NULL) == ERROR_SUCCESS)
        {
            skip("The device names its interface with DeviceInterfaceGUID\n");
            RegCloseKey(Key);
            SetupDiDestroyDeviceInfoList(Set);
            return;
        }
        Error = RegQueryValueExW(Key, L"DeviceInterfaceGUIDs", NULL, &Type, (PBYTE)Value, &Size);
        ok(Error == ERROR_SUCCESS, "DeviceInterfaceGUIDs: %ld\n", Error);
        ok(Type == REG_MULTI_SZ, "DeviceInterfaceGUIDs type %lu\n", Type);
        ok(Size == (wcslen(ExpectedGuid) + 2) * sizeof(WCHAR), "DeviceInterfaceGUIDs size %lu\n", Size);
        ok(!_wcsicmp(Value, ExpectedGuid), "DeviceInterfaceGUIDs %S\n", Value);
        Size = sizeof(Value);
        Error = RegQueryValueExW(Key, L"DeviceInterfaceGUID", NULL, &Type, (PBYTE)Value, &Size);
        ok(Error == ERROR_FILE_NOT_FOUND, "DeviceInterfaceGUID: %ld\n", Error);
        RegCloseKey(Key);
    }
    SetupDiDestroyDeviceInfoList(Set);
}

START_TEST(Enum)
{
    WCHAR Path[512];
    HANDLE File, File2;
    WINUSB_INTERFACE_HANDLE Interface, Interface2;
    BOOL Result;
    DWORD Error;

    if (FindInterfacePath(&GUID_WINUSB_TEST2, 0, Path, ARRAYSIZE(Path)))
    {
        TestOneFunction(&GUID_WINUSB_TEST, L"{5d4a3d8e-91f2-4c6b-8e1a-7b2c3d4e5f60}",
                        L"USB\\VID_46F4&PID_0011&REV_0100&MI_00;USB\\VID_46F4&PID_0011&MI_00",
                        L"USB\\MS_COMP_WINUSB;USB\\COMPAT_VID_46f4&Class_ff&SubClass_57&Prot_00;"
                        L"USB\\COMPAT_VID_46f4&Class_ff&SubClass_57;USB\\COMPAT_VID_46f4&Class_ff;"
                        L"USB\\Class_ff&SubClass_57&Prot_00;USB\\Class_ff&SubClass_57;USB\\Class_ff",
                        L"USB\\VID_46F4&PID_0011&MI_00\\", L"WinUSB loopback");
        TestOneFunction(&GUID_WINUSB_TEST2, L"{5d4a3d8e-91f2-4c6b-8e1a-7b2c3d4e5f61}",
                        L"USB\\VID_46F4&PID_0011&REV_0100&MI_02;USB\\VID_46F4&PID_0011&MI_02",
                        L"USB\\MS_COMP_WINUSB;USB\\COMPAT_VID_46f4&Class_ff&SubClass_02&Prot_00;"
                        L"USB\\COMPAT_VID_46f4&Class_ff&SubClass_02;USB\\COMPAT_VID_46f4&Class_ff;"
                        L"USB\\Class_ff&SubClass_02&Prot_00;USB\\Class_ff&SubClass_02;USB\\Class_ff",
                        L"USB\\VID_46F4&PID_0011&MI_02\\", L"WinUSB second function");
        return;
    }
    if (!FindInterfacePath(&GUID_WINUSB_TEST, 0, Path, ARRAYSIZE(Path)))
    {
        skip("No usb-winusb device\n");
        return;
    }
    TestOneFunction(&GUID_WINUSB_TEST, L"{5d4a3d8e-91f2-4c6b-8e1a-7b2c3d4e5f60}",
                    L"USB\\VID_46F4&PID_0010&REV_0100;USB\\VID_46F4&PID_0010",
                    L"USB\\MS_COMP_WINUSB;USB\\COMPAT_VID_46F4&Class_FF&SubClass_57&Prot_00;"
                    L"USB\\COMPAT_VID_46F4&Class_FF&SubClass_57;USB\\COMPAT_VID_46F4&Class_FF;"
                    L"USB\\Class_FF&SubClass_57&Prot_00;USB\\Class_FF&SubClass_57;USB\\Class_FF",
                    L"USB\\VID_46F4&PID_0010\\", L"QEMU USB WinUSB Test Device");

    SetLastError(0xdeadbeef);
    Interface = (WINUSB_INTERFACE_HANDLE)(ULONG_PTR)1;
    Result = WinUsb_Initialize(NULL, &Interface);
    Error = GetLastError();
    ok(!Result && Error == ERROR_INVALID_HANDLE, "Initialize(NULL): %d %lu\n", Result, Error);
    ok(Interface == NULL, "Initialize(NULL) left %p\n", Interface);
    SetLastError(0xdeadbeef);
    Result = WinUsb_Initialize(INVALID_HANDLE_VALUE, &Interface);
    Error = GetLastError();
    ok(!Result && Error == ERROR_INVALID_HANDLE, "Initialize(INVALID_HANDLE_VALUE): %d %lu\n", Result, Error);

    File = CreateFileW(Path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0,
                       NULL);
    ok(File != INVALID_HANDLE_VALUE, "Synchronous open failed: %lu\n", GetLastError());
    if (File != INVALID_HANDLE_VALUE)
    {
        SetLastError(0xdeadbeef);
        Interface = NULL;
        Result = WinUsb_Initialize(File, &Interface);
        Error = GetLastError();
        ok(!Result && Error == ERROR_INVALID_HANDLE, "Initialize on a synchronous handle: %d %lu\n", Result, Error);
        if (Result)
            WinUsb_Free(Interface);
        CloseHandle(File);
    }

    File = CreateFileW(Path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
                       FILE_FLAG_OVERLAPPED, NULL);
    ok(File != INVALID_HANDLE_VALUE, "Open failed: %lu\n", GetLastError());
    if (File == INVALID_HANDLE_VALUE)
        return;
    SetLastError(0xdeadbeef);
    File2 = CreateFileW(Path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
                        FILE_FLAG_OVERLAPPED, NULL);
    Error = GetLastError();
    ok(File2 == INVALID_HANDLE_VALUE && Error == ERROR_ACCESS_DENIED, "Second open: %p %lu\n", File2, Error);
    if (File2 != INVALID_HANDLE_VALUE)
        CloseHandle(File2);

    Interface = NULL;
    ok(WinUsb_Initialize(File, &Interface), "Initialize failed: %lu\n", GetLastError());
    SetLastError(0xdeadbeef);
    Interface2 = NULL;
    Result = WinUsb_Initialize(File, &Interface2);
    Error = GetLastError();
    ok(Result, "Second Initialize on one handle: %d %lu\n", Result, Error);
    ok(Interface2 != NULL && Interface2 != Interface, "Second Initialize returned %p (first %p)\n", Interface2,
       Interface);
    if (Result)
        ok(WinUsb_Free(Interface2), "Free of the second handle failed\n");
    ok(WinUsb_Free(Interface), "Free failed\n");
    SetLastError(0xdeadbeef);
    Result = WinUsb_Free(NULL);
    Error = GetLastError();
    ok(Result && Error == 0xdeadbeef, "Free(NULL): %d %lu\n", Result, Error);
    CloseHandle(File);
}

START_TEST(RegSz)
{
    SP_DEVINFO_DATA Info;
    WCHAR Text[512], Value[128];
    DWORD Type, Size, Index;
    HDEVINFO Set;
    HKEY Key;
    BOOL Found = FALSE;
    LONG Error;

    Set = SetupDiGetClassDevsW(NULL, L"USB", NULL, DIGCF_PRESENT | DIGCF_ALLCLASSES);
    ok(Set != INVALID_HANDLE_VALUE, "SetupDiGetClassDevs failed: %lu\n", GetLastError());
    if (Set == INVALID_HANDLE_VALUE)
        return;
    Info.cbSize = sizeof(Info);
    for (Index = 0; !Found && SetupDiEnumDeviceInfo(Set, Index, &Info); Index++)
    {
        if (!GetDeviceString(Set, &Info, SPDRP_HARDWAREID, Text, ARRAYSIZE(Text)) ||
            wcsncmp(Text, L"USB\\VID_46F4&PID_0010&", 22))
        {
            continue;
        }
        Key = SetupDiOpenDevRegKey(Set, &Info, DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_READ);
        if (Key == INVALID_HANDLE_VALUE)
            continue;
        ZeroMemory(Value, sizeof(Value));
        Size = sizeof(Value) - sizeof(WCHAR);
        Error = RegQueryValueExW(Key, L"DeviceInterfaceGUID", NULL, &Type, (PBYTE)Value, &Size);
        RegCloseKey(Key);
        if (Error != ERROR_SUCCESS)
            continue;
        Found = TRUE;
        ok(Type == REG_SZ && !_wcsicmp(Value, L"{5d4a3d8e-91f2-4c6b-8e1a-7b2c3d4e5f60}"),
           "DeviceInterfaceGUID type %lu value %S\n", Type, Value);
        ok(GetDeviceString(Set, &Info, SPDRP_SERVICE, Text, ARRAYSIZE(Text)) && !wcscmp(Text, L"WINUSB"),
           "Service %S\n", Text);
        ok(FindInterfacePath(&GUID_WINUSB_TEST, 0, Text, ARRAYSIZE(Text)),
           "No device interface registered from DeviceInterfaceGUID\n");
    }
    SetupDiDestroyDeviceInfoList(Set);
    if (!Found)
        skip("No usb-winusb device with a DeviceInterfaceGUID value\n");
}

START_TEST(Descriptors)
{
    TEST_DEVICE Device;
    USB_DEVICE_DESCRIPTOR DeviceDescriptor;
    USB_INTERFACE_DESCRIPTOR Setting;
    WINUSB_PIPE_INFORMATION Pipe;
    WINUSB_PIPE_INFORMATION_EX PipeEx;
    WINUSB_INTERFACE_HANDLE Associated, Associated2;
    UCHAR Buffer[512], Speed[8], Alt;
    ULONG Transferred, Length;
    BOOL Result;
    DWORD Error;
    static const struct
    {
        UCHAR Alt, Index;
        USBD_PIPE_TYPE Type;
        UCHAR Id;
        USHORT MaxPacket;
        UCHAR Interval;
    } Pipes[] =
    {
        { 0, 0, UsbdPipeTypeBulk, 0x01, 512, 0 },
        { 0, 1, UsbdPipeTypeBulk, 0x81, 512, 0 },
        { 0, 2, UsbdPipeTypeInterrupt, 0x82, 16, 4 },
        { 0, 3, UsbdPipeTypeBulk, 0x83, 512, 0 },
        { 1, 0, UsbdPipeTypeBulk, 0x01, 512, 0 },
        { 1, 1, UsbdPipeTypeBulk, 0x81, 512, 0 },
    };
    ULONG Index;

    if (!OpenTestDevice(&GUID_WINUSB_TEST, &Device))
    {
        skip("No usb-winusb device: %lu\n", GetLastError());
        return;
    }
    if (FindInterfacePath(&GUID_WINUSB_TEST2, 0, (PWSTR)Buffer, sizeof(Buffer) / sizeof(WCHAR)))
    {
        Result = WinUsb_GetDescriptor(Device.Interface, USB_CONFIGURATION_DESCRIPTOR_TYPE, 0, 0, Buffer,
                                      sizeof(Buffer), &Transferred);
        ok(Result && Transferred == 92 && ((PUSB_CONFIGURATION_DESCRIPTOR)Buffer)->wTotalLength == 92 &&
           ((PUSB_CONFIGURATION_DESCRIPTOR)Buffer)->bNumInterfaces == 2,
           "Composite function configuration: %d %lu total %u interfaces %u\n", Result, Transferred,
           ((PUSB_CONFIGURATION_DESCRIPTOR)Buffer)->wTotalLength,
           ((PUSB_CONFIGURATION_DESCRIPTOR)Buffer)->bNumInterfaces);
        for (Index = 9; Result && Index + 2 <= Transferred && Buffer[Index] >= 2; Index += Buffer[Index])
        {
            if (Buffer[Index + 1] == USB_INTERFACE_DESCRIPTOR_TYPE)
                trace("Composite function interface %u alt %u\n", Buffer[Index + 2], Buffer[Index + 3]);
        }
        ZeroMemory(&Setting, sizeof(Setting));
        ok(WinUsb_QueryInterfaceSettings(Device.Interface, 1, &Setting) && Setting.bInterfaceNumber == 0 &&
           Setting.bAlternateSetting == 1, "Composite setting 1: interface %u alt %u\n", Setting.bInterfaceNumber,
           Setting.bAlternateSetting);
        Associated = NULL;
        ok(WinUsb_GetAssociatedInterface(Device.Interface, 0, &Associated), "Composite associated interface: %lu\n",
           GetLastError());
        if (Associated)
        {
            ZeroMemory(&Setting, sizeof(Setting));
            ok(WinUsb_QueryInterfaceSettings(Associated, 0, &Setting) && Setting.bInterfaceNumber == 1,
               "Composite associated interface %u\n", Setting.bInterfaceNumber);
            WinUsb_Free(Associated);
        }
        SetLastError(0xdeadbeef);
        Result = WinUsb_GetAssociatedInterface(Device.Interface, 1, &Associated);
        Error = GetLastError();
        ok(!Result && Error == ERROR_NO_MORE_ITEMS, "Composite GetAssociatedInterface(1): %d %lu\n", Result, Error);
        CloseTestDevice(&Device);
        return;
    }

    ZeroMemory(&DeviceDescriptor, sizeof(DeviceDescriptor));
    Result = WinUsb_GetDescriptor(Device.Interface, USB_DEVICE_DESCRIPTOR_TYPE, 0, 0, (PUCHAR)&DeviceDescriptor,
                                  sizeof(DeviceDescriptor), &Transferred);
    ok(Result && Transferred == sizeof(DeviceDescriptor), "Device descriptor: %d %lu %lu\n", Result, Transferred,
       GetLastError());
    ok(DeviceDescriptor.idVendor == TEST_VID && DeviceDescriptor.idProduct == TEST_PID, "IDs %04x:%04x\n",
       DeviceDescriptor.idVendor, DeviceDescriptor.idProduct);
    ok(DeviceDescriptor.bcdUSB == 0x0210 && DeviceDescriptor.bDeviceClass == 0xFF &&
       DeviceDescriptor.bNumConfigurations == 1 && DeviceDescriptor.bMaxPacketSize0 == 64,
       "bcdUSB %04x class %02x configurations %u ep0 %u\n", DeviceDescriptor.bcdUSB, DeviceDescriptor.bDeviceClass,
       DeviceDescriptor.bNumConfigurations, DeviceDescriptor.bMaxPacketSize0);

    memset(Buffer, 0xCC, sizeof(Buffer));
    SetLastError(0xdeadbeef);
    Result = WinUsb_GetDescriptor(Device.Interface, USB_DEVICE_DESCRIPTOR_TYPE, 0, 0, Buffer, 8, &Transferred);
    Error = GetLastError();
    ok(!Result && Error == ERROR_INSUFFICIENT_BUFFER && Transferred == 0, "Device descriptor into 8 bytes: %d %lu %lu\n",
       Result, Transferred, Error);
    ok(Buffer[0] == 0xCC && Buffer[1] == 0xCC, "Short device descriptor buffer %02x %02x\n", Buffer[0], Buffer[1]);
    memset(Buffer, 0xCC, sizeof(Buffer));
    SetLastError(0xdeadbeef);
    Result = WinUsb_GetDescriptor(Device.Interface, USB_CONFIGURATION_DESCRIPTOR_TYPE, 0, 0, Buffer, 9, &Transferred);
    Error = GetLastError();
    ok(Result && Transferred == 9, "Configuration descriptor into 9 bytes: %d %lu %lu\n", Result, Transferred, Error);
    ok(Buffer[0] == 9 && Buffer[1] == USB_CONFIGURATION_DESCRIPTOR_TYPE && Buffer[2] == 92 && Buffer[3] == 0 &&
       Buffer[9] == 0xCC, "Configuration header %02x %02x %02x %02x %02x\n", Buffer[0], Buffer[1], Buffer[2], Buffer[3],
       Buffer[9]);
    memset(Buffer, 0xCC, sizeof(Buffer));
    SetLastError(0xdeadbeef);
    Result = WinUsb_GetDescriptor(Device.Interface, USB_STRING_DESCRIPTOR_TYPE, 2, 0x0409, Buffer, 4, &Transferred);
    Error = GetLastError();
    ok(Result && Transferred == 4, "Product string into 4 bytes: %d %lu %lu\n", Result, Transferred, Error);
    ok(Buffer[0] == 56 && Buffer[1] == USB_STRING_DESCRIPTOR_TYPE && Buffer[2] == 'Q' && Buffer[3] == 0 &&
       Buffer[4] == 0xCC, "Product string head %02x %02x %02x %02x %02x\n", Buffer[0], Buffer[1], Buffer[2], Buffer[3],
       Buffer[4]);

    SetLastError(0xdeadbeef);
    Result = WinUsb_GetDescriptor(Device.Interface, USB_CONFIGURATION_DESCRIPTOR_TYPE, 0, 0, Buffer, sizeof(Buffer),
                                  &Transferred);
    ok(Result && Transferred == 92, "Configuration descriptor: %d %lu %lu\n", Result, Transferred, GetLastError());
    ok(((PUSB_CONFIGURATION_DESCRIPTOR)Buffer)->wTotalLength == 92 &&
       ((PUSB_CONFIGURATION_DESCRIPTOR)Buffer)->bNumInterfaces == 2, "Configuration total %u interfaces %u\n",
       ((PUSB_CONFIGURATION_DESCRIPTOR)Buffer)->wTotalLength, ((PUSB_CONFIGURATION_DESCRIPTOR)Buffer)->bNumInterfaces);
    SetLastError(0xdeadbeef);
    Result = WinUsb_GetDescriptor(Device.Interface, USB_CONFIGURATION_DESCRIPTOR_TYPE, 1, 0, Buffer, sizeof(Buffer),
                                  &Transferred);
    Error = GetLastError();
    ok(Result && Transferred == 92 && Buffer[1] == USB_CONFIGURATION_DESCRIPTOR_TYPE, "Configuration 1: %d %lu %lu\n",
       Result, Transferred, Error);

    Result = WinUsb_GetDescriptor(Device.Interface, USB_STRING_DESCRIPTOR_TYPE, 0, 0, Buffer, sizeof(Buffer),
                                  &Transferred);
    ok(Result && Transferred == 4 && Buffer[2] == 0x09 && Buffer[3] == 0x04, "Language IDs: %d %lu %02x%02x\n", Result,
       Transferred, Buffer[3], Buffer[2]);
    Result = WinUsb_GetDescriptor(Device.Interface, USB_STRING_DESCRIPTOR_TYPE, 2, 0x0409, Buffer, sizeof(Buffer),
                                  &Transferred);
    ok(Result && Transferred == 2 + 27 * sizeof(WCHAR), "Product string: %d %lu\n", Result, Transferred);
    ok(Result && !memcmp(Buffer + 2, L"QEMU USB WinUSB Test Device", 27 * sizeof(WCHAR)), "Product string differs\n");
    Result = WinUsb_GetDescriptor(Device.Interface, 0x0F, 0, 0, Buffer, sizeof(Buffer), &Transferred);
    ok(Result && Transferred == 40 && Buffer[0] == 5 && Buffer[1] == 0x0F, "BOS: %d %lu %lu\n", Result, Transferred,
       GetLastError());
    SetLastError(0xdeadbeef);
    Result = WinUsb_GetDescriptor(NULL, USB_DEVICE_DESCRIPTOR_TYPE, 0, 0, Buffer, sizeof(Buffer), &Transferred);
    Error = GetLastError();
    ok(!Result && Error == ERROR_INVALID_HANDLE, "GetDescriptor(NULL): %d %lu\n", Result, Error);

    for (Index = 0; Index < 2; Index++)
    {
        ZeroMemory(&Setting, sizeof(Setting));
        Result = WinUsb_QueryInterfaceSettings(Device.Interface, (UCHAR)Index, &Setting);
        ok(Result, "QueryInterfaceSettings(%lu) failed: %lu\n", Index, GetLastError());
        ok(Setting.bLength == 9 && Setting.bInterfaceNumber == 0 && Setting.bAlternateSetting == Index &&
           Setting.bNumEndpoints == (Index ? 2 : 4) && Setting.bInterfaceClass == 0xFF &&
           Setting.bInterfaceProtocol == Index, "Setting %lu: number %u alt %u endpoints %u class %02x protocol %u\n",
           Index, Setting.bInterfaceNumber, Setting.bAlternateSetting, Setting.bNumEndpoints, Setting.bInterfaceClass,
           Setting.bInterfaceProtocol);
    }
    SetLastError(0xdeadbeef);
    Result = WinUsb_QueryInterfaceSettings(Device.Interface, 2, &Setting);
    Error = GetLastError();
    ok(!Result && Error == ERROR_NO_MORE_ITEMS, "QueryInterfaceSettings(2): %d %lu\n", Result, Error);

    for (Index = 0; Index < ARRAYSIZE(Pipes); Index++)
    {
        memset(&Pipe, 0xCC, sizeof(Pipe));
        Result = WinUsb_QueryPipe(Device.Interface, Pipes[Index].Alt, Pipes[Index].Index, &Pipe);
        ok(Result, "QueryPipe(%u, %u) failed: %lu\n", Pipes[Index].Alt, Pipes[Index].Index, GetLastError());
        ok(Pipe.PipeType == Pipes[Index].Type && Pipe.PipeId == Pipes[Index].Id &&
           Pipe.MaximumPacketSize == Pipes[Index].MaxPacket && Pipe.Interval == Pipes[Index].Interval,
           "QueryPipe(%u, %u): type %u id %02x max %u interval %u\n", Pipes[Index].Alt, Pipes[Index].Index,
           Pipe.PipeType, Pipe.PipeId, Pipe.MaximumPacketSize, Pipe.Interval);
        memset(&PipeEx, 0xCC, sizeof(PipeEx));
        Result = WinUsb_QueryPipeEx(Device.Interface, Pipes[Index].Alt, Pipes[Index].Index, &PipeEx);
        ok(Result, "QueryPipeEx(%u, %u) failed: %lu\n", Pipes[Index].Alt, Pipes[Index].Index, GetLastError());
        ok(PipeEx.PipeType == Pipes[Index].Type && PipeEx.PipeId == Pipes[Index].Id &&
           PipeEx.MaximumPacketSize == Pipes[Index].MaxPacket && PipeEx.Interval == Pipes[Index].Interval &&
           PipeEx.MaximumBytesPerInterval == Pipes[Index].MaxPacket,
           "QueryPipeEx(%u, %u): type %u id %02x max %u interval %u bytes per interval %lu\n", Pipes[Index].Alt,
           Pipes[Index].Index, PipeEx.PipeType, PipeEx.PipeId, PipeEx.MaximumPacketSize, PipeEx.Interval,
           PipeEx.MaximumBytesPerInterval);
    }
    SetLastError(0xdeadbeef);
    Result = WinUsb_QueryPipe(Device.Interface, 0, 4, &Pipe);
    Error = GetLastError();
    ok(!Result && Error == ERROR_NO_MORE_ITEMS, "QueryPipe(0, 4): %d %lu\n", Result, Error);
    SetLastError(0xdeadbeef);
    Result = WinUsb_QueryPipe(Device.Interface, 2, 0, &Pipe);
    Error = GetLastError();
    ok(!Result && Error == ERROR_INVALID_PARAMETER, "QueryPipe(2, 0): %d %lu\n", Result, Error);
    SetLastError(0xdeadbeef);
    Result = WinUsb_QueryPipe(Device.Interface, 0, 0, NULL);
    Error = GetLastError();
    ok(!Result && Error == ERROR_INVALID_PARAMETER, "QueryPipe(NULL): %d %lu\n", Result, Error);

    memset(Speed, 0xCC, sizeof(Speed));
    Length = 1;
    Result = WinUsb_QueryDeviceInformation(Device.Interface, DEVICE_SPEED, &Length, Speed);
    ok(Result && Length == 1 && Speed[0] == HighSpeed, "DEVICE_SPEED: %d length %lu speed %u %lu\n", Result, Length,
       Speed[0], GetLastError());
    memset(Speed, 0xCC, sizeof(Speed));
    Length = sizeof(Speed);
    Result = WinUsb_QueryDeviceInformation(Device.Interface, DEVICE_SPEED, &Length, Speed);
    ok(Result && Length == 1 && Speed[0] == HighSpeed && Speed[1] == 0xCC,
       "DEVICE_SPEED into 8 bytes: %d length %lu speed %u %02x\n", Result, Length, Speed[0], Speed[1]);
    Length = 0;
    SetLastError(0xdeadbeef);
    Result = WinUsb_QueryDeviceInformation(Device.Interface, DEVICE_SPEED, &Length, Speed);
    Error = GetLastError();
    ok(!Result && Error == ERROR_INSUFFICIENT_BUFFER && Length == 0, "DEVICE_SPEED into 0 bytes: %d %lu length %lu\n",
       Result, Error, Length);
    Length = sizeof(Speed);
    SetLastError(0xdeadbeef);
    Result = WinUsb_QueryDeviceInformation(Device.Interface, 2, &Length, Speed);
    Error = GetLastError();
    ok(!Result && Error == ERROR_INVALID_PARAMETER, "Information type 2: %d %lu length %lu\n", Result, Error, Length);

    Alt = 0xCC;
    ok(WinUsb_GetCurrentAlternateSetting(Device.Interface, &Alt) && Alt == 0, "Current setting %u\n", Alt);

    Associated = NULL;
    Result = WinUsb_GetAssociatedInterface(Device.Interface, 0, &Associated);
    ok(Result && Associated != NULL, "GetAssociatedInterface(0): %d %lu\n", Result, GetLastError());
    SetLastError(0xdeadbeef);
    Associated2 = (WINUSB_INTERFACE_HANDLE)(ULONG_PTR)1;
    Result = WinUsb_GetAssociatedInterface(Device.Interface, 0, &Associated2);
    Error = GetLastError();
    ok(!Result && Error == ERROR_ALREADY_EXISTS, "Second GetAssociatedInterface(0): %d %lu\n", Result, Error);
    ok(Associated2 == (WINUSB_INTERFACE_HANDLE)(ULONG_PTR)1, "Failed GetAssociatedInterface left %p\n", Associated2);
    SetLastError(0xdeadbeef);
    Result = WinUsb_GetAssociatedInterface(Device.Interface, 1, &Associated2);
    Error = GetLastError();
    ok(!Result && Error == ERROR_NO_MORE_ITEMS, "GetAssociatedInterface(1): %d %lu\n", Result, Error);
    if (Associated)
    {
        ZeroMemory(&Setting, sizeof(Setting));
        ok(WinUsb_QueryInterfaceSettings(Associated, 0, &Setting), "Associated settings failed\n");
        ok(Setting.bInterfaceNumber == 1 && Setting.bNumEndpoints == 2 && Setting.bInterfaceSubClass == 1,
           "Associated interface %u endpoints %u subclass %u\n", Setting.bInterfaceNumber, Setting.bNumEndpoints,
           Setting.bInterfaceSubClass);
        SetLastError(0xdeadbeef);
        Result = WinUsb_QueryInterfaceSettings(Associated, 1, &Setting);
        Error = GetLastError();
        ok(!Result && Error == ERROR_NO_MORE_ITEMS, "Associated setting 1: %d %lu\n", Result, Error);
        ok(WinUsb_QueryPipe(Associated, 0, 1, &Pipe) && Pipe.PipeId == 0x84, "Associated pipe %02x\n", Pipe.PipeId);
        SetLastError(0xdeadbeef);
        Result = WinUsb_GetAssociatedInterface(Associated, 0, &Associated2);
        Error = GetLastError();
        ok(!Result && Error == ERROR_INVALID_HANDLE, "GetAssociatedInterface from the associated handle: %d %lu\n",
           Result, Error);
        Result = WinUsb_GetDescriptor(Associated, USB_DEVICE_DESCRIPTOR_TYPE, 0, 0, Buffer, sizeof(Buffer),
                                      &Transferred);
        ok(Result && Transferred == 18, "Device descriptor through the associated handle: %d %lu\n", Result,
           Transferred);
        Length = 1;
        Result = WinUsb_QueryDeviceInformation(Associated, DEVICE_SPEED, &Length, Speed);
        ok(Result && Speed[0] == HighSpeed, "DEVICE_SPEED through the associated handle: %d %lu\n", Result,
           GetLastError());
        ok(WinUsb_Free(Associated), "Free of the associated handle failed\n");
        Associated = NULL;
        Result = WinUsb_GetAssociatedInterface(Device.Interface, 0, &Associated);
        ok(Result, "GetAssociatedInterface(0) after Free: %d %lu\n", Result, GetLastError());
        if (Result)
            WinUsb_Free(Associated);
    }
    CloseTestDevice(&Device);
}

START_TEST(Policy)
{
    TEST_DEVICE Device;
    static const UCHAR PipeIds[] = { 0x00, PIPE_LOOP_OUT, PIPE_LOOP_IN, PIPE_INTR_IN, PIPE_HELD_IN };
    static const ULONG ExpectedLength[] = { 0, 1, 1, 4, 1, 1, 1, 1, 4, 1 };
    static const UCHAR ValidControl[] = { 0, 0, 0, 1, 0, 0, 0, 0, 1, 0 };
    static const UCHAR ValidOut[] = { 0, 1, 1, 1, 0, 0, 0, 1, 1, 1 };
    static const UCHAR ValidIn[] = { 0, 0, 1, 1, 1, 1, 1, 1, 1, 1 };
    const UCHAR *Valid;
    UCHAR Bytes[16];
    ULONG Value, Length, Policy, Index;
    BOOL Result;
    DWORD Error;

    if (!OpenTestDevice(&GUID_WINUSB_TEST, &Device))
    {
        skip("No usb-winusb device: %lu\n", GetLastError());
        return;
    }

    for (Index = 0; Index < ARRAYSIZE(PipeIds); Index++)
    {
        Valid = !PipeIds[Index] ? ValidControl : (PipeIds[Index] & 0x80) ? ValidIn : ValidOut;
        for (Policy = SHORT_PACKET_TERMINATE; Policy <= RESET_PIPE_ON_RESUME; Policy++)
        {
            memset(Bytes, 0xCC, sizeof(Bytes));
            Length = sizeof(Bytes);
            SetLastError(0xdeadbeef);
            Result = WinUsb_GetPipePolicy(Device.Interface, PipeIds[Index], Policy, &Length, Bytes);
            Error = GetLastError();
            Value = Length == 4 ? *(PULONG)Bytes : Bytes[0];
            trace("Pipe %02x policy %lu: %d error %lu length %lu value %lu\n", PipeIds[Index], Policy, Result,
                  Result ? 0 : Error, Length, Value);
            if (!Valid[Policy])
            {
                ok(!Result && Error == ERROR_INVALID_PARAMETER && Length == sizeof(Bytes) && Bytes[0] == 0xCC,
                   "Pipe %02x policy %lu: %d %lu length %lu\n", PipeIds[Index], Policy, Result, Error, Length);
                continue;
            }
            ok(Result, "Pipe %02x policy %lu failed: %lu\n", PipeIds[Index], Policy, Error);
            ok(Length == ExpectedLength[Policy], "Pipe %02x policy %lu length %lu\n", PipeIds[Index], Policy,
               Length);
            if (Policy == PIPE_TRANSFER_TIMEOUT)
                ok(Value == (PipeIds[Index] ? 0 : 5000), "Pipe %02x timeout %lu\n", PipeIds[Index], Value);
            else if (Policy == ALLOW_PARTIAL_READS)
                ok(Value == 1, "Pipe %02x ALLOW_PARTIAL_READS %lu\n", PipeIds[Index], Value);
            else if (Policy == MAXIMUM_TRANSFER_SIZE)
                ok(Value == (PipeIds[Index] ? 2 * 1024 * 1024 : 0), "Pipe %02x MAXIMUM_TRANSFER_SIZE %lu\n",
                   PipeIds[Index], Value);
            else
                ok(Value == 0, "Pipe %02x policy %lu value %lu\n", PipeIds[Index], Policy, Value);
        }
    }

    Length = sizeof(Value);
    ok(WinUsb_GetPipePolicy(Device.Interface, PIPE_LOOP_IN, MAXIMUM_TRANSFER_SIZE, &Length, &Value),
       "MAXIMUM_TRANSFER_SIZE failed\n");
    ok(Value == 2 * 1024 * 1024, "MAXIMUM_TRANSFER_SIZE %lu\n", Value);

    Length = 0;
    SetLastError(0xdeadbeef);
    Result = WinUsb_GetPipePolicy(Device.Interface, PIPE_LOOP_IN, RAW_IO, &Length, Bytes);
    Error = GetLastError();
    ok(!Result && Error == ERROR_INSUFFICIENT_BUFFER && Length == 0, "RAW_IO into 0 bytes: %d %lu length %lu\n",
       Result, Error, Length);
    Length = 2;
    SetLastError(0xdeadbeef);
    Result = WinUsb_GetPipePolicy(Device.Interface, PIPE_LOOP_IN, PIPE_TRANSFER_TIMEOUT, &Length, Bytes);
    Error = GetLastError();
    ok(!Result && Error == ERROR_INSUFFICIENT_BUFFER && Length == 2, "Timeout into 2 bytes: %d %lu length %lu\n",
       Result, Error, Length);
    Length = sizeof(Bytes);
    SetLastError(0xdeadbeef);
    Result = WinUsb_GetPipePolicy(Device.Interface, 0x8F, RAW_IO, &Length, Bytes);
    Error = GetLastError();
    ok(!Result && Error == ERROR_INVALID_PARAMETER, "Policy of pipe 8f: %d %lu\n", Result, Error);
    Length = sizeof(Bytes);
    SetLastError(0xdeadbeef);
    Result = WinUsb_GetPipePolicy(Device.Interface, PIPE_LOOP_IN, 0x55, &Length, Bytes);
    Error = GetLastError();
    ok(!Result && Error == ERROR_INVALID_PARAMETER, "Policy 0x55: %d %lu\n", Result, Error);
    Length = sizeof(Bytes);
    SetLastError(0xdeadbeef);
    Result = WinUsb_GetPipePolicy(NULL, PIPE_LOOP_IN, RAW_IO, &Length, Bytes);
    Error = GetLastError();
    ok(!Result && Error == ERROR_INVALID_HANDLE, "GetPipePolicy(NULL): %d %lu\n", Result, Error);

    ok(SetPolicyBool(Device.Interface, PIPE_LOOP_IN, RAW_IO, TRUE), "Set RAW_IO failed: %lu\n", GetLastError());
    Length = 1;
    Bytes[0] = 0;
    ok(WinUsb_GetPipePolicy(Device.Interface, PIPE_LOOP_IN, RAW_IO, &Length, Bytes) && Bytes[0] == 1,
       "RAW_IO now %u\n", Bytes[0]);
    ok(SetPolicyBool(Device.Interface, PIPE_LOOP_IN, RAW_IO, 7), "Set RAW_IO 7 failed: %lu\n", GetLastError());
    Length = 1;
    ok(WinUsb_GetPipePolicy(Device.Interface, PIPE_LOOP_IN, RAW_IO, &Length, Bytes) && Bytes[0] == 1,
       "RAW_IO after 7 is %u\n", Bytes[0]);
    SetLastError(0xdeadbeef);
    Value = 1;
    Result = WinUsb_SetPipePolicy(Device.Interface, PIPE_LOOP_IN, RAW_IO, sizeof(Value), &Value);
    Error = GetLastError();
    ok(Result, "Set RAW_IO with 4 bytes: %d %lu\n", Result, Error);
    Value = 0x100;
    ok(WinUsb_SetPipePolicy(Device.Interface, PIPE_LOOP_IN, RAW_IO, sizeof(Value), &Value), "Set RAW_IO 0x100 failed\n");
    Length = 1;
    Bytes[0] = 0xCC;
    ok(WinUsb_GetPipePolicy(Device.Interface, PIPE_LOOP_IN, RAW_IO, &Length, Bytes) && Bytes[0] == 0,
       "RAW_IO after 0x100 in 4 bytes is %u\n", Bytes[0]);
    SetLastError(0xdeadbeef);
    Result = WinUsb_SetPipePolicy(Device.Interface, PIPE_LOOP_IN, RAW_IO, 0, Bytes);
    Error = GetLastError();
    ok(!Result && Error == ERROR_INVALID_PARAMETER, "Set RAW_IO with 0 bytes: %d %lu\n", Result, Error);
    SetLastError(0xdeadbeef);
    Result = SetPolicyBool(Device.Interface, PIPE_LOOP_IN, SHORT_PACKET_TERMINATE, TRUE);
    Error = GetLastError();
    ok(Result, "Set SHORT_PACKET_TERMINATE on an IN pipe: %d %lu\n", Result, Error);
    SetLastError(0xdeadbeef);
    Result = SetPolicyBool(Device.Interface, PIPE_LOOP_OUT, IGNORE_SHORT_PACKETS, TRUE);
    Error = GetLastError();
    ok(Result, "Set IGNORE_SHORT_PACKETS on an OUT pipe: %d %lu\n", Result, Error);
    SetLastError(0xdeadbeef);
    Result = SetPolicyBool(Device.Interface, 0, RAW_IO, TRUE);
    Error = GetLastError();
    ok(!Result && Error == ERROR_INVALID_PARAMETER, "Set RAW_IO on the control pipe: %d %lu\n", Result, Error);
    SetLastError(0xdeadbeef);
    Result = WinUsb_SetPipePolicy(Device.Interface, PIPE_LOOP_IN, PIPE_TRANSFER_TIMEOUT, 1, Bytes);
    Error = GetLastError();
    ok(!Result && Error == ERROR_INVALID_PARAMETER, "Set timeout with 1 byte: %d %lu\n", Result, Error);
    SetLastError(0xdeadbeef);
    Result = SetPolicyUlong(Device.Interface, PIPE_LOOP_IN, MAXIMUM_TRANSFER_SIZE, 4096);
    Error = GetLastError();
    ok(!Result && Error == ERROR_INVALID_PARAMETER, "Set MAXIMUM_TRANSFER_SIZE: %d %lu\n", Result, Error);
    SetLastError(0xdeadbeef);
    Result = SetPolicyBool(Device.Interface, 0x8F, RAW_IO, TRUE);
    Error = GetLastError();
    ok(!Result && Error == ERROR_INVALID_PARAMETER, "Set policy of pipe 8f: %d %lu\n", Result, Error);
    SetLastError(0xdeadbeef);
    Result = SetPolicyBool(NULL, PIPE_LOOP_IN, RAW_IO, TRUE);
    Error = GetLastError();
    ok(!Result && Error == ERROR_INVALID_HANDLE, "SetPipePolicy(NULL): %d %lu\n", Result, Error);
    ok(SetPolicyUlong(Device.Interface, 0, PIPE_TRANSFER_TIMEOUT, 1234), "Set control timeout failed: %lu\n",
       GetLastError());
    Length = sizeof(Value);
    ok(WinUsb_GetPipePolicy(Device.Interface, 0, PIPE_TRANSFER_TIMEOUT, &Length, &Value) && Value == 1234,
       "Control timeout now %lu\n", Value);
    SetLastError(0xdeadbeef);
    Result = SetPolicyBool(Device.Interface, PIPE_LOOP_OUT, RAW_IO, TRUE);
    Error = GetLastError();
    ok(Result, "Set RAW_IO on an OUT pipe: %d %lu\n", Result, Error);

    WinUsb_Free(Device.Interface);
    Device.Interface = NULL;
    ok(WinUsb_Initialize(Device.File, &Device.Interface), "Initialize again failed: %lu\n", GetLastError());
    Length = 1;
    Bytes[0] = 0xCC;
    ok(WinUsb_GetPipePolicy(Device.Interface, PIPE_LOOP_IN, RAW_IO, &Length, Bytes) && Bytes[0] == 0,
       "RAW_IO after Initialize %u\n", Bytes[0]);
    Length = sizeof(Value);
    ok(WinUsb_GetPipePolicy(Device.Interface, 0, PIPE_TRANSFER_TIMEOUT, &Length, &Value) && Value == 5000,
       "Control timeout after Initialize %lu\n", Value);

    memset(Bytes, 0xCC, sizeof(Bytes));
    Length = sizeof(Bytes);
    Result = WinUsb_GetPowerPolicy(Device.Interface, AUTO_SUSPEND, &Length, Bytes);
    ok(Result && Length == 1 && Bytes[0] == 0, "AUTO_SUSPEND: %d length %lu value %u %lu\n", Result, Length, Bytes[0],
       GetLastError());
    memset(Bytes, 0xCC, sizeof(Bytes));
    Length = sizeof(Bytes);
    Result = WinUsb_GetPowerPolicy(Device.Interface, SUSPEND_DELAY, &Length, Bytes);
    ok(Result && Length == 4 && *(PULONG)Bytes == 0, "SUSPEND_DELAY: %d length %lu value %lu %lu\n", Result, Length,
       *(PULONG)Bytes, GetLastError());
    Length = 0;
    SetLastError(0xdeadbeef);
    Result = WinUsb_GetPowerPolicy(Device.Interface, SUSPEND_DELAY, &Length, Bytes);
    Error = GetLastError();
    ok(!Result && Error == ERROR_INVALID_PARAMETER && Length == 0, "SUSPEND_DELAY into 0 bytes: %d %lu length %lu\n",
       Result, Error, Length);
    Length = sizeof(Bytes);
    SetLastError(0xdeadbeef);
    Result = WinUsb_GetPowerPolicy(Device.Interface, 0x82, &Length, Bytes);
    Error = GetLastError();
    ok(!Result && Error == ERROR_INVALID_PARAMETER, "Power policy 0x82: %d %lu\n", Result, Error);
    Bytes[0] = 0;
    ok(WinUsb_SetPowerPolicy(Device.Interface, AUTO_SUSPEND, 1, Bytes), "Set AUTO_SUSPEND failed: %lu\n",
       GetLastError());
    Length = 1;
    Bytes[0] = 0xCC;
    ok(WinUsb_GetPowerPolicy(Device.Interface, AUTO_SUSPEND, &Length, Bytes) && Bytes[0] == 0, "AUTO_SUSPEND now %u\n",
       Bytes[0]);
    Value = 250;
    ok(WinUsb_SetPowerPolicy(Device.Interface, SUSPEND_DELAY, sizeof(Value), &Value), "Set SUSPEND_DELAY failed: %lu\n",
       GetLastError());
    Length = sizeof(Value);
    Value = 0;
    ok(WinUsb_GetPowerPolicy(Device.Interface, SUSPEND_DELAY, &Length, &Value) && Value == 250,
       "SUSPEND_DELAY now %lu\n", Value);
    SetLastError(0xdeadbeef);
    Result = WinUsb_SetPowerPolicy(Device.Interface, SUSPEND_DELAY, 1, Bytes);
    Error = GetLastError();
    ok(!Result && Error == ERROR_INVALID_PARAMETER, "Set SUSPEND_DELAY with 1 byte: %d %lu\n", Result, Error);
    SetLastError(0xdeadbeef);
    Value = 1;
    Result = WinUsb_SetPowerPolicy(Device.Interface, AUTO_SUSPEND, sizeof(Value), &Value);
    Error = GetLastError();
    ok(Result, "Set AUTO_SUSPEND with 4 bytes: %d %lu\n", Result, Error);
    Bytes[0] = 1;
    WinUsb_SetPowerPolicy(Device.Interface, AUTO_SUSPEND, 1, Bytes);
    CloseTestDevice(&Device);
}
