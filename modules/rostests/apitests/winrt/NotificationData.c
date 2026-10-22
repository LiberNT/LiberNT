/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests Windows.UI.Notifications.NotificationData and toast data updates
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "winrt_apitest.h"

#define WIDL_using_Windows_Data_Xml_Dom
#include <windows.data.xml.dom.h>
#define WIDL_using_Windows_UI_Notifications
#include <windows.ui.notifications.h>

static const WCHAR DataToastXml[] =
    L"<toast><visual><binding template=\"ToastGeneric\"><text>LiberNT data test</text>"
    L"<progress value=\"{progressValue}\" status=\"{progressStatus}\"/></binding></visual></toast>";

static HRESULT
PutValue(
    _In_ IMap_HSTRING_HSTRING *Map,
    _In_ const WCHAR *KeyText,
    _In_ const WCHAR *ValueText,
    _Out_ boolean *Replaced)
{
    HSTRING Key = CreateHString(KeyText), Value = CreateHString(ValueText);
    HRESULT hr;

    *Replaced = 0xcc;
    hr = IMap_HSTRING_HSTRING_Insert(Map, Key, Value, Replaced);
    WindowsDeleteString(Key);
    WindowsDeleteString(Value);
    return hr;
}

static BOOL
HasValue(
    _In_ INotificationData *Data,
    _In_ const WCHAR *KeyText,
    _In_ const WCHAR *Expected)
{
    IMap_HSTRING_HSTRING *Values;
    HSTRING Key = CreateHString(KeyText), Value = NULL;
    BOOL Match = FALSE;

    if (SUCCEEDED(INotificationData_get_Values(Data, &Values)))
    {
        if (SUCCEEDED(IMap_HSTRING_HSTRING_Lookup(Values, Key, &Value)))
            Match = !wcscmp(WindowsGetStringRawBuffer(Value, NULL), Expected);
        WindowsDeleteString(Value);
        IMap_HSTRING_HSTRING_Release(Values);
    }
    WindowsDeleteString(Key);
    return Match;
}

static INotificationData *
CreateData(
    _In_ INotificationDataFactory *Factory,
    _In_ const WCHAR *Progress,
    _In_ UINT32 Sequence)
{
    IIterable_IKeyValuePair_HSTRING_HSTRING *Iterable;
    INotificationData *Source = NULL, *Data = NULL;
    IActivationFactory *Activation;
    IMap_HSTRING_HSTRING *Values;
    IInspectable *Instance;
    boolean Replaced;

    if (FAILED(INotificationDataFactory_QueryInterface(Factory, &IID_IActivationFactory, (void **)&Activation)))
        return NULL;
    if (SUCCEEDED(IActivationFactory_ActivateInstance(Activation, &Instance)))
    {
        IInspectable_QueryInterface(Instance, &IID_INotificationData, (void **)&Source);
        IInspectable_Release(Instance);
    }
    IActivationFactory_Release(Activation);
    if (!Source)
        return NULL;
    if (SUCCEEDED(INotificationData_get_Values(Source, &Values)))
    {
        PutValue(Values, L"progressValue", Progress, &Replaced);
        PutValue(Values, L"progressStatus", L"Running", &Replaced);
        if (SUCCEEDED(IMap_HSTRING_HSTRING_QueryInterface(Values, &IID_IIterable_IKeyValuePair_HSTRING_HSTRING,
                                                          (void **)&Iterable)))
        {
            INotificationDataFactory_CreateNotificationDataWithValuesAndSequenceNumber(Factory, Iterable, Sequence,
                                                                                       &Data);
            IIterable_IKeyValuePair_HSTRING_HSTRING_Release(Iterable);
        }
        IMap_HSTRING_HSTRING_Release(Values);
    }
    INotificationData_Release(Source);
    return Data;
}

static void
TestData(
    _In_ INotificationDataFactory *Factory)
{
    IIterable_IKeyValuePair_HSTRING_HSTRING *Iterable;
    IMap_HSTRING_HSTRING *Values, *Again;
    INotificationData *Data, *Copy;
    IActivationFactory *Activation;
    IInspectable *Instance;
    boolean Replaced;
    HSTRING Name;
    UINT32 Value;
    HRESULT hr;

    hr = INotificationDataFactory_QueryInterface(Factory, &IID_IActivationFactory, (void **)&Activation);
    ok(hr == S_OK, "IActivationFactory query returned hr %#lx\n", hr);
    if (FAILED(hr))
        return;
    hr = IActivationFactory_ActivateInstance(Activation, &Instance);
    IActivationFactory_Release(Activation);
    ok(hr == S_OK, "ActivateInstance returned hr %#lx\n", hr);
    if (FAILED(hr))
        return;
    hr = IInspectable_GetRuntimeClassName(Instance, &Name);
    ok(hr == S_OK && !wcscmp(WindowsGetStringRawBuffer(Name, NULL), RuntimeClass_Windows_UI_Notifications_NotificationData),
       "class name hr %#lx %ls\n", hr, WindowsGetStringRawBuffer(Name, NULL));
    WindowsDeleteString(Name);
    hr = IInspectable_QueryInterface(Instance, &IID_INotificationData, (void **)&Data);
    IInspectable_Release(Instance);
    ok(hr == S_OK, "INotificationData query returned hr %#lx\n", hr);
    if (FAILED(hr))
        return;

    Value = 0xdead;
    hr = INotificationData_get_SequenceNumber(Data, &Value);
    ok(hr == S_OK && Value == 0, "default SequenceNumber hr %#lx value %u\n", hr, Value);
    hr = INotificationData_put_SequenceNumber(Data, 7);
    ok(hr == S_OK, "put_SequenceNumber returned hr %#lx\n", hr);
    INotificationData_get_SequenceNumber(Data, &Value);
    ok(Value == 7, "SequenceNumber %u\n", Value);

    hr = INotificationData_get_Values(Data, &Values);
    ok(hr == S_OK, "get_Values returned hr %#lx\n", hr);
    if (SUCCEEDED(hr))
    {
        Value = 0xdead;
        IMap_HSTRING_HSTRING_get_Size(Values, &Value);
        ok(Value == 0, "default Values size %u\n", Value);
        hr = IMap_HSTRING_HSTRING_GetRuntimeClassName(Values, &Name);
        ok(hr == S_OK && !wcscmp(WindowsGetStringRawBuffer(Name, NULL), L"Windows.Foundation.Collections.IMap`2<String, String>"),
           "Values class hr %#lx %ls\n", hr, WindowsGetStringRawBuffer(Name, NULL));
        WindowsDeleteString(Name);
        hr = PutValue(Values, L"progressValue", L"0.25", &Replaced);
        ok(hr == S_OK && !Replaced, "Insert returned hr %#lx replaced %u\n", hr, Replaced);
        hr = PutValue(Values, L"progressValue", L"0.5", &Replaced);
        ok(hr == S_OK && Replaced, "second Insert returned hr %#lx replaced %u\n", hr, Replaced);
        ok(HasValue(Data, L"progressValue", L"0.5"), "Values does not hold the inserted value\n");
        hr = INotificationData_get_Values(Data, &Again);
        ok(hr == S_OK && Again == Values, "second get_Values returned hr %#lx map %p, expected %p\n", hr, Again, Values);
        if (SUCCEEDED(hr))
            IMap_HSTRING_HSTRING_Release(Again);

        hr = IMap_HSTRING_HSTRING_QueryInterface(Values, &IID_IIterable_IKeyValuePair_HSTRING_HSTRING, (void **)&Iterable);
        ok(hr == S_OK, "IIterable query returned hr %#lx\n", hr);
        if (SUCCEEDED(hr))
        {
            Copy = NULL;
            hr = INotificationDataFactory_CreateNotificationDataWithValuesAndSequenceNumber(Factory, Iterable, 9, &Copy);
            ok(hr == S_OK && Copy, "CreateNotificationDataWithValuesAndSequenceNumber returned hr %#lx\n", hr);
            if (Copy)
            {
                INotificationData_get_SequenceNumber(Copy, &Value);
                ok(Value == 9, "copied SequenceNumber %u\n", Value);
                ok(HasValue(Copy, L"progressValue", L"0.5"), "copy lacks the source value\n");
                PutValue(Values, L"progressValue", L"0.75", &Replaced);
                ok(HasValue(Copy, L"progressValue", L"0.5"), "copy follows changes of the source map\n");
                INotificationData_Release(Copy);
            }
            Copy = NULL;
            hr = INotificationDataFactory_CreateNotificationDataWithValues(Factory, Iterable, &Copy);
            ok(hr == S_OK && Copy, "CreateNotificationDataWithValues returned hr %#lx\n", hr);
            if (Copy)
            {
                Value = 0xdead;
                INotificationData_get_SequenceNumber(Copy, &Value);
                ok(Value == 0, "CreateNotificationDataWithValues SequenceNumber %u\n", Value);
                INotificationData_Release(Copy);
            }
            IIterable_IKeyValuePair_HSTRING_HSTRING_Release(Iterable);
        }
        Copy = NULL;
        hr = INotificationDataFactory_CreateNotificationDataWithValues(Factory, NULL, &Copy);
        ok(hr == S_OK && Copy, "CreateNotificationDataWithValues(NULL) returned hr %#lx data %p\n", hr, Copy);
        if (Copy)
        {
            if (SUCCEEDED(INotificationData_get_Values(Copy, &Again)))
            {
                Value = 0xdead;
                IMap_HSTRING_HSTRING_get_Size(Again, &Value);
                ok(Value == 0, "CreateNotificationDataWithValues(NULL) Values size %u\n", Value);
                IMap_HSTRING_HSTRING_Release(Again);
            }
            INotificationData_Release(Copy);
        }
        IMap_HSTRING_HSTRING_Release(Values);
    }
    INotificationData_Release(Data);
}

static IToastNotification *
CreateTaggedToast(
    _In_ const WCHAR *TagText,
    _In_ const WCHAR *GroupText)
{
    IToastNotificationFactory *Factory;
    IToastNotification *Toast = NULL;
    IToastNotification2 *Toast2;
    IXmlDocumentIO *DocumentIO;
    IXmlDocument *Document;
    IInspectable *Instance;
    HSTRING Class, Text;

    Class = CreateHString(RuntimeClass_Windows_Data_Xml_Dom_XmlDocument);
    if (FAILED(RoActivateInstance(Class, &Instance)))
    {
        WindowsDeleteString(Class);
        return NULL;
    }
    WindowsDeleteString(Class);
    IInspectable_QueryInterface(Instance, &IID_IXmlDocument, (void **)&Document);
    IInspectable_QueryInterface(Instance, &IID_IXmlDocumentIO, (void **)&DocumentIO);
    IInspectable_Release(Instance);
    Text = CreateHString(DataToastXml);
    IXmlDocumentIO_LoadXml(DocumentIO, Text);
    WindowsDeleteString(Text);
    IXmlDocumentIO_Release(DocumentIO);
    if (SUCCEEDED(GetFactory(RuntimeClass_Windows_UI_Notifications_ToastNotification, &IID_IToastNotificationFactory,
                             (void **)&Factory)))
    {
        IToastNotificationFactory_CreateToastNotification(Factory, Document, &Toast);
        IToastNotificationFactory_Release(Factory);
    }
    IXmlDocument_Release(Document);
    if (Toast && SUCCEEDED(IToastNotification_QueryInterface(Toast, &IID_IToastNotification2, (void **)&Toast2)))
    {
        Text = CreateHString(TagText);
        IToastNotification2_put_Tag(Toast2, Text);
        WindowsDeleteString(Text);
        Text = CreateHString(GroupText);
        IToastNotification2_put_Group(Toast2, Text);
        WindowsDeleteString(Text);
        IToastNotification2_Release(Toast2);
    }
    return Toast;
}

static void
TestUpdate(
    _In_ INotificationDataFactory *Factory)
{
    IToastNotificationManagerStatics *Statics;
    INotificationData *First, *Second, *Older, *Current;
    IToastNotificationHistory *History;
    IToastNotificationManagerStatics2 *Statics2;
    NotificationUpdateResult Result;
    IToastNotification4 *Toast4;
    IToastNotifier2 *Notifier2;
    IToastNotifier *Notifier;
    IToastNotification *Toast;
    HSTRING Id, Tag, Group, Missing;
    WCHAR AppId[64];
    HRESULT hr;

    swprintf(AppId, ARRAYSIZE(AppId), L"LiberNT.ApiTest.ToastData.%lu", GetCurrentProcessId());
    hr = GetFactory(RuntimeClass_Windows_UI_Notifications_ToastNotificationManager,
                    &IID_IToastNotificationManagerStatics, (void **)&Statics);
    ok(hr == S_OK, "ToastNotificationManager returned hr %#lx\n", hr);
    if (FAILED(hr))
        return;
    Id = CreateHString(AppId);
    hr = IToastNotificationManagerStatics_CreateToastNotifierWithId(Statics, Id, &Notifier);
    ok(hr == S_OK, "CreateToastNotifierWithId returned hr %#lx\n", hr);
    if (FAILED(hr))
        goto done;
    hr = IToastNotifier_QueryInterface(Notifier, &IID_IToastNotifier2, (void **)&Notifier2);
    ok(hr == S_OK, "IToastNotifier2 query returned hr %#lx\n", hr);
    First = CreateData(Factory, L"0.1", 1);
    Second = CreateData(Factory, L"0.6", 2);
    Older = CreateData(Factory, L"0.2", 1);
    Toast = CreateTaggedToast(L"data", L"apitest");
    ok(First && Second && Older && Toast, "cannot create the data or the toast\n");
    if (FAILED(hr) || !First || !Second || !Older || !Toast)
        goto release;

    Tag = CreateHString(L"data");
    Group = CreateHString(L"apitest");
    Missing = CreateHString(L"missing");

    Result = 0xdead;
    hr = IToastNotifier2_UpdateWithTagAndGroup(Notifier2, First, Tag, Group, &Result);
    ok(hr == S_OK && Result == NotificationUpdateResult_NotificationNotFound,
       "Update before Show returned hr %#lx result %d\n", hr, Result);

    IToastNotification_QueryInterface(Toast, &IID_IToastNotification4, (void **)&Toast4);
    hr = IToastNotification4_put_Data(Toast4, First);
    ok(hr == S_OK, "put_Data returned hr %#lx\n", hr);
    Current = NULL;
    hr = IToastNotification4_get_Data(Toast4, &Current);
    ok(hr == S_OK && Current == First, "get_Data returned hr %#lx data %p, expected %p\n", hr, Current, First);
    if (Current)
        INotificationData_Release(Current);
    hr = IToastNotifier_Show(Notifier, Toast);
    ok(hr == S_OK, "Show returned hr %#lx\n", hr);

    Result = 0xdead;
    hr = IToastNotifier2_UpdateWithTagAndGroup(Notifier2, Second, Tag, Group, &Result);
    ok(hr == S_OK && Result == NotificationUpdateResult_Succeeded, "Update returned hr %#lx result %d\n", hr, Result);
    Current = NULL;
    IToastNotification4_get_Data(Toast4, &Current);
    ok(Current == First, "Data after Update is %p, expected the posted %p\n", Current, First);
    if (Current)
        INotificationData_Release(Current);
    ok(HasValue(First, L"progressValue", L"0.1"), "Update changed the posted data values\n");
    Result = 0xdead;
    hr = IToastNotifier2_UpdateWithTagAndGroup(Notifier2, Older, Tag, Group, &Result);
    ok(hr == S_OK && Result == NotificationUpdateResult_Succeeded,
       "Update with an older sequence returned hr %#lx result %d\n", hr, Result);
    Result = 0xdead;
    hr = IToastNotifier2_UpdateWithTag(Notifier2, Second, Tag, &Result);
    ok(hr == S_OK && Result == NotificationUpdateResult_NotificationNotFound,
       "UpdateWithTag of a grouped toast returned hr %#lx result %d\n", hr, Result);
    Result = 0xdead;
    hr = IToastNotifier2_UpdateWithTagAndGroup(Notifier2, Second, Missing, Group, &Result);
    ok(hr == S_OK && Result == NotificationUpdateResult_NotificationNotFound,
       "Update of a missing tag returned hr %#lx result %d\n", hr, Result);

    IToastNotifier_Hide(Notifier, Toast);
    hr = IToastNotificationManagerStatics_QueryInterface(Statics, &IID_IToastNotificationManagerStatics2,
                                                         (void **)&Statics2);
    if (SUCCEEDED(hr))
    {
        if (SUCCEEDED(IToastNotificationManagerStatics2_get_History(Statics2, &History)))
        {
            IToastNotificationHistory_ClearWithId(History, Id);
            IToastNotificationHistory_Release(History);
        }
        IToastNotificationManagerStatics2_Release(Statics2);
    }
    IToastNotification4_Release(Toast4);
    WindowsDeleteString(Tag);
    WindowsDeleteString(Group);
    WindowsDeleteString(Missing);

release:
    if (Toast)
        IToastNotification_Release(Toast);
    if (First)
        INotificationData_Release(First);
    if (Second)
        INotificationData_Release(Second);
    if (Older)
        INotificationData_Release(Older);
    if (Notifier2)
        IToastNotifier2_Release(Notifier2);
    IToastNotifier_Release(Notifier);
done:
    WindowsDeleteString(Id);
    IToastNotificationManagerStatics_Release(Statics);
}

START_TEST(NotificationData)
{
    INotificationDataFactory *Factory;
    HRESULT hr;

    hr = RoInitialize(RO_INIT_MULTITHREADED);
    ok(SUCCEEDED(hr), "RoInitialize failed, hr %#lx\n", hr);
    TraceActivatableClass(RuntimeClass_Windows_UI_Notifications_NotificationData);
    hr = GetFactory(RuntimeClass_Windows_UI_Notifications_NotificationData, &IID_INotificationDataFactory,
                    (void **)&Factory);
    ok(hr == S_OK, "NotificationData factory returned hr %#lx\n", hr);
    if (SUCCEEDED(hr))
    {
        TestData(Factory);
        TestUpdate(Factory);
        INotificationDataFactory_Release(Factory);
    }
    RoUninitialize();
}
