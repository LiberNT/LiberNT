/*
 * PROJECT:     LiberNT Linux Subsystem
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Windows host for the windows of Linux Wayland programs
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WIN32_NO_STATUS
#include <windef.h>
#include <winbase.h>
#include <wingdi.h>
#include <winuser.h>
#include <winnls.h>
#include <winioctl.h>
#include <winsvc.h>
#define COBJMACROS
#include <objbase.h>
#include <shellapi.h>
#include <initguid.h>
#include <propsys.h>
#include <propkey.h>
#define NTOS_MODE_USER
#include <ndk/rtlfuncs.h>

#include "../include/lxioctl.h"
#include "../include/lxwin.h"
#include "../include/lxappid.h"
#include "../include/lxlaunch.h"
#include "lxwinhost.h"

#define WM_LX_MESSAGE   (WM_APP + 1)
#define WM_LX_GONE      (WM_APP + 2)

#define LX_HOST_CLASS   L"LiberNT.LinuxWindow"
#define LX_HOST_MESSAGE L"LiberNT.LinuxWindowHost"

#define LX_BTN_LEFT     0x110
#define LX_BTN_RIGHT    0x111
#define LX_BTN_MIDDLE   0x112
#define LX_BTN_SIDE     0x113
#define LX_BTN_EXTRA    0x114

typedef struct _LX_HOST_WINDOW
{
    struct _LX_HOST_WINDOW *Next;
    ULONG Id;
    ULONG Parent;
    ULONG Flags;
    HWND Hwnd;
    HDC MemoryDc;
    HBITMAP Bitmap;
    HBITMAP OldBitmap;
    PUCHAR Bits;
    LONG Width;
    LONG Height;
    ULONG Cursor;
    ULONG MinWidth;
    ULONG MinHeight;
    ULONG MaxWidth;
    ULONG MaxHeight;
    ULONG Buttons;
    BOOL Tracking;
    BOOL Applying;
    BOOL Shown;
    HICON BigIcon;
    HICON SmallIcon;
    CHAR AppId[LX_APP_ID_MAX];
    ULONG AppIdLength;
} LX_HOST_WINDOW, *PLX_HOST_WINDOW;

static HANDLE LxDevice = INVALID_HANDLE_VALUE;
static ULONG LxRootTagValue;
static PCWSTR LxRoot;
static int LxPendingFirst;
static int LxPendingCount;
static HWND LxMessageWindow;
static PLX_HOST_WINDOW LxWindows;
static HANDLE LxSendEvent;

static
VOID
LxSend(
    _In_ PVOID Message,
    _In_ ULONG Size)
{
    OVERLAPPED Overlapped;
    DWORD Bytes;

    ZeroMemory(&Overlapped, sizeof(Overlapped));
    Overlapped.hEvent = LxSendEvent;
    ResetEvent(LxSendEvent);
    if (!DeviceIoControl(LxDevice, IOCTL_LX_WIN_SEND, Message, Size, NULL, 0, &Bytes, &Overlapped) &&
        (GetLastError() == ERROR_IO_PENDING))
    {
        GetOverlappedResult(LxDevice, &Overlapped, &Bytes, TRUE);
    }
}

static
VOID
LxSendSimple(
    _In_ ULONG Type,
    _In_ ULONG Window)
{
    LX_WIN_HEADER Header;

    ZeroMemory(&Header, sizeof(Header));
    Header.Type = Type;
    Header.Size = sizeof(Header);
    Header.Window = Window;
    LxSend(&Header, sizeof(Header));
}

static
VOID
LxSendValue(
    _In_ ULONG Type,
    _In_ ULONG Window,
    _In_ ULONG Value,
    _In_ ULONG Value2)
{
    LX_WIN_VALUE_MSG Message;

    ZeroMemory(&Message, sizeof(Message));
    Message.Header.Type = Type;
    Message.Header.Size = sizeof(Message);
    Message.Header.Window = Window;
    Message.Value = Value;
    Message.Value2 = Value2;
    LxSend(&Message, sizeof(Message));
}

static
PLX_HOST_WINDOW
LxFindWindow(
    _In_ ULONG Id)
{
    PLX_HOST_WINDOW Window;

    for (Window = LxWindows; Window != NULL; Window = Window->Next)
    {
        if (Window->Id == Id)
            return Window;
    }
    return NULL;
}

static
VOID
LxSendConfigure(
    _In_ PLX_HOST_WINDOW Window)
{
    LX_WIN_CONFIGURE_MSG Message;
    RECT Client;
    WINDOWPLACEMENT Placement;

    if (Window->Flags & LX_WIN_FLAG_POPUP)
        return;
    GetClientRect(Window->Hwnd, &Client);
    Placement.length = sizeof(Placement);
    GetWindowPlacement(Window->Hwnd, &Placement);
    if ((Client.right <= 0) || (Client.bottom <= 0))
        return;

    ZeroMemory(&Message, sizeof(Message));
    Message.Header.Type = LX_WIN_CONFIGURE;
    Message.Header.Size = sizeof(Message);
    Message.Header.Window = Window->Id;
    Message.Width = (ULONG)Client.right;
    Message.Height = (ULONG)Client.bottom;
    if (Placement.showCmd == SW_SHOWMAXIMIZED)
        Message.States |= LX_WIN_CONFIGURE_MAXIMIZED;
    if (GetForegroundWindow() == Window->Hwnd)
        Message.States |= LX_WIN_CONFIGURE_ACTIVATED;
    LxSend(&Message, sizeof(Message));
}

static
BOOL
LxResizeSurface(
    _Inout_ PLX_HOST_WINDOW Window,
    _In_ LONG Width,
    _In_ LONG Height)
{
    BITMAPINFO Info;
    HBITMAP Bitmap;
    PVOID Bits;
    HDC Screen;

    if ((Width <= 0) || (Height <= 0))
        return FALSE;
    if ((Window->Bitmap != NULL) && (Window->Width == Width) && (Window->Height == Height))
        return TRUE;

    ZeroMemory(&Info, sizeof(Info));
    Info.bmiHeader.biSize = sizeof(Info.bmiHeader);
    Info.bmiHeader.biWidth = Width;
    Info.bmiHeader.biHeight = -Height;
    Info.bmiHeader.biPlanes = 1;
    Info.bmiHeader.biBitCount = 32;
    Info.bmiHeader.biCompression = BI_RGB;

    Screen = GetDC(NULL);
    if (Window->MemoryDc == NULL)
        Window->MemoryDc = CreateCompatibleDC(Screen);
    Bitmap = CreateDIBSection(Screen, &Info, DIB_RGB_COLORS, &Bits, NULL, 0);
    ReleaseDC(NULL, Screen);
    if (Bitmap == NULL)
        return FALSE;

    ZeroMemory(Bits, (SIZE_T)Width * Height * 4);
    if (Window->Bitmap != NULL)
    {
        LONG Rows = min(Height, Window->Height);
        LONG Columns = min(Width, Window->Width);
        LONG Row;

        for (Row = 0; Row < Rows; Row++)
        {
            CopyMemory((PUCHAR)Bits + (SIZE_T)Row * Width * 4, Window->Bits + (SIZE_T)Row * Window->Width * 4,
                       (SIZE_T)Columns * 4);
        }
        SelectObject(Window->MemoryDc, Window->OldBitmap);
        DeleteObject(Window->Bitmap);
    }
    Window->OldBitmap = SelectObject(Window->MemoryDc, Bitmap);
    Window->Bitmap = Bitmap;
    Window->Bits = Bits;
    Window->Width = Width;
    Window->Height = Height;
    return TRUE;
}

static
VOID
LxDestroyWindow(
    _In_ PLX_HOST_WINDOW Window)
{
    PLX_HOST_WINDOW *Link;

    for (Link = &LxWindows; *Link != NULL; Link = &(*Link)->Next)
    {
        if (*Link == Window)
        {
            *Link = Window->Next;
            break;
        }
    }
    if (Window->Hwnd != NULL)
    {
        SetWindowLongPtrW(Window->Hwnd, GWLP_USERDATA, 0);
        DestroyWindow(Window->Hwnd);
    }
    if (Window->MemoryDc != NULL)
    {
        if (Window->Bitmap != NULL)
        {
            SelectObject(Window->MemoryDc, Window->OldBitmap);
            DeleteObject(Window->Bitmap);
        }
        DeleteDC(Window->MemoryDc);
    }
    if (Window->BigIcon != NULL)
        DestroyIcon(Window->BigIcon);
    if (Window->SmallIcon != NULL)
        DestroyIcon(Window->SmallIcon);
    HeapFree(GetProcessHeap(), 0, Window);
}

static
VOID
LxApplyIcons(
    _In_ PLX_HOST_WINDOW Window,
    _In_opt_ HICON BigIcon,
    _In_opt_ HICON SmallIcon)
{
    SendMessageW(Window->Hwnd, WM_SETICON, ICON_BIG, (LPARAM)BigIcon);
    SendMessageW(Window->Hwnd, WM_SETICON, ICON_SMALL, (LPARAM)SmallIcon);
    if (Window->BigIcon != NULL)
        DestroyIcon(Window->BigIcon);
    if (Window->SmallIcon != NULL)
        DestroyIcon(Window->SmallIcon);
    Window->BigIcon = BigIcon;
    Window->SmallIcon = SmallIcon;
}

static
VOID
LxSetAppId(
    _In_ PLX_HOST_WINDOW Window,
    _In_reads_bytes_(Length) PCSTR AppId,
    _In_ ULONG Length)
{
    WCHAR Name[LX_APP_ID_MAX], Value[LX_APP_ID_MAX];
    HICON BigIcon, SmallIcon;
    IPropertyStore *Store;
    PROPVARIANT Variant;
    int Count;

    if ((Length == 0) || (Length >= sizeof(Window->AppId)))
        return;
    Count = MultiByteToWideChar(CP_UTF8, 0, AppId, (int)Length, Name, ARRAYSIZE(Name) - 1);
    if (Count <= 0)
        return;
    Name[Count] = UNICODE_NULL;
    if (_snwprintf(Value, ARRAYSIZE(Value), LX_APP_ID_FORMAT, LxRootTagValue, Name) < 0)
        return;
    Value[ARRAYSIZE(Value) - 1] = UNICODE_NULL;
    memcpy(Window->AppId, AppId, Length);
    Window->AppIdLength = Length;

    if (SUCCEEDED(SHGetPropertyStoreForWindow(Window->Hwnd, &IID_IPropertyStore, (void **)&Store)))
    {
        PropVariantInit(&Variant);
        Variant.vt = VT_LPWSTR;
        Variant.pwszVal = Value;
        if (SUCCEEDED(IPropertyStore_SetValue(Store, &PKEY_AppUserModel_ID, &Variant)))
            IPropertyStore_Commit(Store);
        IPropertyStore_Release(Store);
    }

    if (LxIconCacheLoad(Window->AppId, Window->AppIdLength, &BigIcon, &SmallIcon))
        LxApplyIcons(Window, BigIcon, SmallIcon);
}

static
VOID
LxSetIcon(
    _In_ PLX_HOST_WINDOW Window,
    _In_ PLX_WIN_ICON_MSG Message,
    _In_ ULONG Size)
{
    HICON BigIcon = NULL, SmallIcon = NULL;
    PUCHAR Icon;
    ULONG IconSize;

    if (Message->Count == 0)
    {
        if ((Window->AppIdLength == 0) || !LxIconCacheLoad(Window->AppId, Window->AppIdLength, &BigIcon, &SmallIcon))
            BigIcon = SmallIcon = NULL;
        LxApplyIcons(Window, BigIcon, SmallIcon);
        return;
    }
    Icon = LxBuildIcon(Message, Size, &IconSize);
    if (Icon == NULL)
        return;
    BigIcon = LxCreateIconFromIco(Icon, IconSize, GetSystemMetrics(SM_CXICON));
    SmallIcon = LxCreateIconFromIco(Icon, IconSize, GetSystemMetrics(SM_CXSMICON));
    if ((BigIcon != NULL) || (SmallIcon != NULL))
    {
        LxApplyIcons(Window, BigIcon, SmallIcon);
        if (Window->AppIdLength != 0)
            LxIconCacheStore(Window->AppId, Window->AppIdLength, Icon, IconSize);
    }
    HeapFree(GetProcessHeap(), 0, Icon);
}

static
VOID
LxDestroyAll(VOID)
{
    while (LxWindows != NULL)
        LxDestroyWindow(LxWindows);
}

static
VOID
LxClientToWindowSize(
    _In_ HWND Hwnd,
    _Inout_ PLONG Width,
    _Inout_ PLONG Height)
{
    PLX_HOST_WINDOW Window = (PLX_HOST_WINDOW)GetWindowLongPtrW(Hwnd, GWLP_USERDATA);
    RECT Rect = { 0, 0, *Width, *Height };

    if ((Window == NULL) || !(Window->Flags & LX_WIN_FLAG_DECORATED))
        return;
    AdjustWindowRectEx(&Rect, (DWORD)GetWindowLongPtrW(Hwnd, GWL_STYLE), FALSE,
                       (DWORD)GetWindowLongPtrW(Hwnd, GWL_EXSTYLE));
    *Width = Rect.right - Rect.left;
    *Height = Rect.bottom - Rect.top;
}

static
VOID
LxClampToWorkArea(
    _Inout_ PPOINT Origin,
    _In_ LONG Width,
    _In_ LONG Height)
{
    RECT Work;

    if (!SystemParametersInfoW(SPI_GETWORKAREA, 0, &Work, 0))
        return;
    Origin->x = max(Work.left, min(Origin->x, Work.right - Width));
    Origin->y = max(Work.top, min(Origin->y, Work.bottom - Height));
}

static
VOID
LxCreateWindow(
    _In_ PLX_WIN_CREATE_MSG Message)
{
    PLX_HOST_WINDOW Window, Parent = NULL;
    DWORD Style, ExStyle;
    LONG X = CW_USEDEFAULT, Y = CW_USEDEFAULT;
    LONG Width = (LONG)Message->Width, Height = (LONG)Message->Height;
    HWND Owner = NULL;

    if (LxFindWindow(Message->Header.Window) != NULL)
        return;
    Window = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*Window));
    if (Window == NULL)
        return;
    Window->Id = Message->Header.Window;
    Window->Parent = Message->Parent;
    Window->Flags = Message->Flags;

    if (Message->Flags & LX_WIN_FLAG_POPUP)
    {
        POINT Origin = { Message->X, Message->Y };

        Parent = LxFindWindow(Message->Parent);
        if (Parent != NULL)
        {
            ClientToScreen(Parent->Hwnd, &Origin);
            Owner = Parent->Hwnd;
        }
        LxClampToWorkArea(&Origin, Width, Height);
        X = Origin.x;
        Y = Origin.y;
        Style = WS_POPUP;
        ExStyle = WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST;
    }
    else if (Message->Flags & LX_WIN_FLAG_DECORATED)
    {
        Style = WS_OVERLAPPEDWINDOW;
        ExStyle = WS_EX_APPWINDOW;
    }
    else
    {
        Style = WS_POPUP | WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX;
        ExStyle = WS_EX_APPWINDOW;
    }

    if (Message->Flags & LX_WIN_FLAG_DECORATED)
    {
        RECT Rect = { 0, 0, Width, Height };

        AdjustWindowRectEx(&Rect, Style, FALSE, ExStyle);
        Width = Rect.right - Rect.left;
        Height = Rect.bottom - Rect.top;
    }
    if (!(Message->Flags & LX_WIN_FLAG_POPUP) && !(Message->Flags & LX_WIN_FLAG_DECORATED))
    {
        X = max(0, (GetSystemMetrics(SM_CXSCREEN) - Width) / 2);
        Y = max(0, (GetSystemMetrics(SM_CYSCREEN) - Height) / 3);
    }

    Window->Next = LxWindows;
    LxWindows = Window;
    Window->Applying = TRUE;
    Window->Hwnd = CreateWindowExW(ExStyle, LX_HOST_CLASS, L"", Style, X, Y, Width, Height, Owner, NULL,
                                   GetModuleHandleW(NULL), Window);
    Window->Applying = FALSE;
    if (Window->Hwnd == NULL)
    {
        LxDestroyWindow(Window);
        return;
    }
    LxResizeSurface(Window, (LONG)Message->Width, (LONG)Message->Height);
}

static
VOID
LxApplyGeometry(
    _In_ PLX_HOST_WINDOW Window,
    _In_ PLX_WIN_RECT_MSG Message)
{
    LONG Width = (LONG)Message->Width, Height = (LONG)Message->Height;
    UINT Flags = SWP_NOZORDER | SWP_NOACTIVATE;
    POINT Origin = { Message->X, Message->Y };

    if ((Width <= 0) || (Height <= 0))
        return;
    LxResizeSurface(Window, Width, Height);
    if (Window->Flags & LX_WIN_FLAG_POPUP)
    {
        PLX_HOST_WINDOW Parent = LxFindWindow(Window->Parent);

        if (Parent != NULL)
            ClientToScreen(Parent->Hwnd, &Origin);
        LxClampToWorkArea(&Origin, Width, Height);
    }
    else
    {
        RECT Client;

        GetClientRect(Window->Hwnd, &Client);
        if ((Client.right == Width) && (Client.bottom == Height))
            return;
        if (IsZoomed(Window->Hwnd))
            return;
        Flags |= SWP_NOMOVE;
    }
    LxClientToWindowSize(Window->Hwnd, &Width, &Height);
    Window->Applying = TRUE;
    SetWindowPos(Window->Hwnd, NULL, Origin.x, Origin.y, Width, Height, Flags);
    Window->Applying = FALSE;
}

static
VOID
LxApplyPixels(
    _In_ PLX_HOST_WINDOW Window,
    _In_ PLX_WIN_RECT_MSG Message)
{
    PUCHAR Source = (PUCHAR)(Message + 1);
    ULONG Available = Message->Header.Size - sizeof(*Message);
    LONG X = Message->X, Y = Message->Y;
    LONG Width = (LONG)Message->Width, Height = (LONG)Message->Height;
    LONG Row, Columns;
    RECT Dirty;

    if ((Width <= 0) || (Height <= 0) || ((ULONG64)Width * Height * 4 > Available))
        return;
    if ((Window->Bits == NULL) || (X < 0) || (Y < 0) || (X >= Window->Width) || (Y >= Window->Height))
        return;
    Columns = min(Width, Window->Width - X);
    for (Row = 0; (Row < Height) && (Y + Row < Window->Height); Row++)
    {
        CopyMemory(Window->Bits + ((SIZE_T)(Y + Row) * Window->Width + X) * 4,
                   Source + (SIZE_T)Row * Width * 4, (SIZE_T)Columns * 4);
    }
    GdiFlush();
    SetRect(&Dirty, X, Y, X + Columns, Y + Row);
    InvalidateRect(Window->Hwnd, &Dirty, FALSE);
    if (!Window->Shown)
    {
        CHAR Text[96];

        sprintf(Text, "LXWINHOST_SHOWN %lu %ldx%ld\n", Window->Id, Window->Width, Window->Height);
        OutputDebugStringA(Text);
        Window->Shown = TRUE;
        ShowWindow(Window->Hwnd, (Window->Flags & LX_WIN_FLAG_POPUP) ? SW_SHOWNOACTIVATE : SW_SHOW);
        if (!(Window->Flags & LX_WIN_FLAG_POPUP))
            SetForegroundWindow(Window->Hwnd);
    }
}

static
VOID
LxSetClipboard(
    _In_ PLX_WIN_HEADER Header,
    _In_ ULONG Size)
{
    ULONG Length = Size - sizeof(*Header);
    int Characters = MultiByteToWideChar(CP_UTF8, 0, (PCSTR)(Header + 1), (int)Length, NULL, 0);
    HGLOBAL Memory = GlobalAlloc(GMEM_MOVEABLE, ((SIZE_T)Characters + 1) * sizeof(WCHAR));
    PWSTR Text;

    if (Memory == NULL)
        return;
    Text = GlobalLock(Memory);
    MultiByteToWideChar(CP_UTF8, 0, (PCSTR)(Header + 1), (int)Length, Text, Characters);
    Text[Characters] = UNICODE_NULL;
    GlobalUnlock(Memory);
    if (!OpenClipboard(LxMessageWindow))
    {
        GlobalFree(Memory);
        return;
    }
    EmptyClipboard();
    if (SetClipboardData(CF_UNICODETEXT, Memory) == NULL)
        GlobalFree(Memory);
    CloseClipboard();
    {
        CHAR Line[64];

        sprintf(Line, "LXWINHOST_CLIPBOARD from Linux, %d characters\n", Characters);
        OutputDebugStringA(Line);
    }
}

static
VOID
LxSendClipboard(VOID)
{
    PLX_WIN_HEADER Message;
    HANDLE Data;
    PCWSTR Text;
    int Length;

    if ((GetClipboardOwner() == LxMessageWindow) || !IsClipboardFormatAvailable(CF_UNICODETEXT))
        return;
    if (!OpenClipboard(LxMessageWindow))
        return;
    Data = GetClipboardData(CF_UNICODETEXT);
    Text = (Data != NULL) ? GlobalLock(Data) : NULL;
    if (Text != NULL)
    {
        Length = WideCharToMultiByte(CP_UTF8, 0, Text, -1, NULL, 0, NULL, NULL);
        if ((Length > 1) && ((ULONG)Length + sizeof(*Message) <= LX_WIN_MAX_EVENT))
        {
            Message = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*Message) + Length);
            if (Message != NULL)
            {
                WideCharToMultiByte(CP_UTF8, 0, Text, -1, (PSTR)(Message + 1), Length, NULL, NULL);
                Message->Type = LX_WIN_CLIPBOARD_SET;
                Message->Size = sizeof(*Message) + Length - 1;
                LxSend(Message, Message->Size);
                OutputDebugStringA("LXWINHOST_CLIPBOARD to Linux\n");
                HeapFree(GetProcessHeap(), 0, Message);
            }
        }
        GlobalUnlock(Data);
    }
    CloseClipboard();
}

static
VOID
LxSendHello(VOID)
{
    LX_WIN_HELLO_MSG Hello;
    HDC Screen = GetDC(NULL);

    ZeroMemory(&Hello, sizeof(Hello));
    Hello.Header.Type = LX_WIN_HELLO;
    Hello.Header.Size = sizeof(Hello);
    Hello.ScreenWidth = (ULONG)GetSystemMetrics(SM_CXSCREEN);
    Hello.ScreenHeight = (ULONG)GetSystemMetrics(SM_CYSCREEN);
    Hello.Dpi = (ULONG)GetDeviceCaps(Screen, LOGPIXELSX);
    Hello.RefreshMilliHz = 60000;
    ReleaseDC(NULL, Screen);
    LxSend(&Hello, sizeof(Hello));
}

static
VOID
LxStartCompositor(
    _In_ PCWSTR Root)
{
    LX_LAUNCH Launch;

    if (!LxRootHasFile(Root, L"\\usr\\local\\lib\\libernt\\lxwayland\\lxwayland"))
        return;
    ZeroMemory(&Launch, sizeof(Launch));
    Launch.Name = L"lxwinhost";
    Launch.Root = Root;
    Launch.Cwd = "/";
    Launch.FileName = "/usr/local/lib/libernt/lxwayland/lxwayland";
    Launch.Detached = TRUE;
    if (LxStringsAdd(&Launch.Args, "lxwayland") &&
        LxEnvSet(&Launch.Env, "PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin"))
    {
        LxLaunch(&Launch);
    }
    free(Launch.Args.Buffer);
    free(Launch.Env.Buffer);
}

static
VOID
LxLaunchProgram(
    _In_ PCWSTR Root,
    _In_ int First,
    _In_ int Count)
{
    LX_ACCOUNT Account;
    LX_LAUNCH Launch;
    CHAR UserName[64];
    PCHAR Program = NULL;
    int Index;

    if (Count <= 0)
        return;
    if ((!LxRootDefaultUser(Root, UserName, sizeof(UserName)) || !LxRootAccount(Root, UserName, 0, &Account)) &&
        !LxRootAccount(Root, NULL, 0, &Account))
    {
        return;
    }
    ZeroMemory(&Launch, sizeof(Launch));
    Launch.Name = L"lxwinhost";
    Launch.Root = Root;
    Launch.Detached = TRUE;
    Launch.SearchPath = TRUE;
    if (LxSetSession(&Launch, Root, &Account))
    {
        for (Index = First; Index < First + Count; Index++)
        {
            PCHAR Argument = LxToUtf8(__wargv[Index]);

            if ((Argument == NULL) || !LxStringsAdd(&Launch.Args, Argument))
            {
                free(Argument);
                break;
            }
            if (Index == First)
                Program = Argument;
            else
                free(Argument);
        }
        if ((Program != NULL) && (Index == First + Count))
        {
            Launch.FileName = Program;
            LxLaunch(&Launch);
        }
    }
    free(Program);
    free(Launch.Args.Buffer);
    free(Launch.Env.Buffer);
}

static
VOID
LxHandleMessage(
    _In_ PLX_WIN_HEADER Header,
    _In_ ULONG Size)
{
    PLX_HOST_WINDOW Window;

    if ((Size < sizeof(*Header)) || (Header->Size > Size))
        return;

    switch (Header->Type)
    {
        case LX_WIN_COMPOSITOR_ATTACHED:
            OutputDebugStringA("LXWINHOST_COMPOSITOR attached\n");
            LxDestroyAll();
            LxSendHello();
            LxSendClipboard();
            return;

        case LX_WIN_CLIPBOARD:
            LxSetClipboard(Header, Size);
            return;

        case LX_WIN_COMPOSITOR_DETACHED:
            LxDestroyAll();
            return;

        case LX_WIN_READY:
            if (LxPendingCount > 0)
            {
                LxLaunchProgram(LxRoot, LxPendingFirst, LxPendingCount);
                LxPendingCount = 0;
            }
            return;

        case LX_WIN_CREATE:
            if (Size >= sizeof(LX_WIN_CREATE_MSG))
                LxCreateWindow((PLX_WIN_CREATE_MSG)Header);
            return;
    }

    Window = LxFindWindow(Header->Window);
    if (Window == NULL)
        return;

    switch (Header->Type)
    {
        case LX_WIN_DESTROY:
            LxDestroyWindow(Window);
            break;

        case LX_WIN_TITLE:
        {
            ULONG Length = Size - sizeof(*Header);
            PWSTR Title = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (Length + 1) * sizeof(WCHAR));

            if (Title != NULL)
            {
                MultiByteToWideChar(CP_UTF8, 0, (PCSTR)(Header + 1), (int)Length, Title, (int)Length);
                SetWindowTextW(Window->Hwnd, Title);
                HeapFree(GetProcessHeap(), 0, Title);
            }
            break;
        }

        case LX_WIN_APP_ID:
            LxSetAppId(Window, (PCSTR)(Header + 1), Size - sizeof(*Header));
            break;

        case LX_WIN_ICON:
            if (Header->Size >= sizeof(LX_WIN_ICON_MSG))
                LxSetIcon(Window, (PLX_WIN_ICON_MSG)Header, Header->Size);
            break;

        case LX_WIN_SHOW:
            if (Size >= sizeof(LX_WIN_VALUE_MSG))
            {
                Window->Shown = (((PLX_WIN_VALUE_MSG)Header)->Value != 0);
                ShowWindow(Window->Hwnd, Window->Shown ? SW_SHOWNOACTIVATE : SW_HIDE);
            }
            break;

        case LX_WIN_GEOMETRY:
            if (Size >= sizeof(LX_WIN_RECT_MSG))
                LxApplyGeometry(Window, (PLX_WIN_RECT_MSG)Header);
            break;

        case LX_WIN_PIXELS:
            if (Size >= sizeof(LX_WIN_RECT_MSG))
                LxApplyPixels(Window, (PLX_WIN_RECT_MSG)Header);
            break;

        case LX_WIN_MOVE:
            ReleaseCapture();
            Window->Buttons = 0;
            SendMessageW(Window->Hwnd, WM_SYSCOMMAND, SC_MOVE | HTCAPTION, 0);
            LxSendConfigure(Window);
            break;

        case LX_WIN_RESIZE:
            if (Size >= sizeof(LX_WIN_VALUE_MSG))
            {
                static const UCHAR Edges[16] = { 0, 3, 6, 0, 1, 4, 7, 0, 2, 5, 8, 0, 0, 0, 0, 0 };
                ULONG Edge = Edges[((PLX_WIN_VALUE_MSG)Header)->Value & 15];

                if (Edge != 0)
                {
                    ReleaseCapture();
                    Window->Buttons = 0;
                    SendMessageW(Window->Hwnd, WM_SYSCOMMAND, SC_SIZE + Edge, 0);
                    LxSendConfigure(Window);
                }
            }
            break;

        case LX_WIN_STATE:
            if (Size >= sizeof(LX_WIN_VALUE_MSG))
            {
                switch (((PLX_WIN_VALUE_MSG)Header)->Value)
                {
                    case LX_WIN_STATE_MAXIMIZED: ShowWindow(Window->Hwnd, SW_MAXIMIZE); break;
                    case LX_WIN_STATE_MINIMIZED: ShowWindow(Window->Hwnd, SW_MINIMIZE); break;
                    default: ShowWindow(Window->Hwnd, SW_RESTORE); break;
                }
                LxSendConfigure(Window);
            }
            break;

        case LX_WIN_CURSOR:
            if (Size >= sizeof(LX_WIN_VALUE_MSG))
            {
                POINT Point;

                Window->Cursor = ((PLX_WIN_VALUE_MSG)Header)->Value;
                GetCursorPos(&Point);
                if (WindowFromPoint(Point) == Window->Hwnd)
                    SendMessageW(Window->Hwnd, WM_SETCURSOR, (WPARAM)Window->Hwnd, MAKELPARAM(HTCLIENT, WM_MOUSEMOVE));
            }
            break;

        case LX_WIN_SIZES:
            if (Size >= sizeof(LX_WIN_SIZES_MSG))
            {
                PLX_WIN_SIZES_MSG Sizes = (PLX_WIN_SIZES_MSG)Header;

                Window->MinWidth = Sizes->MinWidth;
                Window->MinHeight = Sizes->MinHeight;
                Window->MaxWidth = Sizes->MaxWidth;
                Window->MaxHeight = Sizes->MaxHeight;
            }
            break;

        case LX_WIN_ACTIVATE:
            SetForegroundWindow(Window->Hwnd);
            break;
    }
}

static
ULONG
LxScanToKey(
    _In_ WPARAM VirtualKey,
    _In_ LPARAM Info)
{
    static const struct { UCHAR Scan; UCHAR Key; } Extended[] =
    {
        { 0x1C, 96 }, { 0x1D, 97 }, { 0x35, 98 }, { 0x37, 99 }, { 0x38, 100 }, { 0x47, 102 },
        { 0x48, 103 }, { 0x49, 104 }, { 0x4B, 105 }, { 0x4D, 106 }, { 0x4F, 107 }, { 0x50, 108 },
        { 0x51, 109 }, { 0x52, 110 }, { 0x53, 111 }, { 0x5B, 125 }, { 0x5C, 126 }, { 0x5D, 127 }
    };
    ULONG Scan = (ULONG)((Info >> 16) & 0xFF);
    ULONG Index;

    if (VirtualKey == VK_PAUSE)
        return 119;
    if (VirtualKey == VK_NUMLOCK)
        return 69;
    if (!(Info & (1 << 24)))
        return (Scan <= 0x58) ? Scan : 0;
    for (Index = 0; Index < ARRAYSIZE(Extended); Index++)
    {
        if (Extended[Index].Scan == Scan)
            return Extended[Index].Key;
    }
    return 0;
}

static
LPCWSTR
LxCursorName(
    _In_ ULONG Shape)
{
    switch (Shape)
    {
        case LX_WIN_CURSOR_TEXT: return (LPCWSTR)IDC_IBEAM;
        case LX_WIN_CURSOR_POINTER: return (LPCWSTR)IDC_HAND;
        case LX_WIN_CURSOR_WAIT: return (LPCWSTR)IDC_WAIT;
        case LX_WIN_CURSOR_CROSSHAIR: return (LPCWSTR)IDC_CROSS;
        case LX_WIN_CURSOR_SIZE_NS: return (LPCWSTR)IDC_SIZENS;
        case LX_WIN_CURSOR_SIZE_WE: return (LPCWSTR)IDC_SIZEWE;
        case LX_WIN_CURSOR_SIZE_NWSE: return (LPCWSTR)IDC_SIZENWSE;
        case LX_WIN_CURSOR_SIZE_NESW: return (LPCWSTR)IDC_SIZENESW;
        case LX_WIN_CURSOR_MOVE: return (LPCWSTR)IDC_SIZEALL;
        case LX_WIN_CURSOR_NOT_ALLOWED: return (LPCWSTR)IDC_NO;
        default: return (LPCWSTR)IDC_ARROW;
    }
}

static
VOID
LxSendButton(
    _In_ PLX_HOST_WINDOW Window,
    _In_ ULONG Button,
    _In_ BOOL Pressed)
{
    ULONG Bit = 1U << (Button - LX_BTN_LEFT);

    if (Pressed)
    {
        if (Window->Buttons == 0)
            SetCapture(Window->Hwnd);
        Window->Buttons |= Bit;
    }
    else
    {
        Window->Buttons &= ~Bit;
        if (Window->Buttons == 0)
            ReleaseCapture();
    }
    LxSendValue(LX_WIN_BUTTON, Window->Id, Button, Pressed);
}

static
LRESULT
CALLBACK
LxWindowProc(
    _In_ HWND Hwnd,
    _In_ UINT Message,
    _In_ WPARAM WParam,
    _In_ LPARAM LParam)
{
    PLX_HOST_WINDOW Window = (PLX_HOST_WINDOW)GetWindowLongPtrW(Hwnd, GWLP_USERDATA);

    if (Message == WM_NCCREATE)
    {
        Window = ((LPCREATESTRUCTW)LParam)->lpCreateParams;
        Window->Hwnd = Hwnd;
        SetWindowLongPtrW(Hwnd, GWLP_USERDATA, (LONG_PTR)Window);
    }
    if (Window == NULL)
        return DefWindowProcW(Hwnd, Message, WParam, LParam);

    switch (Message)
    {
        case WM_PAINT:
        {
            PAINTSTRUCT Paint;
            HDC Dc = BeginPaint(Hwnd, &Paint);

            if (Window->MemoryDc != NULL)
            {
                BitBlt(Dc, Paint.rcPaint.left, Paint.rcPaint.top,
                       Paint.rcPaint.right - Paint.rcPaint.left, Paint.rcPaint.bottom - Paint.rcPaint.top,
                       Window->MemoryDc, Paint.rcPaint.left, Paint.rcPaint.top, SRCCOPY);
            }
            EndPaint(Hwnd, &Paint);
            return 0;
        }

        case WM_ERASEBKGND:
            return 1;

        case WM_SIZE:
            if (!Window->Applying && (WParam != SIZE_MINIMIZED))
                LxSendConfigure(Window);
            break;

        case WM_NCCALCSIZE:
            if (!(Window->Flags & LX_WIN_FLAG_DECORATED))
                return 0;
            break;

        case WM_GETMINMAXINFO:
        {
            PMINMAXINFO Info = (PMINMAXINFO)LParam;
            LONG Width, Height;
            RECT Work;

            if (!(Window->Flags & (LX_WIN_FLAG_DECORATED | LX_WIN_FLAG_POPUP)) &&
                SystemParametersInfoW(SPI_GETWORKAREA, 0, &Work, 0))
            {
                Info->ptMaxPosition.x = Work.left;
                Info->ptMaxPosition.y = Work.top;
                Info->ptMaxSize.x = Work.right - Work.left;
                Info->ptMaxSize.y = Work.bottom - Work.top;
            }

            if ((Window->MinWidth != 0) && (Window->MinHeight != 0))
            {
                Width = (LONG)Window->MinWidth;
                Height = (LONG)Window->MinHeight;
                LxClientToWindowSize(Hwnd, &Width, &Height);
                Info->ptMinTrackSize.x = Width;
                Info->ptMinTrackSize.y = Height;
            }
            if ((Window->MaxWidth != 0) && (Window->MaxHeight != 0))
            {
                Width = (LONG)Window->MaxWidth;
                Height = (LONG)Window->MaxHeight;
                LxClientToWindowSize(Hwnd, &Width, &Height);
                Info->ptMaxTrackSize.x = Width;
                Info->ptMaxTrackSize.y = Height;
            }
            return 0;
        }

        case WM_CLOSE:
            LxSendSimple(LX_WIN_CLOSE, Window->Id);
            return 0;

        case WM_ACTIVATE:
            if (!(Window->Flags & LX_WIN_FLAG_POPUP))
            {
                LxSendValue(LX_WIN_FOCUS, Window->Id, LOWORD(WParam) != WA_INACTIVE, 0);
                LxSendConfigure(Window);
            }
            break;

        case WM_MOUSEACTIVATE:
            if (Window->Flags & LX_WIN_FLAG_POPUP)
                return MA_NOACTIVATE;
            break;

        case WM_NCHITTEST:
            if (!(Window->Flags & LX_WIN_FLAG_DECORATED))
                return HTCLIENT;
            break;

        case WM_SETCURSOR:
            if (LOWORD(LParam) == HTCLIENT)
            {
                SetCursor((Window->Cursor == LX_WIN_CURSOR_HIDDEN) ? NULL : LoadCursorW(NULL, LxCursorName(Window->Cursor)));
                return TRUE;
            }
            break;

        case WM_MOUSEMOVE:
        {
            LX_WIN_POINTER_MSG Pointer;

            if (!Window->Tracking)
            {
                TRACKMOUSEEVENT Track = { sizeof(Track), TME_LEAVE, Hwnd, 0 };

                Window->Tracking = TrackMouseEvent(&Track);
            }
            ZeroMemory(&Pointer, sizeof(Pointer));
            Pointer.Header.Type = LX_WIN_POINTER;
            Pointer.Header.Size = sizeof(Pointer);
            Pointer.Header.Window = Window->Id;
            Pointer.X = (SHORT)LOWORD(LParam);
            Pointer.Y = (SHORT)HIWORD(LParam);
            LxSend(&Pointer, sizeof(Pointer));
            return 0;
        }

        case WM_MOUSELEAVE:
            Window->Tracking = FALSE;
            LxSendSimple(LX_WIN_LEAVE, Window->Id);
            return 0;

        case WM_LBUTTONDOWN: LxSendButton(Window, LX_BTN_LEFT, TRUE); return 0;
        case WM_LBUTTONUP: LxSendButton(Window, LX_BTN_LEFT, FALSE); return 0;
        case WM_RBUTTONDOWN: LxSendButton(Window, LX_BTN_RIGHT, TRUE); return 0;
        case WM_RBUTTONUP: LxSendButton(Window, LX_BTN_RIGHT, FALSE); return 0;
        case WM_MBUTTONDOWN: LxSendButton(Window, LX_BTN_MIDDLE, TRUE); return 0;
        case WM_MBUTTONUP: LxSendButton(Window, LX_BTN_MIDDLE, FALSE); return 0;
        case WM_XBUTTONDOWN:
        case WM_XBUTTONUP:
            LxSendButton(Window, (HIWORD(WParam) == XBUTTON1) ? LX_BTN_SIDE : LX_BTN_EXTRA, Message == WM_XBUTTONDOWN);
            return TRUE;

        case WM_CAPTURECHANGED:
            if (((HWND)LParam != Hwnd) && (Window->Buttons != 0))
            {
                ULONG Button;

                for (Button = LX_BTN_LEFT; Button <= LX_BTN_EXTRA; Button++)
                {
                    if (Window->Buttons & (1U << (Button - LX_BTN_LEFT)))
                        LxSendValue(LX_WIN_BUTTON, Window->Id, Button, FALSE);
                }
                Window->Buttons = 0;
            }
            break;

        case WM_MOUSEWHEEL:
        case WM_MOUSEHWHEEL:
            LxSendValue(LX_WIN_AXIS, Window->Id, Message == WM_MOUSEHWHEEL,
                        (ULONG)((Message == WM_MOUSEHWHEEL) ? (LONG)GET_WHEEL_DELTA_WPARAM(WParam) :
                                                              -(LONG)GET_WHEEL_DELTA_WPARAM(WParam)));
            return 0;

        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
        case WM_KEYUP:
        case WM_SYSKEYUP:
        {
            BOOL Pressed = (Message == WM_KEYDOWN) || (Message == WM_SYSKEYDOWN);
            ULONG Key = LxScanToKey(WParam, LParam);

            if (Pressed && (LParam & (1 << 30)))
                return 0;
            if ((Message == WM_SYSKEYDOWN) && (WParam == VK_F4))
                break;
            if (Key != 0)
                LxSendValue(LX_WIN_KEY, Window->Id, Key, Pressed);
            return 0;
        }

        case WM_SYSCHAR:
        case WM_CHAR:
            return 0;

        case WM_NCDESTROY:
            Window->Hwnd = NULL;
            SetWindowLongPtrW(Hwnd, GWLP_USERDATA, 0);
            break;
    }
    return DefWindowProcW(Hwnd, Message, WParam, LParam);
}

static
LRESULT
CALLBACK
LxMessageProc(
    _In_ HWND Hwnd,
    _In_ UINT Message,
    _In_ WPARAM WParam,
    _In_ LPARAM LParam)
{
    if (Message == WM_LX_MESSAGE)
    {
        LxHandleMessage((PLX_WIN_HEADER)LParam, (ULONG)WParam);
        return 0;
    }
    if (Message == WM_CLIPBOARDUPDATE)
    {
        LxSendClipboard();
        return 0;
    }
    if (Message == WM_LX_GONE)
    {
        LxDestroyAll();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(Hwnd, Message, WParam, LParam);
}

static
DWORD
WINAPI
LxReaderThread(
    _In_ PVOID Parameter)
{
    ULONG Capacity = 1024 * 1024;
    PUCHAR Buffer = HeapAlloc(GetProcessHeap(), 0, Capacity);
    OVERLAPPED Overlapped;
    DWORD Bytes;
    BOOL Success;

    UNREFERENCED_PARAMETER(Parameter);

    ZeroMemory(&Overlapped, sizeof(Overlapped));
    Overlapped.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    while ((Buffer != NULL) && (Overlapped.hEvent != NULL))
    {
        ResetEvent(Overlapped.hEvent);
        Bytes = 0;
        Success = DeviceIoControl(LxDevice, IOCTL_LX_WIN_RECEIVE, NULL, 0, Buffer, Capacity, &Bytes, &Overlapped);
        if (!Success && (GetLastError() == ERROR_IO_PENDING))
            Success = GetOverlappedResult(LxDevice, &Overlapped, &Bytes, TRUE);
        if (!Success)
        {
            if ((GetLastError() == ERROR_MORE_DATA) && (Bytes > Capacity) && (Bytes <= LX_WIN_MAX_MESSAGE))
            {
                PUCHAR Larger = HeapReAlloc(GetProcessHeap(), 0, Buffer, Bytes);

                if (Larger == NULL)
                    break;
                Buffer = Larger;
                Capacity = Bytes;
                continue;
            }
            break;
        }
        SendMessageW(LxMessageWindow, WM_LX_MESSAGE, Bytes, (LPARAM)Buffer);
    }
    PostMessageW(LxMessageWindow, WM_LX_GONE, 0, 0);
    return 0;
}

static
VOID
LxStartDriver(VOID)
{
    WCHAR ImagePath[] = L"System32\\drivers\\lxcore.sys";
    SC_HANDLE Manager, Service;

    Manager = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT | SC_MANAGER_CREATE_SERVICE);
    if (Manager == NULL)
        return;
    Service = OpenServiceW(Manager, LX_SERVICE_NAME, SERVICE_START);
    if (Service == NULL)
    {
        Service = CreateServiceW(Manager, LX_SERVICE_NAME, L"Linux Subsystem", SERVICE_START, SERVICE_KERNEL_DRIVER,
                                 SERVICE_DEMAND_START, SERVICE_ERROR_NORMAL, ImagePath, NULL, NULL, NULL, NULL, NULL);
    }
    if (Service != NULL)
    {
        StartServiceW(Service, 0, NULL);
        CloseServiceHandle(Service);
    }
    CloseServiceHandle(Manager);
}

static
BOOL
LxAttach(
    _In_ PCWSTR Root)
{
    UNICODE_STRING NtRoot;
    WCHAR FullRoot[MAX_PATH];
    OVERLAPPED Overlapped;
    DWORD Bytes;
    BOOL Success;

    if (!GetFullPathNameW(Root, MAX_PATH, FullRoot, NULL) ||
        !RtlDosPathNameToNtPathName_U(FullRoot, &NtRoot, NULL, NULL))
    {
        return FALSE;
    }

    LxDevice = CreateFileW(LX_USER_WINDOW_NAME, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING,
                           FILE_FLAG_OVERLAPPED, NULL);
    if (LxDevice == INVALID_HANDLE_VALUE)
    {
        LxStartDriver();
        LxDevice = CreateFileW(LX_USER_WINDOW_NAME, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING,
                               FILE_FLAG_OVERLAPPED, NULL);
    }
    if (LxDevice == INVALID_HANDLE_VALUE)
    {
        RtlFreeUnicodeString(&NtRoot);
        return FALSE;
    }

    ZeroMemory(&Overlapped, sizeof(Overlapped));
    Overlapped.hEvent = LxSendEvent;
    Success = DeviceIoControl(LxDevice, IOCTL_LX_WIN_ATTACH, NtRoot.Buffer, NtRoot.Length, NULL, 0, &Bytes, &Overlapped);
    if (!Success && (GetLastError() == ERROR_IO_PENDING))
        Success = GetOverlappedResult(LxDevice, &Overlapped, &Bytes, TRUE);
    RtlFreeUnicodeString(&NtRoot);
    return Success;
}

int
WINAPI
wWinMain(
    _In_ HINSTANCE Instance,
    _In_opt_ HINSTANCE PreviousInstance,
    _In_ LPWSTR CommandLine,
    _In_ int ShowCommand)
{
    WNDCLASSEXW Class;
    WCHAR FullRoot[MAX_PATH];
    PCWSTR Root = NULL;
    HANDLE Reader;
    MSG Message;
    int Index, First = __argc;

    UNREFERENCED_PARAMETER(PreviousInstance);
    UNREFERENCED_PARAMETER(CommandLine);
    UNREFERENCED_PARAMETER(ShowCommand);

    for (Index = 1; Index < __argc; Index++)
    {
        if ((wcscmp(__wargv[Index], L"-r") == 0) && (Index + 1 < __argc))
        {
            Root = __wargv[++Index];
        }
        else
        {
            First = Index;
            break;
        }
    }
    if (Root == NULL)
        return 1;

    LxSendEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if ((LxSendEvent == NULL) || !LxAttach(Root))
    {
        LxLaunchProgram(Root, First, __argc - First);
        return 2;
    }
    LxRoot = Root;
    LxPendingFirst = First;
    LxPendingCount = __argc - First;

    if (LxNormalizeRoot(Root, FullRoot, ARRAYSIZE(FullRoot)))
        LxRootTagValue = LxRootTag(FullRoot);
    LxIconCacheInitialize(Root);

    ZeroMemory(&Class, sizeof(Class));
    Class.cbSize = sizeof(Class);
    Class.lpfnWndProc = LxWindowProc;
    Class.hInstance = Instance;
    Class.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    Class.hIcon = LoadImageW(Instance, MAKEINTRESOURCEW(IDI_LXWINHOST), IMAGE_ICON, GetSystemMetrics(SM_CXICON),
                             GetSystemMetrics(SM_CYICON), LR_DEFAULTCOLOR);
    Class.hIconSm = LoadImageW(Instance, MAKEINTRESOURCEW(IDI_LXWINHOST), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
                               GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR);
    Class.lpszClassName = LX_HOST_CLASS;
    Class.style = CS_DBLCLKS;
    if (!RegisterClassExW(&Class))
        return 3;

    Class.hIcon = NULL;
    Class.hIconSm = NULL;
    Class.lpfnWndProc = LxMessageProc;
    Class.lpszClassName = LX_HOST_MESSAGE;
    Class.style = 0;
    if (!RegisterClassExW(&Class))
        return 3;
    LxMessageWindow = CreateWindowExW(0, LX_HOST_MESSAGE, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, Instance, NULL);
    if (LxMessageWindow == NULL)
        return 3;
    AddClipboardFormatListener(LxMessageWindow);

    Reader = CreateThread(NULL, 0, LxReaderThread, NULL, 0, NULL);
    if (Reader == NULL)
        return 4;
    LxStartCompositor(Root);

    while (GetMessageW(&Message, NULL, 0, 0) > 0)
    {
        TranslateMessage(&Message);
        DispatchMessageW(&Message);
    }
    CloseHandle(LxDevice);
    WaitForSingleObject(Reader, 5000);
    return 0;
}
