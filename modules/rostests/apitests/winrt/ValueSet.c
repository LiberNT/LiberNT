/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests Windows.Foundation.Collections ValueSet and PropertySet
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "winrt_apitest.h"

struct map_handler
{
    IMapChangedEventHandler_HSTRING_IInspectable IMapChangedEventHandler_iface;
    LONG Calls;
    CollectionChange Change;
    WCHAR Key[32];
    IObservableMap_HSTRING_IInspectable *Sender;
};

static HRESULT WINAPI
map_handler_QueryInterface(IMapChangedEventHandler_HSTRING_IInspectable *iface, REFIID iid, void **out)
{
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IAgileObject) ||
        IsEqualGUID(iid, &IID_IMapChangedEventHandler_HSTRING_IInspectable))
    {
        *out = iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI
map_handler_AddRef(IMapChangedEventHandler_HSTRING_IInspectable *iface)
{
    return 2;
}

static ULONG WINAPI
map_handler_Release(IMapChangedEventHandler_HSTRING_IInspectable *iface)
{
    return 1;
}

static HRESULT WINAPI
map_handler_Invoke(IMapChangedEventHandler_HSTRING_IInspectable *iface, IObservableMap_HSTRING_IInspectable *sender,
                   IMapChangedEventArgs_HSTRING *args)
{
    struct map_handler *Handler = CONTAINING_RECORD(iface, struct map_handler, IMapChangedEventHandler_iface);
    HSTRING Key = NULL;

    Handler->Calls++;
    Handler->Sender = sender;
    Handler->Change = 0xdead;
    Handler->Key[0] = 0;
    IMapChangedEventArgs_HSTRING_get_CollectionChanged(args, &Handler->Change);
    if (SUCCEEDED(IMapChangedEventArgs_HSTRING_get_Key(args, &Key)))
    {
        wcsncpy(Handler->Key, WindowsGetStringRawBuffer(Key, NULL), ARRAYSIZE(Handler->Key) - 1);
        WindowsDeleteString(Key);
    }
    return S_OK;
}

static IMapChangedEventHandler_HSTRING_IInspectableVtbl map_handler_vtbl =
{
    map_handler_QueryInterface,
    map_handler_AddRef,
    map_handler_Release,
    map_handler_Invoke,
};

static IInspectable *
CreateString(
    _In_ IPropertyValueStatics *Statics,
    _In_ const WCHAR *Text)
{
    IInspectable *Value = NULL;
    HSTRING String = CreateHString(Text);

    IPropertyValueStatics_CreateString(Statics, String, &Value);
    WindowsDeleteString(String);
    return Value;
}

static HRESULT
Insert(
    _In_ IMap_HSTRING_IInspectable *Map,
    _In_opt_ const WCHAR *KeyText,
    _In_opt_ IInspectable *Value,
    _Out_ boolean *Replaced)
{
    HSTRING Key = KeyText ? CreateHString(KeyText) : NULL;
    HRESULT hr;

    *Replaced = 0xcc;
    hr = IMap_HSTRING_IInspectable_Insert(Map, Key, Value, Replaced);
    WindowsDeleteString(Key);
    return hr;
}

static void
CheckClassName(
    _In_ IInspectable *Object,
    _In_ const WCHAR *Expected)
{
    HSTRING Name = NULL;
    HRESULT hr = IInspectable_GetRuntimeClassName(Object, &Name);

    ok(hr == S_OK, "GetRuntimeClassName returned hr %#lx\n", hr);
    if (SUCCEEDED(hr))
    {
        ok(!wcscmp(WindowsGetStringRawBuffer(Name, NULL), Expected), "class name %ls, expected %ls\n",
           WindowsGetStringRawBuffer(Name, NULL), Expected);
        WindowsDeleteString(Name);
    }
}

static IInspectable *
Activate(
    _In_ const WCHAR *ClassName)
{
    HSTRING Class = CreateHString(ClassName);
    IInspectable *Instance = NULL;
    HRESULT hr = RoActivateInstance(Class, &Instance);

    WindowsDeleteString(Class);
    ok(hr == S_OK, "%ls activation returned hr %#lx\n", ClassName, hr);
    return Instance;
}

static void
CheckKeys(
    _In_ IMap_HSTRING_IInspectable *Map,
    _In_ const WCHAR *const *Expected,
    _In_ UINT32 ExpectedCount)
{
    IIterable_IKeyValuePair_HSTRING_IInspectable *Iterable;
    IIterator_IKeyValuePair_HSTRING_IInspectable *Iterator;
    IKeyValuePair_HSTRING_IInspectable *Pair;
    boolean HasCurrent = FALSE;
    UINT32 Count = 0, Found = 0, i;
    HSTRING Key;

    if (FAILED(IMap_HSTRING_IInspectable_QueryInterface(Map, &IID_IIterable_IKeyValuePair_HSTRING_IInspectable,
                                                        (void **)&Iterable)))
    {
        ok(FALSE, "IIterable query failed\n");
        return;
    }
    if (SUCCEEDED(IIterable_IKeyValuePair_HSTRING_IInspectable_First(Iterable, &Iterator)))
    {
        IIterator_IKeyValuePair_HSTRING_IInspectable_get_HasCurrent(Iterator, &HasCurrent);
        while (HasCurrent)
        {
            Count++;
            if (SUCCEEDED(IIterator_IKeyValuePair_HSTRING_IInspectable_get_Current(Iterator, &Pair)))
            {
                if (SUCCEEDED(IKeyValuePair_HSTRING_IInspectable_get_Key(Pair, &Key)))
                {
                    for (i = 0; i < ExpectedCount; i++)
                    {
                        if (!wcscmp(WindowsGetStringRawBuffer(Key, NULL), Expected[i]))
                            Found++;
                    }
                    WindowsDeleteString(Key);
                }
                IKeyValuePair_HSTRING_IInspectable_Release(Pair);
            }
            IIterator_IKeyValuePair_HSTRING_IInspectable_MoveNext(Iterator, &HasCurrent);
        }
        IIterator_IKeyValuePair_HSTRING_IInspectable_Release(Iterator);
    }
    IIterable_IKeyValuePair_HSTRING_IInspectable_Release(Iterable);
    ok(Count == ExpectedCount && Found == ExpectedCount, "iterated %u keys, %u expected ones\n", Count, Found);
}

static void
TestMap(
    _In_ const WCHAR *ClassName,
    _In_ IPropertyValueStatics *Statics)
{
    struct map_handler Handler = { { &map_handler_vtbl } };
    IObservableMap_HSTRING_IInspectable *Observable = NULL;
    IMap_HSTRING_IInspectable *Map = NULL;
    IInspectable *Instance, *Value, *Other, *Found;
    IPropertySet *PropertySet;
    EventRegistrationToken Token;
    IPropertyValue *Property;
    boolean Replaced;
    HSTRING Key, Text;
    UINT32 Size;
    HRESULT hr;

    TraceActivatableClass(ClassName);
    if (!(Instance = Activate(ClassName)))
        return;
    CheckClassName(Instance, ClassName);
    hr = IInspectable_QueryInterface(Instance, &IID_IPropertySet, (void **)&PropertySet);
    ok(hr == S_OK, "%ls: IPropertySet query returned hr %#lx\n", ClassName, hr);
    if (SUCCEEDED(hr))
        IPropertySet_Release(PropertySet);
    hr = IInspectable_QueryInterface(Instance, &IID_IObservableMap_HSTRING_IInspectable, (void **)&Observable);
    ok(hr == S_OK, "%ls: IObservableMap query returned hr %#lx\n", ClassName, hr);
    hr = IInspectable_QueryInterface(Instance, &IID_IMap_HSTRING_IInspectable, (void **)&Map);
    ok(hr == S_OK, "%ls: IMap query returned hr %#lx\n", ClassName, hr);
    if (!Map || !Observable)
        goto done;

    hr = IObservableMap_HSTRING_IInspectable_add_MapChanged(Observable, &Handler.IMapChangedEventHandler_iface, &Token);
    ok(hr == S_OK, "%ls: add_MapChanged returned hr %#lx\n", ClassName, hr);

    Value = CreateString(Statics, L"one");
    hr = Insert(Map, L"b", Value, &Replaced);
    ok(hr == S_OK && Replaced == FALSE, "%ls: Insert(b) returned hr %#lx replaced %u\n", ClassName, hr, Replaced);
    ok(Handler.Calls == 1 && Handler.Change == CollectionChange_ItemInserted && !wcscmp(Handler.Key, L"b"),
       "%ls: insert raised %ld calls, change %u, key %ls\n", ClassName, Handler.Calls, Handler.Change, Handler.Key);
    ok(Handler.Sender == Observable, "%ls: sender %p, expected %p\n", ClassName, Handler.Sender, Observable);
    hr = Insert(Map, L"b", Value, &Replaced);
    ok(hr == S_OK && Replaced == TRUE, "%ls: second Insert(b) returned hr %#lx replaced %u\n", ClassName, hr, Replaced);
    ok(Handler.Calls == 2 && Handler.Change == CollectionChange_ItemChanged && !wcscmp(Handler.Key, L"b"),
       "%ls: replace raised %ld calls, change %u, key %ls\n", ClassName, Handler.Calls, Handler.Change, Handler.Key);
    hr = Insert(Map, L"a", Value, &Replaced);
    ok(hr == S_OK && Replaced == FALSE, "%ls: Insert(a) returned hr %#lx replaced %u\n", ClassName, hr, Replaced);
    hr = Insert(Map, L"C", Value, &Replaced);
    ok(hr == S_OK && Replaced == FALSE, "%ls: Insert(C) returned hr %#lx replaced %u\n", ClassName, hr, Replaced);
    hr = Insert(Map, L"B", Value, &Replaced);
    ok(hr == S_OK && Replaced == FALSE, "%ls: Insert(B) returned hr %#lx replaced %u\n", ClassName, hr, Replaced);
    {
        static const WCHAR *const Keys[] = { L"B", L"C", L"a", L"b" };

        CheckKeys(Map, Keys, ARRAYSIZE(Keys));
    }

    Key = CreateHString(L"b");
    Found = NULL;
    hr = IMap_HSTRING_IInspectable_Lookup(Map, Key, &Found);
    ok(hr == S_OK && Found == Value, "%ls: Lookup(b) returned hr %#lx value %p, expected %p\n", ClassName, hr, Found,
       Value);
    if (Found && SUCCEEDED(IInspectable_QueryInterface(Found, &IID_IPropertyValue, (void **)&Property)))
    {
        Text = NULL;
        ok(IPropertyValue_GetString(Property, &Text) == S_OK && !wcscmp(WindowsGetStringRawBuffer(Text, NULL), L"one"),
           "%ls: value string %ls\n", ClassName, WindowsGetStringRawBuffer(Text, NULL));
        WindowsDeleteString(Text);
        IPropertyValue_Release(Property);
    }
    if (Found)
        IInspectable_Release(Found);
    hr = IMap_HSTRING_IInspectable_Remove(Map, Key);
    ok(hr == S_OK, "%ls: Remove(b) returned hr %#lx\n", ClassName, hr);
    ok(Handler.Change == CollectionChange_ItemRemoved && !wcscmp(Handler.Key, L"b"),
       "%ls: remove raised change %u, key %ls\n", ClassName, Handler.Change, Handler.Key);
    Found = (IInspectable *)(ULONG_PTR)0xdeadbeef;
    hr = IMap_HSTRING_IInspectable_Lookup(Map, Key, &Found);
    ok(hr == E_BOUNDS && Found == NULL, "%ls: Lookup(removed) returned hr %#lx value %p\n", ClassName, hr, Found);
    hr = IMap_HSTRING_IInspectable_Remove(Map, Key);
    ok(hr == E_BOUNDS, "%ls: Remove(missing) returned hr %#lx\n", ClassName, hr);
    WindowsDeleteString(Key);

    hr = Insert(Map, L"null", NULL, &Replaced);
    ok(hr == S_OK && Replaced == FALSE, "%ls: Insert(NULL value) returned hr %#lx replaced %u\n", ClassName, hr,
       Replaced);
    hr = Insert(Map, NULL, Value, &Replaced);
    ok(hr == S_OK, "%ls: Insert(empty key) returned hr %#lx\n", ClassName, hr);

    Other = Activate(RuntimeClass_Windows_Foundation_Collections_PropertySet);
    if (Other)
    {
        hr = Insert(Map, L"object", Other, &Replaced);
        ok(hr == S_OK && Replaced == FALSE, "%ls: Insert(PropertySet) returned hr %#lx replaced %u\n", ClassName, hr,
           Replaced);
        IInspectable_Release(Other);
    }
    Other = Activate(RuntimeClass_Windows_Foundation_Collections_ValueSet);
    if (Other)
    {
        hr = Insert(Map, L"nested", Other, &Replaced);
        ok(hr == S_OK && Replaced == FALSE, "%ls: Insert(ValueSet) returned hr %#lx replaced %u\n", ClassName, hr,
           Replaced);
        IInspectable_Release(Other);
    }

    Size = 0;
    IMap_HSTRING_IInspectable_get_Size(Map, &Size);
    ok(Size == 7, "%ls: size %u\n", ClassName, Size);
    hr = IMap_HSTRING_IInspectable_Clear(Map);
    ok(hr == S_OK, "%ls: Clear returned hr %#lx\n", ClassName, hr);
    ok(Handler.Change == CollectionChange_Reset && !Handler.Key[0], "%ls: clear raised change %u, key %ls\n",
       ClassName, Handler.Change, Handler.Key);
    Size = 0xdead;
    IMap_HSTRING_IInspectable_get_Size(Map, &Size);
    ok(Size == 0, "%ls: size after Clear %u\n", ClassName, Size);

    hr = IObservableMap_HSTRING_IInspectable_remove_MapChanged(Observable, Token);
    ok(hr == S_OK, "%ls: remove_MapChanged returned hr %#lx\n", ClassName, hr);
    Handler.Calls = 0;
    hr = Insert(Map, L"after", Value, &Replaced);
    ok(hr == S_OK && Handler.Calls == 0, "%ls: Insert after remove_MapChanged returned hr %#lx, %ld calls\n",
       ClassName, hr, Handler.Calls);
    hr = IObservableMap_HSTRING_IInspectable_add_MapChanged(Observable, NULL, &Token);
    ok(hr == E_INVALIDARG, "%ls: add_MapChanged(NULL) returned hr %#lx\n", ClassName, hr);
    if (Value)
        IInspectable_Release(Value);

done:
    if (Map)
        IMap_HSTRING_IInspectable_Release(Map);
    if (Observable)
        IObservableMap_HSTRING_IInspectable_Release(Observable);
    IInspectable_Release(Instance);
}

START_TEST(ValueSet)
{
    IPropertyValueStatics *Statics;
    HRESULT hr;

    hr = RoInitialize(RO_INIT_MULTITHREADED);
    ok(SUCCEEDED(hr), "RoInitialize failed, hr %#lx\n", hr);
    hr = GetFactory(RuntimeClass_Windows_Foundation_PropertyValue, &IID_IPropertyValueStatics, (void **)&Statics);
    ok(hr == S_OK, "PropertyValue statics returned hr %#lx\n", hr);
    if (SUCCEEDED(hr))
    {
        TestMap(RuntimeClass_Windows_Foundation_Collections_ValueSet, Statics);
        TestMap(RuntimeClass_Windows_Foundation_Collections_PropertySet, Statics);
        IPropertyValueStatics_Release(Statics);
    }
    RoUninitialize();
}
