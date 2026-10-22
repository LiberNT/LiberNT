/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests toast history shared between processes and toast activation user input
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "winrt_apitest.h"

#define WIDL_using_Windows_Data_Xml_Dom
#include <windows.data.xml.dom.h>
#define WIDL_using_Windows_UI_Notifications
#include <windows.ui.notifications.h>

static const WCHAR ChildAppIdVariable[] = L"LIBERNT_TOAST_HISTORY_APPID";
static const WCHAR ChildModeVariable[] = L"LIBERNT_TOAST_HISTORY_MODE";
static const WCHAR HistoryXml[] =
    L"<toast launch=\"history-launch\"><visual><binding template=\"ToastGeneric\">"
    L"<text>LiberNT history test</text></binding></visual></toast>";
static const WCHAR InputXml[] =
    L"<toast launch=\"input-launch\"><visual><binding template=\"ToastGeneric\">"
    L"<text>LiberNT input test</text></binding></visual>"
    L"<actions><input id=\"reply\" type=\"text\"/></actions></toast>";

struct activated_handler
{
    ITypedEventHandler_ToastNotification_IInspectable ITypedEventHandler_iface;
    LONG Calls;
    WCHAR Arguments[64];
    WCHAR Reply[64];
    UINT32 InputCount;
    HRESULT InputResult;
};

static HRESULT WINAPI
activated_QueryInterface(ITypedEventHandler_ToastNotification_IInspectable *iface, REFIID iid, void **out)
{
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IAgileObject) ||
        IsEqualGUID(iid, &IID_ITypedEventHandler_ToastNotification_IInspectable))
    {
        *out = iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI
activated_AddRef(ITypedEventHandler_ToastNotification_IInspectable *iface)
{
    return 2;
}

static ULONG WINAPI
activated_Release(ITypedEventHandler_ToastNotification_IInspectable *iface)
{
    return 1;
}

static HRESULT WINAPI
activated_Invoke(ITypedEventHandler_ToastNotification_IInspectable *iface, IToastNotification *sender,
                 IInspectable *args)
{
    struct activated_handler *Handler = CONTAINING_RECORD(iface, struct activated_handler, ITypedEventHandler_iface);
    IToastActivatedEventArgs2 *Args2;
    IToastActivatedEventArgs *Args;
    IMap_HSTRING_IInspectable *Map;
    IPropertyValue *Property;
    IPropertySet *Input;
    IInspectable *Value;
    HSTRING Text, Key;

    if (SUCCEEDED(IInspectable_QueryInterface(args, &IID_IToastActivatedEventArgs, (void **)&Args)))
    {
        if (SUCCEEDED(IToastActivatedEventArgs_get_Arguments(Args, &Text)))
        {
            wcsncpy(Handler->Arguments, WindowsGetStringRawBuffer(Text, NULL), ARRAYSIZE(Handler->Arguments) - 1);
            WindowsDeleteString(Text);
        }
        IToastActivatedEventArgs_Release(Args);
    }
    Handler->InputResult = IInspectable_QueryInterface(args, &IID_IToastActivatedEventArgs2, (void **)&Args2);
    if (SUCCEEDED(Handler->InputResult))
    {
        Input = NULL;
        Handler->InputResult = IToastActivatedEventArgs2_get_UserInput(Args2, (IPropertySet **)&Input);
        if (SUCCEEDED(Handler->InputResult) && Input)
        {
            if (SUCCEEDED(IPropertySet_QueryInterface(Input, &IID_IMap_HSTRING_IInspectable, (void **)&Map)))
            {
                IMap_HSTRING_IInspectable_get_Size(Map, &Handler->InputCount);
                Key = CreateHString(L"reply");
                if (SUCCEEDED(IMap_HSTRING_IInspectable_Lookup(Map, Key, &Value)))
                {
                    if (SUCCEEDED(IInspectable_QueryInterface(Value, &IID_IPropertyValue, (void **)&Property)))
                    {
                        if (SUCCEEDED(IPropertyValue_GetString(Property, &Text)))
                        {
                            wcsncpy(Handler->Reply, WindowsGetStringRawBuffer(Text, NULL), ARRAYSIZE(Handler->Reply) - 1);
                            WindowsDeleteString(Text);
                        }
                        IPropertyValue_Release(Property);
                    }
                    IInspectable_Release(Value);
                }
                WindowsDeleteString(Key);
                IMap_HSTRING_IInspectable_Release(Map);
            }
            IPropertySet_Release(Input);
        }
        IToastActivatedEventArgs2_Release(Args2);
    }
    InterlockedIncrement(&Handler->Calls);
    return S_OK;
}

static ITypedEventHandler_ToastNotification_IInspectableVtbl activated_vtbl =
{
    activated_QueryInterface,
    activated_AddRef,
    activated_Release,
    activated_Invoke,
};

static IXmlDocument *
LoadXml(
    _In_ const WCHAR *Xml)
{
    IXmlDocumentIO *DocumentIO;
    IXmlDocument *Document = NULL;
    IInspectable *Instance;
    HSTRING Class, Text;

    Class = CreateHString(RuntimeClass_Windows_Data_Xml_Dom_XmlDocument);
    if (SUCCEEDED(RoActivateInstance(Class, &Instance)))
    {
        IInspectable_QueryInterface(Instance, &IID_IXmlDocument, (void **)&Document);
        if (SUCCEEDED(IInspectable_QueryInterface(Instance, &IID_IXmlDocumentIO, (void **)&DocumentIO)))
        {
            Text = CreateHString(Xml);
            IXmlDocumentIO_LoadXml(DocumentIO, Text);
            WindowsDeleteString(Text);
            IXmlDocumentIO_Release(DocumentIO);
        }
        IInspectable_Release(Instance);
    }
    WindowsDeleteString(Class);
    return Document;
}

static IToastNotification *
CreateToast(
    _In_ const WCHAR *Xml,
    _In_opt_ const WCHAR *TagText,
    _In_opt_ const WCHAR *GroupText)
{
    IToastNotificationFactory *Factory;
    IToastNotification *Toast = NULL;
    IToastNotification2 *Toast2;
    IXmlDocument *Document;
    HSTRING Text;

    if (!(Document = LoadXml(Xml)))
        return NULL;
    if (SUCCEEDED(GetFactory(RuntimeClass_Windows_UI_Notifications_ToastNotification, &IID_IToastNotificationFactory,
                             (void **)&Factory)))
    {
        IToastNotificationFactory_CreateToastNotification(Factory, Document, &Toast);
        IToastNotificationFactory_Release(Factory);
    }
    IXmlDocument_Release(Document);
    if (Toast && (TagText || GroupText) &&
        SUCCEEDED(IToastNotification_QueryInterface(Toast, &IID_IToastNotification2, (void **)&Toast2)))
    {
        if (TagText)
        {
            Text = CreateHString(TagText);
            IToastNotification2_put_Tag(Toast2, Text);
            WindowsDeleteString(Text);
        }
        if (GroupText)
        {
            Text = CreateHString(GroupText);
            IToastNotification2_put_Group(Toast2, Text);
            WindowsDeleteString(Text);
        }
        IToastNotification2_Release(Toast2);
    }
    return Toast;
}

static HRESULT
GetStatics(
    _Out_ IToastNotificationManagerStatics **Statics,
    _Out_ IToastNotificationHistory2 **History)
{
    IToastNotificationManagerStatics2 *Statics2;
    IToastNotificationHistory *History1;
    HRESULT hr;

    *History = NULL;
    hr = GetFactory(RuntimeClass_Windows_UI_Notifications_ToastNotificationManager,
                    &IID_IToastNotificationManagerStatics, (void **)Statics);
    if (FAILED(hr))
        return hr;
    hr = IToastNotificationManagerStatics_QueryInterface(*Statics, &IID_IToastNotificationManagerStatics2,
                                                         (void **)&Statics2);
    if (SUCCEEDED(hr))
    {
        hr = IToastNotificationManagerStatics2_get_History(Statics2, &History1);
        if (SUCCEEDED(hr))
        {
            hr = IToastNotificationHistory_QueryInterface(History1, &IID_IToastNotificationHistory2, (void **)History);
            IToastNotificationHistory_Release(History1);
        }
        IToastNotificationManagerStatics2_Release(Statics2);
    }
    if (FAILED(hr))
        IToastNotificationManagerStatics_Release(*Statics);
    return hr;
}

static HRESULT
ShowToast(
    _In_ IToastNotificationManagerStatics *Statics,
    _In_ const WCHAR *AppId,
    _In_ IToastNotification *Toast)
{
    IToastNotifier *Notifier;
    HSTRING Id = CreateHString(AppId);
    HRESULT hr;

    hr = IToastNotificationManagerStatics_CreateToastNotifierWithId(Statics, Id, &Notifier);
    WindowsDeleteString(Id);
    if (FAILED(hr))
        return hr;
    hr = IToastNotifier_Show(Notifier, Toast);
    IToastNotifier_Release(Notifier);
    return hr;
}

static IVectorView_ToastNotification *
GetHistory(
    _In_ IToastNotificationHistory2 *History,
    _In_ const WCHAR *AppId)
{
    IVectorView_ToastNotification *View = NULL;
    HSTRING Id = CreateHString(AppId);

    IToastNotificationHistory2_GetHistoryWithId(History, Id, &View);
    WindowsDeleteString(Id);
    return View;
}

static UINT32
HistorySize(
    _In_ IToastNotificationHistory2 *History,
    _In_ const WCHAR *AppId)
{
    IVectorView_ToastNotification *View = GetHistory(History, AppId);
    UINT32 Size = 0xdeadbeef;

    if (View)
    {
        IVectorView_ToastNotification_get_Size(View, &Size);
        IVectorView_ToastNotification_Release(View);
    }
    return Size;
}

static HRESULT
HistoryRemove(
    _In_ IToastNotificationHistory2 *History,
    _In_ const WCHAR *AppId,
    _In_opt_ const WCHAR *TagText,
    _In_opt_ const WCHAR *GroupText)
{
    IToastNotificationHistory *History1;
    HSTRING Id, Tag, Group;
    HRESULT hr;

    hr = IToastNotificationHistory2_QueryInterface(History, &IID_IToastNotificationHistory, (void **)&History1);
    if (FAILED(hr))
        return hr;
    Id = CreateHString(AppId);
    if (TagText)
    {
        Tag = CreateHString(TagText);
        Group = CreateHString(GroupText);
        hr = IToastNotificationHistory_RemoveGroupedTagWithId(History1, Tag, Group, Id);
        WindowsDeleteString(Tag);
        WindowsDeleteString(Group);
    }
    else
    {
        hr = IToastNotificationHistory_ClearWithId(History1, Id);
    }
    WindowsDeleteString(Id);
    IToastNotificationHistory_Release(History1);
    return hr;
}

static void
GetIdentity(
    _In_ IToastNotification *Toast,
    _Out_writes_(32) WCHAR *Tag,
    _Out_writes_(32) WCHAR *Group)
{
    IToastNotification2 *Toast2;
    HSTRING Text;

    Tag[0] = Group[0] = 0;
    if (FAILED(IToastNotification_QueryInterface(Toast, &IID_IToastNotification2, (void **)&Toast2)))
        return;
    if (SUCCEEDED(IToastNotification2_get_Tag(Toast2, &Text)))
    {
        wcsncpy(Tag, WindowsGetStringRawBuffer(Text, NULL), 31);
        Tag[31] = 0;
        WindowsDeleteString(Text);
    }
    if (SUCCEEDED(IToastNotification2_get_Group(Toast2, &Text)))
    {
        wcsncpy(Group, WindowsGetStringRawBuffer(Text, NULL), 31);
        Group[31] = 0;
        WindowsDeleteString(Text);
    }
    IToastNotification2_Release(Toast2);
}

static BOOL
ContentHasText(
    _In_ IToastNotification *Toast,
    _In_ const WCHAR *Text)
{
    IXmlNodeSerializer *Serializer;
    IXmlDocument *Content;
    HSTRING Xml;
    BOOL Found = FALSE;

    if (FAILED(IToastNotification_get_Content(Toast, &Content)))
        return FALSE;
    if (SUCCEEDED(IXmlDocument_QueryInterface(Content, &IID_IXmlNodeSerializer, (void **)&Serializer)))
    {
        if (SUCCEEDED(IXmlNodeSerializer_GetXml(Serializer, &Xml)))
        {
            Found = wcsstr(WindowsGetStringRawBuffer(Xml, NULL), Text) != NULL;
            WindowsDeleteString(Xml);
        }
        IXmlNodeSerializer_Release(Serializer);
    }
    IXmlDocument_Release(Content);
    return Found;
}

static DWORD
RunChild(
    _In_ const WCHAR *AppId,
    _In_ const WCHAR *Mode)
{
    WCHAR Path[MAX_PATH], CommandLine[MAX_PATH + 32];
    PROCESS_INFORMATION Process;
    STARTUPINFOW Startup = { sizeof(Startup) };
    DWORD ExitCode = 0xdeadbeef;

    GetModuleFileNameW(NULL, Path, ARRAYSIZE(Path));
    swprintf(CommandLine, ARRAYSIZE(CommandLine), L"\"%ls\" ToastHistoryChild", Path);
    SetEnvironmentVariableW(ChildAppIdVariable, AppId);
    SetEnvironmentVariableW(ChildModeVariable, Mode);
    if (!CreateProcessW(Path, CommandLine, NULL, NULL, FALSE, 0, NULL, NULL, &Startup, &Process))
    {
        ok(FALSE, "CreateProcess failed, error %lu\n", GetLastError());
        return ExitCode;
    }
    ok(WaitForSingleObject(Process.hProcess, 20000) == WAIT_OBJECT_0, "child %ls did not exit\n", Mode);
    GetExitCodeProcess(Process.hProcess, &ExitCode);
    CloseHandle(Process.hThread);
    CloseHandle(Process.hProcess);
    SetEnvironmentVariableW(ChildAppIdVariable, NULL);
    SetEnvironmentVariableW(ChildModeVariable, NULL);
    return ExitCode;
}

static void
TestSharedHistory(
    _In_ IToastNotificationManagerStatics *Statics,
    _In_ IToastNotificationHistory2 *History)
{
    IVectorView_ToastNotification *View;
    IToastNotification *Toast, *Item;
    WCHAR AppId[64], Tag[32], Group[32];
    BOOL SawChild = FALSE, SawParent = FALSE;
    UINT32 Size, i;
    DWORD ExitCode;
    HRESULT hr;

    swprintf(AppId, ARRAYSIZE(AppId), L"LiberNT.ApiTest.ToastHistory.%lu", GetCurrentProcessId());
    Toast = CreateToast(HistoryXml, L"parent", L"local");
    ok(Toast != NULL, "cannot create the parent toast\n");
    if (!Toast)
        return;
    hr = ShowToast(Statics, AppId, Toast);
    ok(hr == S_OK, "parent Show returned hr %#lx\n", hr);

    ExitCode = RunChild(AppId, L"post");
    ok(ExitCode == 0, "posting child returned %#lx\n", ExitCode);

    View = GetHistory(History, AppId);
    ok(View != NULL, "GetHistoryWithId failed\n");
    if (View)
    {
        Size = 0;
        IVectorView_ToastNotification_get_Size(View, &Size);
        ok(Size == 2, "history holds %u toasts after the child posted\n", Size);
        for (i = 0; i < Size; i++)
        {
            if (FAILED(IVectorView_ToastNotification_GetAt(View, i, &Item)))
                continue;
            GetIdentity(Item, Tag, Group);
            if (!wcscmp(Tag, L"child"))
            {
                SawChild = TRUE;
                ok(!wcscmp(Group, L"remote"), "child toast group %ls\n", Group);
                ok(ContentHasText(Item, L"LiberNT history test"), "child toast content was not kept\n");
            }
            else if (!wcscmp(Tag, L"parent"))
            {
                SawParent = TRUE;
                ok(Item != Toast, "the history returned the posted toast object\n");
            }
            IToastNotification_Release(Item);
        }
        IVectorView_ToastNotification_Release(View);
    }
    ok(SawChild, "the toast posted by the child process is not in the history\n");
    ok(SawParent, "the toast posted by this process is not in the history\n");

    ExitCode = RunChild(AppId, L"count");
    ok(ExitCode == 2, "child counted %lu toasts in the history\n", ExitCode);

    hr = HistoryRemove(History, AppId, L"child", L"remote");
    ok(hr == S_OK, "RemoveGroupedTagWithId returned hr %#lx\n", hr);
    ok(HistorySize(History, AppId) == 1, "history holds %u toasts after removing the child toast\n",
       HistorySize(History, AppId));
    ExitCode = RunChild(AppId, L"count");
    ok(ExitCode == 1, "child counted %lu toasts after the removal\n", ExitCode);

    ExitCode = RunChild(AppId, L"clear");
    ok(ExitCode == 0, "clearing child returned %#lx\n", ExitCode);
    ok(HistorySize(History, AppId) == 0, "history holds %u toasts after the child cleared it\n",
       HistorySize(History, AppId));
    HistoryRemove(History, AppId, NULL, NULL);
    IToastNotification_Release(Toast);
}

static INT64
FileTimeNow(void)
{
    FILETIME Now;

    GetSystemTimeAsFileTime(&Now);
    return (INT64)(((UINT64)Now.dwHighDateTime << 32) | Now.dwLowDateTime);
}

static HRESULT
ScheduleToast(
    _In_ IToastNotificationManagerStatics *Statics,
    _In_ const WCHAR *AppId,
    _In_ const WCHAR *TagText,
    _In_ UINT32 Seconds)
{
    IScheduledToastNotificationFactory *Factory;
    IScheduledToastNotification2 *Scheduled2;
    IScheduledToastNotification *Scheduled;
    IToastNotifier *Notifier;
    IXmlDocument *Document;
    DateTime Delivery;
    HSTRING Text, Id;
    HRESULT hr;

    if (!(Document = LoadXml(HistoryXml)))
        return E_FAIL;
    hr = GetFactory(RuntimeClass_Windows_UI_Notifications_ScheduledToastNotification,
                    &IID_IScheduledToastNotificationFactory, (void **)&Factory);
    if (SUCCEEDED(hr))
    {
        Delivery.UniversalTime = FileTimeNow() + (INT64)Seconds * 10000000;
        hr = IScheduledToastNotificationFactory_CreateScheduledToastNotification(Factory, Document, Delivery, &Scheduled);
        IScheduledToastNotificationFactory_Release(Factory);
    }
    IXmlDocument_Release(Document);
    if (FAILED(hr))
        return hr;
    if (SUCCEEDED(IScheduledToastNotification_QueryInterface(Scheduled, &IID_IScheduledToastNotification2,
                                                             (void **)&Scheduled2)))
    {
        Text = CreateHString(TagText);
        IScheduledToastNotification2_put_Tag(Scheduled2, Text);
        WindowsDeleteString(Text);
        IScheduledToastNotification2_Release(Scheduled2);
    }
    Id = CreateHString(AppId);
    hr = IToastNotificationManagerStatics_CreateToastNotifierWithId(Statics, Id, &Notifier);
    WindowsDeleteString(Id);
    if (SUCCEEDED(hr))
    {
        hr = IToastNotifier_AddToSchedule(Notifier, Scheduled);
        IToastNotifier_Release(Notifier);
    }
    IScheduledToastNotification_Release(Scheduled);
    return hr;
}

static IVectorView_ScheduledToastNotification *
GetSchedule(
    _In_ IToastNotificationManagerStatics *Statics,
    _In_ const WCHAR *AppId,
    _Out_opt_ IToastNotifier **NotifierOut)
{
    IVectorView_ScheduledToastNotification *View = NULL;
    IToastNotifier *Notifier;
    HSTRING Id = CreateHString(AppId);

    if (NotifierOut)
        *NotifierOut = NULL;
    if (SUCCEEDED(IToastNotificationManagerStatics_CreateToastNotifierWithId(Statics, Id, &Notifier)))
    {
        IToastNotifier_GetScheduledToastNotifications(Notifier, &View);
        if (NotifierOut)
            *NotifierOut = Notifier;
        else
            IToastNotifier_Release(Notifier);
    }
    WindowsDeleteString(Id);
    return View;
}

static UINT32
ScheduleSize(
    _In_ IToastNotificationManagerStatics *Statics,
    _In_ const WCHAR *AppId)
{
    IVectorView_ScheduledToastNotification *View = GetSchedule(Statics, AppId, NULL);
    UINT32 Size = 0xdeadbeef;

    if (View)
    {
        IVectorView_ScheduledToastNotification_get_Size(View, &Size);
        IVectorView_ScheduledToastNotification_Release(View);
    }
    return Size;
}

static void
TestSharedSchedule(
    _In_ IToastNotificationManagerStatics *Statics,
    _In_ IToastNotificationHistory2 *History,
    _In_ BOOL Registered)
{
    IVectorView_ScheduledToastNotification *View;
    IScheduledToastNotification2 *Scheduled2;
    IScheduledToastNotification *Item;
    IToastNotifier *Notifier;
    WCHAR AppId[64], Key[128];
    BOOL Removed = FALSE;
    UINT32 Size, i;
    DWORD ExitCode, Start;
    HKEY Handle;
    HSTRING Tag;
    HRESULT hr;

    swprintf(AppId, ARRAYSIZE(AppId), L"LiberNT.ApiTest.ToastSchedule%ls.%lu", Registered ? L"Registered" : L"",
             GetCurrentProcessId());
    swprintf(Key, ARRAYSIZE(Key), L"Software\\Classes\\AppUserModelId\\%ls", AppId);
    if (Registered)
    {
        if (RegCreateKeyExW(HKEY_CURRENT_USER, Key, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &Handle, NULL))
        {
            skip("cannot register the schedule test application\n");
            return;
        }
        RegSetValueExW(Handle, L"DisplayName", 0, REG_SZ, (const BYTE *)L"LiberNT schedule test",
                       sizeof(L"LiberNT schedule test"));
        RegCloseKey(Handle);
    }
    ExitCode = RunChild(AppId, L"schedule");
    ok(ExitCode == 2, "scheduling child saw %lu scheduled toasts\n", ExitCode);

    View = GetSchedule(Statics, AppId, &Notifier);
    ok(View != NULL, "GetScheduledToastNotifications failed\n");
    if (View)
    {
        Size = 0;
        IVectorView_ScheduledToastNotification_get_Size(View, &Size);
        ok(Size == 2, "this process sees %u toasts scheduled by the child\n", Size);
        for (i = 0; i < Size; i++)
        {
            if (FAILED(IVectorView_ScheduledToastNotification_GetAt(View, i, &Item)))
                continue;
            if (SUCCEEDED(IScheduledToastNotification_QueryInterface(Item, &IID_IScheduledToastNotification2,
                                                                     (void **)&Scheduled2)))
            {
                if (SUCCEEDED(IScheduledToastNotification2_get_Tag(Scheduled2, &Tag)))
                {
                    if (!wcscmp(WindowsGetStringRawBuffer(Tag, NULL), L"later"))
                    {
                        hr = IToastNotifier_RemoveFromSchedule(Notifier, Item);
                        ok(hr == S_OK, "RemoveFromSchedule of the child toast returned hr %#lx\n", hr);
                        Removed = TRUE;
                    }
                    WindowsDeleteString(Tag);
                }
                IScheduledToastNotification2_Release(Scheduled2);
            }
            IScheduledToastNotification_Release(Item);
        }
        IVectorView_ScheduledToastNotification_Release(View);
    }
    ok(Removed, "the toast scheduled for later was not found\n");
    ExitCode = RunChild(AppId, L"schedcount");
    ok(ExitCode == 1, "child saw %lu scheduled toasts after the removal\n", ExitCode);

    Start = GetTickCount();
    while (GetTickCount() - Start < 8000 && ScheduleSize(Statics, AppId))
        Sleep(100);
    Sleep(500);
    ok(HistorySize(History, AppId) == 0, "%u toasts in the history after the delivery time\n",
       HistorySize(History, AppId));
    ok(ScheduleSize(Statics, AppId) == 0, "%u toasts are still scheduled after the delivery\n",
       ScheduleSize(Statics, AppId));
    if (Notifier)
        IToastNotifier_Release(Notifier);
    HistoryRemove(History, AppId, NULL, NULL);
    if (Registered)
        RegDeleteKeyW(HKEY_CURRENT_USER, Key);
}

struct showing_handler
{
    ITypedEventHandler_ToastNotifier_ScheduledToastNotificationShowingEventArgs ITypedEventHandler_iface;
    LONG Calls;
};

static HRESULT WINAPI
showing_QueryInterface(ITypedEventHandler_ToastNotifier_ScheduledToastNotificationShowingEventArgs *iface, REFIID iid,
                       void **out)
{
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IAgileObject) ||
        IsEqualGUID(iid, &IID_ITypedEventHandler_ToastNotifier_ScheduledToastNotificationShowingEventArgs))
    {
        *out = iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI
showing_AddRef(ITypedEventHandler_ToastNotifier_ScheduledToastNotificationShowingEventArgs *iface)
{
    return 2;
}

static ULONG WINAPI
showing_Release(ITypedEventHandler_ToastNotifier_ScheduledToastNotificationShowingEventArgs *iface)
{
    return 1;
}

static HRESULT WINAPI
showing_Invoke(ITypedEventHandler_ToastNotifier_ScheduledToastNotificationShowingEventArgs *iface, IToastNotifier *sender,
               IScheduledToastNotificationShowingEventArgs *args)
{
    InterlockedIncrement(&CONTAINING_RECORD(iface, struct showing_handler, ITypedEventHandler_iface)->Calls);
    return S_OK;
}

static ITypedEventHandler_ToastNotifier_ScheduledToastNotificationShowingEventArgsVtbl showing_vtbl =
{
    showing_QueryInterface,
    showing_AddRef,
    showing_Release,
    showing_Invoke,
};

static void
TestLocalSchedule(
    _In_ IToastNotificationManagerStatics *Statics,
    _In_ IToastNotificationHistory2 *History)
{
    struct showing_handler Showing = { { &showing_vtbl } };
    EventRegistrationToken Token;
    IToastNotifier3 *Notifier3;
    IToastNotifier *Notifier;
    WCHAR AppId[64], Key[128];
    DWORD Start;
    HKEY Handle;
    HSTRING Id;
    HRESULT hr;

    swprintf(AppId, ARRAYSIZE(AppId), L"LiberNT.ApiTest.ToastLocalSchedule.%lu", GetCurrentProcessId());
    swprintf(Key, ARRAYSIZE(Key), L"Software\\Classes\\AppUserModelId\\%ls", AppId);
    if (RegCreateKeyExW(HKEY_CURRENT_USER, Key, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &Handle, NULL))
    {
        skip("cannot register the local schedule test application\n");
        return;
    }
    RegSetValueExW(Handle, L"DisplayName", 0, REG_SZ, (const BYTE *)L"LiberNT local schedule",
                   sizeof(L"LiberNT local schedule"));
    RegCloseKey(Handle);
    Id = CreateHString(AppId);
    hr = IToastNotificationManagerStatics_CreateToastNotifierWithId(Statics, Id, &Notifier);
    WindowsDeleteString(Id);
    ok(hr == S_OK, "CreateToastNotifierWithId returned hr %#lx\n", hr);
    if (SUCCEEDED(hr))
    {
        hr = IToastNotifier_QueryInterface(Notifier, &IID_IToastNotifier3, (void **)&Notifier3);
        ok(hr == S_OK, "IToastNotifier3 query returned hr %#lx\n", hr);
        if (SUCCEEDED(hr))
        {
            hr = IToastNotifier3_add_ScheduledToastNotificationShowing(Notifier3, &Showing.ITypedEventHandler_iface, &Token);
            ok(hr == S_OK, "add_ScheduledToastNotificationShowing returned hr %#lx\n", hr);
            hr = ScheduleToast(Statics, AppId, L"local", 2);
            ok(hr == S_OK, "AddToSchedule returned hr %#lx\n", hr);
            ok(ScheduleSize(Statics, AppId) == 1, "%u toasts scheduled\n", ScheduleSize(Statics, AppId));
            Start = GetTickCount();
            while (GetTickCount() - Start < 6000 && ScheduleSize(Statics, AppId))
                Sleep(100);
            Sleep(500);
            ok(ScheduleSize(Statics, AppId) == 0, "%u toasts still scheduled after the delivery time\n",
               ScheduleSize(Statics, AppId));
            ok(HistorySize(History, AppId) == 0, "%u toasts in the history after the delivery\n",
               HistorySize(History, AppId));
            ok(Showing.Calls == 0, "ScheduledToastNotificationShowing raised %ld times on another notifier\n",
               Showing.Calls);
            IToastNotifier3_remove_ScheduledToastNotificationShowing(Notifier3, Token);
            IToastNotifier3_Release(Notifier3);
        }
        IToastNotifier_Release(Notifier);
    }
    HistoryRemove(History, AppId, NULL, NULL);
    RegDeleteKeyW(HKEY_CURRENT_USER, Key);
}

static HWND
FindPopup(void)
{
    HWND Window = NULL;

    while ((Window = FindWindowExW(NULL, Window, L"LiberNTToastPopup", NULL)))
    {
        if (IsWindowVisible(Window))
            return Window;
    }
    return NULL;
}

static void
TestUserInput(
    _In_ IToastNotificationManagerStatics *Statics,
    _In_ IToastNotificationHistory2 *History)
{
    struct activated_handler Handler = { { &activated_vtbl } };
    EventRegistrationToken Token;
    IToastNotification *Toast;
    WCHAR AppId[64], Key[128];
    HWND Popup = NULL, Edit;
    HKEY Handle;
    DWORD Start;
    HRESULT hr;

    swprintf(AppId, ARRAYSIZE(AppId), L"LiberNT.ApiTest.ToastInput.%lu", GetCurrentProcessId());
    swprintf(Key, ARRAYSIZE(Key), L"Software\\Classes\\AppUserModelId\\%ls", AppId);
    if (RegCreateKeyExW(HKEY_CURRENT_USER, Key, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &Handle, NULL))
    {
        skip("cannot register the input test application\n");
        return;
    }
    RegSetValueExW(Handle, L"DisplayName", 0, REG_SZ, (const BYTE *)L"LiberNT input test", sizeof(L"LiberNT input test"));
    RegCloseKey(Handle);

    Toast = CreateToast(InputXml, L"input", NULL);
    ok(Toast != NULL, "cannot create the input toast\n");
    if (Toast)
    {
        hr = IToastNotification_add_Activated(Toast, &Handler.ITypedEventHandler_iface, &Token);
        ok(hr == S_OK, "add_Activated returned hr %#lx\n", hr);
        hr = ShowToast(Statics, AppId, Toast);
        ok(hr == S_OK, "input toast Show returned hr %#lx\n", hr);
        Start = GetTickCount();
        while (GetTickCount() - Start < 3000 && !(Popup = FindPopup()))
            Sleep(50);
        if (!Popup)
        {
            skip("no LiberNT toast popup to type into\n");
        }
        else
        {
            Edit = FindWindowExW(Popup, NULL, L"EDIT", NULL);
            ok(Edit != NULL, "the popup has no text input\n");
            if (Edit)
                SendMessageW(Edit, WM_SETTEXT, 0, (LPARAM)L"hello from the test");
            PostMessageW(Popup, WM_LBUTTONUP, 0, MAKELPARAM(12, 12));
            Start = GetTickCount();
            while (GetTickCount() - Start < 3000 && !Handler.Calls)
                Sleep(50);
            ok(Handler.Calls == 1, "Activated raised %ld times\n", Handler.Calls);
            ok(!wcscmp(Handler.Arguments, L"input-launch"), "Arguments %ls\n", Handler.Arguments);
            ok(Handler.InputResult == S_OK, "UserInput returned hr %#lx\n", Handler.InputResult);
            ok(Handler.InputCount == 1, "UserInput holds %u values\n", Handler.InputCount);
            ok(!wcscmp(Handler.Reply, L"hello from the test"), "reply input %ls\n", Handler.Reply);
        }
        IToastNotification_remove_Activated(Toast, Token);
        IToastNotification_Release(Toast);
    }
    HistoryRemove(History, AppId, NULL, NULL);
    RegDeleteKeyW(HKEY_CURRENT_USER, Key);
}

START_TEST(ToastHistory)
{
    IToastNotificationManagerStatics *Statics;
    IToastNotificationHistory2 *History;
    HRESULT hr;

    hr = RoInitialize(RO_INIT_MULTITHREADED);
    ok(SUCCEEDED(hr), "RoInitialize failed, hr %#lx\n", hr);
    hr = GetStatics(&Statics, &History);
    ok(hr == S_OK, "toast manager statics returned hr %#lx\n", hr);
    if (SUCCEEDED(hr))
    {
        TestSharedHistory(Statics, History);
        TestLocalSchedule(Statics, History);
        TestSharedSchedule(Statics, History, FALSE);
        TestSharedSchedule(Statics, History, TRUE);
        TestUserInput(Statics, History);
        IToastNotificationHistory2_Release(History);
        IToastNotificationManagerStatics_Release(Statics);
    }
    RoUninitialize();
}

START_TEST(ToastHistoryChild)
{
    IToastNotificationManagerStatics *Statics;
    IToastNotificationHistory2 *History;
    IToastNotification *Toast;
    WCHAR AppId[64], Mode[16];
    DWORD Result = 0xff;
    HRESULT hr;

    if (!GetEnvironmentVariableW(ChildAppIdVariable, AppId, ARRAYSIZE(AppId)) ||
        !GetEnvironmentVariableW(ChildModeVariable, Mode, ARRAYSIZE(Mode)))
    {
        skip("ToastHistoryChild runs only from ToastHistory\n");
        return;
    }
    RoInitialize(RO_INIT_MULTITHREADED);
    if (SUCCEEDED(GetStatics(&Statics, &History)))
    {
        if (!wcscmp(Mode, L"post"))
        {
            if ((Toast = CreateToast(HistoryXml, L"child", L"remote")))
            {
                hr = ShowToast(Statics, AppId, Toast);
                Result = SUCCEEDED(hr) ? 0 : (DWORD)hr;
                IToastNotification_Release(Toast);
            }
        }
        else if (!wcscmp(Mode, L"count"))
        {
            Result = HistorySize(History, AppId);
        }
        else if (!wcscmp(Mode, L"schedule"))
        {
            if (SUCCEEDED(ScheduleToast(Statics, AppId, L"soon", 3)) &&
                SUCCEEDED(ScheduleToast(Statics, AppId, L"later", 600)))
                Result = ScheduleSize(Statics, AppId);
        }
        else if (!wcscmp(Mode, L"schedcount"))
        {
            Result = ScheduleSize(Statics, AppId);
        }
        else if (!wcscmp(Mode, L"clear"))
        {
            hr = HistoryRemove(History, AppId, NULL, NULL);
            Result = SUCCEEDED(hr) ? 0 : (DWORD)hr;
        }
        IToastNotificationHistory2_Release(History);
        IToastNotificationManagerStatics_Release(Statics);
    }
    RoUninitialize();
    ExitProcess(Result);
}
