/*
 * PROJECT:     LiberNT WDDM UI presentation diagnostics
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Exercise overlapping windows that repaint text on several UI threads at once
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#define OVERLAP_TEXT_THREADS 4
#define OVERLAP_TEXT_ROUNDS 96

typedef struct _OVERLAP_TEXT_ROUND
{
    LOGFONTW Font;
    HANDLE Start;
    volatile LONG Ready;
    volatile LONG Failures;
} OVERLAP_TEXT_ROUND, *POVERLAP_TEXT_ROUND;

static DWORD WINAPI
OverlapTextThread(PVOID Parameter)
{
    static const WCHAR Text[] = L"C:\\Users\\Public\\Documents - Address";
    POVERLAP_TEXT_ROUND Round = Parameter;
    SCRIPT_STRING_ANALYSIS Analysis = NULL;
    HDC Dc = CreateCompatibleDC(NULL);
    HFONT Font = CreateFontIndirectW(&Round->Font);
    HGDIOBJ OldFont = NULL;
    HRESULT Status = E_FAIL;

    if (Dc && Font)
        OldFont = SelectObject(Dc, Font);
    InterlockedIncrement(&Round->Ready);
    WaitForSingleObject(Round->Start, INFINITE);
    if (OldFont)
    {
        Status = ScriptStringAnalyse(Dc, Text, ARRAYSIZE(Text) - 1,
                                     (ARRAYSIZE(Text) - 1) * 3 / 2 + 16, -1,
                                     SSA_GLYPHS | SSA_FALLBACK | SSA_LINK, -1,
                                     NULL, NULL, NULL, NULL, NULL, &Analysis);
        if (SUCCEEDED(Status))
            ScriptStringFree(&Analysis);
        SelectObject(Dc, OldFont);
    }
    if (FAILED(Status))
        InterlockedIncrement(&Round->Failures);
    if (Font)
        DeleteObject(Font);
    if (Dc)
        DeleteDC(Dc);
    return 0;
}

static BOOL
OverlapHeapsValid(VOID)
{
    HANDLE Heaps[64];
    DWORD Count = GetProcessHeaps(ARRAYSIZE(Heaps), Heaps), Index;

    if (Count == 0 || Count > ARRAYSIZE(Heaps))
        return FALSE;
    for (Index = 0; Index < Count; ++Index)
    {
        if (!HeapValidate(Heaps[Index], 0, NULL))
            return FALSE;
    }
    return TRUE;
}

static BOOL
RunOverlapTextPhase(VOID)
{
    NONCLIENTMETRICSW Metrics = {sizeof(Metrics)};
    OVERLAP_TEXT_ROUND Round;
    HANDLE Threads[OVERLAP_TEXT_THREADS];
    ULONG RoundIndex, Index, Created, Failures = 0;
    BOOL Heaps;

    if (!SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(Metrics), &Metrics, 0))
        return FALSE;
    for (RoundIndex = 0; RoundIndex < OVERLAP_TEXT_ROUNDS; ++RoundIndex)
    {
        ZeroMemory(&Round, sizeof(Round));
        Round.Font = Metrics.lfMessageFont;
        Round.Font.lfHeight = -(LONG)(9 + RoundIndex);
        Round.Start = CreateEventW(NULL, TRUE, FALSE, NULL);
        if (!Round.Start)
            return FALSE;
        for (Created = 0; Created < OVERLAP_TEXT_THREADS; ++Created)
        {
            Threads[Created] = CreateThread(NULL, 0, OverlapTextThread, &Round, 0, NULL);
            if (!Threads[Created])
                break;
        }
        while (Round.Ready < (LONG)Created)
            Sleep(0);
        SetEvent(Round.Start);
        WaitForMultipleObjects(Created, Threads, TRUE, INFINITE);
        for (Index = 0; Index < Created; ++Index)
            CloseHandle(Threads[Index]);
        CloseHandle(Round.Start);
        Failures += Round.Failures + (OVERLAP_TEXT_THREADS - Created);
    }
    Heaps = OverlapHeapsValid();
    TestPrint("WDDM_OVERLAP_TEXT rounds=%u threads=%u failures=%lu heaps=%u pass=%u\n",
              OVERLAP_TEXT_ROUNDS, OVERLAP_TEXT_THREADS, Failures, Heaps,
              Failures == 0 && Heaps);
    return Failures == 0 && Heaps;
}

#define OVERLAP_CHANNEL_TOLERANCE 3

typedef struct _OVERLAP_WINDOW
{
    PCWSTR Title;
    RECT Rect;
    HANDLE Ready;
    HANDLE Thread;
    HWND Window;
} OVERLAP_WINDOW, *POVERLAP_WINDOW;

static const WCHAR OverlapClassName[] = L"WddmOverlapProbe";

static LRESULT CALLBACK
OverlapWindowProc(HWND Window, UINT Message, WPARAM WParam, LPARAM LParam)
{
    if (Message == WM_CREATE)
    {
        CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT",
                        L"C:\\Users\\Public\\Documents\\Overlapping windows repaint this text",
                        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 8, 8, 400, 28,
                        Window, (HMENU)1, GetModuleHandleW(NULL), NULL);
        return 0;
    }
    if (Message == WM_DESTROY)
    {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(Window, Message, WParam, LParam);
}

static DWORD WINAPI
OverlapWindowThread(PVOID Parameter)
{
    POVERLAP_WINDOW Data = Parameter;
    MSG Message;

    Data->Window = CreateWindowExW(0, OverlapClassName, Data->Title, WS_OVERLAPPEDWINDOW,
                                   Data->Rect.left, Data->Rect.top,
                                   Data->Rect.right - Data->Rect.left,
                                   Data->Rect.bottom - Data->Rect.top,
                                   NULL, NULL, GetModuleHandleW(NULL), NULL);
    SetEvent(Data->Ready);
    if (!Data->Window)
        return 1;
    while (GetMessageW(&Message, NULL, 0, 0) > 0)
    {
        TranslateMessage(&Message);
        DispatchMessageW(&Message);
    }
    return 0;
}

static VOID
OverlapSettle(DWORD Milliseconds)
{
    DWORD Start = GetTickCount();

    do
    {
        PumpMessages();
        Sleep(10);
    } while (GetTickCount() - Start < Milliseconds);
}

static VOID
OverlapMouse(LONG X, LONG Y, DWORD Flags)
{
    INPUT Input;

    ZeroMemory(&Input, sizeof(Input));
    Input.type = INPUT_MOUSE;
    Input.mi.dx = MulDiv(X, 65535, GetSystemMetrics(SM_CXSCREEN) - 1);
    Input.mi.dy = MulDiv(Y, 65535, GetSystemMetrics(SM_CYSCREEN) - 1);
    Input.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | Flags;
    SendInput(1, &Input, sizeof(Input));
}

static VOID
OverlapFlush(const OVERLAP_WINDOW *Windows, ULONG Count)
{
    ULONG Index;

    for (Index = 0; Index < Count; ++Index)
        RedrawWindow(Windows[Index].Window, NULL, NULL, RDW_UPDATENOW | RDW_ALLCHILDREN);
    DwmFlush();
    OverlapSettle(100);
}

static ULONG *
OverlapCaptureFrame(HWND Window, RECT *Band)
{
    WINDOWINFO Info = {sizeof(Info)};
    BITMAPINFO Bitmap;
    ULONG *Bits = NULL, *Copy = NULL;
    HBITMAP Section;
    HGDIOBJ Old;
    HDC Screen, Memory;
    LONG Width, Height;

    if (!GetWindowInfo(Window, &Info))
        return NULL;
    SetRect(Band, Info.rcWindow.left, Info.rcWindow.top, Info.rcWindow.right, Info.rcClient.top);
    Width = Band->right - Band->left;
    Height = Band->bottom - Band->top;
    if (Width <= 0 || Height <= 0)
        return NULL;
    ZeroMemory(&Bitmap, sizeof(Bitmap));
    Bitmap.bmiHeader.biSize = sizeof(Bitmap.bmiHeader);
    Bitmap.bmiHeader.biWidth = Width;
    Bitmap.bmiHeader.biHeight = -Height;
    Bitmap.bmiHeader.biPlanes = 1;
    Bitmap.bmiHeader.biBitCount = 32;
    Bitmap.bmiHeader.biCompression = BI_RGB;
    Screen = GetDC(NULL);
    Memory = CreateCompatibleDC(Screen);
    Section = CreateDIBSection(Memory, &Bitmap, DIB_RGB_COLORS, (PVOID *)&Bits, NULL, 0);
    if (Screen && Memory && Section && Bits)
    {
        Old = SelectObject(Memory, Section);
        if (BitBlt(Memory, 0, 0, Width, Height, Screen, Band->left, Band->top, SRCCOPY))
        {
            GdiFlush();
            Copy = HeapAlloc(GetProcessHeap(), 0, (SIZE_T)Width * Height * sizeof(ULONG));
            if (Copy)
                CopyMemory(Copy, Bits, (SIZE_T)Width * Height * sizeof(ULONG));
        }
        SelectObject(Memory, Old);
    }
    if (Section)
        DeleteObject(Section);
    if (Memory)
        DeleteDC(Memory);
    if (Screen)
        ReleaseDC(NULL, Screen);
    return Copy;
}

static BOOL
OverlapCheckFrame(PCSTR Stage, HWND Window, PCSTR Name)
{
    RECT Before, After;
    ULONG *Shown, *Repainted;
    ULONG Count = 0, Differing = 0, MaxDelta = 0, Index, Channel;
    ULONG FirstIndex = 0, FirstShown = 0, FirstRepainted = 0;
    BOOL Passed = FALSE;

    Shown = OverlapCaptureFrame(Window, &Before);
    RedrawWindow(Window, NULL, NULL, RDW_FRAME | RDW_INVALIDATE | RDW_UPDATENOW);
    DwmFlush();
    OverlapSettle(400);
    Repainted = OverlapCaptureFrame(Window, &After);
    if (Shown && Repainted && EqualRect(&Before, &After))
    {
        Count = (ULONG)((Before.right - Before.left) * (Before.bottom - Before.top));
        for (Index = 0; Index < Count; ++Index)
        {
            ULONG Delta = 0;

            for (Channel = 0; Channel < 24; Channel += 8)
            {
                ULONG ShownValue = (Shown[Index] >> Channel) & 0xFF;
                ULONG RepaintedValue = (Repainted[Index] >> Channel) & 0xFF;
                ULONG ChannelDelta = ShownValue > RepaintedValue ? ShownValue - RepaintedValue :
                                                                   RepaintedValue - ShownValue;
                if (ChannelDelta > Delta)
                    Delta = ChannelDelta;
            }
            if (Delta > MaxDelta)
                MaxDelta = Delta;
            if (Delta > OVERLAP_CHANNEL_TOLERANCE && Differing++ == 0)
            {
                FirstIndex = Index;
                FirstShown = Shown[Index] & 0xFFFFFF;
                FirstRepainted = Repainted[Index] & 0xFFFFFF;
            }
        }
        Passed = Differing == 0;
    }
    TestPrint("WDDM_OVERLAP_FRAME stage=%s window=%s pixels=%lu differing=%lu max_delta=%lu pass=%u\n",
              Stage, Name, Count, Differing, MaxDelta, Passed);
    if (Differing)
    {
        LONG Width = Before.right - Before.left;
        TestPrint("WDDM_OVERLAP_FRAME_FIRST stage=%s window=%s x=%ld y=%ld width=%ld shown=%06lx repainted=%06lx\n",
                  Stage, Name, (LONG)(FirstIndex % Width), (LONG)(FirstIndex / Width), Width,
                  FirstShown, FirstRepainted);
    }
    if (Shown)
        HeapFree(GetProcessHeap(), 0, Shown);
    if (Repainted)
        HeapFree(GetProcessHeap(), 0, Repainted);
    return Passed;
}

static HICON
OverlapCreateIcon(VOID)
{
    BITMAPINFO Bitmap;
    ICONINFO Info;
    ULONG *Bits = NULL, Index;
    HICON Icon = NULL;
    HDC Screen = GetDC(NULL);

    ZeroMemory(&Bitmap, sizeof(Bitmap));
    Bitmap.bmiHeader.biSize = sizeof(Bitmap.bmiHeader);
    Bitmap.bmiHeader.biWidth = 16;
    Bitmap.bmiHeader.biHeight = -16;
    Bitmap.bmiHeader.biPlanes = 1;
    Bitmap.bmiHeader.biBitCount = 32;
    Bitmap.bmiHeader.biCompression = BI_RGB;
    ZeroMemory(&Info, sizeof(Info));
    Info.fIcon = TRUE;
    Info.hbmColor = CreateDIBSection(Screen, &Bitmap, DIB_RGB_COLORS, (PVOID *)&Bits, NULL, 0);
    Info.hbmMask = CreateBitmap(16, 16, 1, 1, NULL);
    if (Info.hbmColor && Bits && Info.hbmMask)
    {
        for (Index = 0; Index < 16 * 16; ++Index)
            Bits[Index] = 0xFFFF00FF;
        Icon = CreateIconIndirect(&Info);
    }
    if (Info.hbmColor)
        DeleteObject(Info.hbmColor);
    if (Info.hbmMask)
        DeleteObject(Info.hbmMask);
    if (Screen)
        ReleaseDC(NULL, Screen);
    return Icon;
}

static BOOL
OverlapCheckIcon(PCSTR Stage, HWND Window, PCSTR Name)
{
    static const INT Buttons[] = {2, 3, 5};
    TITLEBARINFOEX Bar = {sizeof(Bar)};
    RECT Band;
    ULONG *Pixels;
    ULONG Inside = 0, Total = 0, Index;
    LONG X, Y, Width;
    BOOL Passed = FALSE;

    Pixels = OverlapCaptureFrame(Window, &Band);
    if (Pixels && SendMessageW(Window, WM_GETTITLEBARINFOEX, 0, (LPARAM)&Bar))
    {
        Width = Band.right - Band.left;
        for (Y = Band.top; Y < Band.bottom; ++Y)
        {
            for (X = Band.left; X < Band.right; ++X)
            {
                ULONG Pixel = Pixels[(Y - Band.top) * Width + (X - Band.left)];
                POINT Point = {X, Y};

                if (((Pixel >> 16) & 0xFF) < 200 || ((Pixel >> 8) & 0xFF) > 60 || (Pixel & 0xFF) < 200)
                    continue;
                ++Total;
                for (Index = 0; Index < ARRAYSIZE(Buttons); ++Index)
                {
                    if (PtInRect(&Bar.rgrect[Buttons[Index]], Point))
                    {
                        ++Inside;
                        break;
                    }
                }
            }
        }
        Passed = Inside == 0;
    }
    TestPrint("WDDM_OVERLAP_ICON stage=%s window=%s icon_pixels=%lu inside_buttons=%lu pass=%u\n",
              Stage, Name, Total, Inside, Passed);
    if (Pixels)
        HeapFree(GetProcessHeap(), 0, Pixels);
    return Passed;
}

static BOOL
RunOverlapWindowPhase(const RECT *WorkArea)
{
    WNDCLASSW Class = {0};
    OVERLAP_WINDOW Windows[2];
    HWND Console = GetConsoleWindow();
    BOOL ConsoleVisible = Console && IsWindowVisible(Console);
    LONG Left = WorkArea->left + 60, Top = WorkArea->top + 60, X, Y;
    ULONG Index, Failures = 0;
    HICON Icon = OverlapCreateIcon();
    RECT Upper;

    if (WorkArea->right - Left < 1000 || WorkArea->bottom - Top < 440)
        return FALSE;
    Class.lpfnWndProc = OverlapWindowProc;
    Class.hInstance = GetModuleHandleW(NULL);
    Class.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    Class.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
    Class.lpszClassName = OverlapClassName;
    if (!RegisterClassW(&Class))
        return FALSE;

    ZeroMemory(Windows, sizeof(Windows));
    Windows[0].Title = L"Overlap lower";
    SetRect(&Windows[0].Rect, Left, Top + 40, Left + 680, Top + 420);
    Windows[1].Title = L"Overlap upper";
    SetRect(&Windows[1].Rect, Left + 20, Top, Left + 620, Top + 380);
    for (Index = 0; Index < ARRAYSIZE(Windows); ++Index)
    {
        Windows[Index].Ready = CreateEventW(NULL, TRUE, FALSE, NULL);
        Windows[Index].Thread = Windows[Index].Ready ?
            CreateThread(NULL, 0, OverlapWindowThread, &Windows[Index], 0, NULL) : NULL;
        if (!Windows[Index].Thread ||
            WaitForSingleObject(Windows[Index].Ready, 10000) != WAIT_OBJECT_0 ||
            !Windows[Index].Window)
        {
            ++Failures;
            goto Cleanup;
        }
    }
    if (ConsoleVisible)
        ShowWindow(Console, SW_HIDE);
    for (Index = 0; Index < ARRAYSIZE(Windows); ++Index)
    {
        SendMessageW(Windows[Index].Window, WM_SETICON, ICON_SMALL, (LPARAM)Icon);
        SendMessageW(Windows[Index].Window, WM_SETICON, ICON_BIG, (LPARAM)Icon);
    }

    ShowWindow(Windows[0].Window, SW_SHOW);
    SetForegroundWindow(Windows[0].Window);
    OverlapSettle(300);
    ShowWindow(Windows[1].Window, SW_SHOW);
    SetForegroundWindow(Windows[1].Window);
    OverlapSettle(400);

    GetWindowRect(Windows[1].Window, &Upper);
    X = Upper.right - 2;
    Y = (Upper.top + Upper.bottom) / 2;
    OverlapMouse(X, Y, 0);
    OverlapSettle(150);
    OverlapMouse(X, Y, MOUSEEVENTF_LEFTDOWN);
    OverlapSettle(100);
    for (; X > Upper.left + 300; X -= 9)
    {
        OverlapMouse(X, Y, 0);
        OverlapSettle(25);
    }
    OverlapMouse(X, Y, MOUSEEVENTF_LEFTUP);
    OverlapMouse(WorkArea->left + 10, WorkArea->bottom - 10, 0);
    OverlapSettle(500);
    OverlapFlush(Windows, ARRAYSIZE(Windows));
    if (!OverlapCheckFrame("sized", Windows[1].Window, "upper"))
        ++Failures;

    SetWindowPos(Windows[1].Window, NULL, Left + 700, Top, 0, 0,
                 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    OverlapSettle(500);
    OverlapFlush(Windows, ARRAYSIZE(Windows));
    if (!OverlapCheckFrame("uncovered", Windows[0].Window, "lower"))
        ++Failures;

    SetWindowPos(Windows[1].Window, NULL, 0, 0, 140, 300,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    OverlapSettle(400);
    OverlapFlush(Windows, ARRAYSIZE(Windows));
    if (!OverlapCheckIcon("narrow", Windows[1].Window, "upper"))
        ++Failures;

Cleanup:
    for (Index = 0; Index < ARRAYSIZE(Windows); ++Index)
    {
        if (Windows[Index].Window)
            PostMessageW(Windows[Index].Window, WM_CLOSE, 0, 0);
        if (Windows[Index].Thread)
        {
            WaitForSingleObject(Windows[Index].Thread, 10000);
            CloseHandle(Windows[Index].Thread);
        }
        if (Windows[Index].Ready)
            CloseHandle(Windows[Index].Ready);
    }
    if (ConsoleVisible)
        ShowWindow(Console, SW_SHOWNOACTIVATE);
    UnregisterClassW(OverlapClassName, GetModuleHandleW(NULL));
    if (Icon)
        DestroyIcon(Icon);
    TestPrint("WDDM_OVERLAP_WINDOWS failures=%lu pass=%u\n", Failures, Failures == 0);
    return Failures == 0;
}

static INT
RunOverlapProbe(const RECT *WorkArea, PCSTR Phase)
{
    BOOL Text = TRUE, Windows = TRUE;

    if (!Phase || !strcmp(Phase, "text"))
        Text = RunOverlapTextPhase();
    if (!Phase || !strcmp(Phase, "windows"))
        Windows = RunOverlapWindowPhase(WorkArea);
    TestPrint("WDDM_OVERLAP_RESULT text=%s windows=%s\n",
              Text ? "pass" : "fail", Windows ? "pass" : "fail");
    return Text && Windows ? 0 : 1;
}
