/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test the answers of KS multiple-item list properties to each buffer size
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <windows.h>
#include <setupapi.h>
#include <ks.h>
#include <ksmedia.h>

#define IOCTL_WAIT_MS 5000

static const GUID CategoryAudio = { STATIC_KSCATEGORY_AUDIO };
static const GUID PropsetPin = { STATIC_KSPROPSETID_Pin };
static const GUID PropsetTopology = { STATIC_KSPROPSETID_Topology };

static
HANDLE
OpenAudioFilter(VOID)
{
    SP_DEVICE_INTERFACE_DATA Interface = { sizeof(Interface) };
    PSP_DEVICE_INTERFACE_DETAIL_DATA_W Detail;
    HANDLE Filter = INVALID_HANDLE_VALUE;
    BYTE Buffer[1024];
    HDEVINFO Set;
    DWORD Index;

    Set = SetupDiGetClassDevsW(&CategoryAudio, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (Set == INVALID_HANDLE_VALUE)
        return INVALID_HANDLE_VALUE;

    Detail = (PSP_DEVICE_INTERFACE_DETAIL_DATA_W)Buffer;
    for (Index = 0;
         Filter == INVALID_HANDLE_VALUE && SetupDiEnumDeviceInterfaces(Set, NULL, &CategoryAudio, Index, &Interface);
         Index++)
    {
        Detail->cbSize = sizeof(*Detail);
        if (!SetupDiGetDeviceInterfaceDetailW(Set, &Interface, Detail, sizeof(Buffer), NULL, NULL))
            continue;
        Filter = CreateFileW(Detail->DevicePath, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING,
                             FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, NULL);
        if (Filter != INVALID_HANDLE_VALUE)
            trace("filter %ls\n", Detail->DevicePath);
    }

    SetupDiDestroyDeviceInfoList(Set);
    return Filter;
}

static
DWORD
QueryProperty(
    HANDLE Filter,
    PVOID Property,
    DWORD PropertyLength,
    PVOID Data,
    DWORD Length,
    DWORD *Returned)
{
    OVERLAPPED Overlapped = { 0 };
    DWORD Error = ERROR_SUCCESS;

    *Returned = 0;
    Overlapped.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!DeviceIoControl(Filter, IOCTL_KS_PROPERTY, Property, PropertyLength, Data, Length, Returned, &Overlapped))
    {
        Error = GetLastError();
        if (Error == ERROR_IO_PENDING)
        {
            if (WaitForSingleObject(Overlapped.hEvent, IOCTL_WAIT_MS) != WAIT_OBJECT_0)
            {
                CancelIo(Filter);
                GetOverlappedResult(Filter, &Overlapped, Returned, TRUE);
                CloseHandle(Overlapped.hEvent);
                return WAIT_TIMEOUT;
            }
            Error = GetOverlappedResult(Filter, &Overlapped, Returned, FALSE) ? ERROR_SUCCESS : GetLastError();
        }
    }
    CloseHandle(Overlapped.hEvent);
    return Error;
}

static
VOID
TestSizedList(
    HANDLE Filter,
    PVOID Property,
    DWORD PropertyLength,
    ULONG ItemSize,
    PCSTR Name)
{
    BYTE Data[4096];
    PKSMULTIPLE_ITEM Item = (PKSMULTIPLE_ITEM)Data;
    DWORD Error, Returned, Size;

    Error = QueryProperty(Filter, Property, PropertyLength, NULL, 0, &Returned);
    ok(Error == ERROR_MORE_DATA, "%s: zero-length query returned %lu\n", Name, Error);
    Size = Returned;
    ok(Size >= sizeof(KSMULTIPLE_ITEM) && (Size - sizeof(KSMULTIPLE_ITEM)) % ItemSize == 0 && Size <= sizeof(Data),
       "%s: required size %lu\n", Name, Size);
    if (Error != ERROR_MORE_DATA || Size < sizeof(KSMULTIPLE_ITEM) || Size > sizeof(Data))
        return;

    memset(Data, 0xA5, sizeof(Data));
    Error = QueryProperty(Filter, Property, PropertyLength, Data, sizeof(ULONG), &Returned);
    ok(Error == ERROR_SUCCESS, "%s: size query returned %lu\n", Name, Error);
    ok(Returned == sizeof(ULONG), "%s: size query returned %lu bytes\n", Name, Returned);
    ok(*(PULONG)Data == Size, "%s: size query answered %lu, expected %lu\n", Name, *(PULONG)Data, Size);

    memset(Data, 0xA5, sizeof(Data));
    Error = QueryProperty(Filter, Property, PropertyLength, Data, sizeof(KSMULTIPLE_ITEM), &Returned);
    ok(Error == ERROR_SUCCESS, "%s: header query returned %lu\n", Name, Error);
    ok(Returned == sizeof(KSMULTIPLE_ITEM), "%s: header query returned %lu bytes\n", Name, Returned);
    ok(Item->Size == Size, "%s: header Size %lu, expected %lu\n", Name, Item->Size, Size);
    ok(Item->Count == (Size - sizeof(KSMULTIPLE_ITEM)) / ItemSize, "%s: header Count %lu\n", Name, Item->Count);

    if (Size > sizeof(KSMULTIPLE_ITEM) + 1)
    {
        Error = QueryProperty(Filter, Property, PropertyLength, Data, Size - 1, &Returned);
        ok(Error == ERROR_INSUFFICIENT_BUFFER, "%s: short query returned %lu\n", Name, Error);
    }

    memset(Data, 0xA5, sizeof(Data));
    Error = QueryProperty(Filter, Property, PropertyLength, Data, Size, &Returned);
    ok(Error == ERROR_SUCCESS, "%s: full query returned %lu\n", Name, Error);
    ok(Returned == Size, "%s: full query returned %lu bytes, expected %lu\n", Name, Returned, Size);
    ok(Item->Size == Size, "%s: full Size %lu, expected %lu\n", Name, Item->Size, Size);
    ok(Item->Count == (Size - sizeof(KSMULTIPLE_ITEM)) / ItemSize, "%s: full Count %lu\n", Name, Item->Count);
}

START_TEST(SizedList)
{
    KSPROPERTY Categories;
    KSP_PIN Interfaces;
    HANDLE Filter;

    Filter = OpenAudioFilter();
    if (Filter == INVALID_HANDLE_VALUE)
    {
        skip("no audio filter\n");
        return;
    }

    Categories.Set = PropsetTopology;
    Categories.Id = KSPROPERTY_TOPOLOGY_CATEGORIES;
    Categories.Flags = KSPROPERTY_TYPE_GET;
    TestSizedList(Filter, &Categories, sizeof(Categories), sizeof(GUID), "TOPOLOGY_CATEGORIES");

    Interfaces.Property.Set = PropsetPin;
    Interfaces.Property.Id = KSPROPERTY_PIN_INTERFACES;
    Interfaces.Property.Flags = KSPROPERTY_TYPE_GET;
    Interfaces.PinId = 0;
    Interfaces.Reserved = 0;
    TestSizedList(Filter, &Interfaces, sizeof(Interfaces), sizeof(KSPIN_INTERFACE), "PIN_INTERFACES");

    CloseHandle(Filter);
}
