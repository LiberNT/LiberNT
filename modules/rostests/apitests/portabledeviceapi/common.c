/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Helpers for the Windows Portable Devices API tests
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

void
WpdFormatGuid(REFGUID Guid, char *Buffer, size_t Size)
{
    _snprintf(Buffer, Size, "{%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}",
              Guid->Data1, Guid->Data2, Guid->Data3,
              Guid->Data4[0], Guid->Data4[1], Guid->Data4[2], Guid->Data4[3],
              Guid->Data4[4], Guid->Data4[5], Guid->Data4[6], Guid->Data4[7]);
    Buffer[Size - 1] = 0;
}

void
WpdFormatKey(REFPROPERTYKEY Key, char *Buffer, size_t Size)
{
    char Guid[64];

    WpdFormatGuid(&Key->fmtid, Guid, sizeof(Guid));
    _snprintf(Buffer, Size, "%s.%lu", Guid, Key->pid);
    Buffer[Size - 1] = 0;
}

ULONG
WpdHash(const BYTE *Data, SIZE_T Size, ULONG Hash)
{
    SIZE_T i;

    for (i = 0; i < Size; i++)
    {
        Hash ^= Data[i];
        Hash *= 16777619;
    }
    return Hash;
}

void
WpdDumpBytes(const char *Prefix, const BYTE *Data, DWORD Size, DWORD Limit)
{
    char Line[3 * 32 + 1];
    DWORD i, n, Count = min(Size, Limit);

    for (i = 0; i < Count; i += 32)
    {
        Line[0] = 0;
        for (n = 0; n < 32 && i + n < Count; n++)
            sprintf(Line + 3 * n, "%02X ", Data[i + n]);
        trace("%s%04lX: %s\n", Prefix, i, Line);
    }
    if (Count < Size)
        trace("%s... %lu more bytes\n", Prefix, Size - Count);
}

static void
DumpUnknown(const char *Prefix, IUnknown *Unknown, int Depth)
{
    IPortableDeviceValues *Values;
    IPortableDeviceKeyCollection *Keys;
    IPortableDevicePropVariantCollection *Collection;
    IPortableDeviceValuesCollection *ValuesCollection;

    if (!Unknown)
    {
        trace("%sVT_UNKNOWN NULL\n", Prefix);
        return;
    }
    if (SUCCEEDED(IUnknown_QueryInterface(Unknown, &IID_IPortableDeviceValues, (void **)&Values)))
    {
        trace("%sIPortableDeviceValues\n", Prefix);
        WpdDumpValues(Prefix, Values, Depth + 1);
        IPortableDeviceValues_Release(Values);
    }
    else if (SUCCEEDED(IUnknown_QueryInterface(Unknown, &IID_IPortableDeviceKeyCollection, (void **)&Keys)))
    {
        trace("%sIPortableDeviceKeyCollection\n", Prefix);
        WpdDumpKeys(Prefix, Keys);
        IPortableDeviceKeyCollection_Release(Keys);
    }
    else if (SUCCEEDED(IUnknown_QueryInterface(Unknown, &IID_IPortableDevicePropVariantCollection, (void **)&Collection)))
    {
        trace("%sIPortableDevicePropVariantCollection\n", Prefix);
        WpdDumpPropVariants(Prefix, Collection, Depth + 1);
        IPortableDevicePropVariantCollection_Release(Collection);
    }
    else if (SUCCEEDED(IUnknown_QueryInterface(Unknown, &IID_IPortableDeviceValuesCollection, (void **)&ValuesCollection)))
    {
        DWORD Count = 0, i;
        char Sub[128];

        IPortableDeviceValuesCollection_GetCount(ValuesCollection, &Count);
        trace("%sIPortableDeviceValuesCollection count %lu\n", Prefix, Count);
        for (i = 0; i < Count; i++)
        {
            if (SUCCEEDED(IPortableDeviceValuesCollection_GetAt(ValuesCollection, i, &Values)))
            {
                _snprintf(Sub, sizeof(Sub), "%s  [%lu] ", Prefix, i);
                Sub[sizeof(Sub) - 1] = 0;
                WpdDumpValues(Sub, Values, Depth + 1);
                IPortableDeviceValues_Release(Values);
            }
        }
        IPortableDeviceValuesCollection_Release(ValuesCollection);
    }
    else
    {
        trace("%sVT_UNKNOWN other interface\n", Prefix);
    }
}

void
WpdDumpPropVariant(const char *Prefix, const PROPVARIANT *Value, int Depth)
{
    char Text[160];

    if (Depth > 4)
        return;

    switch (Value->vt)
    {
        case VT_EMPTY: trace("%sVT_EMPTY\n", Prefix); break;
        case VT_NULL: trace("%sVT_NULL\n", Prefix); break;
        case VT_I1: trace("%sVT_I1 %d\n", Prefix, Value->cVal); break;
        case VT_UI1: trace("%sVT_UI1 %u\n", Prefix, Value->bVal); break;
        case VT_I2: trace("%sVT_I2 %d\n", Prefix, Value->iVal); break;
        case VT_UI2: trace("%sVT_UI2 %u\n", Prefix, Value->uiVal); break;
        case VT_I4: trace("%sVT_I4 %ld\n", Prefix, Value->lVal); break;
        case VT_UI4: trace("%sVT_UI4 %lu (0x%lX)\n", Prefix, Value->ulVal, Value->ulVal); break;
        case VT_I8: trace("%sVT_I8 %I64d\n", Prefix, Value->hVal.QuadPart); break;
        case VT_UI8: trace("%sVT_UI8 %I64u (0x%I64X)\n", Prefix, Value->uhVal.QuadPart, Value->uhVal.QuadPart); break;
        case VT_R4: trace("%sVT_R4 %f\n", Prefix, (double)Value->fltVal); break;
        case VT_R8: trace("%sVT_R8 %f\n", Prefix, Value->dblVal); break;
        case VT_BOOL: trace("%sVT_BOOL %d\n", Prefix, Value->boolVal); break;
        case VT_ERROR: trace("%sVT_ERROR 0x%08lX\n", Prefix, Value->scode); break;
        case VT_DATE: trace("%sVT_DATE %f\n", Prefix, Value->date); break;
        case VT_FILETIME:
            trace("%sVT_FILETIME %08lX:%08lX\n", Prefix, Value->filetime.dwHighDateTime, Value->filetime.dwLowDateTime);
            break;
        case VT_CLSID:
            if (Value->puuid)
                WpdFormatGuid(Value->puuid, Text, sizeof(Text));
            else
                strcpy(Text, "NULL");
            trace("%sVT_CLSID %s\n", Prefix, Text);
            break;
        case VT_LPWSTR: trace("%sVT_LPWSTR \"%ls\"\n", Prefix, Value->pwszVal ? Value->pwszVal : L"(null)"); break;
        case VT_BSTR: trace("%sVT_BSTR \"%ls\"\n", Prefix, Value->bstrVal ? Value->bstrVal : L"(null)"); break;
        case VT_LPSTR: trace("%sVT_LPSTR \"%s\"\n", Prefix, Value->pszVal ? Value->pszVal : "(null)"); break;
        case VT_UNKNOWN: DumpUnknown(Prefix, Value->punkVal, Depth); break;
        case VT_BLOB:
            trace("%sVT_BLOB %lu bytes\n", Prefix, Value->blob.cbSize);
            WpdDumpBytes(Prefix, Value->blob.pBlobData, Value->blob.cbSize, 64);
            break;
        case VT_VECTOR | VT_UI1:
            trace("%sVT_VECTOR|VT_UI1 %lu bytes\n", Prefix, Value->caub.cElems);
            WpdDumpBytes(Prefix, Value->caub.pElems, Value->caub.cElems, 64);
            break;
        case VT_VECTOR | VT_UI4:
        {
            ULONG i;
            trace("%sVT_VECTOR|VT_UI4 %lu elements\n", Prefix, Value->caul.cElems);
            for (i = 0; i < Value->caul.cElems && i < 32; i++)
                trace("%s  [%lu] 0x%lX\n", Prefix, i, Value->caul.pElems[i]);
            break;
        }
        case VT_VECTOR | VT_LPWSTR:
        {
            ULONG i;
            trace("%sVT_VECTOR|VT_LPWSTR %lu elements\n", Prefix, Value->calpwstr.cElems);
            for (i = 0; i < Value->calpwstr.cElems && i < 32; i++)
                trace("%s  [%lu] \"%ls\"\n", Prefix, i, Value->calpwstr.pElems[i]);
            break;
        }
        default:
            trace("%svt 0x%X\n", Prefix, Value->vt);
            break;
    }
}

void
WpdDumpValues(const char *Prefix, IPortableDeviceValues *Values, int Depth)
{
    DWORD Count = 0, i;
    HRESULT hr;
    PROPERTYKEY Key;
    PROPVARIANT Value;
    char KeyText[96], Sub[256];

    hr = IPortableDeviceValues_GetCount(Values, &Count);
    trace("%svalues hr 0x%08lX count %lu\n", Prefix, hr, Count);
    for (i = 0; i < Count; i++)
    {
        PropVariantInit(&Value);
        hr = IPortableDeviceValues_GetAt(Values, i, &Key, &Value);
        if (FAILED(hr))
        {
            trace("%s  [%lu] GetAt hr 0x%08lX\n", Prefix, i, hr);
            continue;
        }
        WpdFormatKey(&Key, KeyText, sizeof(KeyText));
        _snprintf(Sub, sizeof(Sub), "%s  %s = ", Prefix, KeyText);
        Sub[sizeof(Sub) - 1] = 0;
        WpdDumpPropVariant(Sub, &Value, Depth);
        PropVariantClear(&Value);
    }
}

void
WpdDumpKeys(const char *Prefix, IPortableDeviceKeyCollection *Keys)
{
    DWORD Count = 0, i;
    PROPERTYKEY Key;
    char KeyText[96];

    IPortableDeviceKeyCollection_GetCount(Keys, &Count);
    trace("%skeys count %lu\n", Prefix, Count);
    for (i = 0; i < Count; i++)
    {
        if (SUCCEEDED(IPortableDeviceKeyCollection_GetAt(Keys, i, &Key)))
        {
            WpdFormatKey(&Key, KeyText, sizeof(KeyText));
            trace("%s  [%lu] %s\n", Prefix, i, KeyText);
        }
    }
}

void
WpdDumpPropVariants(const char *Prefix, IPortableDevicePropVariantCollection *Collection, int Depth)
{
    DWORD Count = 0, i;
    PROPVARIANT Value;
    char Sub[160];

    IPortableDevicePropVariantCollection_GetCount(Collection, &Count);
    trace("%scollection count %lu\n", Prefix, Count);
    for (i = 0; i < Count; i++)
    {
        PropVariantInit(&Value);
        if (SUCCEEDED(IPortableDevicePropVariantCollection_GetAt(Collection, i, &Value)))
        {
            _snprintf(Sub, sizeof(Sub), "%s  [%lu] ", Prefix, i);
            Sub[sizeof(Sub) - 1] = 0;
            WpdDumpPropVariant(Sub, &Value, Depth);
            PropVariantClear(&Value);
        }
    }
}

HRESULT
WpdCreateValues(IPortableDeviceValues **Values)
{
    return CoCreateInstance(&CLSID_PortableDeviceValues, NULL, CLSCTX_INPROC_SERVER,
                            &IID_IPortableDeviceValues, (void **)Values);
}

static
BOOL
WpdTestDevicePresent(const WCHAR *Match)
{
    HDEVINFO Set = SetupDiGetClassDevsW(NULL, NULL, NULL, DIGCF_PRESENT | DIGCF_ALLCLASSES);
    SP_DEVINFO_DATA Info = { sizeof(Info) };
    WCHAR Id[512], *At;
    DWORD Index;
    BOOL Present = FALSE;

    if (Set == INVALID_HANDLE_VALUE)
        return TRUE;
    for (Index = 0; !Present && SetupDiEnumDeviceInfo(Set, Index, &Info); Index++)
    {
        Id[0] = L'#';
        if (!SetupDiGetDeviceInstanceIdW(Set, &Info, Id + 1, ARRAYSIZE(Id) - 2, NULL))
            continue;
        lstrcatW(Id, L"#");
        for (At = Id; *At; At++)
        {
            if (*At == L'\\')
                *At = L'#';
        }
        _wcslwr(Id);
        Present = wcsstr(Id, Match) != NULL;
    }
    SetupDiDestroyDeviceInfoList(Set);
    return Present;
}

BOOL
WpdWaitForDevice(IPortableDeviceManager **Manager, WCHAR *PnPId, DWORD PnPIdChars)
{
    HRESULT hr;
    DWORD Count, Second;
    LPWSTR Ids[8];
    WCHAR Match[128];

    if (!GetEnvironmentVariableW(L"WPD_TEST_MATCH", Match, ARRAYSIZE(Match)))
        lstrcpyW(Match, L"#vid_46f4&pid_0004#");
    _wcslwr(Match);

    winetest_mute_threshold = 0x7fffffff;
    hr = CoCreateInstance(&CLSID_PortableDeviceManager, NULL, CLSCTX_INPROC_SERVER,
                          &IID_IPortableDeviceManager, (void **)Manager);
    ok(hr == S_OK, "CoCreateInstance(PortableDeviceManager) hr 0x%08lX\n", hr);
    if (FAILED(hr))
        return FALSE;

    WpdTraceIoctlsHookModules();

    if (!WpdTestDevicePresent(Match))
    {
        skip("no device matches \"%ls\"\n", Match);
        IPortableDeviceManager_Release(*Manager);
        *Manager = NULL;
        return FALSE;
    }
    for (Second = 0; Second < WPD_TEST_WAIT_SECONDS; Second++)
    {
        IPortableDeviceManager_RefreshDeviceList(*Manager);
        Count = ARRAYSIZE(Ids);
        hr = IPortableDeviceManager_GetDevices(*Manager, Ids, &Count);
        if (SUCCEEDED(hr) && Count)
        {
            DWORD i;
            BOOL Found = FALSE;

            for (i = 0; i < Count; i++)
            {
                WCHAR Lower[512];

                lstrcpynW(Lower, Ids[i], ARRAYSIZE(Lower));
                _wcslwr(Lower);
                trace("device %lu \"%ls\"\n", i, Ids[i]);
                if (!Found && wcsstr(Lower, Match))
                {
                    lstrcpynW(PnPId, Ids[i], PnPIdChars);
                    Found = TRUE;
                }
                CoTaskMemFree(Ids[i]);
            }
            if (Found)
            {
                trace("device after %lu s\n", Second);
                return TRUE;
            }
        }
        Sleep(1000);
    }
    skip("no WPD device after %u s\n", WPD_TEST_WAIT_SECONDS);
    IPortableDeviceManager_Release(*Manager);
    *Manager = NULL;
    return FALSE;
}

HRESULT
WpdOpenTestDevice(WPD_TEST_DEVICE *Test)
{
    IPortableDeviceManager *Manager;
    IPortableDeviceValues *ClientInfo;
    HRESULT hr;

    ZeroMemory(Test, sizeof(*Test));
    if (!WpdWaitForDevice(&Manager, Test->PnPId, ARRAYSIZE(Test->PnPId)))
        return E_FAIL;
    IPortableDeviceManager_Release(Manager);

    hr = CoCreateInstance(&CLSID_PortableDeviceFTM, NULL, CLSCTX_INPROC_SERVER,
                          &IID_IPortableDevice, (void **)&Test->Device);
    ok(hr == S_OK, "CoCreateInstance(PortableDeviceFTM) hr 0x%08lX\n", hr);
    if (FAILED(hr))
        return hr;
    WpdTraceIoctlsHookModules();

    hr = WpdCreateValues(&ClientInfo);
    ok(hr == S_OK, "CoCreateInstance(PortableDeviceValues) hr 0x%08lX\n", hr);
    if (FAILED(hr))
        return hr;
    IPortableDeviceValues_SetStringValue(ClientInfo, &WPD_CLIENT_NAME, L"LiberNT WPD test");
    IPortableDeviceValues_SetUnsignedIntegerValue(ClientInfo, &WPD_CLIENT_MAJOR_VERSION, 1);
    IPortableDeviceValues_SetUnsignedIntegerValue(ClientInfo, &WPD_CLIENT_MINOR_VERSION, 2);
    IPortableDeviceValues_SetUnsignedIntegerValue(ClientInfo, &WPD_CLIENT_REVISION, 3);
    IPortableDeviceValues_SetUnsignedIntegerValue(ClientInfo, &WPD_CLIENT_SECURITY_QUALITY_OF_SERVICE, SECURITY_IMPERSONATION);
    IPortableDeviceValues_SetUnsignedIntegerValue(ClientInfo, &WPD_CLIENT_DESIRED_ACCESS, GENERIC_READ | GENERIC_WRITE);

    hr = IPortableDevice_Open(Test->Device, Test->PnPId, ClientInfo);
    IPortableDeviceValues_Release(ClientInfo);
    ok(hr == S_OK, "Open hr 0x%08lX\n", hr);
    if (FAILED(hr))
        return hr;

    hr = IPortableDevice_Content(Test->Device, &Test->Content);
    ok(hr == S_OK, "Content hr 0x%08lX\n", hr);
    if (FAILED(hr))
        return hr;
    hr = IPortableDeviceContent_Properties(Test->Content, &Test->Properties);
    ok(hr == S_OK, "Properties hr 0x%08lX\n", hr);
    if (FAILED(hr))
        return hr;
    hr = IPortableDeviceContent_Transfer(Test->Content, &Test->Resources);
    ok(hr == S_OK, "Transfer hr 0x%08lX\n", hr);
    return hr;
}

void
WpdCloseTestDevice(WPD_TEST_DEVICE *Test)
{
    if (Test->Resources)
        IPortableDeviceResources_Release(Test->Resources);
    if (Test->Properties)
        IPortableDeviceProperties_Release(Test->Properties);
    if (Test->Content)
        IPortableDeviceContent_Release(Test->Content);
    if (Test->Device)
    {
        IPortableDevice_Close(Test->Device);
        IPortableDevice_Release(Test->Device);
    }
    ZeroMemory(Test, sizeof(*Test));
}

HRESULT
WpdFindStorage(WPD_TEST_DEVICE *Test, WCHAR *StorageId, DWORD StorageIdChars)
{
    IPortableDeviceCapabilities *Caps;
    IPortableDevicePropVariantCollection *Objects;
    PROPVARIANT Value;
    DWORD Count = 0;
    HRESULT hr;

    hr = IPortableDevice_Capabilities(Test->Device, &Caps);
    if (FAILED(hr))
        return hr;
    hr = IPortableDeviceCapabilities_GetFunctionalObjects(Caps, &WPD_FUNCTIONAL_CATEGORY_STORAGE, &Objects);
    IPortableDeviceCapabilities_Release(Caps);
    if (FAILED(hr))
        return hr;
    IPortableDevicePropVariantCollection_GetCount(Objects, &Count);
    hr = E_FAIL;
    if (Count)
    {
        PropVariantInit(&Value);
        if (SUCCEEDED(IPortableDevicePropVariantCollection_GetAt(Objects, 0, &Value)) && Value.vt == VT_LPWSTR)
        {
            lstrcpynW(StorageId, Value.pwszVal, StorageIdChars);
            hr = S_OK;
        }
        PropVariantClear(&Value);
    }
    IPortableDevicePropVariantCollection_Release(Objects);
    return hr;
}

HRESULT
WpdReadResource(WPD_TEST_DEVICE *Test, LPCWSTR ObjectId, BYTE **Data, SIZE_T *Size)
{
    IStream *Stream;
    DWORD Optimal = 0;
    HRESULT hr;
    BYTE *Buffer = NULL, *New;
    SIZE_T Used = 0, Capacity = 0;
    ULONG Read;

    *Data = NULL;
    *Size = 0;
    hr = IPortableDeviceResources_GetStream(Test->Resources, ObjectId, &WPD_RESOURCE_DEFAULT, STGM_READ, &Optimal, &Stream);
    if (FAILED(hr))
        return hr;
    if (!Optimal)
        Optimal = 0x10000;
    for (;;)
    {
        if (Capacity - Used < Optimal)
        {
            Capacity = Capacity * 2 + Optimal;
            if (Buffer)
                New = HeapReAlloc(GetProcessHeap(), 0, Buffer, Capacity);
            else
                New = HeapAlloc(GetProcessHeap(), 0, Capacity);
            if (!New)
            {
                hr = E_OUTOFMEMORY;
                break;
            }
            Buffer = New;
        }
        Read = 0;
        hr = IStream_Read(Stream, Buffer + Used, Optimal, &Read);
        Used += Read;
        if (FAILED(hr) || !Read)
            break;
    }
    IStream_Release(Stream);
    if (FAILED(hr))
    {
        HeapFree(GetProcessHeap(), 0, Buffer);
        return hr;
    }
    *Data = Buffer;
    *Size = Used;
    return S_OK;
}
