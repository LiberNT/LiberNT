/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests the DPI awareness contract documented for Windows 10 and Windows 11
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"
#include <shellscalingapi.h>

typedef DPI_AWARENESS_CONTEXT (WINAPI *PSETTHREADCTX)(DPI_AWARENESS_CONTEXT);
typedef DPI_AWARENESS_CONTEXT (WINAPI *PGETTHREADCTX)(VOID);
typedef DPI_AWARENESS_CONTEXT (WINAPI *PGETWINDOWCTX)(HWND);
typedef DPI_AWARENESS (WINAPI *PGETAWARENESS)(DPI_AWARENESS_CONTEXT);
typedef UINT (WINAPI *PGETDPIFROMCTX)(DPI_AWARENESS_CONTEXT);
typedef BOOL (WINAPI *PCTXEQUAL)(DPI_AWARENESS_CONTEXT, DPI_AWARENESS_CONTEXT);
typedef BOOL (WINAPI *PISVALIDCTX)(DPI_AWARENESS_CONTEXT);
typedef UINT (WINAPI *PGETDPIFORWINDOW)(HWND);
typedef UINT (WINAPI *PGETDPIFORSYSTEM)(VOID);
typedef INT (WINAPI *PGETMETRICSFORDPI)(INT, UINT);
typedef BOOL (WINAPI *PSPIFORDPI)(UINT, UINT, PVOID, UINT, UINT);
typedef BOOL (WINAPI *PADJUSTFORDPI)(LPRECT, DWORD, BOOL, DWORD, UINT);
typedef BOOL (WINAPI *PPOINTCONVERT)(HWND, LPPOINT);
typedef DPI_HOSTING_BEHAVIOR (WINAPI *PSETHOSTING)(DPI_HOSTING_BEHAVIOR);
typedef DPI_HOSTING_BEHAVIOR (WINAPI *PGETHOSTING)(VOID);
typedef DPI_HOSTING_BEHAVIOR (WINAPI *PGETWINDOWHOSTING)(HWND);
typedef BOOL (WINAPI *PENABLENCSCALING)(HWND);
typedef BOOL (WINAPI *PSETPROCESSCTX)(DPI_AWARENESS_CONTEXT);
typedef HRESULT (WINAPI *PGETDPIFORMONITOR)(HMONITOR, INT, UINT *, UINT *);

static PSETTHREADCTX pSetThreadDpiAwarenessContext;
static PGETTHREADCTX pGetThreadDpiAwarenessContext;
static PGETWINDOWCTX pGetWindowDpiAwarenessContext;
static PGETAWARENESS pGetAwarenessFromDpiAwarenessContext;
static PGETDPIFROMCTX pGetDpiFromDpiAwarenessContext;
static PCTXEQUAL pAreDpiAwarenessContextsEqual;
static PISVALIDCTX pIsValidDpiAwarenessContext;
static PGETDPIFORWINDOW pGetDpiForWindow;
static PGETDPIFORSYSTEM pGetDpiForSystem;
static PGETMETRICSFORDPI pGetSystemMetricsForDpi;
static PSPIFORDPI pSystemParametersInfoForDpi;
static PADJUSTFORDPI pAdjustWindowRectExForDpi;
static PPOINTCONVERT pLogicalToPhysicalPointForPerMonitorDPI;
static PPOINTCONVERT pPhysicalToLogicalPointForPerMonitorDPI;
static PSETHOSTING pSetThreadDpiHostingBehavior;
static PGETHOSTING pGetThreadDpiHostingBehavior;
static PGETWINDOWHOSTING pGetWindowDpiHostingBehavior;
static PENABLENCSCALING pEnableNonClientDpiScaling;
static PSETPROCESSCTX pSetProcessDpiAwarenessContext;
static PGETDPIFORMONITOR pGetDpiForMonitor;

static const WCHAR ClassName[] = L"DpiAwarenessTest";
static DPI_AWARENESS ProcAwareness = DPI_AWARENESS_INVALID;
static BOOL NcScalingResult;
static BOOL NcScalingRequested;
static DWORD NcScalingError;
static CREATESTRUCTW CbtCreate;
static POINT MouseMovePoint = {-1, -1}, HitTestPoint = {-1, -1};
static DWORD MouseMovePos;

#define ok_near(Actual, Expected, What) \
    ok(labs((LONG)(Actual) - (LONG)(Expected)) <= 1, "%s: got %ld, expected %ld\n", What, (LONG)(Actual), (LONG)(Expected))

static DPI_AWARENESS
ThreadAwareness(void)
{
    return pGetAwarenessFromDpiAwarenessContext(pGetThreadDpiAwarenessContext());
}

static DPI_AWARENESS
WindowAwareness(HWND hWnd)
{
    return pGetAwarenessFromDpiAwarenessContext(pGetWindowDpiAwarenessContext(hWnd));
}

static LRESULT CALLBACK
TestWndProc(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
    switch (Msg)
    {
        case WM_NCCREATE:
            if (NcScalingRequested)
            {
                SetLastError(0xdeadbeef);
                NcScalingResult = pEnableNonClientDpiScaling(hWnd);
                NcScalingError = GetLastError();
            }
            break;
        case WM_USER:
            ProcAwareness = ThreadAwareness();
            return 0;
        case WM_NCHITTEST:
            HitTestPoint.x = (SHORT)LOWORD(lParam);
            HitTestPoint.y = (SHORT)HIWORD(lParam);
            break;
        case WM_MOUSEMOVE:
            MouseMovePoint.x = (SHORT)LOWORD(lParam);
            MouseMovePoint.y = (SHORT)HIWORD(lParam);
            MouseMovePos = GetMessagePos();
            return 0;
    }
    return DefWindowProcW(hWnd, Msg, wParam, lParam);
}

static HWND
CreateTestWindow(DWORD Style, INT x, INT y, INT cx, INT cy, HWND Parent)
{
    return CreateWindowExW(0, ClassName, L"DPI awareness", Style, x, y, cx, cy, Parent, NULL,
                           GetModuleHandleW(NULL), NULL);
}

static void
DispatchPending(void)
{
    MSG Msg;

    while (PeekMessageW(&Msg, NULL, 0, 0, PM_REMOVE))
    {
        TranslateMessage(&Msg);
        DispatchMessageW(&Msg);
    }
}

static void
WaitForMouseMove(LONG X, LONG Y)
{
    DWORD Start = GetTickCount();

    DispatchPending();
    while ((labs(MouseMovePoint.x - X) > 1 || labs(MouseMovePoint.y - Y) > 1) && GetTickCount() - Start < 100)
    {
        MsgWaitForMultipleObjects(0, NULL, FALSE, 20, QS_MOUSEMOVE);
        DispatchPending();
    }
}

static UINT
GetAwareSystemDpi(void)
{
    DPI_AWARENESS_CONTEXT Previous = pSetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_SYSTEM_AWARE);
    UINT Dpi = pGetDpiForSystem();

    pSetThreadDpiAwarenessContext(Previous);
    return Dpi;
}

static LONG
CursorImageSize(HCURSOR Cursor)
{
    ICONINFO Info;
    BITMAP Bitmap;
    LONG Size = -1;

    if (!Cursor || !GetIconInfo(Cursor, &Info))
        return -1;
    if (GetObjectW(Info.hbmColor ? Info.hbmColor : Info.hbmMask, sizeof(Bitmap), &Bitmap))
        Size = Bitmap.bmWidth;
    if (Info.hbmColor)
        DeleteObject(Info.hbmColor);
    DeleteObject(Info.hbmMask);
    return Size;
}

static void
TestContextHandles(UINT Dpi)
{
    static const struct
    {
        DPI_AWARENESS_CONTEXT Context;
        DPI_AWARENESS Awareness;
        UINT Dpi;
    } Contexts[] =
    {
        { DPI_AWARENESS_CONTEXT_UNAWARE, DPI_AWARENESS_UNAWARE, USER_DEFAULT_SCREEN_DPI },
        { DPI_AWARENESS_CONTEXT_SYSTEM_AWARE, DPI_AWARENESS_SYSTEM_AWARE, 0 },
        { DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE, DPI_AWARENESS_PER_MONITOR_AWARE, 0 },
        { DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2, DPI_AWARENESS_PER_MONITOR_AWARE, 0 },
        { DPI_AWARENESS_CONTEXT_UNAWARE_GDISCALED, DPI_AWARENESS_UNAWARE, USER_DEFAULT_SCREEN_DPI },
    };
    DPI_AWARENESS_CONTEXT Previous;
    DPI_HOSTING_BEHAVIOR Hosting;
    ULONG Index;

    for (Index = 0; Index < ARRAYSIZE(Contexts); Index++)
    {
        UINT Expected = Contexts[Index].Awareness == DPI_AWARENESS_SYSTEM_AWARE ? Dpi : Contexts[Index].Dpi;

        ok(pIsValidDpiAwarenessContext(Contexts[Index].Context), "[P3] context %lu is not valid\n", Index);
        ok_int(pGetAwarenessFromDpiAwarenessContext(Contexts[Index].Context), Contexts[Index].Awareness);
        ok(pGetDpiFromDpiAwarenessContext(Contexts[Index].Context) == Expected,
           "[P3] context %lu DPI %u, expected %u\n", Index,
           pGetDpiFromDpiAwarenessContext(Contexts[Index].Context), Expected);
    }
    ok(!pIsValidDpiAwarenessContext((DPI_AWARENESS_CONTEXT)-6), "[P3] context -6 is valid\n");
    ok_int(pGetAwarenessFromDpiAwarenessContext((DPI_AWARENESS_CONTEXT)-6), DPI_AWARENESS_INVALID);

    ok_int(ThreadAwareness(), DPI_AWARENESS_UNAWARE);
    ok(pSetThreadDpiAwarenessContext((DPI_AWARENESS_CONTEXT)-6) == NULL, "[P7] invalid context accepted\n");
    ok_int(ThreadAwareness(), DPI_AWARENESS_UNAWARE);
    Previous = pSetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_SYSTEM_AWARE);
    ok(Previous != NULL, "[P7] SetThreadDpiAwarenessContext failed\n");
    ok_int(pGetAwarenessFromDpiAwarenessContext(Previous), DPI_AWARENESS_UNAWARE);
    ok_int(ThreadAwareness(), DPI_AWARENESS_SYSTEM_AWARE);
    ok(pAreDpiAwarenessContextsEqual(pGetThreadDpiAwarenessContext(), DPI_AWARENESS_CONTEXT_SYSTEM_AWARE),
       "[P3] thread context does not equal DPI_AWARENESS_CONTEXT_SYSTEM_AWARE\n");
    pSetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    ok_int(ThreadAwareness(), DPI_AWARENESS_PER_MONITOR_AWARE);
    pSetThreadDpiAwarenessContext(Previous);
    ok_int(ThreadAwareness(), DPI_AWARENESS_UNAWARE);

    ok_int(pGetThreadDpiHostingBehavior(), DPI_HOSTING_BEHAVIOR_DEFAULT);
    ok_int(pSetThreadDpiHostingBehavior((DPI_HOSTING_BEHAVIOR)5), DPI_HOSTING_BEHAVIOR_INVALID);
    ok_int(pGetThreadDpiHostingBehavior(), DPI_HOSTING_BEHAVIOR_DEFAULT);
    Hosting = pSetThreadDpiHostingBehavior(DPI_HOSTING_BEHAVIOR_MIXED);
    ok_int(Hosting, DPI_HOSTING_BEHAVIOR_DEFAULT);
    ok_int(pGetThreadDpiHostingBehavior(), DPI_HOSTING_BEHAVIOR_MIXED);
    ok_int(ThreadAwareness(), DPI_AWARENESS_UNAWARE);
    ok_int(pSetThreadDpiHostingBehavior(DPI_HOSTING_BEHAVIOR_DEFAULT), DPI_HOSTING_BEHAVIOR_MIXED);
}

static void
CheckQueries(UINT Expected, const char *Context)
{
    HMONITOR Monitor = MonitorFromPoint((POINT){0, 0}, MONITOR_DEFAULTTOPRIMARY);
    UINT DpiX = 0, DpiY = 0;
    HDC hDC;

    ok(pGetDpiForSystem() == Expected, "[P6] %s GetDpiForSystem %u, expected %u\n", Context,
       pGetDpiForSystem(), Expected);
    hDC = GetDC(NULL);
    ok(GetDeviceCaps(hDC, LOGPIXELSX) == (INT)Expected && GetDeviceCaps(hDC, LOGPIXELSY) == (INT)Expected,
       "[P1] %s LOGPIXELS %d,%d, expected %u\n", Context, GetDeviceCaps(hDC, LOGPIXELSX),
       GetDeviceCaps(hDC, LOGPIXELSY), Expected);
    ReleaseDC(NULL, hDC);
    if (pGetDpiForMonitor)
    {
        ok(pGetDpiForMonitor(Monitor, MDT_EFFECTIVE_DPI, &DpiX, &DpiY) == S_OK, "[P14] GetDpiForMonitor failed\n");
        ok(DpiX == Expected && DpiY == Expected, "[P14] %s GetDpiForMonitor %u,%u, expected %u\n", Context,
           DpiX, DpiY, Expected);
    }
}

static void
TestSystemDpiQueries(UINT Dpi)
{
    HMONITOR Monitor = MonitorFromPoint((POINT){0, 0}, MONITOR_DEFAULTTOPRIMARY);
    DPI_AWARENESS_CONTEXT Previous;
    UINT DpiX, DpiY;

    CheckQueries(USER_DEFAULT_SCREEN_DPI, "unaware");
    Previous = pSetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_SYSTEM_AWARE);
    CheckQueries(Dpi, "system aware");
    pSetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if (pGetDpiForMonitor)
    {
        DpiX = DpiY = 0;
        ok(pGetDpiForMonitor(Monitor, MDT_EFFECTIVE_DPI, &DpiX, &DpiY) == S_OK, "[P14] GetDpiForMonitor failed\n");
        ok(DpiX == Dpi && DpiY == Dpi, "[P14] per monitor GetDpiForMonitor %u,%u, expected %u\n", DpiX, DpiY, Dpi);
    }
    pSetThreadDpiAwarenessContext(Previous);

    ok_int(pGetDpiForWindow(NULL), 0);
    ok_int(pGetDpiForWindow((HWND)(ULONG_PTR)0xdeadbeef), 0);
}

static void
CompareMetrics(UINT Dpi, const char *Context)
{
    static const INT Scaled[] =
    {
        SM_CXVSCROLL, SM_CYHSCROLL, SM_CYCAPTION, SM_CYVTHUMB, SM_CXHTHUMB, SM_CXICON, SM_CYICON,
        SM_CXCURSOR, SM_CYCURSOR, SM_CYMENU, SM_CXSIZE, SM_CYSIZE, SM_CXSMICON, SM_CYSMICON,
        SM_CYSMCAPTION, SM_CXSMSIZE, SM_CYSMSIZE, SM_CXMENUSIZE, SM_CYMENUSIZE, SM_CXICONSPACING,
        SM_CYICONSPACING, SM_CXMENUCHECK, SM_CYMENUCHECK,
    };
    NONCLIENTMETRICSW Ncm = {sizeof(Ncm)}, NcmDpi = {sizeof(NcmDpi)};
    ICONMETRICSW Icon = {sizeof(Icon)}, IconDpi = {sizeof(IconDpi)};
    RECT Rect = {0, 0, 300, 200}, RectDpi = {0, 0, 300, 200};
    ULONG Index;

    for (Index = 0; Index < ARRAYSIZE(Scaled); Index++)
    {
        ok(GetSystemMetrics(Scaled[Index]) == pGetSystemMetricsForDpi(Scaled[Index], Dpi),
           "[P2] %s metric %d: GetSystemMetrics %d, GetSystemMetricsForDpi(%u) %d\n", Context, Scaled[Index],
           GetSystemMetrics(Scaled[Index]), Dpi, pGetSystemMetricsForDpi(Scaled[Index], Dpi));
    }

    ok(SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(Ncm), &Ncm, 0), "SPI_GETNONCLIENTMETRICS failed\n");
    ok(pSystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(NcmDpi), &NcmDpi, 0, Dpi),
       "SystemParametersInfoForDpi failed\n");
    ok(Ncm.iCaptionHeight == NcmDpi.iCaptionHeight && Ncm.iCaptionWidth == NcmDpi.iCaptionWidth &&
       Ncm.iScrollWidth == NcmDpi.iScrollWidth && Ncm.iScrollHeight == NcmDpi.iScrollHeight &&
       Ncm.iMenuHeight == NcmDpi.iMenuHeight && Ncm.iMenuWidth == NcmDpi.iMenuWidth &&
       Ncm.iSmCaptionHeight == NcmDpi.iSmCaptionHeight && Ncm.iSmCaptionWidth == NcmDpi.iSmCaptionWidth,
       "[P2] %s NONCLIENTMETRICS caption %dx%d scroll %dx%d menu %dx%d sm %dx%d, ForDpi(%u) %dx%d %dx%d %dx%d %dx%d\n",
       Context, Ncm.iCaptionWidth, Ncm.iCaptionHeight, Ncm.iScrollWidth, Ncm.iScrollHeight, Ncm.iMenuWidth,
       Ncm.iMenuHeight, Ncm.iSmCaptionWidth, Ncm.iSmCaptionHeight, Dpi, NcmDpi.iCaptionWidth,
       NcmDpi.iCaptionHeight, NcmDpi.iScrollWidth, NcmDpi.iScrollHeight, NcmDpi.iMenuWidth,
       NcmDpi.iMenuHeight, NcmDpi.iSmCaptionWidth, NcmDpi.iSmCaptionHeight);
    ok(Ncm.lfCaptionFont.lfHeight == NcmDpi.lfCaptionFont.lfHeight &&
       Ncm.lfMenuFont.lfHeight == NcmDpi.lfMenuFont.lfHeight &&
       Ncm.lfMessageFont.lfHeight == NcmDpi.lfMessageFont.lfHeight &&
       Ncm.lfStatusFont.lfHeight == NcmDpi.lfStatusFont.lfHeight,
       "[P2] %s font heights %ld %ld %ld %ld, ForDpi(%u) %ld %ld %ld %ld\n", Context,
       Ncm.lfCaptionFont.lfHeight, Ncm.lfMenuFont.lfHeight, Ncm.lfMessageFont.lfHeight,
       Ncm.lfStatusFont.lfHeight, Dpi, NcmDpi.lfCaptionFont.lfHeight, NcmDpi.lfMenuFont.lfHeight,
       NcmDpi.lfMessageFont.lfHeight, NcmDpi.lfStatusFont.lfHeight);

    ok(SystemParametersInfoW(SPI_GETICONMETRICS, sizeof(Icon), &Icon, 0), "SPI_GETICONMETRICS failed\n");
    ok(pSystemParametersInfoForDpi(SPI_GETICONMETRICS, sizeof(IconDpi), &IconDpi, 0, Dpi),
       "SystemParametersInfoForDpi(SPI_GETICONMETRICS) failed\n");
    ok(Icon.iHorzSpacing == IconDpi.iHorzSpacing && Icon.iVertSpacing == IconDpi.iVertSpacing &&
       Icon.lfFont.lfHeight == IconDpi.lfFont.lfHeight,
       "[P2] %s ICONMETRICS %dx%d font %ld, ForDpi(%u) %dx%d font %ld\n", Context, Icon.iHorzSpacing,
       Icon.iVertSpacing, Icon.lfFont.lfHeight, Dpi, IconDpi.iHorzSpacing, IconDpi.iVertSpacing,
       IconDpi.lfFont.lfHeight);

    AdjustWindowRectEx(&Rect, WS_OVERLAPPEDWINDOW, TRUE, 0);
    pAdjustWindowRectExForDpi(&RectDpi, WS_OVERLAPPEDWINDOW, TRUE, 0, Dpi);
    ok(EqualRect(&Rect, &RectDpi), "[P2] %s AdjustWindowRectEx %ld,%ld,%ld,%ld, ForDpi(%u) %ld,%ld,%ld,%ld\n",
       Context, Rect.left, Rect.top, Rect.right, Rect.bottom, Dpi, RectDpi.left, RectDpi.top, RectDpi.right,
       RectDpi.bottom);
}

static void
TestMetricsEquivalence(UINT Dpi)
{
    DPI_AWARENESS_CONTEXT Previous;
    INT UnawareCx = GetSystemMetrics(SM_CXSCREEN), UnawareCy = GetSystemMetrics(SM_CYSCREEN);
    INT AwareCx, AwareCy;

    CompareMetrics(USER_DEFAULT_SCREEN_DPI, "unaware");
    Previous = pSetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_SYSTEM_AWARE);
    CompareMetrics(Dpi, "system aware");
    AwareCx = GetSystemMetrics(SM_CXSCREEN);
    AwareCy = GetSystemMetrics(SM_CYSCREEN);
    pSetThreadDpiAwarenessContext(Previous);

    ok_near(UnawareCx, MulDiv(AwareCx, USER_DEFAULT_SCREEN_DPI, Dpi), "[P1] unaware SM_CXSCREEN");
    ok_near(UnawareCy, MulDiv(AwareCy, USER_DEFAULT_SCREEN_DPI, Dpi), "[P1] unaware SM_CYSCREEN");
}

static void
TestWindowAwareness(UINT Dpi)
{
    DPI_AWARENESS_CONTEXT Previous;
    HWND Unaware, System, Child, SystemChild, Mixed, MixedChild;

    Unaware = CreateTestWindow(WS_OVERLAPPEDWINDOW, 50, 50, 300, 200, NULL);
    ok(Unaware != NULL, "CreateWindowExW failed\n");
    ok_int(WindowAwareness(Unaware), DPI_AWARENESS_UNAWARE);
    ok_int(pGetDpiForWindow(Unaware), USER_DEFAULT_SCREEN_DPI);

    Previous = pSetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_SYSTEM_AWARE);
    System = CreateTestWindow(WS_OVERLAPPEDWINDOW, 60, 60, 300, 200, NULL);
    Child = CreateTestWindow(WS_CHILD | WS_VISIBLE, 0, 0, 50, 50, Unaware);
    SystemChild = CreateTestWindow(WS_CHILD | WS_VISIBLE, 0, 0, 50, 50, System);
    pSetThreadDpiAwarenessContext(Previous);

    ok_int(WindowAwareness(System), DPI_AWARENESS_SYSTEM_AWARE);
    ok(pGetDpiForWindow(System) == Dpi, "[P5] system aware window DPI %u, expected %u\n", pGetDpiForWindow(System), Dpi);
    ok_int(WindowAwareness(Child), DPI_AWARENESS_UNAWARE);
    ok_int(pGetDpiForWindow(Child), USER_DEFAULT_SCREEN_DPI);
    ok_int(WindowAwareness(SystemChild), DPI_AWARENESS_SYSTEM_AWARE);
    ok(pGetDpiForWindow(SystemChild) == Dpi, "[P2] child DPI %u, expected the parent's %u\n",
       pGetDpiForWindow(SystemChild), Dpi);

    ProcAwareness = DPI_AWARENESS_INVALID;
    SendMessageW(System, WM_USER, 0, 0);
    ok_int(ProcAwareness, DPI_AWARENESS_SYSTEM_AWARE);
    ok_int(ThreadAwareness(), DPI_AWARENESS_UNAWARE);
    ProcAwareness = DPI_AWARENESS_INVALID;
    Previous = pSetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_SYSTEM_AWARE);
    SendMessageW(Unaware, WM_USER, 0, 0);
    ok_int(ProcAwareness, DPI_AWARENESS_UNAWARE);
    ok_int(ThreadAwareness(), DPI_AWARENESS_SYSTEM_AWARE);
    pSetThreadDpiAwarenessContext(Previous);

    SetLastError(0xdeadbeef);
    ok(SetParent(SystemChild, Unaware) == NULL, "[P2] SetParent across DPI awareness succeeded\n");
    ok_err(ERROR_INVALID_STATE);
    ok(GetParent(SystemChild) == System, "[P2] SetParent moved the child\n");

    pSetThreadDpiHostingBehavior(DPI_HOSTING_BEHAVIOR_MIXED);
    Mixed = CreateTestWindow(WS_OVERLAPPEDWINDOW, 70, 70, 300, 200, NULL);
    pSetThreadDpiHostingBehavior(DPI_HOSTING_BEHAVIOR_DEFAULT);
    ok_int(pGetWindowDpiHostingBehavior(Mixed), DPI_HOSTING_BEHAVIOR_MIXED);
    ok_int(pGetWindowDpiHostingBehavior(Unaware), DPI_HOSTING_BEHAVIOR_DEFAULT);
    Previous = pSetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_SYSTEM_AWARE);
    MixedChild = CreateTestWindow(WS_CHILD | WS_VISIBLE, 0, 0, 50, 50, Mixed);
    pSetThreadDpiAwarenessContext(Previous);
    ok_int(WindowAwareness(MixedChild), DPI_AWARENESS_SYSTEM_AWARE);
    ok(pGetDpiForWindow(MixedChild) == Dpi, "[P13] mixed hosted child DPI %u, expected %u\n",
       pGetDpiForWindow(MixedChild), Dpi);

    DestroyWindow(Mixed);
    DestroyWindow(System);
    DestroyWindow(Unaware);
}

static void
TestCoordinates(UINT Dpi)
{
    DPI_AWARENESS_CONTEXT Previous;
    RECT Window, AwareWindow, Client;
    POINT Origin = {0, 0}, Point, Cursor, AwareCursor, DcOrigin;
    HWND Unaware, System;
    HDC Dc;

    Unaware = CreateTestWindow(WS_OVERLAPPEDWINDOW | WS_VISIBLE, 80, 60, 240, 180, NULL);
    SetForegroundWindow(Unaware);
    UpdateWindow(Unaware);
    GetWindowRect(Unaware, &Window);
    ok(Window.left == 80 && Window.top == 60 && Window.right == 320 && Window.bottom == 240,
       "[P10] unaware window rect %ld,%ld,%ld,%ld\n", Window.left, Window.top, Window.right, Window.bottom);
    Previous = pSetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_SYSTEM_AWARE);
    GetWindowRect(Unaware, &AwareWindow);
    System = CreateTestWindow(WS_OVERLAPPEDWINDOW, 40, 40, 200, 150, NULL);
    pSetThreadDpiAwarenessContext(Previous);
    ok_near(AwareWindow.left, MulDiv(Window.left, Dpi, USER_DEFAULT_SCREEN_DPI), "[P10] aware view of unaware left");
    ok_near(AwareWindow.right - AwareWindow.left, MulDiv(Window.right - Window.left, Dpi, USER_DEFAULT_SCREEN_DPI),
            "[P10] aware view of unaware width");

    Point.x = Window.left + 30;
    Point.y = Window.top + 40;
    ok(pLogicalToPhysicalPointForPerMonitorDPI(Unaware, &Point), "[P10] LogicalToPhysicalPointForPerMonitorDPI failed\n");
    ok_near(Point.x, MulDiv(Window.left + 30, Dpi, USER_DEFAULT_SCREEN_DPI), "[P10] physical x");
    ok_near(Point.y, MulDiv(Window.top + 40, Dpi, USER_DEFAULT_SCREEN_DPI), "[P10] physical y");
    ok(pPhysicalToLogicalPointForPerMonitorDPI(Unaware, &Point), "[P10] PhysicalToLogicalPointForPerMonitorDPI failed\n");
    ok_near(Point.x, Window.left + 30, "[P10] logical x round trip");
    ok_near(Point.y, Window.top + 40, "[P10] logical y round trip");
    Previous = pSetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_SYSTEM_AWARE);
    GetWindowRect(System, &AwareWindow);
    Point.x = AwareWindow.left + 20;
    Point.y = AwareWindow.top + 20;
    ok(pLogicalToPhysicalPointForPerMonitorDPI(System, &Point), "[P10] LogicalToPhysicalPointForPerMonitorDPI failed\n");
    pSetThreadDpiAwarenessContext(Previous);
    ok(Point.x == AwareWindow.left + 20 && Point.y == AwareWindow.top + 20,
       "[P10] system aware window point %ld,%ld changed\n", Point.x, Point.y);

    GetClientRect(Unaware, &Client);
    ClientToScreen(Unaware, &Origin);
    DispatchPending();
    MouseMovePoint.x = MouseMovePoint.y = -1;
    HitTestPoint.x = HitTestPoint.y = -1;
    SetCursorPos(Origin.x + 20, Origin.y + 30);
    WaitForMouseMove(20, 30);
    GetCursorPos(&Cursor);
    Previous = pSetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_SYSTEM_AWARE);
    GetCursorPos(&AwareCursor);
    pSetThreadDpiAwarenessContext(Previous);
    ok(Cursor.x == Origin.x + 20 && Cursor.y == Origin.y + 30, "[P1] unaware cursor %ld,%ld, expected %ld,%ld\n",
       Cursor.x, Cursor.y, Origin.x + 20, Origin.y + 30);
    ok_near(AwareCursor.x, MulDiv(Cursor.x, Dpi, USER_DEFAULT_SCREEN_DPI), "[P1] aware cursor x");
    ok_near(AwareCursor.y, MulDiv(Cursor.y, Dpi, USER_DEFAULT_SCREEN_DPI), "[P1] aware cursor y");
    ok(MouseMovePoint.x >= 19 && MouseMovePoint.x <= 21 && MouseMovePoint.y >= 29 && MouseMovePoint.y <= 31,
       "[P1] WM_MOUSEMOVE at %ld,%ld, expected 20,30\n", MouseMovePoint.x, MouseMovePoint.y);
    ok_near((SHORT)LOWORD(MouseMovePos), Origin.x + 20, "[P1] GetMessagePos x");
    ok_near((SHORT)HIWORD(MouseMovePos), Origin.y + 30, "[P1] GetMessagePos y");
    ok_near(HitTestPoint.x, Origin.x + 20, "[P1] WM_NCHITTEST x");
    ok_near(HitTestPoint.y, Origin.y + 30, "[P1] WM_NCHITTEST y");

    Dc = GetDC(Unaware);
    ok(GetDCOrgEx(Dc, &DcOrigin), "GetDCOrgEx failed\n");
    ReleaseDC(Unaware, Dc);
    ok(DcOrigin.x == Origin.x && DcOrigin.y == Origin.y, "unaware client DC origin %ld,%ld, client %ld,%ld\n",
       DcOrigin.x, DcOrigin.y, Origin.x, Origin.y);
    Dc = GetWindowDC(Unaware);
    ok(GetDCOrgEx(Dc, &DcOrigin), "GetDCOrgEx failed\n");
    ReleaseDC(Unaware, Dc);
    ok(DcOrigin.x == Window.left && DcOrigin.y == Window.top, "unaware window DC origin %ld,%ld, window %ld,%ld\n",
       DcOrigin.x, DcOrigin.y, Window.left, Window.top);
    Previous = pSetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_SYSTEM_AWARE);
    Dc = GetDC(Unaware);
    ok(GetDCOrgEx(Dc, &DcOrigin), "GetDCOrgEx failed\n");
    ReleaseDC(Unaware, Dc);
    pSetThreadDpiAwarenessContext(Previous);
    ok(DcOrigin.x == Origin.x && DcOrigin.y == Origin.y, "aware view of the unaware client DC origin %ld,%ld, "
       "expected the logical %ld,%ld\n", DcOrigin.x, DcOrigin.y, Origin.x, Origin.y);

    DestroyWindow(System);
    DestroyWindow(Unaware);
}

static LRESULT CALLBACK
CbtHook(int Code, WPARAM wParam, LPARAM lParam)
{
    if (Code == HCBT_CREATEWND)
    {
        CREATESTRUCTW *Cs = ((CBT_CREATEWNDW *)lParam)->lpcs;

        if (Cs->lpszName && !lstrcmpW(Cs->lpszName, L"cbt"))
            CbtCreate = *Cs;
    }
    return CallNextHookEx(NULL, Code, wParam, lParam);
}

static void
TestHooks(void)
{
    HHOOK Hook = SetWindowsHookExW(WH_CBT, CbtHook, NULL, GetCurrentThreadId());
    RECT Rect;
    HWND Wnd;

    ZeroMemory(&CbtCreate, sizeof(CbtCreate));
    Wnd = CreateWindowExW(0, ClassName, L"cbt", WS_OVERLAPPEDWINDOW, 100, 80, 300, 200, NULL, NULL,
                          GetModuleHandleW(NULL), NULL);
    UnhookWindowsHookEx(Hook);
    ok(CbtCreate.x == 100 && CbtCreate.y == 80 && CbtCreate.cx == 300 && CbtCreate.cy == 200,
       "HCBT_CREATEWND of an unaware window %d,%d %dx%d, expected 100,80 300x200\n", CbtCreate.x, CbtCreate.y,
       CbtCreate.cx, CbtCreate.cy);
    GetWindowRect(Wnd, &Rect);
    ok(Rect.left == 100 && Rect.top == 80 && Rect.right == 400 && Rect.bottom == 280, "window %ld,%ld,%ld,%ld\n",
       Rect.left, Rect.top, Rect.right, Rect.bottom);
    DestroyWindow(Wnd);
}

static void
CheckCursorAndIcon(const char *Context, BOOL Unaware)
{
    LONG Cursor = CursorImageSize(LoadCursorW(NULL, (LPCWSTR)IDC_ARROW));
    LONG Icon = CursorImageSize(LoadIconW(NULL, (LPCWSTR)IDI_APPLICATION));

    ok(Cursor == GetSystemMetrics(SM_CXCURSOR), "%s arrow cursor %ld pixels, SM_CXCURSOR %d\n", Context, Cursor,
       GetSystemMetrics(SM_CXCURSOR));
    ok(Unaware ? Icon >= GetSystemMetrics(SM_CXICON) : Icon == GetSystemMetrics(SM_CXICON),
       "[P1] %s IDI_APPLICATION %ld pixels, SM_CXICON %d\n", Context, Icon, GetSystemMetrics(SM_CXICON));
}

static void
TestCursorAndIcons(UINT Dpi)
{
    DPI_AWARENESS_CONTEXT Previous;
    INT Cursor;

    ok_int(GetSystemMetrics(SM_CXCURSOR), 32);
    CheckCursorAndIcon("unaware", TRUE);
    Previous = pSetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_SYSTEM_AWARE);
    Cursor = GetSystemMetrics(SM_CXCURSOR);
    CheckCursorAndIcon("system aware", FALSE);
    pSetThreadDpiAwarenessContext(Previous);
    ok(Cursor == pGetSystemMetricsForDpi(SM_CXCURSOR, Dpi), "system aware SM_CXCURSOR %d, ForDpi %d\n", Cursor,
       pGetSystemMetricsForDpi(SM_CXCURSOR, Dpi));
}

static BOOL
CascadeMatches(const LONG *Pos, INT Count, LONG Step, const RECT *Work)
{
    INT Start, i;
    LONG Counter, Expected;

    for (Start = 0; Start <= 32; Start++)
    {
        Counter = Start;
        for (i = 0; i < Count; i++)
        {
            Counter++;
            Expected = Counter * Step;
            if (Expected + 2 * Step > (Work->right - Work->left) / 4 || Expected > (Work->bottom - Work->top) / 4)
            {
                Expected = Step;
                Counter = 0;
            }
            if (Pos[i] != Expected)
                break;
        }
        if (i == Count)
            return TRUE;
    }
    return FALSE;
}

static void
CheckDefaultWindows(UINT Dpi, UINT SystemDpi, const char *Context)
{
    RECT Rect, Work;
    HWND Wnd[8];
    LONG Left[8], ScreenCx = GetSystemMetrics(SM_CXSCREEN), ScreenCy = GetSystemMetrics(SM_CYSCREEN), PhysicalCx;
    DPI_AWARENESS_CONTEXT Previous;
    static const struct { UINT Dpi; LONG System; LONG Unaware; } Margins[] =
    {
        { 96, 21, 21 }, { 144, 29, 29 }, { 168, 34, 34 }, { 192, 37, 37 }, { 216, 42, 43 }, { 240, 46, 46 },
        { 288, 54, 54 },
    };
    LONG WorkCy, Margin = -1;
    INT Step = GetSystemMetrics(SM_CYSIZE) + pGetSystemMetricsForDpi(SM_CYFRAME, Dpi);
    INT i;

    Previous = pSetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_SYSTEM_AWARE);
    PhysicalCx = GetSystemMetrics(SM_CXSCREEN);
    pSetThreadDpiAwarenessContext(Previous);
    for (i = 0; i < ARRAYSIZE(Margins); i++)
    {
        if (Margins[i].Dpi == SystemDpi)
            Margin = Dpi == SystemDpi ? Margins[i].System : Margins[i].Unaware;
    }
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &Work, 0);
    WorkCy = Work.bottom - Work.top;
    Wnd[0] = CreateTestWindow(WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, NULL);
    GetWindowRect(Wnd[0], &Rect);
    ok(Rect.right - Rect.left == MulDiv(PhysicalCx - PhysicalCx / 4, Dpi, SystemDpi),
       "%s default width %ld for screen width %ld\n", Context, Rect.right - Rect.left, ScreenCx);
    if (Margin >= 0)
        ok(Rect.bottom - Rect.top == WorkCy - WorkCy / 4 - Margin, "%s default height %ld for work height %ld\n",
           Context, Rect.bottom - Rect.top, WorkCy);
    else
        ok(Rect.bottom - Rect.top < WorkCy - WorkCy / 4, "%s default height %ld for work height %ld\n", Context,
           Rect.bottom - Rect.top, WorkCy);
    DestroyWindow(Wnd[0]);

    for (i = 0; i < 8; i++)
    {
        Wnd[i] = CreateTestWindow(WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 100, 100, NULL);
        GetWindowRect(Wnd[i], &Rect);
        Left[i] = Rect.left;
        ok(Rect.left == Rect.top && Rect.right - Rect.left == max(100, GetSystemMetrics(SM_CXMINTRACK)),
           "%s cascaded window %d at %ld,%ld,%ld,%ld\n", Context, i, Rect.left, Rect.top, Rect.right, Rect.bottom);
    }
    ok(CascadeMatches(Left, 8, Step, &Work), "%s cascade %ld %ld %ld %ld %ld %ld %ld %ld, step %d\n", Context,
       Left[0], Left[1], Left[2], Left[3], Left[4], Left[5], Left[6], Left[7], Step);
    for (i = 0; i < 8; i++)
        DestroyWindow(Wnd[i]);

    Wnd[0] = CreateTestWindow(WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, ScreenCx - Step / 2, 100, NULL);
    GetWindowRect(Wnd[0], &Rect);
    ok(Rect.left == Step / 2 && Rect.right == ScreenCx, "%s wide window at %ld,%ld,%ld,%ld\n", Context, Rect.left,
       Rect.top, Rect.right, Rect.bottom);
    DestroyWindow(Wnd[0]);

    Wnd[0] = CreateTestWindow(WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 100, ScreenCy - Step / 2, NULL);
    GetWindowRect(Wnd[0], &Rect);
    ok(Rect.top == Step / 2 && Rect.bottom == ScreenCy, "%s tall window at %ld,%ld,%ld,%ld\n", Context, Rect.left,
       Rect.top, Rect.right, Rect.bottom);
    DestroyWindow(Wnd[0]);

    Wnd[0] = CreateTestWindow(WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, ScreenCx + 100, 100, NULL);
    GetWindowRect(Wnd[0], &Rect);
    ok(Rect.left == 0 && Rect.right - Rect.left == min(ScreenCx + 100, GetSystemMetrics(SM_CXMAXTRACK)),
       "%s oversized window at %ld,%ld,%ld,%ld\n", Context, Rect.left, Rect.top, Rect.right, Rect.bottom);
    DestroyWindow(Wnd[0]);

    Wnd[0] = CreateTestWindow(WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 100, 3 * ScreenCy, NULL);
    GetWindowRect(Wnd[0], &Rect);
    ok(Rect.top == 0 && Rect.bottom - Rect.top == GetSystemMetrics(SM_CYMAXTRACK),
       "%s window taller than the maximum tracking size at %ld,%ld,%ld,%ld, SM_CYMAXTRACK %d\n", Context, Rect.left,
       Rect.top, Rect.right, Rect.bottom, GetSystemMetrics(SM_CYMAXTRACK));
    DestroyWindow(Wnd[0]);
}

static void
TestDefaultWindow(UINT Dpi)
{
    DPI_AWARENESS_CONTEXT Previous;

    CheckDefaultWindows(USER_DEFAULT_SCREEN_DPI, Dpi, "unaware");
    Previous = pSetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_SYSTEM_AWARE);
    CheckDefaultWindows(Dpi, Dpi, "system aware");
    pSetThreadDpiAwarenessContext(Previous);
}

static void
CheckNcScaling(DPI_AWARENESS_CONTEXT Context, BOOL Expected, const char *Name)
{
    DPI_AWARENESS_CONTEXT Previous = pSetThreadDpiAwarenessContext(Context);
    HWND Wnd;

    NcScalingRequested = TRUE;
    NcScalingResult = !Expected;
    Wnd = CreateTestWindow(WS_OVERLAPPEDWINDOW, 10, 10, 100, 100, NULL);
    NcScalingRequested = FALSE;
    pSetThreadDpiAwarenessContext(Previous);
    ok(NcScalingResult == Expected, "EnableNonClientDpiScaling in WM_NCCREATE of a %s window returned %d\n", Name,
       NcScalingResult);
    if (!Expected)
        ok(NcScalingError == 0xdeadbeef, "EnableNonClientDpiScaling of a %s window set error %lu\n", Name,
           NcScalingError);
    DestroyWindow(Wnd);
}

static void
TestNonClient(UINT Dpi)
{
    DPI_AWARENESS_CONTEXT Previous;
    SIZE Size = {100, 100};
    HWND Wnd;

    Wnd = CreateTestWindow(WS_OVERLAPPEDWINDOW, 10, 10, 100, 100, NULL);
    ok(DefWindowProcW(Wnd, WM_GETDPISCALEDSIZE, Dpi, (LPARAM)&Size) == 0, "[P12] DefWindowProc returned nonzero\n");
    ok(Size.cx == 100 && Size.cy == 100, "[P12] DefWindowProc changed the size to %ldx%ld\n", Size.cx, Size.cy);
    DestroyWindow(Wnd);

    Previous = pSetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE);
    NcScalingRequested = TRUE;
    NcScalingResult = FALSE;
    Wnd = CreateTestWindow(WS_OVERLAPPEDWINDOW, 10, 10, 100, 100, NULL);
    NcScalingRequested = FALSE;
    pSetThreadDpiAwarenessContext(Previous);
    ok(NcScalingResult, "[P1] EnableNonClientDpiScaling failed in WM_NCCREATE\n");
    ok_int(WindowAwareness(Wnd), DPI_AWARENESS_PER_MONITOR_AWARE);
    ok(pGetDpiForWindow(Wnd) == Dpi, "[P5] per monitor window DPI %u, expected %u\n", pGetDpiForWindow(Wnd), Dpi);
    SetLastError(0xdeadbeef);
    ok(!pEnableNonClientDpiScaling(Wnd), "EnableNonClientDpiScaling succeeded after WM_NCCREATE\n");
    ok_err(0xdeadbeef);
    DestroyWindow(Wnd);

    CheckNcScaling(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2, TRUE, "per monitor v2");
    CheckNcScaling(DPI_AWARENESS_CONTEXT_SYSTEM_AWARE, FALSE, "system aware");
    CheckNcScaling(DPI_AWARENESS_CONTEXT_UNAWARE, FALSE, "unaware");
    SetLastError(0xdeadbeef);
    ok(!pEnableNonClientDpiScaling(NULL), "EnableNonClientDpiScaling succeeded for NULL\n");
    ok_err(ERROR_INVALID_WINDOW_HANDLE);
}

static BOOL
LoadDpiFunctions(void)
{
    HMODULE User32 = GetModuleHandleW(L"user32.dll"), Shcore = LoadLibraryW(L"shcore.dll");

#define LOAD(Name) p##Name = (void *)GetProcAddress(User32, #Name)
    LOAD(SetThreadDpiAwarenessContext);
    LOAD(GetThreadDpiAwarenessContext);
    LOAD(GetWindowDpiAwarenessContext);
    LOAD(GetAwarenessFromDpiAwarenessContext);
    LOAD(GetDpiFromDpiAwarenessContext);
    LOAD(AreDpiAwarenessContextsEqual);
    LOAD(IsValidDpiAwarenessContext);
    LOAD(GetDpiForWindow);
    LOAD(GetDpiForSystem);
    LOAD(GetSystemMetricsForDpi);
    LOAD(SystemParametersInfoForDpi);
    LOAD(AdjustWindowRectExForDpi);
    LOAD(LogicalToPhysicalPointForPerMonitorDPI);
    LOAD(PhysicalToLogicalPointForPerMonitorDPI);
    LOAD(SetThreadDpiHostingBehavior);
    LOAD(GetThreadDpiHostingBehavior);
    LOAD(GetWindowDpiHostingBehavior);
    LOAD(EnableNonClientDpiScaling);
    LOAD(SetProcessDpiAwarenessContext);
#undef LOAD
    pGetDpiForMonitor = Shcore ? (PGETDPIFORMONITOR)GetProcAddress(Shcore, "GetDpiForMonitor") : NULL;

    return pSetThreadDpiAwarenessContext && pGetThreadDpiAwarenessContext && pGetWindowDpiAwarenessContext &&
           pGetAwarenessFromDpiAwarenessContext && pGetDpiFromDpiAwarenessContext &&
           pAreDpiAwarenessContextsEqual && pIsValidDpiAwarenessContext && pGetDpiForWindow && pGetDpiForSystem &&
           pGetSystemMetricsForDpi && pSystemParametersInfoForDpi && pAdjustWindowRectExForDpi &&
           pLogicalToPhysicalPointForPerMonitorDPI && pPhysicalToLogicalPointForPerMonitorDPI &&
           pSetThreadDpiHostingBehavior && pGetThreadDpiHostingBehavior && pGetWindowDpiHostingBehavior &&
           pEnableNonClientDpiScaling && pSetProcessDpiAwarenessContext;
}

static BOOL
RegisterTestClass(void)
{
    WNDCLASSW Class = {0};

    Class.lpfnWndProc = TestWndProc;
    Class.hInstance = GetModuleHandleW(NULL);
    Class.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    Class.hbrBackground = GetStockObject(WHITE_BRUSH);
    Class.lpszClassName = ClassName;
    return RegisterClassW(&Class) != 0;
}

START_TEST(DpiAwareness)
{
    UINT Dpi;

    if (!LoadDpiFunctions())
    {
        skip("DPI awareness APIs are not exported\n");
        return;
    }
    Dpi = GetAwareSystemDpi();
    trace("System DPI %u\n", Dpi);
    if (IsProcessDPIAware())
    {
        skip("The test process is DPI aware at %u DPI (XP-style scaling)\n", Dpi);
        return;
    }
    RegisterTestClass();

    TestContextHandles(Dpi);
    TestSystemDpiQueries(Dpi);
    TestMetricsEquivalence(Dpi);
    TestWindowAwareness(Dpi);
    TestCoordinates(Dpi);
    TestCursorAndIcons(Dpi);
    TestDefaultWindow(Dpi);
    TestNonClient(Dpi);
    TestHooks();
}

START_TEST(DpiProcessDefault)
{
    HANDLE Thread;
    DWORD ThreadAware = 0;
    UINT Dpi;

    if (!LoadDpiFunctions())
    {
        skip("DPI awareness APIs are not exported\n");
        return;
    }
    if (IsProcessDPIAware())
    {
        skip("The test process is DPI aware\n");
        return;
    }
    Dpi = GetAwareSystemDpi();

    SetLastError(0xdeadbeef);
    ok(!pSetProcessDpiAwarenessContext((DPI_AWARENESS_CONTEXT)-6), "[P8] invalid process context accepted\n");
    ok_err(ERROR_INVALID_PARAMETER);
    ok(!IsProcessDPIAware(), "[P8] a failed call changed the process\n");

    ok(pSetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_SYSTEM_AWARE), "[P8] SetProcessDpiAwarenessContext failed\n");
    ok(IsProcessDPIAware(), "[P8] the process is not DPI aware\n");
    ok_int(ThreadAwareness(), DPI_AWARENESS_SYSTEM_AWARE);
    ok(pGetDpiForSystem() == Dpi, "[P6] GetDpiForSystem %u, expected %u\n", pGetDpiForSystem(), Dpi);

    Thread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)(PVOID)ThreadAwareness, NULL, 0, NULL);
    WaitForSingleObject(Thread, INFINITE);
    GetExitCodeThread(Thread, &ThreadAware);
    CloseHandle(Thread);
    ok_int(ThreadAware, DPI_AWARENESS_SYSTEM_AWARE);

    SetLastError(0xdeadbeef);
    ok(!pSetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2),
       "[P8] the process default changed twice\n");
    ok_err(ERROR_ACCESS_DENIED);
    ok_int(ThreadAwareness(), DPI_AWARENESS_SYSTEM_AWARE);
}
