/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests Windows.UI.Notifications toast manager, notifier, notification and history
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "winrt_apitest.h"
#include <stdlib.h>

#define WIDL_using_Windows_Data_Xml_Dom
#include <windows.data.xml.dom.h>
#define WIDL_using_Windows_UI_Notifications
#include <windows.ui.notifications.h>

static WCHAR UnregisteredAppId[64];
static WCHAR RegisteredAppId[64];
static WCHAR ShortcutAppId[64];
static WCHAR RegisteredKey[128];
static const WCHAR ToastXml[] =
    L"<toast launch=\"apitest-launch\"><visual><binding template=\"ToastGeneric\">"
    L"<text>LiberNT API test</text><text>Toast body</text></binding></visual></toast>";

struct failed_handler
{
    ITypedEventHandler_ToastNotification_ToastFailedEventArgs ITypedEventHandler_iface;
    LONG Calls;
    HRESULT Error;
};

struct dismissed_handler
{
    ITypedEventHandler_ToastNotification_ToastDismissedEventArgs ITypedEventHandler_iface;
    LONG Calls;
    ToastDismissalReason Reason;
    DWORD ThreadId;
    LONG RefCount;
    DWORD OwnerThreadId;
    DWORD WrongReleaseThreadId;
    EventRegistrationToken Token;
    HRESULT RemoveResult;
};

static HRESULT WINAPI
failed_QueryInterface(ITypedEventHandler_ToastNotification_ToastFailedEventArgs *iface, REFIID iid, void **out)
{
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IAgileObject) ||
        IsEqualGUID(iid, &IID_ITypedEventHandler_ToastNotification_ToastFailedEventArgs))
    {
        *out = iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI
failed_AddRef(ITypedEventHandler_ToastNotification_ToastFailedEventArgs *iface)
{
    return 2;
}

static ULONG WINAPI
failed_Release(ITypedEventHandler_ToastNotification_ToastFailedEventArgs *iface)
{
    return 1;
}

static HRESULT WINAPI
failed_Invoke(ITypedEventHandler_ToastNotification_ToastFailedEventArgs *iface,
              IToastNotification *sender, IToastFailedEventArgs *args)
{
    struct failed_handler *Handler = CONTAINING_RECORD(iface, struct failed_handler, ITypedEventHandler_iface);

    IToastFailedEventArgs_get_ErrorCode(args, &Handler->Error);
    InterlockedIncrement(&Handler->Calls);
    return S_OK;
}

static ITypedEventHandler_ToastNotification_ToastFailedEventArgsVtbl failed_vtbl =
{
    failed_QueryInterface, failed_AddRef, failed_Release, failed_Invoke
};

static HRESULT WINAPI
dismissed_QueryInterface(ITypedEventHandler_ToastNotification_ToastDismissedEventArgs *iface, REFIID iid, void **out)
{
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IAgileObject) ||
        IsEqualGUID(iid, &IID_ITypedEventHandler_ToastNotification_ToastDismissedEventArgs))
    {
        *out = iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI
dismissed_AddRef(ITypedEventHandler_ToastNotification_ToastDismissedEventArgs *iface)
{
    return 2;
}

static ULONG WINAPI
dismissed_Release(ITypedEventHandler_ToastNotification_ToastDismissedEventArgs *iface)
{
    return 1;
}

static HRESULT WINAPI
dismissed_Invoke(ITypedEventHandler_ToastNotification_ToastDismissedEventArgs *iface,
                 IToastNotification *sender, IToastDismissedEventArgs *args)
{
    struct dismissed_handler *Handler = CONTAINING_RECORD(iface, struct dismissed_handler, ITypedEventHandler_iface);

    IToastDismissedEventArgs_get_Reason(args, &Handler->Reason);
    Handler->ThreadId = GetCurrentThreadId();
    InterlockedIncrement(&Handler->Calls);
    return S_OK;
}

static ITypedEventHandler_ToastNotification_ToastDismissedEventArgsVtbl dismissed_vtbl =
{
    dismissed_QueryInterface, dismissed_AddRef, dismissed_Release, dismissed_Invoke
};

static HRESULT WINAPI
apartment_dismissed_QueryInterface(ITypedEventHandler_ToastNotification_ToastDismissedEventArgs *iface,
                                    REFIID iid, void **out)
{
    if (IsEqualGUID(iid, &IID_IUnknown) ||
        IsEqualGUID(iid, &IID_ITypedEventHandler_ToastNotification_ToastDismissedEventArgs))
    {
        *out = iface;
        InterlockedIncrement(&CONTAINING_RECORD(iface, struct dismissed_handler, ITypedEventHandler_iface)->RefCount);
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI
apartment_dismissed_AddRef(ITypedEventHandler_ToastNotification_ToastDismissedEventArgs *iface)
{
    return InterlockedIncrement(&CONTAINING_RECORD(iface, struct dismissed_handler, ITypedEventHandler_iface)->RefCount);
}

static ULONG WINAPI
apartment_dismissed_Release(ITypedEventHandler_ToastNotification_ToastDismissedEventArgs *iface)
{
    struct dismissed_handler *Handler = CONTAINING_RECORD(iface, struct dismissed_handler, ITypedEventHandler_iface);

    ULONG refs;
    DWORD thread = GetCurrentThreadId();

    if (Handler->OwnerThreadId && thread != Handler->OwnerThreadId)
        InterlockedCompareExchange((LONG *)&Handler->WrongReleaseThreadId, thread, 0);
    refs = InterlockedDecrement(&Handler->RefCount);
    if (!refs)
        free(Handler);
    return refs;
}

static HRESULT WINAPI
apartment_dismissed_Invoke(ITypedEventHandler_ToastNotification_ToastDismissedEventArgs *iface,
                           IToastNotification *sender, IToastDismissedEventArgs *args)
{
    struct dismissed_handler *Handler = CONTAINING_RECORD(iface, struct dismissed_handler, ITypedEventHandler_iface);

    IToastDismissedEventArgs_get_Reason(args, &Handler->Reason);
    Handler->ThreadId = GetCurrentThreadId();
    Handler->RemoveResult = IToastNotification_remove_Dismissed(sender, Handler->Token);
    InterlockedIncrement(&Handler->Calls);
    return S_OK;
}

static ITypedEventHandler_ToastNotification_ToastDismissedEventArgsVtbl apartment_dismissed_vtbl =
{
    apartment_dismissed_QueryInterface, apartment_dismissed_AddRef, apartment_dismissed_Release,
    apartment_dismissed_Invoke
};

static void
CheckXml(
    _In_ const char *Label,
    _In_ IXmlDocument *Document,
    _In_ const WCHAR *Expected)
{
    IXmlNodeSerializer *Serializer;
    HSTRING Xml;
    HRESULT hr;

    hr = IXmlDocument_QueryInterface(Document, &IID_IXmlNodeSerializer, (void **)&Serializer);
    ok(hr == S_OK, "%s: IXmlNodeSerializer query failed, hr %#lx\n", Label, hr);
    if (FAILED(hr))
        return;
    hr = IXmlNodeSerializer_GetXml(Serializer, &Xml);
    ok(hr == S_OK, "%s: GetXml failed, hr %#lx\n", Label, hr);
    if (SUCCEEDED(hr))
    {
        ok(!wcscmp(WindowsGetStringRawBuffer(Xml, NULL), Expected), "%s: got %ls\n", Label,
           WindowsGetStringRawBuffer(Xml, NULL));
        WindowsDeleteString(Xml);
    }
    IXmlNodeSerializer_Release(Serializer);
}

static IXmlDocument *
LoadToastXml(void)
{
    IXmlDocumentIO *DocumentIO;
    IXmlDocument *Document = NULL;
    IInspectable *Instance;
    HSTRING Class, Text;
    HRESULT hr;

    Class = CreateHString(RuntimeClass_Windows_Data_Xml_Dom_XmlDocument);
    hr = RoActivateInstance(Class, &Instance);
    WindowsDeleteString(Class);
    ok(hr == S_OK, "XmlDocument activation failed, hr %#lx\n", hr);
    if (FAILED(hr))
        return NULL;
    IInspectable_QueryInterface(Instance, &IID_IXmlDocument, (void **)&Document);
    hr = IInspectable_QueryInterface(Instance, &IID_IXmlDocumentIO, (void **)&DocumentIO);
    ok(hr == S_OK, "IXmlDocumentIO query failed, hr %#lx\n", hr);
    if (SUCCEEDED(hr))
    {
        Text = CreateHString(ToastXml);
        hr = IXmlDocumentIO_LoadXml(DocumentIO, Text);
        ok(hr == S_OK, "LoadXml failed, hr %#lx\n", hr);
        WindowsDeleteString(Text);
        IXmlDocumentIO_Release(DocumentIO);
    }
    IInspectable_Release(Instance);
    return Document;
}

static UINT32
HistoryCount(
    _In_ IToastNotificationHistory2 *History,
    _In_ const WCHAR *AppId)
{
    IVectorView_ToastNotification *View;
    HSTRING Id = CreateHString(AppId);
    UINT32 Size = 0xdeadbeef;
    HRESULT hr;

    hr = IToastNotificationHistory2_GetHistoryWithId(History, Id, &View);
    WindowsDeleteString(Id);
    if (FAILED(hr))
    {
        trace("GetHistoryWithId(%ls) hr %#lx\n", AppId, hr);
        return Size;
    }
    IVectorView_ToastNotification_get_Size(View, &Size);
    IVectorView_ToastNotification_Release(View);
    return Size;
}

static void
TestNotification(
    _In_ IXmlDocument *Document)
{
    static const WCHAR Tag64[] = L"0123456789012345678901234567890123456789012345678901234567890123";
    static const WCHAR Tag65[] = L"01234567890123456789012345678901234567890123456789012345678901234";
    IToastNotificationFactory *Factory;
    IToastNotification *Toast;
    IToastNotification2 *Toast2;
    IReference_DateTime *Expiration;
    IXmlDocument *Content;
    IUnknown *Unknown;
    HSTRING Text, Value;
    boolean Suppress;
    HRESULT hr;

    TraceActivatableClass(RuntimeClass_Windows_UI_Notifications_ToastNotification);
    hr = GetFactory(RuntimeClass_Windows_UI_Notifications_ToastNotification, &IID_IToastNotificationFactory, (void **)&Factory);
    ok(hr == S_OK, "ToastNotification factory failed, hr %#lx\n", hr);
    if (FAILED(hr))
        return;

    Toast = (IToastNotification *)(ULONG_PTR)0xdeadbeef;
    hr = IToastNotificationFactory_CreateToastNotification(Factory, NULL, &Toast);
    ok(hr == E_INVALIDARG, "CreateToastNotification(NULL) returned hr %#lx\n", hr);
    ok(Toast == NULL, "CreateToastNotification(NULL) returned toast %p\n", Toast);

    hr = IToastNotificationFactory_CreateToastNotification(Factory, Document, &Toast);
    ok(hr == S_OK, "CreateToastNotification failed, hr %#lx\n", hr);
    if (FAILED(hr))
    {
        IToastNotificationFactory_Release(Factory);
        return;
    }

    hr = IToastNotification_get_Content(Toast, &Content);
    ok(hr == S_OK, "get_Content failed, hr %#lx\n", hr);
    if (SUCCEEDED(hr))
    {
        ok(Content == Document, "Content is not the source document\n");
        IXmlDocument_Release(Content);
    }

    Expiration = (IReference_DateTime *)(ULONG_PTR)0xdeadbeef;
    hr = IToastNotification_get_ExpirationTime(Toast, &Expiration);
    ok(hr == S_OK && Expiration == NULL, "get_ExpirationTime returned hr %#lx value %p\n", hr, Expiration);
    if (SUCCEEDED(hr) && Expiration)
        IReference_DateTime_Release(Expiration);

    hr = IToastNotification_QueryInterface(Toast, &IID_IToastNotification2, (void **)&Toast2);
    ok(hr == S_OK, "IToastNotification2 query failed, hr %#lx\n", hr);
    if (SUCCEEDED(hr))
    {
        hr = IToastNotification2_get_Tag(Toast2, &Value);
        ok(hr == S_OK && !WindowsGetStringLen(Value), "default Tag returned hr %#lx '%ls'\n", hr,
           WindowsGetStringRawBuffer(Value, NULL));
        WindowsDeleteString(Value);
        Text = CreateHString(L"apitest-tag");
        hr = IToastNotification2_put_Tag(Toast2, Text);
        ok(hr == S_OK, "put_Tag failed, hr %#lx\n", hr);
        WindowsDeleteString(Text);
        hr = IToastNotification2_get_Tag(Toast2, &Value);
        ok(hr == S_OK && !wcscmp(WindowsGetStringRawBuffer(Value, NULL), L"apitest-tag"),
           "get_Tag returned hr %#lx '%ls'\n", hr, WindowsGetStringRawBuffer(Value, NULL));
        WindowsDeleteString(Value);
        Text = CreateHString(Tag64);
        hr = IToastNotification2_put_Tag(Toast2, Text);
        ok(hr == S_OK, "put_Tag(64 chars) returned hr %#lx\n", hr);
        WindowsDeleteString(Text);
        Text = CreateHString(Tag65);
        hr = IToastNotification2_put_Tag(Toast2, Text);
        ok(hr == WPN_E_TAG_SIZE, "put_Tag(65 chars) returned hr %#lx\n", hr);
        WindowsDeleteString(Text);
        Text = CreateHString(Tag65);
        hr = IToastNotification2_put_Group(Toast2, Text);
        ok(hr == WPN_E_GROUP_SIZE, "put_Group(65 chars) returned hr %#lx\n", hr);
        WindowsDeleteString(Text);
        hr = IToastNotification2_get_SuppressPopup(Toast2, &Suppress);
        ok(hr == S_OK && !Suppress, "default SuppressPopup returned hr %#lx value %d\n", hr, Suppress);
        IToastNotification2_Release(Toast2);
    }

    hr = IToastNotification_QueryInterface(Toast, &IID_IToastNotification3, (void **)&Unknown);
    ok(hr == S_OK, "IToastNotification3 query returned hr %#lx\n", hr);
    if (SUCCEEDED(hr))
        IUnknown_Release(Unknown);
    hr = IToastNotification_QueryInterface(Toast, &IID_IToastNotification4, (void **)&Unknown);
    ok(hr == S_OK, "IToastNotification4 query returned hr %#lx\n", hr);
    if (SUCCEEDED(hr))
        IUnknown_Release(Unknown);
    hr = IToastNotification_QueryInterface(Toast, &IID_IToastNotification6, (void **)&Unknown);
    ok(hr == S_OK, "IToastNotification6 query returned hr %#lx\n", hr);
    if (SUCCEEDED(hr))
        IUnknown_Release(Unknown);

    IToastNotification_Release(Toast);
    IToastNotificationFactory_Release(Factory);
}

static void
TestShow(
    _In_ IToastNotificationManagerStatics *Statics,
    _In_ IToastNotificationHistory2 *History,
    _In_ IXmlDocument *Document,
    _In_ const WCHAR *AppId,
    _In_ BOOL Popup)
{
    struct failed_handler Failed = {{&failed_vtbl}, 0, S_OK};
    struct dismissed_handler Dismissed = {{&dismissed_vtbl}, 0, -1};
    EventRegistrationToken FailedToken, DismissedToken;
    IToastNotificationFactory *Factory;
    NotificationSetting Setting;
    IToastNotifier *Notifier;
    IToastNotification *Toast;
    HSTRING Id;
    HRESULT hr;
    DWORD Start;
    LONG Calls;

    Id = CreateHString(AppId);
    hr = IToastNotificationManagerStatics_CreateToastNotifierWithId(Statics, Id, &Notifier);
    WindowsDeleteString(Id);
    ok(hr == S_OK, "%ls: CreateToastNotifierWithId failed, hr %#lx\n", AppId, hr);
    if (FAILED(hr))
        return;
    hr = IToastNotifier_get_Setting(Notifier, &Setting);
    ok(hr == HRESULT_FROM_WIN32(ERROR_NOT_FOUND), "%ls: get_Setting before the first toast returned hr %#lx\n", AppId, hr);

    hr = GetFactory(RuntimeClass_Windows_UI_Notifications_ToastNotification, &IID_IToastNotificationFactory, (void **)&Factory);
    if (FAILED(hr) || FAILED(IToastNotificationFactory_CreateToastNotification(Factory, Document, &Toast)))
    {
        ok(FALSE, "%ls: cannot create the toast\n", AppId);
        IToastNotifier_Release(Notifier);
        return;
    }
    IToastNotificationFactory_Release(Factory);

    hr = IToastNotification_add_Failed(Toast, &Failed.ITypedEventHandler_iface, &FailedToken);
    ok(hr == S_OK, "%ls: add_Failed failed, hr %#lx\n", AppId, hr);
    hr = IToastNotification_add_Dismissed(Toast, &Dismissed.ITypedEventHandler_iface, &DismissedToken);
    ok(hr == S_OK, "%ls: add_Dismissed failed, hr %#lx\n", AppId, hr);

    ok(HistoryCount(History, AppId) == 0, "%ls: history is not empty before Show\n", AppId);
    hr = IToastNotifier_Show(Notifier, Toast);
    ok(hr == S_OK, "%ls: Show returned hr %#lx\n", AppId, hr);
    Start = GetTickCount();
    while (GetTickCount() - Start < 2000 && !Failed.Calls)
        Sleep(50);
    ok(!Failed.Calls, "%ls: Failed raised with %#lx\n", AppId, Failed.Error);
    ok(!Dismissed.Calls, "%ls: Dismissed raised within 2 s, reason %d\n", AppId, Dismissed.Reason);
    ok(HistoryCount(History, AppId) == 1, "%ls: toast is not in the history after Show\n", AppId);
    if (Popup)
    {
        DWORD Report = GetTickCount();

        while (GetTickCount() - Start < 20000 && !Dismissed.Calls)
        {
            Sleep(50);
            if (GetTickCount() - Report >= 1000)
            {
                trace("%ls: waiting for the popup to time out\n", AppId);
                Report = GetTickCount();
            }
        }
    }
    if (Popup)
        ok(Dismissed.Calls == 1 && Dismissed.Reason == ToastDismissalReason_TimedOut,
           "%ls: popup dismissed %ld times, reason %d\n", AppId, Dismissed.Calls, Dismissed.Reason);
    else
        ok(!Dismissed.Calls, "%ls: a toast without identity raised Dismissed, reason %d\n", AppId, Dismissed.Reason);
    ok(HistoryCount(History, AppId) == 1, "%ls: toast left the history after the popup\n", AppId);
    hr = IToastNotifier_get_Setting(Notifier, &Setting);
    ok(hr == S_OK && Setting == NotificationSetting_Enabled, "%ls: get_Setting after Show returned hr %#lx value %d\n",
       AppId, hr, Setting);

    Calls = Dismissed.Calls;
    hr = IToastNotifier_Hide(Notifier, Toast);
    ok(hr == S_OK, "%ls: Hide returned hr %#lx\n", AppId, hr);
    Start = GetTickCount();
    while (GetTickCount() - Start < 2000 && Dismissed.Calls == Calls)
        Sleep(50);
    ok(Dismissed.Calls == Calls + 1 && Dismissed.Reason == ToastDismissalReason_ApplicationHidden,
       "%ls: Hide dismissed %ld times, reason %d\n", AppId, Dismissed.Calls - Calls, Dismissed.Reason);
    ok(HistoryCount(History, AppId) == 0, "%ls: toast is still in the history after Hide\n", AppId);

    IToastNotification_remove_Failed(Toast, FailedToken);
    IToastNotification_remove_Dismissed(Toast, DismissedToken);
    IToastNotification_Release(Toast);
    IToastNotifier_Release(Notifier);
}

START_TEST(ToastNotification)
{
    IToastNotificationManagerStatics *Statics;
    IToastNotificationManagerStatics2 *Statics2;
    IToastNotificationHistory2 *History2 = NULL;
    IToastNotificationHistory *History = NULL;
    IToastNotifier *Notifier;
    IXmlDocument *Template, *Document;
    WCHAR ShortcutPath[MAX_PATH];
    HSTRING Id;
    HKEY Key;
    HRESULT hr;

    hr = RoInitialize(RO_INIT_MULTITHREADED);
    ok(SUCCEEDED(hr), "RoInitialize failed, hr %#lx\n", hr);

    swprintf(UnregisteredAppId, ARRAYSIZE(UnregisteredAppId), L"LiberNT.ApiTest.ToastUnregistered.%lu", GetCurrentProcessId());
    swprintf(RegisteredAppId, ARRAYSIZE(RegisteredAppId), L"LiberNT.ApiTest.ToastRegistered.%lu", GetCurrentProcessId());
    swprintf(ShortcutAppId, ARRAYSIZE(ShortcutAppId), L"LiberNT.ApiTest.ToastShortcut.%lu", GetCurrentProcessId());
    swprintf(RegisteredKey, ARRAYSIZE(RegisteredKey), L"Software\\Classes\\AppUserModelId\\%ls", RegisteredAppId);

    TraceActivatableClass(RuntimeClass_Windows_UI_Notifications_ToastNotificationManager);
    hr = GetFactory(RuntimeClass_Windows_UI_Notifications_ToastNotificationManager,
                    &IID_IToastNotificationManagerStatics, (void **)&Statics);
    ok(hr == S_OK, "ToastNotificationManager activation failed, hr %#lx\n", hr);
    if (FAILED(hr))
    {
        RoUninitialize();
        return;
    }

    hr = IToastNotificationManagerStatics_GetTemplateContent(Statics, ToastTemplateType_ToastText02, &Template);
    ok(hr == S_OK, "GetTemplateContent failed, hr %#lx\n", hr);
    if (SUCCEEDED(hr))
    {
        CheckXml("ToastText02", Template,
                 L"<toast><visual><binding template=\"ToastText02\"><text id=\"1\"></text><text id=\"2\"></text>"
                 L"</binding></visual></toast>");
        IXmlDocument_Release(Template);
    }
    hr = IToastNotificationManagerStatics_GetTemplateContent(Statics, ToastTemplateType_ToastImageAndText04, &Template);
    ok(hr == S_OK, "GetTemplateContent failed, hr %#lx\n", hr);
    if (SUCCEEDED(hr))
    {
        CheckXml("ToastImageAndText04", Template,
                 L"<toast><visual><binding template=\"ToastImageAndText04\"><image id=\"1\" src=\"\"/>"
                 L"<text id=\"1\"></text><text id=\"2\"></text><text id=\"3\"></text></binding></visual></toast>");
        IXmlDocument_Release(Template);
    }
    Template = (IXmlDocument *)(ULONG_PTR)0xdeadbeef;
    hr = IToastNotificationManagerStatics_GetTemplateContent(Statics, 99, &Template);
    ok(hr == E_INVALIDARG && Template == NULL, "GetTemplateContent(99) returned hr %#lx doc %p\n", hr, Template);

    Notifier = (IToastNotifier *)(ULONG_PTR)0xdeadbeef;
    hr = IToastNotificationManagerStatics_CreateToastNotifier(Statics, &Notifier);
    ok(hr == HRESULT_FROM_WIN32(ERROR_NOT_FOUND) && Notifier == NULL,
       "CreateToastNotifier returned hr %#lx notifier %p\n", hr, Notifier);
    Notifier = (IToastNotifier *)(ULONG_PTR)0xdeadbeef;
    hr = IToastNotificationManagerStatics_CreateToastNotifierWithId(Statics, NULL, &Notifier);
    ok(hr == E_INVALIDARG && Notifier == NULL, "CreateToastNotifierWithId(NULL) returned hr %#lx notifier %p\n",
       hr, Notifier);

    hr = IToastNotificationManagerStatics_QueryInterface(Statics, &IID_IToastNotificationManagerStatics2, (void **)&Statics2);
    ok(hr == S_OK, "IToastNotificationManagerStatics2 query failed, hr %#lx\n", hr);
    if (SUCCEEDED(hr))
    {
        hr = IToastNotificationManagerStatics2_get_History(Statics2, &History);
        ok(hr == S_OK, "get_History failed, hr %#lx\n", hr);
        if (SUCCEEDED(hr))
            IToastNotificationHistory_QueryInterface(History, &IID_IToastNotificationHistory2, (void **)&History2);
        IToastNotificationManagerStatics2_Release(Statics2);
    }

    Document = LoadToastXml();
    if (Document)
    {
        TestNotification(Document);
        if (History2)
        {
            TestShow(Statics, History2, Document, UnregisteredAppId, FALSE);
            if (!RegCreateKeyExW(HKEY_CURRENT_USER, RegisteredKey, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &Key, NULL))
            {
                RegSetValueExW(Key, L"DisplayName", 0, REG_SZ, (const BYTE *)L"LiberNT API test",
                               sizeof(L"LiberNT API test"));
                RegCloseKey(Key);
                TestShow(Statics, History2, Document, RegisteredAppId, TRUE);
                Id = CreateHString(RegisteredAppId);
                hr = IToastNotificationHistory_ClearWithId(History, Id);
                ok(hr == S_OK, "ClearWithId returned hr %#lx\n", hr);
                ok(HistoryCount(History2, RegisteredAppId) == 0, "history is not empty after ClearWithId\n");
                WindowsDeleteString(Id);
                RegDeleteKeyW(HKEY_CURRENT_USER, RegisteredKey);
            }
            if (CreateAppShortcut(ShortcutPath, ShortcutAppId))
            {
                Sleep(3000);
                TestShow(Statics, History2, Document, ShortcutAppId, TRUE);
                DeleteFileW(ShortcutPath);
            }
        }
        IXmlDocument_Release(Document);
    }

    if (History2)
        IToastNotificationHistory2_Release(History2);
    if (History)
        IToastNotificationHistory_Release(History);
    IToastNotificationManagerStatics_Release(Statics);
    RoUninitialize();
}

START_TEST(ToastCallback)
{
    struct dismissed_handler *Handler;
    static struct dismissed_handler Agile = {{&dismissed_vtbl}, 0, -1, 0};
    IToastNotificationManagerStatics *Statics = NULL;
    IToastNotificationFactory *Factory = NULL;
    IToastNotification *Toast = NULL;
    IToastNotifier *Notifier = NULL;
    IXmlDocument *Document = NULL;
    EventRegistrationToken Token, AgileToken;
    WCHAR AppId[96], KeyName[160], ShortcutPath[MAX_PATH];
    HSTRING Id;
    DWORD Start, ThreadId = GetCurrentThreadId();
    BOOL Registered = FALSE, Subscribed = FALSE, AgileSubscribed = FALSE, Shortcut = FALSE;
    HKEY Key;
    MSG Message;
    HRESULT hr;

    hr = RoInitialize(RO_INIT_SINGLETHREADED);
    ok(SUCCEEDED(hr), "RoInitialize returned %#lx\n", hr);
    if (FAILED(hr)) return;
    Handler = calloc(1, sizeof(*Handler));
    ok(Handler != NULL, "Cannot allocate the handler\n");
    if (!Handler)
    {
        RoUninitialize();
        return;
    }
    Handler->ITypedEventHandler_iface.lpVtbl = &apartment_dismissed_vtbl;
    Handler->RefCount = 1;
    Handler->OwnerThreadId = ThreadId;
    Handler->Reason = -1;
    Handler->RemoveResult = E_FAIL;

    swprintf(AppId, ARRAYSIZE(AppId), L"LiberNT.ApiTest.ToastCallback.%lu", GetCurrentProcessId());
    swprintf(KeyName, ARRAYSIZE(KeyName), L"Software\\Classes\\AppUserModelId\\%ls", AppId);
    if (RegCreateKeyExW(HKEY_CURRENT_USER, KeyName, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &Key, NULL))
    {
        ok(FALSE, "Cannot register the test application ID\n");
        goto done;
    }
    RegSetValueExW(Key, L"DisplayName", 0, REG_SZ, (const BYTE *)L"LiberNT callback test",
                   sizeof(L"LiberNT callback test"));
    RegCloseKey(Key);
    Registered = TRUE;
    Shortcut = CreateAppShortcut(ShortcutPath, AppId);
    ok(Shortcut, "Cannot create the test application shortcut\n");
    if (!Shortcut) goto done;
    Sleep(3000);

    hr = GetFactory(RuntimeClass_Windows_UI_Notifications_ToastNotificationManager,
                    &IID_IToastNotificationManagerStatics, (void **)&Statics);
    ok(SUCCEEDED(hr), "Toast manager activation returned %#lx\n", hr);
    if (FAILED(hr)) goto done;
    Id = CreateHString(AppId);
    hr = IToastNotificationManagerStatics_CreateToastNotifierWithId(Statics, Id, &Notifier);
    WindowsDeleteString(Id);
    ok(SUCCEEDED(hr), "CreateToastNotifierWithId returned %#lx\n", hr);
    if (FAILED(hr)) goto done;
    Document = LoadToastXml();
    if (!Document) goto done;
    hr = GetFactory(RuntimeClass_Windows_UI_Notifications_ToastNotification,
                    &IID_IToastNotificationFactory, (void **)&Factory);
    ok(SUCCEEDED(hr), "Toast factory activation returned %#lx\n", hr);
    if (FAILED(hr)) goto done;
    hr = IToastNotificationFactory_CreateToastNotification(Factory, Document, &Toast);
    ok(SUCCEEDED(hr), "CreateToastNotification returned %#lx\n", hr);
    if (FAILED(hr)) goto done;
    hr = IToastNotification_add_Dismissed(Toast, &Handler->ITypedEventHandler_iface, &Token);
    ok(SUCCEEDED(hr), "add_Dismissed returned %#lx\n", hr);
    if (FAILED(hr)) goto done;
    Subscribed = TRUE;
    Handler->Token = Token;
    hr = IToastNotification_add_Dismissed(Toast, &Agile.ITypedEventHandler_iface, &AgileToken);
    ok(SUCCEEDED(hr), "add_Dismissed for agile handler returned %#lx\n", hr);
    if (FAILED(hr)) goto done;
    AgileSubscribed = TRUE;
    hr = IToastNotifier_Show(Notifier, Toast);
    ok(SUCCEEDED(hr), "Show returned %#lx\n", hr);
    if (FAILED(hr)) goto done;

    Start = GetTickCount();
    while ((!Handler->Calls || !Agile.Calls) && GetTickCount() - Start < 11000)
    {
        MsgWaitForMultipleObjects(0, NULL, FALSE, 50, QS_ALLINPUT);
        while (PeekMessageW(&Message, NULL, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&Message);
            DispatchMessageW(&Message);
        }
    }
    ok(Handler->Calls == 1, "Dismissed called %ld times\n", Handler->Calls);
    ok(Agile.Calls == 1, "Agile Dismissed called %ld times\n", Agile.Calls);
    if (Agile.Calls)
        ok(Agile.Reason == ToastDismissalReason_TimedOut, "Agile Dismissed reason %d\n", Agile.Reason);
    if (Handler->Calls)
    {
        ok(Handler->ThreadId == ThreadId, "Dismissed ran on thread %lu instead of %lu\n", Handler->ThreadId, ThreadId);
        ok(Handler->Reason == ToastDismissalReason_TimedOut, "Dismissed reason %d\n", Handler->Reason);
        ok(Handler->RemoveResult == S_OK, "remove_Dismissed from callback returned %#lx\n", Handler->RemoveResult);
        Start = GetTickCount();
        while (InterlockedCompareExchange(&Handler->RefCount, 0, 0) != 1 && GetTickCount() - Start < 2000)
        {
            MsgWaitForMultipleObjects(0, NULL, FALSE, 50, QS_ALLINPUT);
            while (PeekMessageW(&Message, NULL, 0, 0, PM_REMOVE))
            {
                TranslateMessage(&Message);
                DispatchMessageW(&Message);
            }
        }
        ok(Handler->RefCount == 1, "Handler retains %ld references after removal\n", Handler->RefCount - 1);
        ok(!Handler->WrongReleaseThreadId, "Handler released on thread %lu instead of %lu\n",
           Handler->WrongReleaseThreadId, ThreadId);
    }

done:
    if (AgileSubscribed) IToastNotification_remove_Dismissed(Toast, AgileToken);
    if (Subscribed) IToastNotification_remove_Dismissed(Toast, Token);
    if (Notifier && Toast) IToastNotifier_Hide(Notifier, Toast);
    if (Toast) IToastNotification_Release(Toast);
    if (Factory) IToastNotificationFactory_Release(Factory);
    if (Document) IXmlDocument_Release(Document);
    if (Notifier) IToastNotifier_Release(Notifier);
    if (Statics) IToastNotificationManagerStatics_Release(Statics);
    if (Shortcut) DeleteFileW(ShortcutPath);
    if (Registered) RegDeleteKeyW(HKEY_CURRENT_USER, KeyName);
    apartment_dismissed_Release(&Handler->ITypedEventHandler_iface);
    RoUninitialize();
}
