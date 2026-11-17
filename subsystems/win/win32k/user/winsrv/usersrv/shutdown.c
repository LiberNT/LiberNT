/*
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         ReactOS User API Server DLL
 * FILE:            win32ss/user/winsrv/usersrv/shutdown.c
 * PURPOSE:         Logout/shutdown
 * PROGRAMMERS:
 */

/* INCLUDES *******************************************************************/

#include "usersrv.h"

#include <commctrl.h>
#include <psapi.h>
#include <ntstrsafe.h>

#include "resource.h"

#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

// Those flags (that are used for CsrProcess->ShutdownFlags) are named
// in accordance to the only public one: SHUTDOWN_NORETRY used for the
// SetProcessShutdownParameters API.
#if !defined(SHUTDOWN_SYSTEMCONTEXT) && !defined(SHUTDOWN_OTHERCONTEXT)
#define SHUTDOWN_SYSTEMCONTEXT  CsrShutdownSystem
#define SHUTDOWN_OTHERCONTEXT   CsrShutdownOther
#endif

// The DPRINTs that need to really be removed as soon as everything works.
#define MY_DPRINT  DPRINT1
#define MY_DPRINT2 DPRINT

typedef struct tagNOTIFY_CONTEXT
{
    UINT Msg;
    WPARAM wParam;
    LPARAM lParam;
    // HDESK Desktop;
    // HDESK OldDesktop;
    DWORD StartTime;
    DWORD QueryResult;
    HWND Dlg;
    DWORD EndNowResult;
    BOOL ShowUI;
    HANDLE UIThread;
    HWND WndClient;
    HWND BlockingWnd;
    PSHUTDOWN_SETTINGS ShutdownSettings;
} NOTIFY_CONTEXT, *PNOTIFY_CONTEXT;

#define QUERY_RESULT_ABORT    0
#define QUERY_RESULT_CONTINUE 1
#define QUERY_RESULT_TIMEOUT  2
#define QUERY_RESULT_ERROR    3
#define QUERY_RESULT_FORCE    4

#define BLOCK_SCREEN_TIMEOUT    60000
#define BLOCK_SCREEN_MAX_APPS   16

typedef struct tagBLOCK_SCREEN_APP
{
    HWND Wnd;
    HICON Icon;
} BLOCK_SCREEN_APP, *PBLOCK_SCREEN_APP;

typedef struct tagBLOCK_SCREEN
{
    HWND BlockingWnd;
    UINT StringBase;
    HANDLE CloseEvent;
    DWORD Result;
    HFONT TitleFont;
    HFONT TextFont;
    ULONG AppCount;
    BLOCK_SCREEN_APP Apps[BLOCK_SCREEN_MAX_APPS];
} BLOCK_SCREEN, *PBLOCK_SCREEN;

static LONG PendingExitFlags;
static ULONG BlockScreenFlags;

typedef void (WINAPI *INITCOMMONCONTROLS_PROC)(void);

typedef struct tagMESSAGE_CONTEXT
{
    HWND Wnd;
    UINT Msg;
    WPARAM wParam;
    LPARAM lParam;
    DWORD Timeout;
} MESSAGE_CONTEXT, *PMESSAGE_CONTEXT;


/* FUNCTIONS ******************************************************************/

static HMODULE hComCtl32Lib = NULL;

static VOID
CallInitCommonControls(VOID)
{
    static BOOL Initialized = FALSE;
    INITCOMMONCONTROLS_PROC InitProc;

    if (Initialized) return;

    hComCtl32Lib = LoadLibraryW(L"COMCTL32.DLL");
    if (hComCtl32Lib == NULL) return;

    InitProc = (INITCOMMONCONTROLS_PROC)GetProcAddress(hComCtl32Lib, "InitCommonControls");
    if (InitProc == NULL) return;

    (*InitProc)();

    Initialized = TRUE;
}

static VOID FASTCALL
UpdateProgressBar(HWND ProgressBar, PNOTIFY_CONTEXT NotifyContext)
{
    DWORD Passed;

    Passed = GetTickCount() - NotifyContext->StartTime;
    Passed -= NotifyContext->ShutdownSettings->HungAppTimeout;
    if (NotifyContext->ShutdownSettings->WaitToKillAppTimeout < Passed)
    {
        Passed = NotifyContext->ShutdownSettings->WaitToKillAppTimeout;
    }
    SendMessageW(ProgressBar, PBM_SETPOS, Passed / 2, 0);
}

static INT_PTR CALLBACK
EndNowDlgProc(HWND Dlg, UINT Msg, WPARAM wParam, LPARAM lParam)
{
    INT_PTR Result;
    PNOTIFY_CONTEXT NotifyContext;
    HWND ProgressBar;
    DWORD TitleLength;
    int Len;
    LPWSTR Title;

    switch(Msg)
    {
    case WM_INITDIALOG:
        NotifyContext = (PNOTIFY_CONTEXT)lParam;
        NotifyContext->EndNowResult = QUERY_RESULT_ABORT;
        SetWindowLongPtrW(Dlg, DWLP_USER, (LONG_PTR)lParam);
        TitleLength = SendMessageW(NotifyContext->WndClient, WM_GETTEXTLENGTH,
                                   0, 0) +
                      GetWindowTextLengthW(Dlg);
        Title = HeapAlloc(UserServerHeap, 0, (TitleLength + 1) * sizeof(WCHAR));
        if (Title)
        {
            Len = GetWindowTextW(Dlg, Title, TitleLength + 1);
            SendMessageW(NotifyContext->WndClient, WM_GETTEXT,
                         TitleLength + 1 - Len, (LPARAM)(Title + Len));
            SetWindowTextW(Dlg, Title);
            HeapFree(UserServerHeap, 0, Title);
        }
        ProgressBar = GetDlgItem(Dlg, IDC_PROGRESS);
        SendMessageW(ProgressBar, PBM_SETRANGE32, 0,
                     NotifyContext->ShutdownSettings->WaitToKillAppTimeout / 2);
        UpdateProgressBar(ProgressBar, NotifyContext);
        SetTimer(Dlg, 0, 200, NULL);
        Result = FALSE;
        break;

    case WM_TIMER:
        NotifyContext = (PNOTIFY_CONTEXT)GetWindowLongPtrW(Dlg, DWLP_USER);
        ProgressBar = GetDlgItem(Dlg, IDC_PROGRESS);
        UpdateProgressBar(ProgressBar, NotifyContext);
        Result = TRUE;
        break;

    case WM_COMMAND:
        if (BN_CLICKED == HIWORD(wParam) && IDC_END_NOW == LOWORD(wParam))
        {
            NotifyContext = (PNOTIFY_CONTEXT)GetWindowLongPtrW(Dlg, DWLP_USER);
            NotifyContext->EndNowResult = QUERY_RESULT_FORCE;
            MY_DPRINT("Closing progress dlg by hand\n");
            SendMessageW(Dlg, WM_CLOSE, 0, 0);
            Result = TRUE;
        }
        else
        {
            Result = FALSE;
        }
        break;

    case WM_CLOSE:
        MY_DPRINT("WM_CLOSE\n");
        DestroyWindow(Dlg);
        Result = TRUE;
        break;

    case WM_DESTROY:
        MY_DPRINT("WM_DESTROY\n");
        NotifyContext = (PNOTIFY_CONTEXT)GetWindowLongPtrW(Dlg, DWLP_USER);
        NotifyContext->Dlg = NULL;
        KillTimer(Dlg, 0);
        PostQuitMessage(NotifyContext->EndNowResult);
        Result = TRUE;
        break;

    default:
        Result = FALSE;
        break;
    }

    return Result;
}

static DWORD WINAPI
EndNowThreadProc(LPVOID Parameter)
{
    PNOTIFY_CONTEXT NotifyContext = (PNOTIFY_CONTEXT)Parameter;
    MSG Msg;

#if 0
    SetThreadDesktop(NotifyContext->Desktop);
    SwitchDesktop(NotifyContext->Desktop);
#else
    /* For now show the end task dialog in the active desktop */
    NtUserSetInformationThread(NtCurrentThread(),
                               UserThreadUseActiveDesktop,
                               NULL,
                               0);
#endif

    CallInitCommonControls();
    NotifyContext->Dlg = CreateDialogParam(UserServerDllInstance,
                                           MAKEINTRESOURCE(IDD_END_NOW), NULL,
                                           EndNowDlgProc, (LPARAM)NotifyContext);
    if (NotifyContext->Dlg == NULL)
        return 0;

    ShowWindow(NotifyContext->Dlg, SW_SHOWNORMAL);

    while (GetMessageW(&Msg, NULL, 0, 0))
    {
        if (!IsDialogMessage(NotifyContext->Dlg, &Msg))
        {
            TranslateMessage(&Msg);
            DispatchMessageW(&Msg);
        }
    }

    return Msg.wParam;
}

static DWORD WINAPI
SendClientShutdown(LPVOID Parameter)
{
    PMESSAGE_CONTEXT Context = (PMESSAGE_CONTEXT)Parameter;
    DWORD_PTR Result;

    /* If the shutdown is aborted, just notify the process, there is no need to wait */
    if ((Context->wParam & (MCS_QUERYENDSESSION | MCS_ENDSESSION)) == 0)
    {
        DPRINT("Called WM_CLIENTSHUTDOWN with wParam == 0 ...\n");
        SendNotifyMessageW(Context->Wnd, WM_CLIENTSHUTDOWN,
                           Context->wParam, Context->lParam);
        return QUERY_RESULT_CONTINUE;
    }

    if (SendMessageTimeoutW(Context->Wnd, WM_CLIENTSHUTDOWN,
                            Context->wParam, Context->lParam,
                            SMTO_NORMAL, Context->Timeout, &Result))
    {
        DWORD Ret;

        if (Context->wParam & MCS_QUERYENDSESSION)
        {
            /* WM_QUERYENDSESSION case */
            switch (Result)
            {
                case MCSR_DONOTSHUTDOWN:
                    Ret = QUERY_RESULT_ABORT;
                    break;

                case MCSR_GOODFORSHUTDOWN:
                case MCSR_SHUTDOWNFINISHED:
                default:
                    Ret = QUERY_RESULT_CONTINUE;
            }
        }
        else
        {
            /* WM_ENDSESSION case */
            Ret = QUERY_RESULT_CONTINUE;
        }

        DPRINT("SendClientShutdown -- Return == %s\n",
                  Ret == QUERY_RESULT_CONTINUE ? "Continue" : "Abort");
        return Ret;
    }

    DPRINT1("SendClientShutdown -- Error == %s\n",
              GetLastError() == 0 ? "Timeout" : "error");

    return (GetLastError() == 0 ? QUERY_RESULT_TIMEOUT : QUERY_RESULT_ERROR);
}

static BOOL
NotifyTopLevelWindow(HWND Wnd, PNOTIFY_CONTEXT NotifyContext)
{
    MESSAGE_CONTEXT MessageContext;
    DWORD Now, Passed;
    DWORD Timeout, WaitStatus;
    HANDLE MessageThread;
    HANDLE Threads[2];

    SetForegroundWindow(Wnd);

    Now = GetTickCount();
    if (NotifyContext->StartTime == 0)
        NotifyContext->StartTime = Now;

    /*
     * Note: Passed is computed correctly even when GetTickCount()
     * wraps due to unsigned arithmetic.
     */
    Passed = Now - NotifyContext->StartTime;
    MessageContext.Wnd = Wnd;
    MessageContext.Msg = NotifyContext->Msg;
    MessageContext.wParam = NotifyContext->wParam;
    MessageContext.lParam = NotifyContext->lParam;
    MessageContext.Timeout = NotifyContext->ShutdownSettings->HungAppTimeout;
    if (!NotifyContext->ShutdownSettings->AutoEndTasks)
    {
        MessageContext.Timeout += NotifyContext->ShutdownSettings->WaitToKillAppTimeout;
    }
    if (Passed < MessageContext.Timeout)
    {
        MessageContext.Timeout -= Passed;
        MessageThread = CreateThread(NULL, 0, SendClientShutdown,
                                     (LPVOID)&MessageContext, 0, NULL);
        if (MessageThread == NULL)
        {
            NotifyContext->QueryResult = QUERY_RESULT_ERROR;
            return FALSE;
        }
        Timeout = NotifyContext->ShutdownSettings->HungAppTimeout;
        if (Passed < Timeout)
        {
            Timeout -= Passed;
            WaitStatus = WaitForSingleObjectEx(MessageThread, Timeout, FALSE);
        }
        else
        {
            WaitStatus = WAIT_TIMEOUT;
        }
        if (WAIT_TIMEOUT == WaitStatus)
        {
            NotifyContext->WndClient = Wnd;
            if (NotifyContext->UIThread == NULL && NotifyContext->ShowUI)
            {
                NotifyContext->UIThread = CreateThread(NULL, 0,
                                                       EndNowThreadProc,
                                                       (LPVOID)NotifyContext,
                                                       0, NULL);
            }
            Threads[0] = MessageThread;
            Threads[1] = NotifyContext->UIThread;
            WaitStatus = WaitForMultipleObjectsEx(NotifyContext->UIThread == NULL ?
                                                  1 : 2,
                                                  Threads, FALSE, INFINITE,
                                                  FALSE);
            if (WaitStatus == WAIT_OBJECT_0)
            {
                if (!GetExitCodeThread(MessageThread, &NotifyContext->QueryResult))
                {
                    NotifyContext->QueryResult = QUERY_RESULT_ERROR;
                }
            }
            else if (WaitStatus == WAIT_OBJECT_0 + 1)
            {
                if (!GetExitCodeThread(NotifyContext->UIThread,
                                       &NotifyContext->QueryResult))
                {
                    NotifyContext->QueryResult = QUERY_RESULT_ERROR;
                }
            }
            else
            {
                NotifyContext->QueryResult = QUERY_RESULT_ERROR;
            }
            if (WaitStatus != WAIT_OBJECT_0)
            {
                TerminateThread(MessageThread, QUERY_RESULT_TIMEOUT);
            }
        }
        else if (WaitStatus == WAIT_OBJECT_0)
        {
            if (!GetExitCodeThread(MessageThread,
                                   &NotifyContext->QueryResult))
            {
                NotifyContext->QueryResult = QUERY_RESULT_ERROR;
            }
        }
        else
        {
            NotifyContext->QueryResult = QUERY_RESULT_ERROR;
        }
        CloseHandle(MessageThread);
    }
    else
    {
        NotifyContext->QueryResult = QUERY_RESULT_TIMEOUT;
    }

    DPRINT("NotifyContext->QueryResult == %d\n", NotifyContext->QueryResult);
    return (NotifyContext->QueryResult == QUERY_RESULT_CONTINUE);
}

static BOOLEAN
IsConsoleMode(VOID)
{
    return (BOOLEAN)NtUserCallNoParam(NOPARAM_ROUTINE_ISCONSOLEMODE);
}

static INT
BlockScreenScale(HWND Wnd, INT Value)
{
    HDC hdc = GetDC(Wnd);
    INT Dpi = hdc ? GetDeviceCaps(hdc, LOGPIXELSY) : 96;

    if (hdc)
        ReleaseDC(Wnd, hdc);
    return MulDiv(Value, Dpi, 96);
}

static VOID
BlockScreenAddApp(PBLOCK_SCREEN Screen, HWND Wnd)
{
    PBLOCK_SCREEN_APP App;
    DWORD_PTR Icon = 0;

    ASSERT(Screen->AppCount < ARRAYSIZE(Screen->Apps));
    App = &Screen->Apps[Screen->AppCount++];
    App->Wnd = Wnd;
    if (!SendMessageTimeoutW(Wnd, WM_GETICON, ICON_BIG, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK, 200, &Icon) || !Icon)
        Icon = GetClassLongPtrW(Wnd, GCLP_HICON);
    App->Icon = Icon ? (HICON)Icon : LoadIconW(NULL, IDI_APPLICATION);
}

static BOOL CALLBACK
BlockScreenFindApps(HWND Wnd, LPARAM lParam)
{
    PBLOCK_SCREEN Screen = (PBLOCK_SCREEN)lParam;
    DWORD ProcessId, ShellProcessId = 0;
    HWND ShellWnd = GetShellWindow();

    if (Screen->AppCount >= ARRAYSIZE(Screen->Apps))
        return FALSE;
    if (Wnd == Screen->BlockingWnd || !IsWindowVisible(Wnd) || GetWindow(Wnd, GW_OWNER) ||
        (GetWindowLongPtrW(Wnd, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) || !GetWindowTextLengthW(Wnd))
    {
        return TRUE;
    }
    GetWindowThreadProcessId(Wnd, &ProcessId);
    if (ShellWnd)
        GetWindowThreadProcessId(ShellWnd, &ShellProcessId);
    if (ProcessId == GetCurrentProcessId() || ProcessId == ShellProcessId)
        return TRUE;
    BlockScreenAddApp(Screen, Wnd);
    return TRUE;
}

static VOID
BlockScreenPaint(HWND Wnd, PBLOCK_SCREEN Screen)
{
    PAINTSTRUCT ps;
    RECT Client, Rect;
    WCHAR Format[128], Text[256];
    INT Left, Top, Item;
    ULONG Index;
    HDC hdc = BeginPaint(Wnd, &ps);

    GetClientRect(Wnd, &Client);
    FillRect(hdc, &Client, GetStockObject(BLACK_BRUSH));
    Left = (Client.right - BlockScreenScale(Wnd, 548)) / 2;
    Top = (Client.bottom - BlockScreenScale(Wnd, 452)) / 2;
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(255, 255, 255));

    LoadStringW(UserServerDllInstance,
                Screen->StringBase + (Screen->AppCount == 1 ? IDS_BLOCK_TITLE_ONE : IDS_BLOCK_TITLE),
                Format, ARRAYSIZE(Format));
    RtlStringCchPrintfW(Text, ARRAYSIZE(Text), Format, Screen->AppCount);
    SelectObject(hdc, Screen->TitleFont);
    SetRect(&Rect, Left, Top, Client.right, Top + BlockScreenScale(Wnd, 30));
    DrawTextW(hdc, Text, -1, &Rect, DT_SINGLELINE | DT_NOPREFIX);

    SelectObject(hdc, Screen->TextFont);
    LoadStringW(UserServerDllInstance, IDS_BLOCK_HINT, Text, ARRAYSIZE(Text));
    SetRect(&Rect, Left, Top + BlockScreenScale(Wnd, 29), Client.right, Top + BlockScreenScale(Wnd, 50));
    DrawTextW(hdc, Text, -1, &Rect, DT_SINGLELINE | DT_NOPREFIX);

    for (Index = 0; Index < Screen->AppCount; ++Index)
    {
        Item = Top + BlockScreenScale(Wnd, 98 + 64 * Index);
        if (Item + BlockScreenScale(Wnd, 64) > Top + BlockScreenScale(Wnd, 410))
            break;
        DrawIconEx(hdc, Left + BlockScreenScale(Wnd, 18), Item + BlockScreenScale(Wnd, 6), Screen->Apps[Index].Icon,
                   BlockScreenScale(Wnd, 32), BlockScreenScale(Wnd, 32), 0, NULL, DI_NORMAL);
        if (!InternalGetWindowText(Screen->Apps[Index].Wnd, Text, ARRAYSIZE(Text)))
            Text[0] = UNICODE_NULL;
        SetTextColor(hdc, RGB(255, 255, 255));
        SetRect(&Rect, Left + BlockScreenScale(Wnd, 62), Item, Client.right, Item + BlockScreenScale(Wnd, 20));
        DrawTextW(hdc, Text, -1, &Rect, DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
        if (Screen->Apps[Index].Wnd != Screen->BlockingWnd)
            continue;
        /* TODO: show the reason the application registered with ShutdownBlockReasonCreate */
        LoadStringW(UserServerDllInstance, Screen->StringBase + IDS_BLOCK_REASON, Text, ARRAYSIZE(Text));
        SetTextColor(hdc, RGB(160, 160, 160));
        OffsetRect(&Rect, 0, BlockScreenScale(Wnd, 20));
        DrawTextW(hdc, Text, -1, &Rect, DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    }

    EndPaint(Wnd, &ps);
}

static HWND
BlockScreenCreateButton(HWND Wnd, PBLOCK_SCREEN Screen, UINT Id, UINT StringId, DWORD Style, INT x)
{
    WCHAR Text[64];
    SIZE Size = { 0 };
    INT Width;
    HWND Button;
    HDC hdc;

    LoadStringW(UserServerDllInstance, StringId, Text, ARRAYSIZE(Text));
    hdc = GetDC(Wnd);
    if (hdc)
    {
        SelectObject(hdc, Screen->TextFont);
        GetTextExtentPoint32W(hdc, Text, (INT)wcslen(Text), &Size);
        ReleaseDC(Wnd, hdc);
    }
    Width = max(Size.cx + BlockScreenScale(Wnd, 8), BlockScreenScale(Wnd, 120));
    Button = CreateWindowExW(0, L"BUTTON", Text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | Style,
                             x, 0, Width, BlockScreenScale(Wnd, 34), Wnd, (HMENU)UlongToHandle(Id),
                             UserServerDllInstance, NULL);
    if (Button)
        SendMessageW(Button, WM_SETFONT, (WPARAM)Screen->TextFont, FALSE);
    return Button;
}

static LRESULT CALLBACK
BlockScreenWndProc(HWND Wnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
    PBLOCK_SCREEN Screen = (PBLOCK_SCREEN)GetWindowLongPtrW(Wnd, GWLP_USERDATA);
    NONCLIENTMETRICSW Metrics;
    HWND Force, Cancel;
    RECT Rect;
    INT Left, Top;

    switch (Msg)
    {
    case WM_CREATE:
        Screen = (PBLOCK_SCREEN)((LPCREATESTRUCTW)lParam)->lpCreateParams;
        SetWindowLongPtrW(Wnd, GWLP_USERDATA, (LONG_PTR)Screen);
        Metrics.cbSize = sizeof(Metrics);
        if (!SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(Metrics), &Metrics, 0))
            GetObjectW(GetStockObject(DEFAULT_GUI_FONT), sizeof(Metrics.lfMessageFont), &Metrics.lfMessageFont);
        Metrics.lfMessageFont.lfHeight = -BlockScreenScale(Wnd, 20);
        Metrics.lfMessageFont.lfWeight = FW_NORMAL;
        Screen->TitleFont = CreateFontIndirectW(&Metrics.lfMessageFont);
        Metrics.lfMessageFont.lfHeight = -BlockScreenScale(Wnd, 12);
        Screen->TextFont = CreateFontIndirectW(&Metrics.lfMessageFont);

        BlockScreenAddApp(Screen, Screen->BlockingWnd);
        EnumWindows(BlockScreenFindApps, (LPARAM)Screen);

        GetClientRect(Wnd, &Rect);
        Left = (Rect.right - BlockScreenScale(Wnd, 548)) / 2;
        Top = (Rect.bottom - BlockScreenScale(Wnd, 452)) / 2 + BlockScreenScale(Wnd, 418);
        Force = BlockScreenCreateButton(Wnd, Screen, IDC_BLOCK_FORCE, Screen->StringBase + IDS_BLOCK_FORCE,
                                        BS_PUSHBUTTON, Left);
        if (!Force)
            return -1;
        GetWindowRect(Force, &Rect);
        Cancel = BlockScreenCreateButton(Wnd, Screen, IDCANCEL, IDS_BLOCK_CANCEL, BS_DEFPUSHBUTTON,
                                         Left + (Rect.right - Rect.left) + BlockScreenScale(Wnd, 7));
        if (!Cancel)
            return -1;
        SetWindowPos(Force, NULL, Left, Top, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
        GetWindowRect(Cancel, &Rect);
        MapWindowPoints(NULL, Wnd, (LPPOINT)&Rect, 2);
        SetWindowPos(Cancel, NULL, Rect.left, Top, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
        SetFocus(Cancel);
        return 0;

    case WM_PAINT:
        BlockScreenPaint(Wnd, Screen);
        return 0;

    case WM_ERASEBKGND:
        return TRUE;

    case WM_CTLCOLORBTN:
        return (LRESULT)GetStockObject(BLACK_BRUSH);

    case WM_COMMAND:
        if (HIWORD(wParam) != BN_CLICKED)
            break;
        if (LOWORD(wParam) == IDC_BLOCK_FORCE)
            Screen->Result = QUERY_RESULT_FORCE;
        else if (LOWORD(wParam) == IDCANCEL)
            Screen->Result = QUERY_RESULT_ABORT;
        else
            break;
        DestroyWindow(Wnd);
        return 0;

    case WM_CLOSE:
        DestroyWindow(Wnd);
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    case WM_NCDESTROY:
        if (Screen)
        {
            if (Screen->TitleFont)
                DeleteObject(Screen->TitleFont);
            if (Screen->TextFont)
                DeleteObject(Screen->TextFont);
            Screen->TitleFont = Screen->TextFont = NULL;
        }
        break;
    }

    return DefWindowProcW(Wnd, Msg, wParam, lParam);
}

static DWORD WINAPI
BlockScreenThreadProc(LPVOID Parameter)
{
    PBLOCK_SCREEN Screen = (PBLOCK_SCREEN)Parameter;
    WNDCLASSW WndClass = { 0 };
    HWND Wnd;
    MSG Msg;

    NtUserSetInformationThread(NtCurrentThread(), UserThreadUseActiveDesktop, NULL, 0);

    WndClass.lpfnWndProc = BlockScreenWndProc;
    WndClass.hInstance = UserServerDllInstance;
    WndClass.hCursor = LoadCursorW(NULL, IDC_ARROW);
    WndClass.lpszClassName = L"ShutdownBlockScreen";
    if (!RegisterClassW(&WndClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return 0;

    Wnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_CONTROLPARENT, WndClass.lpszClassName, NULL, WS_POPUP,
                          0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN),
                          NULL, NULL, UserServerDllInstance, Screen);
    if (!Wnd)
        return 0;
    ShowWindow(Wnd, SW_SHOWNORMAL);
    SetForegroundWindow(Wnd);

    for (;;)
    {
        if (MsgWaitForMultipleObjects(1, &Screen->CloseEvent, FALSE, INFINITE, QS_ALLINPUT) == WAIT_OBJECT_0 &&
            IsWindow(Wnd))
        {
            DestroyWindow(Wnd);
        }
        while (PeekMessageW(&Msg, NULL, 0, 0, PM_REMOVE))
        {
            if (Msg.message == WM_QUIT)
                return 0;
            if (!IsDialogMessageW(Wnd, &Msg))
            {
                TranslateMessage(&Msg);
                DispatchMessageW(&Msg);
            }
        }
    }
}

static DWORD
AskUserAboutBlockingApp(PCSR_PROCESS CsrProcess, PNOTIFY_CONTEXT Context)
{
    BLOCK_SCREEN Screen;
    HANDLE Handles[2];
    DWORD Wait;

    ASSERT(Context->BlockingWnd != NULL);

    if (IsConsoleMode())
    {
        return WaitForSingleObject(CsrProcess->ProcessHandle, Context->ShutdownSettings->WaitToKillAppTimeout) ==
               WAIT_OBJECT_0 ? QUERY_RESULT_CONTINUE : QUERY_RESULT_ABORT;
    }

    RtlZeroMemory(&Screen, sizeof(Screen));
    Screen.BlockingWnd = Context->BlockingWnd;
    if (BlockScreenFlags & EWX_REBOOT)
        Screen.StringBase = IDS_BLOCK_RESTART_TITLE_ONE;
    else if (BlockScreenFlags & EWX_SHUTDOWN)
        Screen.StringBase = IDS_BLOCK_SHUTDOWN_TITLE_ONE;
    else
        Screen.StringBase = IDS_BLOCK_SIGNOUT_TITLE_ONE;
    Screen.Result = QUERY_RESULT_ABORT;
    Screen.CloseEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!Screen.CloseEvent)
        return QUERY_RESULT_ABORT;

    Handles[0] = CreateThread(NULL, 0, BlockScreenThreadProc, &Screen, 0, NULL);
    if (!Handles[0])
    {
        CloseHandle(Screen.CloseEvent);
        return QUERY_RESULT_ABORT;
    }
    Handles[1] = CsrProcess->ProcessHandle;

    Wait = WaitForMultipleObjects(ARRAYSIZE(Handles), Handles, FALSE, BLOCK_SCREEN_TIMEOUT);
    if (Wait != WAIT_OBJECT_0)
    {
        SetEvent(Screen.CloseEvent);
        WaitForSingleObject(Handles[0], INFINITE);
    }
    CloseHandle(Handles[0]);
    CloseHandle(Screen.CloseEvent);
    ASSERT(Screen.TitleFont == NULL && Screen.TextFont == NULL);

    if (Wait == WAIT_OBJECT_0 + 1)
        return QUERY_RESULT_CONTINUE;
    if (Wait == WAIT_OBJECT_0 && Screen.Result == QUERY_RESULT_FORCE)
        return QUERY_RESULT_CONTINUE;
    /* TODO: Windows 11 locks the workstation when the screen times out */
    return QUERY_RESULT_ABORT;
}

/************************************************/


static BOOL
ThreadShutdownNotify(IN PCSR_THREAD CsrThread,
                     IN ULONG Flags,
                     IN ULONG Flags2,
                     IN PNOTIFY_CONTEXT Context)
{
    HWND TopWnd = NULL;

    EnumThreadWindows(HandleToUlong(CsrThread->ClientId.UniqueThread),
                      FindTopLevelWnd, (LPARAM)&TopWnd);
    if (TopWnd)
    {
        HWND hWndOwner;

        /*** FOR TESTING PURPOSES ONLY!! ***/
        HWND tmpWnd;
        tmpWnd = TopWnd;
        /***********************************/

        while ((hWndOwner = GetWindow(TopWnd, GW_OWNER)) != NULL)
        {
            MY_DPRINT("GetWindow(TopWnd, GW_OWNER) not returned NULL...\n");
            TopWnd = hWndOwner;
        }
        if (TopWnd != tmpWnd) MY_DPRINT("(TopWnd = %x) != (tmpWnd = %x)\n", TopWnd, tmpWnd);
    }
    else
    {
        return FALSE;
    }

    Context->wParam = Flags2 | ((Flags & EWX_CALLER_WINLOGON_LOGOFF) ? ENDSESSION_LOGOFF : 0);
    Context->lParam = 0;

    Context->StartTime = 0;
    Context->UIThread = NULL;
    Context->ShowUI = !IsConsoleMode() && (Flags2 & (MCS_QUERYENDSESSION | MCS_ENDSESSION));
    Context->Dlg = NULL;

#if 0 // Obviously, switching desktops like that from within WINSRV doesn't work...
    {
    BOOL Success;
    Context->OldDesktop = GetThreadDesktop(GetCurrentThreadId());
    // Context->Desktop    = GetThreadDesktop(HandleToUlong(CsrThread->ClientId.UniqueThread));
    Context->Desktop    = GetThreadDesktop(GetWindowThreadProcessId(TopWnd, NULL));
    MY_DPRINT("Last error = %d\n", GetLastError());
    MY_DPRINT("Before switching to desktop 0x%x\n", Context->Desktop);
    Success = SwitchDesktop(Context->Desktop);
    MY_DPRINT("After switching to desktop (Success = %s ; last error = %d); going to notify top-level...\n",
            Success ? "TRUE" : "FALSE", GetLastError());
    }
#endif

    NotifyTopLevelWindow(TopWnd, Context);
    if (Context->QueryResult == QUERY_RESULT_ABORT)
        Context->BlockingWnd = TopWnd;

/******************************************************************************/
#if 1
    if (Context->UIThread)
    {
        MY_DPRINT("Context->UIThread != NULL\n");
        if (Context->Dlg)
        {
            MY_DPRINT("Sending WM_CLOSE because Dlg is != NULL\n");
            SendMessageW(Context->Dlg, WM_CLOSE, 0, 0);
        }
        else
        {
            MY_DPRINT("Terminating UIThread thread with QUERY_RESULT_ERROR\n");
            TerminateThread(Context->UIThread, QUERY_RESULT_ERROR);
        }
        CloseHandle(Context->UIThread);
        /**/Context->UIThread = NULL;/**/
        /**/Context->Dlg = NULL;/**/
    }
#endif
/******************************************************************************/

#if 0
    MY_DPRINT("Switch back to old desktop 0x%x\n", Context->OldDesktop);
    SwitchDesktop(Context->OldDesktop);
    MY_DPRINT("Switched back ok\n");
#endif

    return TRUE;
}

static ULONG
NotifyUserProcessForShutdown(PCSR_PROCESS CsrProcess,
                             PSHUTDOWN_SETTINGS ShutdownSettings,
                             UINT Flags)
{
    DWORD QueryResult = QUERY_RESULT_CONTINUE;
    PCSR_PROCESS Process;
    PCSR_THREAD Thread;
    PCSR_THREAD *Threads;
    ULONG ThreadCount, Index;
    ULONG Result = CsrShutdownNonCsrProcess;
    PLIST_ENTRY NextEntry;
    NOTIFY_CONTEXT Context;
    BOOL FoundWindows = FALSE;

    /* In case we make a forced shutdown, just kill the process */
    if (Flags & EWX_FORCE)
        return CsrShutdownCsrProcess;

    Context.ShutdownSettings = ShutdownSettings;
    Context.QueryResult = QUERY_RESULT_CONTINUE; // We continue shutdown by default.
    Context.BlockingWnd = NULL;

    /* Keep a referenced snapshot: notification handlers can exit threads. */
    if (!NT_SUCCESS(CsrLockProcessByClientId(CsrProcess->ClientId.UniqueProcess, &Process)))
        return CsrShutdownNonCsrProcess;
    if (Process->ThreadCount == 0)
    {
        CsrUnlockProcess(Process);
        return CsrShutdownNonCsrProcess;
    }
    if (Process->ThreadCount > MAXULONG / sizeof(*Threads))
    {
        CsrUnlockProcess(Process);
        return CsrShutdownCancelled;
    }
    Threads = HeapAlloc(UserServerHeap, 0, Process->ThreadCount * sizeof(*Threads));
    if (Threads == NULL)
    {
        CsrUnlockProcess(Process);
        return CsrShutdownCancelled;
    }

    ThreadCount = 0;
    NextEntry = CsrProcess->ThreadList.Flink;
    while (NextEntry != &CsrProcess->ThreadList)
    {
        Thread = CONTAINING_RECORD(NextEntry, CSR_THREAD, Link);
        NextEntry = NextEntry->Flink;
        if (Thread->Flags & CsrThreadTerminated) continue;
        CsrReferenceThread(Thread);
        Threads[ThreadCount++] = Thread;
    }
    CsrUnlockProcess(Process);

    /* Send first the QUERYENDSESSION messages to all the threads of the process */
    MY_DPRINT2("Sending the QUERYENDSESSION messages...\n");
    for (Index = 0; Index < ThreadCount; ++Index)
    {
        Thread = Threads[Index];
        if (Thread->Flags & CsrThreadTerminated) continue;

        Context.QueryResult = QUERY_RESULT_CONTINUE;
        if (ThreadShutdownNotify(Thread, Flags, MCS_QUERYENDSESSION, &Context))
        {
            FoundWindows = TRUE;
        }

        if (Context.QueryResult == QUERY_RESULT_ABORT)
            break;
    }

    if (!FoundWindows)
    {
        /* We looped all threads but no top level window was found so we didn't send any message */
        /* Let the console server run the generic process shutdown handler */
        goto Cleanup;
    }

    QueryResult = Context.QueryResult;
    if (QueryResult == QUERY_RESULT_ABORT)
        QueryResult = AskUserAboutBlockingApp(CsrProcess, &Context);
    MY_DPRINT2("QueryResult = %s\n",
               QueryResult == QUERY_RESULT_ABORT ? "Abort" : "Continue");

    /* Now send the ENDSESSION messages to the threads */
    MY_DPRINT2("Now sending the ENDSESSION messages...\n");

    for (Index = 0; Index < ThreadCount; ++Index)
    {
        Thread = Threads[Index];
        if (Thread->Flags & CsrThreadTerminated) continue;

        Context.QueryResult = QUERY_RESULT_CONTINUE;
        ThreadShutdownNotify(Thread, Flags,
                             (QUERY_RESULT_ABORT != QueryResult) ? MCS_ENDSESSION : 0,
                             &Context);
    }

#if 0
    if (Context.UIThread)
    {
        if (Context.Dlg)
        {
            SendMessageW(Context.Dlg, WM_CLOSE, 0, 0);
        }
        else
        {
            TerminateThread(Context.UIThread, QUERY_RESULT_ERROR);
        }
        CloseHandle(Context.UIThread);
    }
#endif

    Result = (QueryResult == QUERY_RESULT_ABORT) ? CsrShutdownCancelled : CsrShutdownCsrProcess;

Cleanup:
    for (Index = 0; Index < ThreadCount; ++Index)
        CsrDereferenceThread(Threads[Index]);
    HeapFree(UserServerHeap, 0, Threads);
    return Result;
}

static NTSTATUS FASTCALL
UserExitReactOS(PCSR_THREAD CsrThread, UINT Flags)
{
    NTSTATUS Status;
    LUID CallerLuid;

    DWORD ProcessId = HandleToUlong(CsrThread->ClientId.UniqueProcess);
    DWORD ThreadId  = HandleToUlong(CsrThread->ClientId.UniqueThread);

    DPRINT("SrvExitWindowsEx(ClientId: %lx.%lx, Flags: 0x%x)\n",
           ProcessId, ThreadId, Flags);

    /*
     * Check for flags validity
     */

    if (Flags & EWX_CALLER_WINLOGON)
    {
        /* Only Winlogon can call this */
        if (ProcessId != LogonProcessId)
        {
            DPRINT1("SrvExitWindowsEx call not from Winlogon\n");
            return STATUS_ACCESS_DENIED;
        }
    }

    /* Implicitely add the shutdown flag when we poweroff or reboot */
    if (Flags & (EWX_POWEROFF | EWX_REBOOT))
        Flags |= EWX_SHUTDOWN;

    /*
     * Impersonate and retrieve the caller's LUID so that
     * we can only shutdown processes in its context.
     */
    if (!CsrImpersonateClient(NULL))
        return STATUS_BAD_IMPERSONATION_LEVEL;

    Status = CsrGetProcessLuid(NULL, &CallerLuid);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Unable to get caller LUID, Status = 0x%08x\n", Status);
        goto Quit;
    }

    DPRINT("Caller LUID is: %lx.%lx\n", CallerLuid.HighPart, CallerLuid.LowPart);

    /* Shutdown loop */
    while (TRUE)
    {
        /* Notify Win32k and potentially Winlogon of the shutdown */
        Status = NtUserSetInformationThread(CsrThread->ThreadHandle,
                                            UserThreadInitiateShutdown,
                                            &Flags, sizeof(Flags));
        DPRINT("Win32k says: %lx\n", Status);
        switch (Status)
        {
            /* We cannot wait here, the caller should start a new thread */
            case STATUS_CANT_WAIT:
                DPRINT1("NtUserSetInformationThread returned STATUS_CANT_WAIT\n");
                goto Quit;

            /* Shutdown is in progress */
            case STATUS_PENDING:
                DPRINT("NtUserSetInformationThread returned STATUS_PENDING\n");
                InterlockedExchange(&PendingExitFlags, (LONG)Flags);
                goto Quit;

            /* Abort */
            case STATUS_RETRY:
            {
                DPRINT1("NtUserSetInformationThread returned STATUS_RETRY\n");
                UNIMPLEMENTED;
                continue;
            }

            default:
            {
                if (!NT_SUCCESS(Status))
                {
                    // FIXME: Use some UserSetLastNTError or SetLastNtError
                    // that we have defined for user32 or win32k usage only...
                    SetLastError(RtlNtStatusToDosError(Status));
                    goto Quit;
                }
            }
        }

        /* All good */
        break;
    }

    CsrRevertToSelf();

    /*
     * OK we can continue. Now magic happens:
     *
     * Terminate all Win32 processes, stop if we find one kicking
     * and screaming it doesn't want to die.
     *
     * This function calls the ShutdownProcessCallback callback of
     * each CSR server for each Win32 process.
     */
    BlockScreenFlags = (ULONG)InterlockedExchange(&PendingExitFlags, 0);
    if (Flags & EWX_SHUTDOWN)
        BlockScreenFlags = Flags;
    Status = CsrShutdownProcesses(&CallerLuid, Flags);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Failed to shutdown processes, Status = 0x%08x\n", Status);
    }

    // FIXME: If Status == STATUS_CANCELLED, call RecordShutdownReason

    /* Tell Win32k and potentially Winlogon that we're done */
    NtUserSetInformationThread(CsrThread->ThreadHandle,
                               UserThreadEndShutdown,
                               &Status, sizeof(Status));

    DPRINT("SrvExitWindowsEx returned 0x%08x\n", Status);
    return Status;

Quit:
    /* We are done */
    CsrRevertToSelf();
    return Status;
}


ULONG
NTAPI
UserClientShutdown(IN PCSR_PROCESS CsrProcess,
                   IN ULONG Flags,
                   IN BOOLEAN FirstPhase)
{
    ULONG result;

    DPRINT("UserClientShutdown(0x%p, 0x%x, %s) - [0x%x, 0x%x], ShutdownFlags: %lu\n",
            CsrProcess, Flags, FirstPhase ? "FirstPhase" : "LastPhase",
            CsrProcess->ClientId.UniqueProcess, CsrProcess->ClientId.UniqueThread,
            CsrProcess->ShutdownFlags);

    /*
     * Check for process validity
     */

    /* Do not kill system processes when a user is logging off */
    if ((Flags & EWX_SHUTDOWN) == EWX_LOGOFF &&
        (CsrProcess->ShutdownFlags & (SHUTDOWN_OTHERCONTEXT | SHUTDOWN_SYSTEMCONTEXT)))
    {
        DPRINT("Do not kill a system process in a logoff request!\n");
        return CsrShutdownNonCsrProcess;
    }

    /* Do not kill Winlogon */
    if (CsrProcess->ClientId.UniqueProcess == UlongToHandle(LogonProcessId))
    {
        DPRINT("Not killing Winlogon; CsrProcess->ShutdownFlags = %lu\n",
               CsrProcess->ShutdownFlags);

        /* Returning CsrShutdownCsrProcess means that we handled this process by doing nothing */
        /* This will mark winlogon as processed so consrv won't be notified again for it */
        CsrDereferenceProcess(CsrProcess);
        return CsrShutdownCsrProcess;
    }

    /* Notify the process for shutdown if needed */
    result = NotifyUserProcessForShutdown(CsrProcess, &ShutdownSettings, Flags);
    if (result == CsrShutdownCancelled || result == CsrShutdownNonCsrProcess)
    {
        if (result == CsrShutdownCancelled)
        {
            DPRINT1("Process 0x%x aborted shutdown\n", CsrProcess->ClientId.UniqueProcess);
            CsrDereferenceProcess(CsrProcess);
        }
        return result;
    }

    if (CsrProcess->ShutdownFlags & (SHUTDOWN_OTHERCONTEXT | SHUTDOWN_SYSTEMCONTEXT))
    {
        CsrDereferenceProcess(CsrProcess);
        return CsrShutdownCsrProcess;
    }

    /* Terminate this process */
#if DBG
    {
        WCHAR buffer[MAX_PATH];
        if (!GetProcessImageFileNameW(CsrProcess->ProcessHandle, buffer, ARRAYSIZE(buffer)))
        {
            DPRINT("Terminating process %x\n", CsrProcess->ClientId.UniqueProcess);
        }
        else
        {
            DPRINT("Terminating process %x (%S)\n", CsrProcess->ClientId.UniqueProcess, buffer);
        }
    }
#endif
    NtTerminateProcess(CsrProcess->ProcessHandle, 0);
    WaitForSingleObject(CsrProcess->ProcessHandle, ShutdownSettings.ProcessTerminateTimeout);

    /* We are done */
    CsrDereferenceProcess(CsrProcess);
    return CsrShutdownCsrProcess;
}


/* PUBLIC SERVER APIS *********************************************************/

/* API_NUMBER: UserpExitWindowsEx */
CSR_API(SrvExitWindowsEx)
{
    NTSTATUS Status;
    PUSER_EXIT_REACTOS ExitReactOSRequest = &((PUSER_API_MESSAGE)ApiMessage)->Data.ExitReactOSRequest;

    Status = NtUserSetInformationThread(NtCurrentThread(),
                                        UserThreadUseActiveDesktop,
                                        NULL,
                                        0);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Failed to set thread desktop!\n");
        return Status;
    }

    Status = UserExitReactOS(CsrGetClientThread(), ExitReactOSRequest->Flags);
    ExitReactOSRequest->Success   = NT_SUCCESS(Status);
    ExitReactOSRequest->LastError = GetLastError();

    NtUserSetInformationThread(NtCurrentThread(), UserThreadRestoreDesktop, NULL, 0);

    return Status;
}

/* API_NUMBER: UserpEndTask */
CSR_API(SrvEndTask)
{
    PUSER_END_TASK EndTaskRequest = &((PUSER_API_MESSAGE)ApiMessage)->Data.EndTaskRequest;
    NTSTATUS Status;

    // FIXME: This is HACK-plemented!!
    DPRINT1("SrvEndTask is HACKPLEMENTED!!\n");

    Status = NtUserSetInformationThread(NtCurrentThread(),
                                        UserThreadUseActiveDesktop,
                                        NULL,
                                        0);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Failed to set thread desktop!\n");
        return Status;
    }

    SendMessageW(EndTaskRequest->WndHandle, WM_CLOSE, 0, 0);
    // PostMessageW(EndTaskRequest->WndHandle, WM_CLOSE, 0, 0);

    if (IsWindow(EndTaskRequest->WndHandle))
    {
        if (EndTaskRequest->Force)
        {
            EndTaskRequest->Success   = DestroyWindow(EndTaskRequest->WndHandle);
            EndTaskRequest->LastError = GetLastError();
        }
        else
        {
            EndTaskRequest->Success = FALSE;
        }
    }
    else
    {
        EndTaskRequest->Success   = TRUE;
        EndTaskRequest->LastError = ERROR_SUCCESS;
    }

    NtUserSetInformationThread(NtCurrentThread(), UserThreadRestoreDesktop, NULL, 0);

    return STATUS_SUCCESS;
}

/* API_NUMBER: UserpRecordShutdownReason */
CSR_API(SrvRecordShutdownReason)
{
    DPRINT1("%s not yet implemented\n", __FUNCTION__);
    return STATUS_NOT_IMPLEMENTED;
}

/* EOF */
