/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests the device manager, capabilities and content enumeration of a WPD device
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

static ULONG ObjectCount;

static void
TestManager(IPortableDeviceManager *Manager, LPCWSTR PnPId)
{
    static const WCHAR *Names[] =
    {
        PORTABLE_DEVICE_TYPE, PORTABLE_DEVICE_ICON, PORTABLE_DEVICE_NAMESPACE_TIMEOUT,
        PORTABLE_DEVICE_NAMESPACE_EXCLUDE_FROM_SHELL, PORTABLE_DEVICE_NAMESPACE_THUMBNAIL_CONTENT_TYPES,
        PORTABLE_DEVICE_IS_MASS_STORAGE, L"FriendlyName", L"DeviceDescription", L"Manufacturer",
        L"UseAutoPlay", L"EnableLegacySupport",
    };
    WCHAR Text[512];
    BYTE Data[1024];
    DWORD Chars, Size, Type, Count, i;
    LPWSTR Ids[8];
    HRESULT hr;

    trace("pnp id \"%ls\"\n", PnPId);

    Count = 0;
    hr = IPortableDeviceManager_GetDevices(Manager, NULL, &Count);
    ok(hr == S_OK, "GetDevices(NULL) hr 0x%08lX\n", hr);
    trace("GetDevices(NULL) count %lu\n", Count);
    Count = 0;
    hr = IPortableDeviceManager_GetPrivateDevices(Manager, NULL, &Count);
    trace("GetPrivateDevices(NULL) hr 0x%08lX count %lu\n", hr, Count);
    if (SUCCEEDED(hr) && Count)
    {
        Count = min(Count, ARRAYSIZE(Ids));
        if (SUCCEEDED(IPortableDeviceManager_GetPrivateDevices(Manager, Ids, &Count)))
        {
            for (i = 0; i < Count; i++)
            {
                trace("private device \"%ls\"\n", Ids[i]);
                CoTaskMemFree(Ids[i]);
            }
        }
    }

    Chars = 0;
    hr = IPortableDeviceManager_GetDeviceFriendlyName(Manager, PnPId, NULL, &Chars);
    trace("GetDeviceFriendlyName(NULL) hr 0x%08lX chars %lu\n", hr, Chars);
    Chars = 1;
    hr = IPortableDeviceManager_GetDeviceFriendlyName(Manager, PnPId, Text, &Chars);
    trace("GetDeviceFriendlyName(1) hr 0x%08lX chars %lu\n", hr, Chars);
    Chars = ARRAYSIZE(Text);
    hr = IPortableDeviceManager_GetDeviceFriendlyName(Manager, PnPId, Text, &Chars);
    ok(hr == S_OK, "GetDeviceFriendlyName hr 0x%08lX\n", hr);
    trace("friendly \"%ls\" chars %lu\n", SUCCEEDED(hr) ? Text : L"", Chars);
    Chars = ARRAYSIZE(Text);
    hr = IPortableDeviceManager_GetDeviceDescription(Manager, PnPId, Text, &Chars);
    ok(hr == S_OK, "GetDeviceDescription hr 0x%08lX\n", hr);
    trace("description \"%ls\" chars %lu\n", SUCCEEDED(hr) ? Text : L"", Chars);
    Chars = ARRAYSIZE(Text);
    hr = IPortableDeviceManager_GetDeviceManufacturer(Manager, PnPId, Text, &Chars);
    ok(hr == S_OK, "GetDeviceManufacturer hr 0x%08lX\n", hr);
    trace("manufacturer \"%ls\" chars %lu\n", SUCCEEDED(hr) ? Text : L"", Chars);
    Chars = ARRAYSIZE(Text);
    hr = IPortableDeviceManager_GetDeviceFriendlyName(Manager, L"\\\\?\\bogus", Text, &Chars);
    trace("GetDeviceFriendlyName(bogus) hr 0x%08lX\n", hr);

    for (i = 0; i < ARRAYSIZE(Names); i++)
    {
        Size = sizeof(Data);
        Type = 0xdead;
        ZeroMemory(Data, sizeof(Data));
        hr = IPortableDeviceManager_GetDeviceProperty(Manager, PnPId, Names[i], Data, &Size, &Type);
        trace("GetDeviceProperty(%ls) hr 0x%08lX type %lu size %lu\n", Names[i], hr, Type, Size);
        if (SUCCEEDED(hr))
        {
            if (Type == REG_SZ || Type == REG_EXPAND_SZ || Type == REG_MULTI_SZ)
                trace("  \"%ls\"\n", (WCHAR *)Data);
            else
                WpdDumpBytes("  ", Data, Size, 128);
        }
    }
}

static void
TestCapabilities(IPortableDevice *Device)
{
    IPortableDeviceCapabilities *Caps;
    IPortableDeviceKeyCollection *Commands;
    IPortableDevicePropVariantCollection *Categories, *Collection, *Formats;
    IPortableDeviceValues *Options;
    PROPERTYKEY Key;
    PROPVARIANT Category, Type, Format;
    DWORD Count = 0, i, j, k, Types, FormatCount;
    char Text[96];
    HRESULT hr;

    hr = IPortableDevice_Capabilities(Device, &Caps);
    ok(hr == S_OK, "Capabilities hr 0x%08lX\n", hr);
    if (FAILED(hr))
        return;

    hr = IPortableDeviceCapabilities_GetSupportedCommands(Caps, &Commands);
    ok(hr == S_OK, "GetSupportedCommands hr 0x%08lX\n", hr);
    if (SUCCEEDED(hr))
    {
        WpdDumpKeys("commands ", Commands);
        IPortableDeviceKeyCollection_GetCount(Commands, &Count);
        for (i = 0; i < Count; i++)
        {
            if (FAILED(IPortableDeviceKeyCollection_GetAt(Commands, i, &Key)))
                continue;
            hr = IPortableDeviceCapabilities_GetCommandOptions(Caps, &Key, &Options);
            WpdFormatKey(&Key, Text, sizeof(Text));
            trace("command options %s hr 0x%08lX\n", Text, hr);
            if (SUCCEEDED(hr))
            {
                WpdDumpValues("  ", Options, 0);
                IPortableDeviceValues_Release(Options);
            }
        }
        IPortableDeviceKeyCollection_Release(Commands);
    }

    hr = IPortableDeviceCapabilities_GetFunctionalCategories(Caps, &Categories);
    ok(hr == S_OK, "GetFunctionalCategories hr 0x%08lX\n", hr);
    if (SUCCEEDED(hr))
    {
        WpdDumpPropVariants("categories ", Categories, 0);
        Count = 0;
        IPortableDevicePropVariantCollection_GetCount(Categories, &Count);
        for (i = 0; i < Count; i++)
        {
            PropVariantInit(&Category);
            if (FAILED(IPortableDevicePropVariantCollection_GetAt(Categories, i, &Category)) || Category.vt != VT_CLSID)
                continue;
            WpdFormatGuid(Category.puuid, Text, sizeof(Text));
            hr = IPortableDeviceCapabilities_GetFunctionalObjects(Caps, Category.puuid, &Collection);
            trace("functional objects %s hr 0x%08lX\n", Text, hr);
            if (SUCCEEDED(hr))
            {
                WpdDumpPropVariants("  ", Collection, 0);
                IPortableDevicePropVariantCollection_Release(Collection);
            }
            hr = IPortableDeviceCapabilities_GetSupportedContentTypes(Caps, Category.puuid, &Collection);
            trace("content types %s hr 0x%08lX\n", Text, hr);
            if (SUCCEEDED(hr))
            {
                WpdDumpPropVariants("  ", Collection, 0);
                Types = 0;
                IPortableDevicePropVariantCollection_GetCount(Collection, &Types);
                for (j = 0; j < Types; j++)
                {
                    PropVariantInit(&Type);
                    if (FAILED(IPortableDevicePropVariantCollection_GetAt(Collection, j, &Type)) || Type.vt != VT_CLSID)
                        continue;
                    WpdFormatGuid(Type.puuid, Text, sizeof(Text));
                    hr = IPortableDeviceCapabilities_GetSupportedFormats(Caps, Type.puuid, &Formats);
                    trace("  formats %s hr 0x%08lX\n", Text, hr);
                    if (SUCCEEDED(hr))
                    {
                        WpdDumpPropVariants("    ", Formats, 0);
                        FormatCount = 0;
                        IPortableDevicePropVariantCollection_GetCount(Formats, &FormatCount);
                        for (k = 0; k < FormatCount; k++)
                        {
                            IPortableDeviceKeyCollection *FormatKeys;
                            PropVariantInit(&Format);
                            if (FAILED(IPortableDevicePropVariantCollection_GetAt(Formats, k, &Format)) || Format.vt != VT_CLSID)
                                continue;
                            WpdFormatGuid(Format.puuid, Text, sizeof(Text));
                            hr = IPortableDeviceCapabilities_GetSupportedFormatProperties(Caps, Format.puuid, &FormatKeys);
                            trace("    format properties %s hr 0x%08lX\n", Text, hr);
                            if (SUCCEEDED(hr))
                            {
                                WpdDumpKeys("      ", FormatKeys);
                                IPortableDeviceKeyCollection_Release(FormatKeys);
                            }
                            hr = IPortableDeviceCapabilities_GetFixedPropertyAttributes(Caps, Format.puuid, &WPD_OBJECT_NAME, &Options);
                            trace("    fixed attributes OBJECT_NAME hr 0x%08lX\n", hr);
                            if (SUCCEEDED(hr))
                            {
                                WpdDumpValues("      ", Options, 0);
                                IPortableDeviceValues_Release(Options);
                            }
                            PropVariantClear(&Format);
                        }
                        IPortableDevicePropVariantCollection_Release(Formats);
                    }
                    PropVariantClear(&Type);
                }
                IPortableDevicePropVariantCollection_Release(Collection);
            }
            PropVariantClear(&Category);
        }
        IPortableDevicePropVariantCollection_Release(Categories);
    }

    hr = IPortableDeviceCapabilities_GetSupportedEvents(Caps, &Collection);
    ok(hr == S_OK, "GetSupportedEvents hr 0x%08lX\n", hr);
    if (SUCCEEDED(hr))
    {
        WpdDumpPropVariants("events ", Collection, 0);
        Count = 0;
        IPortableDevicePropVariantCollection_GetCount(Collection, &Count);
        for (i = 0; i < Count; i++)
        {
            PropVariantInit(&Type);
            if (FAILED(IPortableDevicePropVariantCollection_GetAt(Collection, i, &Type)) || Type.vt != VT_CLSID)
                continue;
            WpdFormatGuid(Type.puuid, Text, sizeof(Text));
            hr = IPortableDeviceCapabilities_GetEventOptions(Caps, Type.puuid, &Options);
            trace("event options %s hr 0x%08lX\n", Text, hr);
            if (SUCCEEDED(hr))
            {
                WpdDumpValues("  ", Options, 0);
                IPortableDeviceValues_Release(Options);
            }
            PropVariantClear(&Type);
        }
        IPortableDevicePropVariantCollection_Release(Collection);
    }
    IPortableDeviceCapabilities_Release(Caps);
}

static void
DumpObject(WPD_TEST_DEVICE *Test, LPCWSTR ObjectId, int Depth)
{
    IPortableDeviceKeyCollection *Keys;
    IPortableDeviceValues *Values, *Attributes;
    IEnumPortableDeviceObjectIDs *Enum;
    LPWSTR Children[16];
    ULONG Fetched, i, Total = 0;
    GUID ContentType;
    PROPERTYKEY Key;
    DWORD Count;
    HRESULT hr;
    char Text[96];

    if (++ObjectCount > 200 || Depth > 6)
        return;

    trace("object \"%ls\" depth %d\n", ObjectId, Depth);
    hr = IPortableDeviceProperties_GetSupportedProperties(Test->Properties, ObjectId, &Keys);
    ok(hr == S_OK, "GetSupportedProperties(%ls) hr 0x%08lX\n", ObjectId, hr);
    if (SUCCEEDED(hr))
    {
        WpdDumpKeys("  supported ", Keys);
        Count = 0;
        IPortableDeviceKeyCollection_GetCount(Keys, &Count);
        for (i = 0; i < Count; i++)
        {
            if (FAILED(IPortableDeviceKeyCollection_GetAt(Keys, i, &Key)))
                continue;
            hr = IPortableDeviceProperties_GetPropertyAttributes(Test->Properties, ObjectId, &Key, &Attributes);
            WpdFormatKey(&Key, Text, sizeof(Text));
            trace("  attributes %s hr 0x%08lX\n", Text, hr);
            if (SUCCEEDED(hr))
            {
                WpdDumpValues("    ", Attributes, 0);
                IPortableDeviceValues_Release(Attributes);
            }
        }
        IPortableDeviceKeyCollection_Release(Keys);
    }
    hr = IPortableDeviceProperties_GetValues(Test->Properties, ObjectId, NULL, &Values);
    ok(hr == S_OK, "GetValues(%ls) hr 0x%08lX\n", ObjectId, hr);
    if (SUCCEEDED(hr))
    {
        WpdDumpValues("  ", Values, 0);
        ZeroMemory(&ContentType, sizeof(ContentType));
        IPortableDeviceValues_GetGuidValue(Values, &WPD_OBJECT_CONTENT_TYPE, &ContentType);
        IPortableDeviceValues_Release(Values);
    }

    hr = IPortableDeviceResources_GetSupportedResources(Test->Resources, ObjectId, &Keys);
    trace("  GetSupportedResources hr 0x%08lX\n", hr);
    if (SUCCEEDED(hr))
    {
        WpdDumpKeys("  resources ", Keys);
        IPortableDeviceKeyCollection_Release(Keys);
        hr = IPortableDeviceResources_GetResourceAttributes(Test->Resources, ObjectId, &WPD_RESOURCE_DEFAULT, &Attributes);
        trace("  GetResourceAttributes(DEFAULT) hr 0x%08lX\n", hr);
        if (SUCCEEDED(hr))
        {
            WpdDumpValues("    ", Attributes, 0);
            IPortableDeviceValues_Release(Attributes);
        }
    }

    if (IsEqualGUID(&ContentType, &WPD_CONTENT_TYPE_GENERIC_FILE) ||
        IsEqualGUID(&ContentType, &WPD_CONTENT_TYPE_DOCUMENT) ||
        IsEqualGUID(&ContentType, &WPD_CONTENT_TYPE_UNSPECIFIED))
    {
        BYTE *Data;
        SIZE_T Size;

        hr = WpdReadResource(Test, ObjectId, &Data, &Size);
        ok(hr == S_OK, "read %ls hr 0x%08lX\n", ObjectId, hr);
        if (SUCCEEDED(hr))
        {
            trace("  data size %Iu hash 0x%08lX\n", (size_t)Size, WpdHash(Data, Size, 2166136261u));
            WpdDumpBytes("  data ", Data, (DWORD)Size, 32);
            HeapFree(GetProcessHeap(), 0, Data);
        }
        return;
    }

    hr = IPortableDeviceContent_EnumObjects(Test->Content, 0, ObjectId, NULL, &Enum);
    ok(hr == S_OK, "EnumObjects(%ls) hr 0x%08lX\n", ObjectId, hr);
    if (FAILED(hr))
        return;
    for (;;)
    {
        Fetched = 0;
        hr = IEnumPortableDeviceObjectIDs_Next(Enum, ARRAYSIZE(Children), Children, &Fetched);
        if (FAILED(hr) || !Fetched)
            break;
        for (i = 0; i < Fetched; i++)
        {
            DumpObject(Test, Children[i], Depth + 1);
            CoTaskMemFree(Children[i]);
        }
        Total += Fetched;
        if (hr != S_OK)
            break;
    }
    trace("  children of \"%ls\" %lu last hr 0x%08lX\n", ObjectId, Total, hr);
    IEnumPortableDeviceObjectIDs_Release(Enum);
}

static void
TestRawCommand(IPortableDevice *Device)
{
    IPortableDeviceValues *Params, *Results;
    HRESULT hr;

    if (FAILED(WpdCreateValues(&Params)))
        return;
    IPortableDeviceValues_SetGuidValue(Params, &WPD_PROPERTY_COMMON_COMMAND_CATEGORY, &WPD_COMMAND_CAPABILITIES_GET_SUPPORTED_FUNCTIONAL_CATEGORIES.fmtid);
    IPortableDeviceValues_SetUnsignedIntegerValue(Params, &WPD_PROPERTY_COMMON_COMMAND_ID, WPD_COMMAND_CAPABILITIES_GET_SUPPORTED_FUNCTIONAL_CATEGORIES.pid);
    hr = IPortableDevice_SendCommand(Device, 0, Params, &Results);
    ok(hr == S_OK, "SendCommand hr 0x%08lX\n", hr);
    if (SUCCEEDED(hr))
    {
        WpdDumpValues("raw categories ", Results, 0);
        IPortableDeviceValues_Release(Results);
    }
    IPortableDeviceValues_Release(Params);

    if (FAILED(WpdCreateValues(&Params)))
        return;
    IPortableDeviceValues_SetGuidValue(Params, &WPD_PROPERTY_COMMON_COMMAND_CATEGORY, &WPD_COMMAND_MTP_EXT_GET_SUPPORTED_VENDOR_OPCODES.fmtid);
    IPortableDeviceValues_SetUnsignedIntegerValue(Params, &WPD_PROPERTY_COMMON_COMMAND_ID, WPD_COMMAND_MTP_EXT_GET_SUPPORTED_VENDOR_OPCODES.pid);
    hr = IPortableDevice_SendCommand(Device, 0, Params, &Results);
    trace("SendCommand(MTP vendor opcodes) hr 0x%08lX\n", hr);
    if (SUCCEEDED(hr))
    {
        WpdDumpValues("mtp vendor opcodes ", Results, 0);
        IPortableDeviceValues_Release(Results);
    }
    IPortableDeviceValues_Release(Params);
}


static void
TestErrors(WPD_TEST_DEVICE *Test)
{
    IPortableDeviceKeyCollection *Keys;
    IPortableDeviceValues *Values;
    IEnumPortableDeviceObjectIDs *Enum;
    IStream *Stream;
    DWORD Optimal;
    LPWSTR Ids[4];
    ULONG Fetched;
    HRESULT hr;
    static const PROPERTYKEY Bogus = { { 0x12345678, 0x1234, 0x1234, { 1, 2, 3, 4, 5, 6, 7, 8 } }, 99 };

    hr = IPortableDeviceProperties_GetValues(Test->Properties, L"bogus", NULL, &Values);
    trace("GetValues(bogus) hr 0x%08lX\n", hr);
    if (SUCCEEDED(hr))
        IPortableDeviceValues_Release(Values);
    hr = IPortableDeviceProperties_GetValues(Test->Properties, L"o99", NULL, &Values);
    trace("GetValues(o99) hr 0x%08lX\n", hr);
    if (SUCCEEDED(hr))
        IPortableDeviceValues_Release(Values);
    hr = IPortableDeviceProperties_GetSupportedProperties(Test->Properties, L"bogus", &Keys);
    trace("GetSupportedProperties(bogus) hr 0x%08lX\n", hr);
    if (SUCCEEDED(hr))
        IPortableDeviceKeyCollection_Release(Keys);
    hr = IPortableDeviceProperties_GetPropertyAttributes(Test->Properties, L"o6", &Bogus, &Values);
    trace("GetPropertyAttributes(o6, bogus) hr 0x%08lX\n", hr);
    if (SUCCEEDED(hr))
    {
        WpdDumpValues("  ", Values, 0);
        IPortableDeviceValues_Release(Values);
    }
    hr = CoCreateInstance(&CLSID_PortableDeviceKeyCollection, NULL, CLSCTX_INPROC_SERVER,
                          &IID_IPortableDeviceKeyCollection, (void **)&Keys);
    if (SUCCEEDED(hr))
    {
        IPortableDeviceKeyCollection_Add(Keys, &WPD_OBJECT_NAME);
        IPortableDeviceKeyCollection_Add(Keys, &Bogus);
        IPortableDeviceKeyCollection_Add(Keys, &WPD_OBJECT_DATE_MODIFIED);
        hr = IPortableDeviceProperties_GetValues(Test->Properties, L"o6", Keys, &Values);
        trace("GetValues(o6, name+bogus+date) hr 0x%08lX\n", hr);
        if (SUCCEEDED(hr))
        {
            WpdDumpValues("  ", Values, 0);
            IPortableDeviceValues_Release(Values);
        }
        hr = IPortableDeviceProperties_GetValues(Test->Properties, L"bogus", Keys, &Values);
        trace("GetValues(bogus, keys) hr 0x%08lX\n", hr);
        if (SUCCEEDED(hr))
            IPortableDeviceValues_Release(Values);
        IPortableDeviceKeyCollection_Release(Keys);
    }
    hr = IPortableDeviceContent_EnumObjects(Test->Content, 0, L"o6", NULL, &Enum);
    trace("EnumObjects(o6) hr 0x%08lX\n", hr);
    if (SUCCEEDED(hr))
    {
        Fetched = 0;
        hr = IEnumPortableDeviceObjectIDs_Next(Enum, ARRAYSIZE(Ids), Ids, &Fetched);
        trace("  Next hr 0x%08lX fetched %lu\n", hr, Fetched);
        while (Fetched)
            CoTaskMemFree(Ids[--Fetched]);
        IEnumPortableDeviceObjectIDs_Release(Enum);
    }
    hr = IPortableDeviceContent_EnumObjects(Test->Content, 0, L"bogus", NULL, &Enum);
    trace("EnumObjects(bogus) hr 0x%08lX\n", hr);
    if (SUCCEEDED(hr))
    {
        Fetched = 0;
        hr = IEnumPortableDeviceObjectIDs_Next(Enum, ARRAYSIZE(Ids), Ids, &Fetched);
        trace("  Next hr 0x%08lX fetched %lu\n", hr, Fetched);
        while (Fetched)
            CoTaskMemFree(Ids[--Fetched]);
        IEnumPortableDeviceObjectIDs_Release(Enum);
    }
    hr = IPortableDeviceContent_EnumObjects(Test->Content, 0, L"s10001", NULL, &Enum);
    if (SUCCEEDED(hr))
    {
        Fetched = 0;
        hr = IEnumPortableDeviceObjectIDs_Next(Enum, 1, Ids, &Fetched);
        trace("Next(1) hr 0x%08lX fetched %lu\n", hr, Fetched);
        while (Fetched)
            CoTaskMemFree(Ids[--Fetched]);
        hr = IEnumPortableDeviceObjectIDs_Skip(Enum, 1);
        trace("Skip(1) hr 0x%08lX\n", hr);
        hr = IEnumPortableDeviceObjectIDs_Reset(Enum);
        trace("Reset hr 0x%08lX\n", hr);
        hr = IEnumPortableDeviceObjectIDs_Skip(Enum, 10);
        trace("Skip(10) hr 0x%08lX\n", hr);
        IEnumPortableDeviceObjectIDs_Release(Enum);
    }
    Optimal = 0;
    hr = IPortableDeviceResources_GetStream(Test->Resources, L"o5", &WPD_RESOURCE_DEFAULT, STGM_READ, &Optimal, &Stream);
    trace("GetStream(folder) hr 0x%08lX optimal %lu\n", hr, Optimal);
    if (SUCCEEDED(hr))
    {
        BYTE Data[16];
        ULONG Read = 0;
        hr = IStream_Read(Stream, Data, sizeof(Data), &Read);
        trace("  Read hr 0x%08lX read %lu\n", hr, Read);
        IStream_Release(Stream);
    }
    Optimal = 0;
    hr = IPortableDeviceResources_GetStream(Test->Resources, L"bogus", &WPD_RESOURCE_DEFAULT, STGM_READ, &Optimal, &Stream);
    trace("GetStream(bogus) hr 0x%08lX\n", hr);
    if (SUCCEEDED(hr))
        IStream_Release(Stream);
    Optimal = 0;
    hr = IPortableDeviceResources_GetStream(Test->Resources, L"o6", &WPD_RESOURCE_DEFAULT, STGM_READ, &Optimal, &Stream);
    if (SUCCEEDED(hr))
    {
        STATSTG Stat;
        LARGE_INTEGER Move;
        ULARGE_INTEGER Position;
        BYTE Data[4];
        ULONG Read = 0;
        LPWSTR ObjectId = NULL;
        IPortableDeviceDataStream *DataStream;

        ZeroMemory(&Stat, sizeof(Stat));
        hr = IStream_Stat(Stream, &Stat, STATFLAG_DEFAULT);
        trace("Stat hr 0x%08lX name \"%ls\" type %lu size %I64u mode 0x%lX clsid-zero %d\n", hr,
              Stat.pwcsName ? Stat.pwcsName : L"(null)", Stat.type, Stat.cbSize.QuadPart, Stat.grfMode,
              IsEqualGUID(&Stat.clsid, &GUID_NULL));
        CoTaskMemFree(Stat.pwcsName);
        hr = IStream_Read(Stream, Data, 4, &Read);
        trace("Read(4) hr 0x%08lX read %lu\n", hr, Read);
        Move.QuadPart = 0;
        Position.QuadPart = 0xdead;
        hr = IStream_Seek(Stream, Move, STREAM_SEEK_CUR, &Position);
        trace("Seek(cur 0) hr 0x%08lX pos %I64u\n", hr, Position.QuadPart);
        Move.QuadPart = 2;
        Position.QuadPart = 0xdead;
        hr = IStream_Seek(Stream, Move, STREAM_SEEK_SET, &Position);
        trace("Seek(set 2) hr 0x%08lX pos %I64u\n", hr, Position.QuadPart);
        hr = IStream_Write(Stream, Data, 1, &Read);
        trace("Write on read stream hr 0x%08lX\n", hr);
        hr = IStream_Commit(Stream, STGC_DEFAULT);
        trace("Commit on read stream hr 0x%08lX\n", hr);
        if (SUCCEEDED(IStream_QueryInterface(Stream, &IID_IPortableDeviceDataStream, (void **)&DataStream)))
        {
            hr = IPortableDeviceDataStream_GetObjectID(DataStream, &ObjectId);
            trace("GetObjectID hr 0x%08lX \"%ls\"\n", hr, ObjectId ? ObjectId : L"(null)");
            CoTaskMemFree(ObjectId);
            IPortableDeviceDataStream_Release(DataStream);
        }
        IStream_Release(Stream);
    }
}

START_TEST(Device)
{
    IPortableDeviceManager *Manager;
    WPD_TEST_DEVICE Test;
    LPWSTR PnPId;
    HRESULT hr;

    WpdTraceIoctlsStart();
    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(WpdOpenTestDevice(&Test)))
    {
        WpdCloseTestDevice(&Test);
        CoUninitialize();
        return;
    }

    if (WpdWaitForDevice(&Manager, Test.PnPId, ARRAYSIZE(Test.PnPId)))
    {
        TestManager(Manager, Test.PnPId);
        IPortableDeviceManager_Release(Manager);
    }

    hr = IPortableDevice_GetPnPDeviceID(Test.Device, &PnPId);
    ok(hr == S_OK, "GetPnPDeviceID hr 0x%08lX\n", hr);
    if (SUCCEEDED(hr))
    {
        ok(!lstrcmpW(PnPId, Test.PnPId), "GetPnPDeviceID \"%ls\"\n", PnPId);
        CoTaskMemFree(PnPId);
    }

    TestCapabilities(Test.Device);
    TestRawCommand(Test.Device);
    ObjectCount = 0;
    DumpObject(&Test, WPD_DEVICE_OBJECT_ID, 0);
    trace("objects %lu\n", ObjectCount);
    TestErrors(&Test);

    WpdCloseTestDevice(&Test);
    CoUninitialize();
}
