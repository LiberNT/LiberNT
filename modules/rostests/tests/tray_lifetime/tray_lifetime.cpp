/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Pin retained tray image pixels and shared slot lifetime
 * COPYRIGHT:   Copyright 2026 orhunnx <orhuntopcux@gmail.com>
 */

#include "../../../../base/shell/explorer/precomp.h"
#include <stdio.h>

HINSTANCE hExplorerInstance;
HANDLE hProcessHeap;
HKEY hkExplorer;
BOOL bExplorerIsShell;
TaskbarSettings g_TaskbarSettings;

int RunPowerLifetimeTests(void);

BOOL GetVersionInfoString(LPCWSTR, LPCWSTR, LPWSTR, UINT) { return FALSE; }
static BOOL LastNotificationIconValid;
VOID TrayNotifications_Add(HWND, UINT, LPCWSTR, LPCWSTR, LPCWSTR, HICON Icon)
{
    ICONINFO Info = {0};
    LastNotificationIconValid = Icon && GetIconInfo(Icon, &Info);
    if (Info.hbmMask) DeleteObject(Info.hbmMask);
    if (Info.hbmColor) DeleteObject(Info.hbmColor);
}
BOOL TrayNotifications_IsHidden() { return FALSE; }
VOID ShellGetTrayPillRect(const RECT *Input, RECT *Output) { *Output = *Input; }
VOID ShellDrawTrayPill(HDC, const RECT *, INT) {}

static unsigned Failures;
static unsigned LiveMemory;
static unsigned FailMemory, FailExtract;
static BOOL FailImageInsert, FailQueueAdd;
static void *TestAllocate(size_t Size)
{
    if (FailMemory && --FailMemory == 0) return NULL;
    void *Memory = malloc(Size);
    if (Memory) ++LiveMemory;
    return Memory;
}
static void TestFree(void *Memory)
{
    if (Memory) --LiveMemory;
    free(Memory);
}
static HICON WINAPI TestExtract(HIMAGELIST List, INT Index, UINT Flags)
{
    if (FailExtract && --FailExtract == 0) return NULL;
    return ImageList_GetIcon(List, Index, Flags);
}
static INT WINAPI TestInsertImage(HIMAGELIST List, INT Index, HICON Icon)
{
    if (FailImageInsert) { FailImageInsert = FALSE; return -1; }
    return ImageList_ReplaceIcon(List, Index, Icon);
}
template<class T> class TestArray : public CSimpleArray<T>
{
public:
    BOOL Add(const T &Value)
    {
        if (FailQueueAdd) { FailQueueAdd = FALSE; return FALSE; }
        return CSimpleArray<T>::Add(Value);
    }
};
static WNDPROC OriginalTipProc;
static void (*TipHook)(void);
static WCHAR LastTip[256];
static LRESULT CALLBACK TestTipProc(HWND Window, UINT Message, WPARAM Wparam, LPARAM Lparam)
{
    if (Message == TTM_TRACKPOSITION && TipHook)
    {
        void (*Hook)(void) = TipHook;
        TipHook = NULL;
        Hook();
    }
    if (Message == TTM_UPDATETIPTEXTW)
    {
        TTTOOLINFOW *Info = reinterpret_cast<TTTOOLINFOW*>(Lparam);
        if (Info->lpszText && Info->lpszText != LPSTR_TEXTCALLBACKW)
            StringCchCopyW(LastTip, _countof(LastTip), Info->lpszText);
    }
    return CallWindowProcW(OriginalTipProc, Window, Message, Wparam, Lparam);
}
static INT RenderSize = 16;
static void (*CallbackHook)(void);
static DWORD WINAPI TestWindowProcess(HWND Window, LPDWORD Process)
{
    DWORD Result = GetWindowThreadProcessId(Window, Process);
    if (Process) ++*Process;
    return Result;
}
static LRESULT CALLBACK TestWindowProc(HWND Window, UINT Message, WPARAM Wparam, LPARAM Lparam)
{
    if (Message == WM_APP + 71 && Lparam != NIN_BALLOONSHOW && CallbackHook)
    {
        void (*Hook)(void) = CallbackHook;
        CallbackHook = NULL;
        Hook();
    }
    return DefWindowProcW(Window, Message, Wparam, Lparam);
}
#define ShellTrayIconRenderSize() RenderSize
#define GetWindowThreadProcessId TestWindowProcess
#define private public
#define malloc TestAllocate
#define free TestFree
#define ImageList_GetIcon TestExtract
#define ImageList_ReplaceIcon TestInsertImage
#define CSimpleArray TestArray
#include "../../../../base/shell/explorer/syspager.cpp"
#undef ShellTrayIconRenderSize
#undef GetWindowThreadProcessId
#undef private
#undef malloc
#undef free
#undef ImageList_GetIcon
#undef ImageList_ReplaceIcon
#undef CSimpleArray

static void Check(const char *Name, BOOL Passed)
{
    printf("%s %s\n", Passed ? "PASS" : "FAIL", Name);
    if (!Passed) ++Failures;
}

static HICON LoadTrayIcon(UINT Id)
{
    return (HICON)LoadImageW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(Id), IMAGE_ICON, 18, 18, 0);
}

static BOOL HasPixels(HIMAGELIST List, INT Index)
{
    HICON Icon = Index >= 0 ? ImageList_GetIcon(List, Index, ILD_NORMAL) : NULL;
    ICONINFO Info = {0};
    BOOL Result = Icon && GetIconInfo(Icon, &Info);
    if (Info.hbmMask) DeleteObject(Info.hbmMask);
    if (Info.hbmColor) DeleteObject(Info.hbmColor);
    if (Icon) DestroyIcon(Icon);
    return Result;
}

static INT Slot(CNotifyToolbar &Toolbar, UINT Id)
{
    TBBUTTON Button = {0};
    INT Index = Toolbar.FindItem(GetParent(Toolbar.m_hWnd), Id, NULL);
    if (Index < 0 || !Toolbar.GetButton(Index, &Button)) return -1;
    return Button.iBitmap;
}

template<class T> static auto QueueLength(T &Queue, int) -> decltype(Queue.GetCount()) { return Queue.GetCount(); }
template<class T> static auto QueueLength(T &Queue, long) -> decltype(Queue.GetSize()) { return Queue.GetSize(); }
static CBalloonQueue *CallbackQueue;
static CNotifyToolbar *CallbackToolbar;
static InternalIconData *CallbackSource;
static NOTIFYICONDATAW CallbackData;
static BOOL Readded;
static void ReplaceDuringClose(void) { CallbackQueue->UpdateInfo(CallbackSource); }
static void ReaddDuringDelete(void) { Readded = CallbackToolbar->AddButton(&CallbackData); }
static void ReplaceDuringPosition(void)
{
    StringCchCopyW(CallbackSource->szInfo, _countof(CallbackSource->szInfo), L"nested replacement");
    CallbackQueue->UpdateInfo(CallbackSource);
}

int main(void)
{
    INITCOMMONCONTROLSEX Controls = {sizeof(Controls), ICC_BAR_CLASSES};
    InitCommonControlsEx(&Controls);
    hExplorerInstance = GetModuleHandleW(NULL);
    hProcessHeap = GetProcessHeap();
    WNDCLASSW Class = {0};
    Class.hInstance = hExplorerInstance;
    Class.lpszClassName = L"TrayLifetimeFixture";
    Class.lpfnWndProc = TestWindowProc;
    RegisterClassW(&Class);
    HWND Parent = CreateWindowExW(0, Class.lpszClassName, L"tray source fixture", WS_POPUP,
                                  0, 0, 240, 40, NULL, NULL, hExplorerInstance, NULL);
    Check("tray.parent_window", Parent != NULL);
    CTooltips Tips;
    Tips.Create(Parent);
    CBalloonQueue Queue;
    CNotifyToolbar Toolbar;
    Toolbar.Initialize(Parent, &Queue);
    Queue.Init(Parent, &Toolbar, &Tips);
    OriginalTipProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(Tips.m_hWnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(TestTipProc)));
    Check("tray.toolbar_window", Toolbar.m_hWnd != NULL);
    NOTIFYICONDATAW Data = {sizeof(Data)};
    Data.hWnd = Parent;
    Data.uID = 1;
    Data.uFlags = NIF_ICON;
    Data.hIcon = LoadTrayIcon(101);
    Check("tray.fixture_icon", Data.hIcon != NULL);
    printf("CASE sender_destroys_icon_before_resize\n");
    Check("tray.add", Toolbar.AddButton(&Data));
    DestroyIcon(Data.hIcon);
    RenderSize = 18;
    Toolbar.ResizeImagelist();
    Check("tray.resize_retains_pixels", HasPixels(reinterpret_cast<HIMAGELIST>(Toolbar.SendMessage(TB_GETIMAGELIST)), Slot(Toolbar, 1)));
    Check("tray.remove", Toolbar.RemoveButton(&Data));

    printf("CASE deleting_no_image_preserves_other_slots\n");
    Data.hIcon = LoadTrayIcon(101);
    Check("tray.image_add", Toolbar.AddButton(&Data));
    Data.uID = 2;
    Data.uFlags = 0;
    Check("tray.no_image_add", Toolbar.AddButton(&Data));
    Check("tray.no_image_remove", Toolbar.RemoveButton(&Data));
    Check("tray.other_slot_survives", HasPixels(reinterpret_cast<HIMAGELIST>(Toolbar.SendMessage(TB_GETIMAGELIST)), Slot(Toolbar, 1)));
    Data.uID = 1;
    Check("tray.image_remove", Toolbar.RemoveButton(&Data));
    DestroyIcon(Data.hIcon);

    printf("CASE shared_slot_survives_owner_delete\n");
    Data.uFlags = NIF_ICON;
    Data.hIcon = LoadTrayIcon(101);
    Check("tray.owner_add", Toolbar.AddButton(&Data));
    Data.uID = 2;
    Data.uFlags |= NIF_STATE;
    Data.dwState = Data.dwStateMask = NIS_SHAREDICON;
    Check("tray.shared_add", Toolbar.AddButton(&Data));
    Check("tray.same_slot", Slot(Toolbar, 1) == Slot(Toolbar, 2));
    Data.uID = 1;
    Check("tray.owner_remove", Toolbar.RemoveButton(&Data));
    Check("tray.shared_pixels_survive", HasPixels(reinterpret_cast<HIMAGELIST>(Toolbar.SendMessage(TB_GETIMAGELIST)), Slot(Toolbar, 2)));
    Data.uID = 2;
    Check("tray.shared_remove", Toolbar.RemoveButton(&Data));
    DestroyIcon(Data.hIcon);
    printf("CASE repeated_queued_source_is_purged\n");
    Data.uID = 1;
    Data.uFlags = NIF_ICON | NIF_MESSAGE;
    Data.dwState = Data.dwStateMask = 0;
    Data.uCallbackMessage = WM_APP + 71;
    Data.hIcon = LoadTrayIcon(101);
    Check("tray.balloon_source1_add", Toolbar.AddButton(&Data));
    Data.uID = 2;
    Check("tray.balloon_source2_add", Toolbar.AddButton(&Data));
    InternalIconData *One, *Two;
    Toolbar.FindItem(Parent, 1, &One);
    Toolbar.FindItem(Parent, 2, &Two);
    StringCchCopyW(One->szInfo, _countof(One->szInfo), L"first");
    StringCchCopyW(Two->szInfo, _countof(Two->szInfo), L"queued");
    Queue.UpdateInfo(One);
    Queue.UpdateInfo(Two);
    Queue.UpdateInfo(Two);
    Check("tray.two_queued", QueueLength(Queue.m_queue, 0) == 2);
    Check("tray.queued_source_remove", Toolbar.RemoveButton(&Data));
    Check("tray.all_queued_source_entries_removed", QueueLength(Queue.m_queue, 0) == 0);
    Queue.Deinit();
    Queue.Init(Parent, &Toolbar, &Tips);
    Data.uID = 1;
    Check("tray.first_source_remove", Toolbar.RemoveButton(&Data));
    DestroyIcon(Data.hIcon);

    printf("CASE reentrant_close_keeps_replacement\n");
    Data.hIcon = LoadTrayIcon(101);
    Check("tray.reentrant_source_add", Toolbar.AddButton(&Data));
    Toolbar.FindItem(Parent, 1, &One);
    StringCchCopyW(One->szInfo, _countof(One->szInfo), L"replacement");
    Queue.UpdateInfo(One);
    CallbackQueue = &Queue;
    CallbackSource = One;
    CallbackHook = ReplaceDuringClose;
    Queue.CloseCurrent();
    Check("tray.reentrant_replacement_retained", Queue.m_current == One);
    CallbackToolbar = &Toolbar;
    CallbackData = Data;
    CallbackHook = ReaddDuringDelete;
    Check("tray.reentrant_remove", Toolbar.RemoveButton(&Data));
    Check("tray.reentrant_add_succeeds", Readded);
    Check("tray.reentrant_add_not_deleted_by_outer", Toolbar.FindItem(Parent, 1, NULL) >= 0);
    CallbackHook = NULL;
    Toolbar.RemoveButton(&Data);
    DestroyIcon(Data.hIcon);
    Queue.Deinit();
    Queue.Init(Parent, &Toolbar, &Tips);

    printf("CASE queued_balloon_snapshots_caller_icon\n");
    Toolbar.SetModern(TRUE, 40);
    Data.hIcon = LoadTrayIcon(101);
    Check("tray.snapshot_source1_add", Toolbar.AddButton(&Data));
    Toolbar.FindItem(Parent, 1, &One);
    StringCchCopyW(One->szInfo, _countof(One->szInfo), L"active");
    Queue.UpdateInfo(One);
    Data.uID = 2;
    Data.uFlags |= NIF_INFO;
    Data.dwInfoFlags = NIIF_USER | NIIF_NOSOUND;
    Data.hBalloonIcon = LoadTrayIcon(102);
    StringCchCopyW(Data.szInfo, _countof(Data.szInfo), L"queued custom");
    Check("tray.snapshot_source2_add", Toolbar.AddButton(&Data));
    DestroyIcon(Data.hIcon);
    DestroyIcon(Data.hBalloonIcon);
    Queue.CloseCurrent();
    LastNotificationIconValid = FALSE;
    Queue.OnTimer(CBalloonQueue::BalloonsTimerId);
    Check("tray.queued_icon_snapshot_valid", LastNotificationIconValid);
    Toolbar.RemoveButton(&Data);
    Data.uID = 1;
    Toolbar.RemoveButton(&Data);
    Queue.Deinit();
    Queue.Init(Parent, &Toolbar, &Tips);
    printf("CASE shared_modify_uses_new_slot\n");
    Data = NOTIFYICONDATAW();
    Data.cbSize = sizeof(Data);
    Data.hWnd = Parent;
    Data.uID = 1;
    Data.uFlags = NIF_ICON;
    Data.hIcon = LoadTrayIcon(101);
    Check("tray.modify_owner_add", Toolbar.AddButton(&Data));
    Data.uID = 2;
    Data.uFlags |= NIF_STATE;
    Data.dwState = Data.dwStateMask = NIS_SHAREDICON;
    Check("tray.modify_shared_add", Toolbar.AddButton(&Data));
    Data.uID = 1;
    Data.dwState = 0;
    HICON OldIcon = Data.hIcon;
    Data.hIcon = LoadTrayIcon(102);
    Check("tray.owner_modify", Toolbar.UpdateButton(&Data));
    Check("tray.shared_modify_preserves_alias_slot", Slot(Toolbar, 1) != Slot(Toolbar, 2));
    Check("tray.shared_modify_retains_both_pixels", HasPixels(reinterpret_cast<HIMAGELIST>(Toolbar.SendMessage(TB_GETIMAGELIST)), Slot(Toolbar, 1)) && HasPixels(reinterpret_cast<HIMAGELIST>(Toolbar.SendMessage(TB_GETIMAGELIST)), Slot(Toolbar, 2)));
    Toolbar.RemoveButton(&Data);
    Data.uID = 2;
    Toolbar.RemoveButton(&Data);
    DestroyIcon(Data.hIcon);
    DestroyIcon(OldIcon);

    printf("CASE resize_failure_keeps_original_list\n");
    RenderSize = 16;
    Data.uID = 1;
    Data.uFlags = NIF_ICON;
    Data.hIcon = LoadTrayIcon(101);
    Toolbar.ResizeImagelist();
    Check("tray.resize_failure_source_add", Toolbar.AddButton(&Data));
    HIMAGELIST Previous = reinterpret_cast<HIMAGELIST>(Toolbar.SendMessage(TB_GETIMAGELIST));
    RenderSize = 18;
    FailExtract = 1;
    Toolbar.ResizeImagelist();
    Check("tray.resize_failure_preserves_list", reinterpret_cast<HIMAGELIST>(Toolbar.SendMessage(TB_GETIMAGELIST)) == Previous);
    Check("tray.resize_failure_preserves_pixels", HasPixels(Previous, Slot(Toolbar, 1)));
    FailExtract = 0;
    Toolbar.ResizeImagelist();
    Toolbar.RemoveButton(&Data);
    DestroyIcon(Data.hIcon);

    printf("CASE failed_balloon_add_leaves_no_row\n");
    Data.hIcon = LoadTrayIcon(101);
    Data.uFlags = NIF_ICON | NIF_INFO;
    Data.dwInfoFlags = NIIF_INFO;
    StringCchCopyW(Data.szInfo, _countof(Data.szInfo), L"allocation failure");
    FailMemory = 2;
    Check("tray.failed_balloon_add_status", !Toolbar.AddButton(&Data));
    Check("tray.failed_balloon_add_no_row", Toolbar.FindItem(Parent, 1, NULL) < 0);
    FailMemory = 0;
    Toolbar.RemoveButton(&Data);
    DestroyIcon(Data.hIcon);

    printf("CASE queued_balloon_allocation_failure_cleans_snapshot\n");
    Data.hIcon = LoadTrayIcon(101);
    Data.uID = 1;
    Data.uFlags = NIF_ICON;
    Check("tray.queue_failure_source1_add", Toolbar.AddButton(&Data));
    Toolbar.FindItem(Parent, 1, &One);
    StringCchCopyW(One->szInfo, _countof(One->szInfo), L"active");
    Queue.UpdateInfo(One);
    Data.uID = 2;
    Check("tray.queue_failure_source2_add", Toolbar.AddButton(&Data));
    Data.uFlags = NIF_INFO;
    FailQueueAdd = TRUE;
    Check("tray.queue_failure_status", !Toolbar.UpdateButton(&Data));
    Check("tray.queue_failure_no_queued_info", QueueLength(Queue.m_queue, 0) == 0);
    Check("tray.queue_failure_preserves_registered_row", Toolbar.FindItem(Parent, 2, NULL) >= 0);
    Toolbar.RemoveButton(&Data);
    Data.uID = 1;
    Toolbar.RemoveButton(&Data);
    DestroyIcon(Data.hIcon);
    Data.uFlags = NIF_ICON;

    printf("CASE reentrant_position_keeps_new_text\n");
    Toolbar.SetModern(FALSE, 0);
    Data.hIcon = LoadTrayIcon(101);
    Data.uFlags = NIF_ICON;
    Check("tray.position_source_add", Toolbar.AddButton(&Data));
    Toolbar.FindItem(Parent, 1, &One);
    CallbackQueue = &Queue;
    CallbackSource = One;
    StringCchCopyW(One->szInfo, _countof(One->szInfo), L"initial");
    TipHook = ReplaceDuringPosition;
    Queue.UpdateInfo(One);
    Check("tray.position_replacement_text_retained", !wcscmp(LastTip, L"nested replacement"));
    Toolbar.RemoveButton(&Data);
    DestroyIcon(Data.hIcon);
    Queue.Deinit();
    Toolbar.DestroyWindow();
    Check("tray.source_allocations_released", LiveMemory == 0);
    SetWindowLongPtrW(Tips.m_hWnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(OriginalTipProc));
    Tips.DestroyWindow();
    DestroyWindow(Parent);
    Failures += RunPowerLifetimeTests();
    printf("RESULT failures=%u\n", Failures);
    return Failures ? 1 : 0;
}
