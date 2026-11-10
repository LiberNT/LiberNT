/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests object creation, data transfer and deletion on a WPD device
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

typedef struct _EVENT_SINK
{
    IPortableDeviceEventCallback IPortableDeviceEventCallback_iface;
    LONG Ref;
    LONG Events;
} EVENT_SINK;

static HRESULT WINAPI
Sink_QueryInterface(IPortableDeviceEventCallback *iface, REFIID riid, void **ppv)
{
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IPortableDeviceEventCallback))
    {
        *ppv = iface;
        IPortableDeviceEventCallback_AddRef(iface);
        return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI
Sink_AddRef(IPortableDeviceEventCallback *iface)
{
    EVENT_SINK *Sink = CONTAINING_RECORD(iface, EVENT_SINK, IPortableDeviceEventCallback_iface);
    return InterlockedIncrement(&Sink->Ref);
}

static ULONG WINAPI
Sink_Release(IPortableDeviceEventCallback *iface)
{
    EVENT_SINK *Sink = CONTAINING_RECORD(iface, EVENT_SINK, IPortableDeviceEventCallback_iface);
    return InterlockedDecrement(&Sink->Ref);
}

static HRESULT WINAPI
Sink_OnEvent(IPortableDeviceEventCallback *iface, IPortableDeviceValues *Parameters)
{
    EVENT_SINK *Sink = CONTAINING_RECORD(iface, EVENT_SINK, IPortableDeviceEventCallback_iface);

    InterlockedIncrement(&Sink->Events);
    WpdDumpValues("event ", Parameters, 0);
    return S_OK;
}

static const IPortableDeviceEventCallbackVtbl SinkVtbl =
{
    Sink_QueryInterface,
    Sink_AddRef,
    Sink_Release,
    Sink_OnEvent,
};

static void
FillPattern(BYTE *Data, SIZE_T Size)
{
    SIZE_T i;

    for (i = 0; i < Size; i++)
        Data[i] = (BYTE)(i * 7 + (i >> 8));
}

static HRESULT
CreateFile_(WPD_TEST_DEVICE *Test, LPCWSTR ParentId, LPCWSTR Name, const BYTE *Data, SIZE_T Size, LPWSTR *ObjectId)
{
    IPortableDeviceValues *Values;
    IPortableDeviceDataStream *DataStream;
    IStream *Stream;
    DWORD Optimal = 0;
    LPWSTR Cookie = NULL;
    SIZE_T Done = 0;
    ULONG Written;
    HRESULT hr;

    *ObjectId = NULL;
    hr = WpdCreateValues(&Values);
    if (FAILED(hr))
        return hr;
    IPortableDeviceValues_SetStringValue(Values, &WPD_OBJECT_PARENT_ID, ParentId);
    IPortableDeviceValues_SetStringValue(Values, &WPD_OBJECT_NAME, Name);
    IPortableDeviceValues_SetStringValue(Values, &WPD_OBJECT_ORIGINAL_FILE_NAME, Name);
    IPortableDeviceValues_SetUnsignedLargeIntegerValue(Values, &WPD_OBJECT_SIZE, Size);
    IPortableDeviceValues_SetGuidValue(Values, &WPD_OBJECT_CONTENT_TYPE, &WPD_CONTENT_TYPE_GENERIC_FILE);
    IPortableDeviceValues_SetGuidValue(Values, &WPD_OBJECT_FORMAT, &WPD_OBJECT_FORMAT_UNSPECIFIED);

    hr = IPortableDeviceContent_CreateObjectWithPropertiesAndData(Test->Content, Values, &Stream, &Optimal, &Cookie);
    IPortableDeviceValues_Release(Values);
    ok(hr == S_OK, "CreateObjectWithPropertiesAndData(%ls) hr 0x%08lX\n", Name, hr);
    trace("create %ls optimal %lu cookie \"%ls\"\n", Name, Optimal, Cookie ? Cookie : L"(null)");
    CoTaskMemFree(Cookie);
    if (FAILED(hr))
        return hr;
    if (!Optimal)
        Optimal = 0x10000;

    while (Done < Size)
    {
        ULONG Chunk = (ULONG)min(Size - Done, Optimal);
        Written = 0;
        hr = IStream_Write(Stream, Data + Done, Chunk, &Written);
        if (FAILED(hr))
            break;
        Done += Written;
    }
    ok(hr == S_OK, "Write hr 0x%08lX done %Iu\n", hr, Done);
    hr = IStream_Commit(Stream, STGC_DEFAULT);
    ok(hr == S_OK, "Commit hr 0x%08lX\n", hr);
    if (SUCCEEDED(IStream_QueryInterface(Stream, &IID_IPortableDeviceDataStream, (void **)&DataStream)))
    {
        hr = IPortableDeviceDataStream_GetObjectID(DataStream, ObjectId);
        ok(hr == S_OK, "GetObjectID hr 0x%08lX\n", hr);
        trace("created \"%ls\"\n", *ObjectId ? *ObjectId : L"(null)");
        IPortableDeviceDataStream_Release(DataStream);
    }
    IStream_Release(Stream);
    return *ObjectId ? S_OK : E_FAIL;
}

static void
DeleteObject_(WPD_TEST_DEVICE *Test, LPCWSTR ObjectId, DWORD Options, HRESULT Expected)
{
    IPortableDevicePropVariantCollection *Ids, *Results = NULL;
    PROPVARIANT Value, Result;
    HRESULT hr;

    hr = CoCreateInstance(&CLSID_PortableDevicePropVariantCollection, NULL, CLSCTX_INPROC_SERVER,
                          &IID_IPortableDevicePropVariantCollection, (void **)&Ids);
    if (FAILED(hr))
        return;
    PropVariantInit(&Value);
    Value.vt = VT_LPWSTR;
    Value.pwszVal = (LPWSTR)ObjectId;
    IPortableDevicePropVariantCollection_Add(Ids, &Value);
    hr = IPortableDeviceContent_Delete(Test->Content, Options, Ids, &Results);
    ok(hr == (Expected == S_OK ? S_OK : S_FALSE), "Delete(%ls) hr 0x%08lX\n", ObjectId, hr);
    if (Results)
    {
        WpdDumpPropVariants("delete results ", Results, 0);
        PropVariantInit(&Result);
        hr = IPortableDevicePropVariantCollection_GetAt(Results, 0, &Result);
        ok(hr == S_OK && Result.vt == VT_ERROR && Result.scode == Expected, "Delete(%ls) result %u 0x%08lX\n",
           ObjectId, Result.vt, Result.scode);
        PropVariantClear(&Result);
        IPortableDevicePropVariantCollection_Release(Results);
    }
    IPortableDevicePropVariantCollection_Release(Ids);
}

static void
DumpChildren(WPD_TEST_DEVICE *Test, LPCWSTR ParentId)
{
    IEnumPortableDeviceObjectIDs *Enum;
    LPWSTR Ids[32];
    ULONG Fetched = 0, i;
    HRESULT hr;

    hr = IPortableDeviceContent_EnumObjects(Test->Content, 0, ParentId, NULL, &Enum);
    if (FAILED(hr))
        return;
    hr = IEnumPortableDeviceObjectIDs_Next(Enum, ARRAYSIZE(Ids), Ids, &Fetched);
    trace("children of \"%ls\" hr 0x%08lX fetched %lu\n", ParentId, hr, Fetched);
    for (i = 0; i < Fetched; i++)
    {
        trace("  \"%ls\"\n", Ids[i]);
        CoTaskMemFree(Ids[i]);
    }
    IEnumPortableDeviceObjectIDs_Release(Enum);
}

static void
TestPersistentIds(WPD_TEST_DEVICE *Test, LPCWSTR ObjectId)
{
    IPortableDeviceValues *Values;
    IPortableDevicePropVariantCollection *Unique, *Ids = NULL;
    PROPVARIANT Value;
    LPWSTR Persistent = NULL;
    HRESULT hr;

    hr = IPortableDeviceProperties_GetValues(Test->Properties, ObjectId, NULL, &Values);
    if (FAILED(hr))
        return;
    IPortableDeviceValues_GetStringValue(Values, &WPD_OBJECT_PERSISTENT_UNIQUE_ID, &Persistent);
    IPortableDeviceValues_Release(Values);
    if (!Persistent)
        return;
    hr = CoCreateInstance(&CLSID_PortableDevicePropVariantCollection, NULL, CLSCTX_INPROC_SERVER,
                          &IID_IPortableDevicePropVariantCollection, (void **)&Unique);
    if (SUCCEEDED(hr))
    {
        PropVariantInit(&Value);
        Value.vt = VT_LPWSTR;
        Value.pwszVal = Persistent;
        IPortableDevicePropVariantCollection_Add(Unique, &Value);
        Value.pwszVal = (LPWSTR)L"no-such-persistent-id";
        IPortableDevicePropVariantCollection_Add(Unique, &Value);
        hr = IPortableDeviceContent_GetObjectIDsFromPersistentUniqueIDs(Test->Content, Unique, &Ids);
        ok(hr == S_OK, "GetObjectIDsFromPersistentUniqueIDs hr 0x%08lX\n", hr);
        trace("persistent \"%ls\" for \"%ls\"\n", Persistent, ObjectId);
        if (Ids)
        {
            WpdDumpPropVariants("  ids ", Ids, 0);
            IPortableDevicePropVariantCollection_Release(Ids);
        }
        IPortableDevicePropVariantCollection_Release(Unique);
    }
    CoTaskMemFree(Persistent);
}

START_TEST(Transfer)
{
    static const SIZE_T Sizes[] = { 0, 1, 511, 512, 513, 65536, 300000 };
    WPD_TEST_DEVICE Test;
    WCHAR StorageId[128], Name[64];
    EVENT_SINK Sink = { { (IPortableDeviceEventCallbackVtbl *)&SinkVtbl }, 1, 0 };
    LPWSTR ObjectId, FolderId, Cookie = NULL;
    IPortableDeviceValues *Values, *Results;
    BYTE *Data, *Back;
    SIZE_T BackSize, i;
    HRESULT hr;

    WpdTraceIoctlsStart();
    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(WpdOpenTestDevice(&Test)))
    {
        WpdCloseTestDevice(&Test);
        CoUninitialize();
        return;
    }

    hr = IPortableDevice_Advise(Test.Device, 0, &Sink.IPortableDeviceEventCallback_iface, NULL, &Cookie);
    ok(hr == S_OK, "Advise hr 0x%08lX\n", hr);
    trace("advise cookie \"%ls\"\n", Cookie ? Cookie : L"(null)");

    hr = WpdFindStorage(&Test, StorageId, ARRAYSIZE(StorageId));
    ok(hr == S_OK, "storage hr 0x%08lX\n", hr);
    if (FAILED(hr))
        goto done;
    trace("storage \"%ls\"\n", StorageId);
    DumpChildren(&Test, StorageId);

    Data = HeapAlloc(GetProcessHeap(), 0, Sizes[ARRAYSIZE(Sizes) - 1]);
    FillPattern(Data, Sizes[ARRAYSIZE(Sizes) - 1]);
    for (i = 0; i < ARRAYSIZE(Sizes); i++)
    {
        _snwprintf(Name, ARRAYSIZE(Name), L"upload%lu.bin", (ULONG)Sizes[i]);
        if (FAILED(CreateFile_(&Test, StorageId, Name, Data, Sizes[i], &ObjectId)))
            continue;
        hr = IPortableDeviceProperties_GetValues(Test.Properties, ObjectId, NULL, &Values);
        ok(hr == S_OK, "GetValues(new) hr 0x%08lX\n", hr);
        if (SUCCEEDED(hr))
        {
            WpdDumpValues("  new ", Values, 0);
            IPortableDeviceValues_Release(Values);
        }
        hr = WpdReadResource(&Test, ObjectId, &Back, &BackSize);
        ok(hr == S_OK, "read back hr 0x%08lX\n", hr);
        if (SUCCEEDED(hr))
        {
            ok(BackSize == Sizes[i], "read back size %Iu expected %Iu\n", BackSize, Sizes[i]);
            ok(BackSize == Sizes[i] && !memcmp(Back, Data, BackSize), "read back data differs\n");
            HeapFree(GetProcessHeap(), 0, Back);
        }
        if (i == 1)
        {
            TestPersistentIds(&Test, ObjectId);
            hr = WpdCreateValues(&Values);
            if (SUCCEEDED(hr))
            {
                IPortableDeviceValues_SetStringValue(Values, &WPD_OBJECT_ORIGINAL_FILE_NAME, L"renamed.bin");
                Results = NULL;
                hr = IPortableDeviceProperties_SetValues(Test.Properties, ObjectId, Values, &Results);
                trace("SetValues(rename) hr 0x%08lX\n", hr);
                if (Results)
                {
                    WpdDumpValues("  rename ", Results, 0);
                    IPortableDeviceValues_Release(Results);
                }
                IPortableDeviceValues_Release(Values);
            }
        }
        DeleteObject_(&Test, ObjectId, PORTABLE_DEVICE_DELETE_NO_RECURSION, S_OK);
        CoTaskMemFree(ObjectId);
    }
    HeapFree(GetProcessHeap(), 0, Data);

    hr = WpdCreateValues(&Values);
    if (SUCCEEDED(hr))
    {
        IPortableDeviceValues_SetStringValue(Values, &WPD_OBJECT_PARENT_ID, StorageId);
        IPortableDeviceValues_SetStringValue(Values, &WPD_OBJECT_NAME, L"newdir");
        IPortableDeviceValues_SetStringValue(Values, &WPD_OBJECT_ORIGINAL_FILE_NAME, L"newdir");
        IPortableDeviceValues_SetGuidValue(Values, &WPD_OBJECT_CONTENT_TYPE, &WPD_CONTENT_TYPE_FOLDER);
        FolderId = NULL;
        hr = IPortableDeviceContent_CreateObjectWithPropertiesOnly(Test.Content, Values, &FolderId);
        ok(hr == S_OK, "CreateObjectWithPropertiesOnly hr 0x%08lX\n", hr);
        trace("folder \"%ls\"\n", FolderId ? FolderId : L"(null)");
        IPortableDeviceValues_Release(Values);
        if (SUCCEEDED(hr) && FolderId)
        {
            static const BYTE Small[] = "inside";
            DumpChildren(&Test, StorageId);
            if (SUCCEEDED(CreateFile_(&Test, FolderId, L"inner.txt", Small, sizeof(Small) - 1, &ObjectId)))
            {
                DumpChildren(&Test, FolderId);
                CoTaskMemFree(ObjectId);
            }
            DeleteObject_(&Test, FolderId, PORTABLE_DEVICE_DELETE_NO_RECURSION,
                          HRESULT_FROM_WIN32(ERROR_DIR_NOT_EMPTY));
            DeleteObject_(&Test, FolderId, PORTABLE_DEVICE_DELETE_WITH_RECURSION, S_OK);
            CoTaskMemFree(FolderId);
        }
    }
    DumpChildren(&Test, StorageId);

done:
    if (Cookie)
    {
        hr = IPortableDevice_Unadvise(Test.Device, Cookie);
        ok(hr == S_OK, "Unadvise hr 0x%08lX\n", hr);
        CoTaskMemFree(Cookie);
    }
    trace("events %ld\n", Sink.Events);
    WpdCloseTestDevice(&Test);
    CoUninitialize();
}
