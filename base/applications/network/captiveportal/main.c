/*
 * PROJECT:     LiberNT Captive Portal Sign-in
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Window that shows the sign-in page of a network until it reaches the Internet
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <stdarg.h>
#include <stdlib.h>
#include <windef.h>
#include <winbase.h>
#include <wingdi.h>
#include <winuser.h>
#include <winreg.h>
#include <winhttp.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <strsafe.h>

#include "html.h"
#include "resource.h"

#define CP_CLASS_NAME L"LiberNTCaptivePortal"
#define CP_PAGE_CLASS_NAME L"LiberNTCaptivePortalPage"
#define CP_MUTEX_NAME L"Local\\LiberNTCaptivePortal"
#define CP_PARAMETERS_KEY L"SYSTEM\\CurrentControlSet\\Services\\NlaSvc\\Parameters\\Internet"
#define CP_USER_AGENT L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) LiberNT-NetworkSignIn/1.0"
#define CP_URL_LENGTH 4096
#define CP_TEXT_LENGTH 256
#define CP_MAX_BODY (4 * 1024 * 1024)
#define CP_READ_CHUNK 16384
#define CP_ID_BASE 1000
#define CP_ID_ACTION 100
#define CP_ID_STATUS 101
#define CP_TIMER_PROBE 1
#define CP_TIMER_REFRESH 2
#define CP_PROBE_INTERVAL 5000
#define CP_MAX_REFRESH_SECONDS 3600
#define CP_MS_PER_SECOND 1000
#define CP_COMBO_DROP_LINES 8
#define CP_TEXTAREA_LINES 4
#define WM_CP_PAGE (WM_APP + 1)
#define WM_CP_PROBE (WM_APP + 2)

typedef enum _CP_ROLE
{
    CpRoleText,
    CpRoleLink,
    CpRoleEdit,
    CpRoleCheck,
    CpRoleRadio,
    CpRoleCombo,
    CpRoleTextArea,
    CpRoleButton,
    CpRoleAddress
} CP_ROLE;

typedef struct _CP_CONTROL
{
    HWND Hwnd;
    size_t Item;
    CP_ROLE Role;
    int Heading;
} CP_CONTROL;

typedef struct _CP_REQUEST
{
    LONG Sequence;
    WCHAR Url[CP_URL_LENGTH];
    BOOL Post;
    char *Body;
    DWORD BodyLength;
    WCHAR ContentType[CP_TEXT_LENGTH];
} CP_REQUEST;

typedef struct _CP_RESPONSE
{
    LONG Sequence;
    DWORD Error;
    DWORD Status;
    WCHAR Url[CP_URL_LENGTH];
    WCHAR ContentType[CP_TEXT_LENGTH];
    char *Body;
    SIZE_T Length;
} CP_RESPONSE;

typedef struct _CP_PROBE
{
    WCHAR Url[CP_URL_LENGTH];
    char Content[CP_TEXT_LENGTH * 3];
} CP_PROBE;

static HINSTANCE Instance;
static HWND MainWindow;
static HWND PageWindow;
static HWND StatusWindow;
static HWND ActionButton;
static HFONT TextFont;
static HFONT LinkFont;
static HFONT HeadingFont[3];
static HINTERNET PageSession;
static HINTERNET ProbeSession;
static CP_PROBE Probe;
static HTML_DOCUMENT Page;
static WCHAR PageUrl[CP_URL_LENGTH];
static WCHAR BaseUrl[CP_URL_LENGTH];
static WCHAR RefreshUrl[CP_URL_LENGTH];
static CP_CONTROL *Controls;
static size_t ControlCount;
static size_t ControlCapacity;
static int ScrollPos;
static int ContentHeight;
static LONG NavigationSequence;
static LONG ProbeRunning;
static BOOL Online;
static BOOL Loading;
static int Dpi = 96;

static int
Scale(int Value)
{
    return MulDiv(Value, Dpi, 96);
}

static PWSTR
Utf8ToWide(const char *Text)
{
    int Length;
    PWSTR Wide;

    if (!Text)
        Text = "";
    Length = MultiByteToWideChar(CP_UTF8, 0, Text, -1, NULL, 0);
    if (Length <= 0)
        return NULL;
    Wide = malloc(Length * sizeof(WCHAR));
    if (Wide && !MultiByteToWideChar(CP_UTF8, 0, Text, -1, Wide, Length))
    {
        free(Wide);
        return NULL;
    }
    return Wide;
}

static char *
WideToUtf8(PCWSTR Text)
{
    int Length = WideCharToMultiByte(CP_UTF8, 0, Text, -1, NULL, 0, NULL, NULL);
    char *Utf8;

    if (Length <= 0)
        return NULL;
    Utf8 = malloc(Length);
    if (Utf8 && !WideCharToMultiByte(CP_UTF8, 0, Text, -1, Utf8, Length, NULL, NULL))
    {
        free(Utf8);
        return NULL;
    }
    return Utf8;
}

static VOID
LoadText(UINT Id, PWSTR Buffer, int Length)
{
    if (!LoadStringW(Instance, Id, Buffer, Length))
        Buffer[0] = 0;
}

static VOID
ReadProbeConfig(VOID)
{
    WCHAR Host[CP_TEXT_LENGTH], Path[CP_TEXT_LENGTH], Content[CP_TEXT_LENGTH];
    DWORD Size;

    Size = sizeof(Host);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, CP_PARAMETERS_KEY, L"ActiveWebProbeHost", RRF_RT_REG_SZ, NULL, Host, &Size) ||
        !Host[0])
    {
        StringCchCopyW(Host, ARRAYSIZE(Host), L"www.msftconnecttest.com");
    }
    Size = sizeof(Path);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, CP_PARAMETERS_KEY, L"ActiveWebProbePath", RRF_RT_REG_SZ, NULL, Path, &Size) ||
        !Path[0])
    {
        StringCchCopyW(Path, ARRAYSIZE(Path), L"connecttest.txt");
    }
    Size = sizeof(Content);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, CP_PARAMETERS_KEY, L"ActiveWebProbeContent", RRF_RT_REG_SZ, NULL, Content,
                     &Size) || !Content[0])
    {
        StringCchCopyW(Content, ARRAYSIZE(Content), L"Microsoft Connect Test");
    }

    StringCchPrintfW(Probe.Url, ARRAYSIZE(Probe.Url), L"http://%s%s%s", Host, Path[0] == L'/' ? L"" : L"/", Path);
    if (!WideCharToMultiByte(CP_UTF8, 0, Content, -1, Probe.Content, sizeof(Probe.Content), NULL, NULL))
        Probe.Content[0] = 0;
}

static BOOL
BodyIsProbeContent(const char *Body, SIZE_T Length)
{
    size_t Expected = strlen(Probe.Content);

    while (Length && (Body[Length - 1] == ' ' || Body[Length - 1] == '\t' || Body[Length - 1] == '\r' ||
                      Body[Length - 1] == '\n'))
    {
        Length--;
    }
    return Expected != 0 && Length == Expected && memcmp(Body, Probe.Content, Expected) == 0;
}

static DWORD
HttpFetch(HINTERNET Session, PCWSTR Url, BOOL Post, const char *Body, DWORD BodyLength, PCWSTR ContentType,
          BOOL FollowRedirects, CP_RESPONSE *Response)
{
    URL_COMPONENTSW Parts;
    WCHAR Host[CP_TEXT_LENGTH], Path[CP_URL_LENGTH], Headers[CP_TEXT_LENGTH + 32];
    HINTERNET Connect = NULL, Request = NULL;
    DWORD Error = NO_ERROR, Size, Option, PathLength;
    PCWSTR Hash;

    ZeroMemory(&Parts, sizeof(Parts));
    Parts.dwStructSize = sizeof(Parts);
    Parts.dwHostNameLength = (DWORD)-1;
    Parts.dwUrlPathLength = (DWORD)-1;
    Parts.dwExtraInfoLength = (DWORD)-1;
    if (!WinHttpCrackUrl(Url, 0, 0, &Parts) || Parts.dwHostNameLength == 0 ||
        Parts.dwHostNameLength >= ARRAYSIZE(Host) ||
        (Parts.nScheme != INTERNET_SCHEME_HTTP && Parts.nScheme != INTERNET_SCHEME_HTTPS))
    {
        return ERROR_WINHTTP_INVALID_URL;
    }
    CopyMemory(Host, Parts.lpszHostName, Parts.dwHostNameLength * sizeof(WCHAR));
    Host[Parts.dwHostNameLength] = 0;

    PathLength = Parts.dwUrlPathLength + Parts.dwExtraInfoLength;
    if (PathLength >= ARRAYSIZE(Path))
        return ERROR_WINHTTP_INVALID_URL;
    if (PathLength == 0)
    {
        StringCchCopyW(Path, ARRAYSIZE(Path), L"/");
    }
    else
    {
        CopyMemory(Path, Parts.dwUrlPathLength ? Parts.lpszUrlPath : Parts.lpszExtraInfo, PathLength * sizeof(WCHAR));
        Path[PathLength] = 0;
        Hash = wcschr(Path, L'#');
        if (Hash)
            Path[Hash - Path] = 0;
        if (Path[0] != L'/')
        {
            WCHAR Relative[CP_URL_LENGTH];

            StringCchCopyW(Relative, ARRAYSIZE(Relative), Path);
            StringCchPrintfW(Path, ARRAYSIZE(Path), L"/%s", Relative);
        }
    }

    Connect = WinHttpConnect(Session, Host, Parts.nPort, 0);
    if (Connect)
    {
        Request = WinHttpOpenRequest(Connect, Post ? L"POST" : L"GET", Path, NULL, WINHTTP_NO_REFERER,
                                     WINHTTP_DEFAULT_ACCEPT_TYPES,
                                     Parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0);
    }
    if (!Request)
    {
        Error = GetLastError();
        goto Cleanup;
    }

    if (FollowRedirects)
    {
        Option = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
        WinHttpSetOption(Request, WINHTTP_OPTION_REDIRECT_POLICY, &Option, sizeof(Option));
    }
    else
    {
        Option = WINHTTP_DISABLE_REDIRECTS;
        WinHttpSetOption(Request, WINHTTP_OPTION_DISABLE_FEATURE, &Option, sizeof(Option));
    }

    Headers[0] = 0;
    if (Post && ContentType && ContentType[0])
        StringCchPrintfW(Headers, ARRAYSIZE(Headers), L"Content-Type: %s\r\n", ContentType);
    if (!WinHttpSendRequest(Request, Headers[0] ? Headers : WINHTTP_NO_ADDITIONAL_HEADERS, Headers[0] ? (DWORD)-1 : 0,
                            (LPVOID)Body, BodyLength, BodyLength, 0) ||
        !WinHttpReceiveResponse(Request, NULL))
    {
        Error = GetLastError();
        goto Cleanup;
    }

    Size = sizeof(Response->Status);
    if (!WinHttpQueryHeaders(Request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                             &Response->Status, &Size, WINHTTP_NO_HEADER_INDEX))
    {
        Response->Status = 0;
    }
    Size = sizeof(Response->ContentType);
    if (!WinHttpQueryHeaders(Request, WINHTTP_QUERY_CONTENT_TYPE, WINHTTP_HEADER_NAME_BY_INDEX, Response->ContentType,
                             &Size, WINHTTP_NO_HEADER_INDEX))
    {
        Response->ContentType[0] = 0;
    }
    Size = sizeof(Response->Url);
    if (!WinHttpQueryOption(Request, WINHTTP_OPTION_URL, Response->Url, &Size))
        StringCchCopyW(Response->Url, ARRAYSIZE(Response->Url), Url);

    for (;;)
    {
        DWORD Available = 0, Read = 0;
        char *Grown;

        if (!WinHttpQueryDataAvailable(Request, &Available))
        {
            Error = GetLastError();
            break;
        }
        if (Available == 0)
            break;
        if (Available > CP_READ_CHUNK)
            Available = CP_READ_CHUNK;
        if (Response->Length + Available > CP_MAX_BODY)
            break;
        Grown = realloc(Response->Body, Response->Length + Available + 1);
        if (!Grown)
        {
            Error = ERROR_NOT_ENOUGH_MEMORY;
            break;
        }
        Response->Body = Grown;
        if (!WinHttpReadData(Request, Response->Body + Response->Length, Available, &Read))
        {
            Error = GetLastError();
            break;
        }
        if (Read == 0)
            break;
        Response->Length += Read;
        Response->Body[Response->Length] = 0;
    }

Cleanup:
    if (Request)
        WinHttpCloseHandle(Request);
    if (Connect)
        WinHttpCloseHandle(Connect);
    return Error;
}

static DWORD WINAPI
NavigateThread(LPVOID Context)
{
    CP_REQUEST *Request = Context;
    CP_RESPONSE *Response = calloc(1, sizeof(*Response));

    if (Response)
    {
        Response->Sequence = Request->Sequence;
        Response->Error = HttpFetch(PageSession, Request->Url, Request->Post, Request->Body, Request->BodyLength,
                                    Request->ContentType, TRUE, Response);
        if (!PostMessageW(MainWindow, WM_CP_PAGE, 0, (LPARAM)Response))
        {
            free(Response->Body);
            free(Response);
        }
    }
    free(Request->Body);
    free(Request);
    return 0;
}

static DWORD WINAPI
ProbeThread(LPVOID Context)
{
    CP_RESPONSE *Response = calloc(1, sizeof(*Response));
    BOOL Reached = FALSE;

    UNREFERENCED_PARAMETER(Context);

    if (Response)
    {
        if (HttpFetch(ProbeSession, Probe.Url, FALSE, NULL, 0, NULL, FALSE, Response) == NO_ERROR &&
            Response->Status == HTTP_STATUS_OK)
        {
            Reached = BodyIsProbeContent(Response->Body ? Response->Body : "", Response->Length);
        }
        free(Response->Body);
        free(Response);
    }
    InterlockedExchange(&ProbeRunning, 0);
    PostMessageW(MainWindow, WM_CP_PROBE, Reached, 0);
    return 0;
}

static VOID
StartProbe(VOID)
{
    HANDLE Thread;

    if (InterlockedCompareExchange(&ProbeRunning, 1, 0) != 0)
        return;
    Thread = CreateThread(NULL, 0, ProbeThread, NULL, 0, NULL);
    if (Thread)
        CloseHandle(Thread);
    else
        InterlockedExchange(&ProbeRunning, 0);
}

static VOID
UpdateStatus(VOID)
{
    WCHAR Text[CP_TEXT_LENGTH];

    LoadText(Loading ? IDS_LOADING : (Online ? IDS_CONNECTED : IDS_NOT_CONNECTED), Text, ARRAYSIZE(Text));
    SetWindowTextW(StatusWindow, Text);
    LoadText(Online ? IDS_DONE : IDS_CANCEL, Text, ARRAYSIZE(Text));
    SetWindowTextW(ActionButton, Text);
}

static VOID
Navigate(PCWSTR Url, char *Body, DWORD BodyLength, PCWSTR ContentType)
{
    CP_REQUEST *Request = calloc(1, sizeof(*Request));
    HANDLE Thread;

    if (!Request)
    {
        free(Body);
        return;
    }
    KillTimer(MainWindow, CP_TIMER_REFRESH);
    Request->Sequence = InterlockedIncrement(&NavigationSequence);
    StringCchCopyW(Request->Url, ARRAYSIZE(Request->Url), Url);
    Request->Post = Body != NULL;
    Request->Body = Body;
    Request->BodyLength = BodyLength;
    if (ContentType)
        StringCchCopyW(Request->ContentType, ARRAYSIZE(Request->ContentType), ContentType);

    Loading = TRUE;
    EnableWindow(PageWindow, FALSE);
    UpdateStatus();

    Thread = CreateThread(NULL, 0, NavigateThread, Request, 0, NULL);
    if (Thread)
    {
        CloseHandle(Thread);
    }
    else
    {
        free(Request->Body);
        free(Request);
        Loading = FALSE;
        EnableWindow(PageWindow, TRUE);
        UpdateStatus();
    }
}

static BOOL
ResolveUrl(const char *Relative, PWSTR Resolved, DWORD Length)
{
    PWSTR Wide = Utf8ToWide(Relative ? Relative : "");
    DWORD Size = Length;
    HRESULT Result;

    if (!Wide)
        return FALSE;
    Result = UrlCombineW(BaseUrl[0] ? BaseUrl : PageUrl, Wide, Resolved, &Size, URL_ESCAPE_SPACES_ONLY);
    free(Wide);
    return SUCCEEDED(Result) &&
           (StrCmpNIW(Resolved, L"http://", 7) == 0 || StrCmpNIW(Resolved, L"https://", 8) == 0);
}

static VOID
ClearPage(VOID)
{
    size_t Index;

    KillTimer(MainWindow, CP_TIMER_REFRESH);
    for (Index = 0; Index < ControlCount; Index++)
        DestroyWindow(Controls[Index].Hwnd);
    ControlCount = 0;
    HtmlFree(&Page);
    ScrollPos = 0;
    ContentHeight = 0;
}

static CP_CONTROL *
AddControl(PCWSTR Class, PCWSTR Text, DWORD Style, size_t Item, CP_ROLE Role, HFONT Font)
{
    CP_CONTROL *Control;
    HWND Hwnd;

    if (ControlCount == ControlCapacity)
    {
        size_t Capacity = ControlCapacity ? ControlCapacity * 2 : 32;
        CP_CONTROL *Grown = realloc(Controls, Capacity * sizeof(*Grown));

        if (!Grown)
            return NULL;
        Controls = Grown;
        ControlCapacity = Capacity;
    }
    Hwnd = CreateWindowExW(0, Class, Text ? Text : L"", WS_CHILD | WS_VISIBLE | Style, 0, 0, 0, 0, PageWindow,
                           (HMENU)(UINT_PTR)(CP_ID_BASE + ControlCount), Instance, NULL);
    if (!Hwnd)
        return NULL;
    SendMessageW(Hwnd, WM_SETFONT, (WPARAM)Font, FALSE);
    Control = &Controls[ControlCount++];
    Control->Hwnd = Hwnd;
    Control->Item = Item;
    Control->Role = Role;
    Control->Heading = 0;
    return Control;
}

static VOID
AddMessage(UINT Id, PCWSTR Address)
{
    WCHAR Text[CP_TEXT_LENGTH * 2];

    LoadText(Id, Text, ARRAYSIZE(Text));
    AddControl(WC_STATICW, Text, SS_LEFT | SS_NOPREFIX, (size_t)-1, CpRoleText, TextFont);
    if (Address && Address[0])
        AddControl(WC_EDITW, Address, WS_BORDER | WS_TABSTOP | ES_READONLY | ES_AUTOHSCROLL, (size_t)-1, CpRoleAddress,
                   TextFont);
}

static PWSTR
ItemCaption(const HTML_ITEM *Item, UINT DefaultId)
{
    WCHAR Default[CP_TEXT_LENGTH];

    if (Item->Kind == HtmlItemInput && Item->Value && Item->Value[0] && !Item->Text)
        return Utf8ToWide(Item->Value);
    if (Item->Text && Item->Text[0])
        return Utf8ToWide(Item->Text);
    if (Item->Value && Item->Value[0])
        return Utf8ToWide(Item->Value);
    if (DefaultId == 0)
        return Utf8ToWide("");
    LoadText(DefaultId, Default, ARRAYSIZE(Default));
    return _wcsdup(Default);
}

static BOOL
IsTextInput(const HTML_ITEM *Item)
{
    return Item->Kind == HtmlItemInput && Item->Type && strcmp(Item->Type, "checkbox") != 0 &&
           strcmp(Item->Type, "radio") != 0 && strcmp(Item->Type, "submit") != 0 && strcmp(Item->Type, "image") != 0 &&
           strcmp(Item->Type, "button") != 0;
}

static BOOL
IsNavigableHref(const char *Href)
{
    if (!Href || !Href[0] || Href[0] == '#')
        return FALSE;
    return StrCmpNIA(Href, "javascript:", 11) != 0 && StrCmpNIA(Href, "mailto:", 7) != 0 &&
           StrCmpNIA(Href, "tel:", 4) != 0 && StrCmpNIA(Href, "data:", 5) != 0;
}

static VOID
BuildControls(VOID)
{
    size_t Index, Option;

    for (Index = 0; Index < Page.ItemCount; Index++)
    {
        HTML_ITEM *Item = &Page.Items[Index];
        CP_CONTROL *Control = NULL;
        PWSTR Text;

        if (!HtmlIsVisibleItem(Item))
            continue;

        switch (Item->Kind)
        {
            case HtmlItemText:
                Text = Utf8ToWide(Item->Text);
                Control = AddControl(WC_STATICW, Text, SS_LEFT | SS_NOPREFIX, Index, CpRoleText,
                                     Item->Heading ? HeadingFont[min(Item->Heading, 3) - 1] : TextFont);
                if (Control)
                    Control->Heading = Item->Heading;
                free(Text);
                break;

            case HtmlItemLink:
                Text = Utf8ToWide(Item->Text);
                Control = AddControl(WC_BUTTONW, Text, WS_TABSTOP | BS_OWNERDRAW | BS_NOTIFY, Index, CpRoleLink, LinkFont);
                if (Control && !IsNavigableHref(Item->Href))
                    EnableWindow(Control->Hwnd, FALSE);
                free(Text);
                break;

            case HtmlItemInput:
                if (strcmp(Item->Type, "checkbox") == 0 || strcmp(Item->Type, "radio") == 0)
                {
                    BOOL Radio = Item->Type[0] == 'r';

                    Text = Utf8ToWide(Item->Text);
                    Control = AddControl(WC_BUTTONW, Text,
                                         WS_TABSTOP | BS_MULTILINE | BS_NOTIFY | (Radio ? BS_RADIOBUTTON : BS_AUTOCHECKBOX),
                                         Index, Radio ? CpRoleRadio : CpRoleCheck, TextFont);
                    if (Control)
                        SendMessageW(Control->Hwnd, BM_SETCHECK, Item->Checked ? BST_CHECKED : BST_UNCHECKED, 0);
                    free(Text);
                }
                else if (IsTextInput(Item))
                {
                    DWORD Style = WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL;

                    if (strcmp(Item->Type, "password") == 0)
                        Style |= ES_PASSWORD;
                    Text = Utf8ToWide(Item->Value);
                    Control = AddControl(WC_EDITW, Text, Style, Index, CpRoleEdit, TextFont);
                    free(Text);
                    if (Control && Item->Placeholder)
                    {
                        PWSTR Placeholder = Utf8ToWide(Item->Placeholder);

                        if (Placeholder)
                            SendMessageW(Control->Hwnd, EM_SETCUEBANNER, FALSE, (LPARAM)Placeholder);
                        free(Placeholder);
                    }
                }
                else
                {
                    Text = ItemCaption(Item, IDS_SUBMIT);
                    Control = AddControl(WC_BUTTONW, Text, WS_TABSTOP | BS_PUSHBUTTON | BS_NOTIFY, Index, CpRoleButton,
                                         TextFont);
                    if (Control && !HtmlIsSubmitItem(Item))
                        EnableWindow(Control->Hwnd, FALSE);
                    free(Text);
                }
                break;

            case HtmlItemButton:
                Text = ItemCaption(Item, IDS_SUBMIT);
                Control = AddControl(WC_BUTTONW, Text, WS_TABSTOP | BS_PUSHBUTTON | BS_NOTIFY, Index, CpRoleButton, TextFont);
                if (Control && !HtmlIsSubmitItem(Item))
                    EnableWindow(Control->Hwnd, FALSE);
                free(Text);
                break;

            case HtmlItemSelect:
                Control = AddControl(WC_COMBOBOXW, NULL, WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST, Index, CpRoleCombo,
                                     TextFont);
                if (Control)
                {
                    int Selected = 0;

                    for (Option = 0; Option < Item->OptionCount; Option++)
                    {
                        Text = Utf8ToWide(Item->Options[Option].Text);
                        SendMessageW(Control->Hwnd, CB_ADDSTRING, 0, (LPARAM)(Text ? Text : L""));
                        free(Text);
                        if (Item->Options[Option].Selected)
                            Selected = (int)Option;
                    }
                    SendMessageW(Control->Hwnd, CB_SETCURSEL, Selected, 0);
                }
                break;

            case HtmlItemTextArea:
                Text = Utf8ToWide(Item->Value);
                Control = AddControl(WC_EDITW, Text,
                                     WS_BORDER | WS_TABSTOP | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN,
                                     Index, CpRoleTextArea, TextFont);
                free(Text);
                break;
        }

        if (Control && Item->Disabled)
            EnableWindow(Control->Hwnd, FALSE);
    }
}

static int
TextHeight(HDC Hdc, HFONT Font, PCWSTR Text, int Width, int *TextWidth)
{
    RECT Rect = { 0, 0, max(Width, 1), 0 };
    HGDIOBJ Old = SelectObject(Hdc, Font);

    DrawTextW(Hdc, Text[0] ? Text : L" ", -1, &Rect, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX | DT_EDITCONTROL);
    SelectObject(Hdc, Old);
    if (TextWidth)
        *TextWidth = Rect.right - Rect.left;
    return Rect.bottom - Rect.top;
}

static VOID
UpdateScrollBar(VOID)
{
    SCROLLINFO Info;
    RECT Client;

    GetClientRect(PageWindow, &Client);
    ZeroMemory(&Info, sizeof(Info));
    Info.cbSize = sizeof(Info);
    Info.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    Info.nMin = 0;
    Info.nMax = max(ContentHeight - 1, 0);
    Info.nPage = Client.bottom;
    Info.nPos = ScrollPos;
    SetScrollInfo(PageWindow, SB_VERT, &Info, TRUE);
}

static VOID
LayoutPage(VOID)
{
    RECT Client;
    HDC Hdc;
    TEXTMETRICW Metrics;
    int Margin = Scale(16), Gap = Scale(8), Width, Y, LineHeight, Check = GetSystemMetrics(SM_CXMENUCHECK);
    size_t Index;
    HDWP Positions;

    GetClientRect(PageWindow, &Client);
    if (GetWindowLongPtrW(PageWindow, GWL_STYLE) & WS_VSCROLL)
        Width = Client.right - 2 * Margin;
    else
        Width = Client.right - 2 * Margin - GetSystemMetrics(SM_CXVSCROLL);
    Width = max(Width, Scale(80));

    Hdc = GetDC(PageWindow);
    SelectObject(Hdc, TextFont);
    GetTextMetricsW(Hdc, &Metrics);
    LineHeight = Metrics.tmHeight;

    Y = Margin;
    Positions = BeginDeferWindowPos((int)ControlCount);
    for (Index = 0; Index < ControlCount; Index++)
    {
        CP_CONTROL *Control = &Controls[Index];
        int TextLength = GetWindowTextLengthW(Control->Hwnd);
        PWSTR Text = malloc((max(TextLength, 0) + 1) * sizeof(WCHAR));
        int X = Margin, ControlWidth = Width, Height, TextWidth = 0, Extra = 0;

        if (!Text)
            continue;
        GetWindowTextW(Control->Hwnd, Text, TextLength + 1);
        switch (Control->Role)
        {
            case CpRoleText:
                Height = TextHeight(Hdc, (HFONT)SendMessageW(Control->Hwnd, WM_GETFONT, 0, 0), Text, Width, NULL);
                if (Control->Heading)
                    Extra = Gap / 2;
                break;

            case CpRoleLink:
                Height = TextHeight(Hdc, LinkFont, Text, Width - Scale(4), &TextWidth) + Scale(4);
                ControlWidth = min(TextWidth + Scale(6), Width);
                break;

            case CpRoleCheck:
            case CpRoleRadio:
                Height = max(TextHeight(Hdc, TextFont, Text, Width - Check - Scale(8), NULL), Check) + Scale(2);
                break;

            case CpRoleButton:
                TextHeight(Hdc, TextFont, Text, Width, &TextWidth);
                ControlWidth = min(max(TextWidth + Scale(32), Scale(96)), Width);
                Height = LineHeight + Scale(12);
                break;

            case CpRoleTextArea:
                Height = LineHeight * CP_TEXTAREA_LINES + Scale(8);
                break;

            case CpRoleCombo:
                Height = LineHeight + Scale(10);
                break;

            default:
                Height = LineHeight + Scale(10);
                break;
        }
        free(Text);

        Y += Extra;
        if (Positions)
        {
            Positions = DeferWindowPos(Positions, Control->Hwnd, NULL, X, Y - ScrollPos, ControlWidth,
                                       Control->Role == CpRoleCombo ? Height + LineHeight * CP_COMBO_DROP_LINES : Height,
                                       SWP_NOZORDER | SWP_NOACTIVATE);
        }
        Y += Height + Gap + Extra;
    }
    if (Positions)
        EndDeferWindowPos(Positions);
    ReleaseDC(PageWindow, Hdc);

    ContentHeight = Y + Margin;
    ScrollPos = max(0, min(ScrollPos, ContentHeight - Client.bottom));
    UpdateScrollBar();
}

static VOID
ScrollPageTo(int Position)
{
    RECT Client;
    int Old = ScrollPos;

    GetClientRect(PageWindow, &Client);
    Position = max(0, min(Position, ContentHeight - Client.bottom));
    if (Position == Old)
        return;
    ScrollPos = Position;
    ScrollWindowEx(PageWindow, 0, Old - Position, NULL, NULL, NULL, NULL, SW_SCROLLCHILDREN | SW_INVALIDATE | SW_ERASE);
    UpdateScrollBar();
}

static VOID
EnsureVisible(HWND Control)
{
    RECT Rect, Client;

    GetWindowRect(Control, &Rect);
    MapWindowPoints(NULL, PageWindow, (POINT *)&Rect, 2);
    GetClientRect(PageWindow, &Client);
    if (Rect.top < 0)
        ScrollPageTo(ScrollPos + Rect.top - Scale(8));
    else if (Rect.bottom > Client.bottom)
        ScrollPageTo(ScrollPos + Rect.bottom - Client.bottom + Scale(8));
}

static char *
ConvertToUtf8(const char *Body, SIZE_T Length, PCWSTR ContentType, SIZE_T *Converted)
{
    static const struct { const char *Name; UINT CodePage; } CodePages[] =
    {
        { "utf-8", CP_UTF8 }, { "utf8", CP_UTF8 }, { "windows-1250", 1250 }, { "windows-1251", 1251 },
        { "windows-1252", 1252 }, { "windows-1253", 1253 }, { "windows-1254", 1254 }, { "windows-1255", 1255 },
        { "windows-1256", 1256 }, { "windows-1257", 1257 }, { "windows-1258", 1258 }, { "iso-8859-1", 1252 },
        { "latin1", 1252 }, { "us-ascii", 1252 }, { "iso-8859-2", 28592 }, { "iso-8859-5", 28595 },
        { "iso-8859-7", 28597 }, { "iso-8859-9", 28599 }, { "iso-8859-15", 28605 }, { "koi8-r", 20866 },
        { "koi8-u", 21866 }, { "shift_jis", 932 }, { "gb2312", 936 }, { "gbk", 936 }, { "big5", 950 },
        { "euc-kr", 949 },
    };
    char *Charset = NULL, *Result;
    PCWSTR Parameter = ContentType ? StrStrIW(ContentType, L"charset=") : NULL;
    UINT CodePage = 0;
    size_t Index;
    int Wide, Utf8;
    PWSTR Buffer;

    if (Parameter)
    {
        Charset = WideToUtf8(Parameter + 8);
        if (Charset)
        {
            for (Index = 0; Charset[Index]; Index++)
            {
                if (Charset[Index] == ';' || Charset[Index] == ' ' || Charset[Index] == '"')
                {
                    Charset[Index] = 0;
                    break;
                }
            }
        }
    }
    if (!Charset)
        Charset = HtmlFindCharset(Body, Length);
    for (Index = 0; Charset && Index < ARRAYSIZE(CodePages); Index++)
    {
        if (lstrcmpiA(Charset, CodePages[Index].Name) == 0)
        {
            CodePage = CodePages[Index].CodePage;
            break;
        }
    }
    free(Charset);

    if (CodePage == 0)
    {
        CodePage = (Length == 0 || MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, Body, (int)Length, NULL, 0))
                   ? CP_UTF8 : 1252;
    }
    if (CodePage == CP_UTF8)
    {
        Result = malloc(Length + 1);
        if (Result)
        {
            CopyMemory(Result, Body, Length);
            Result[Length] = 0;
            *Converted = Length;
        }
        return Result;
    }

    Wide = MultiByteToWideChar(CodePage, 0, Body, (int)Length, NULL, 0);
    Buffer = malloc((Wide + 1) * sizeof(WCHAR));
    if (!Buffer)
        return NULL;
    MultiByteToWideChar(CodePage, 0, Body, (int)Length, Buffer, Wide);
    Utf8 = WideCharToMultiByte(CP_UTF8, 0, Buffer, Wide, NULL, 0, NULL, NULL);
    Result = malloc(Utf8 + 1);
    if (Result)
    {
        WideCharToMultiByte(CP_UTF8, 0, Buffer, Wide, Result, Utf8, NULL, NULL);
        Result[Utf8] = 0;
        *Converted = Utf8;
    }
    free(Buffer);
    return Result;
}

static BOOL
SameHost(PCWSTR Left, PCWSTR Right)
{
    URL_COMPONENTSW A, B;

    ZeroMemory(&A, sizeof(A));
    ZeroMemory(&B, sizeof(B));
    A.dwStructSize = B.dwStructSize = sizeof(A);
    A.dwHostNameLength = B.dwHostNameLength = (DWORD)-1;
    return WinHttpCrackUrl(Left, 0, 0, &A) && WinHttpCrackUrl(Right, 0, 0, &B) &&
           A.dwHostNameLength == B.dwHostNameLength &&
           _wcsnicmp(A.lpszHostName, B.lpszHostName, A.dwHostNameLength) == 0;
}

static VOID
ShowResponse(CP_RESPONSE *Response)
{
    char *Utf8 = NULL;
    SIZE_T Length = 0;
    PCWSTR Type = Response->ContentType;

    ClearPage();
    Loading = FALSE;
    EnableWindow(PageWindow, TRUE);

    if (Response->Error != NO_ERROR)
    {
        AddMessage(Response->Error == ERROR_WINHTTP_SECURE_FAILURE ? IDS_CERTIFICATE : IDS_LOAD_FAILED, NULL);
        goto Done;
    }

    StringCchCopyW(PageUrl, ARRAYSIZE(PageUrl), Response->Url);
    BaseUrl[0] = 0;

    if (Response->Status == HTTP_STATUS_OK && SameHost(Response->Url, Probe.Url) &&
        BodyIsProbeContent(Response->Body ? Response->Body : "", Response->Length))
    {
        Online = TRUE;
        AddMessage(IDS_ALREADY, NULL);
        goto Done;
    }

    if (Type[0] && StrStrIW(Type, L"html") == NULL && StrCmpNIW(Type, L"text/plain", 10) != 0)
    {
        AddMessage(IDS_UNSUPPORTED, PageUrl);
        goto Done;
    }

    Utf8 = ConvertToUtf8(Response->Body ? Response->Body : "", Response->Length, Type, &Length);
    if (!Utf8)
    {
        AddMessage(IDS_LOAD_FAILED, NULL);
        goto Done;
    }

    if (StrCmpNIW(Type, L"text/plain", 10) == 0)
    {
        PWSTR Wide = Utf8ToWide(Utf8);

        AddControl(WC_STATICW, Wide, SS_LEFT | SS_NOPREFIX, (size_t)-1, CpRoleText, TextFont);
        free(Wide);
        goto Done;
    }

    if (!HtmlParse(Utf8, Length, &Page))
    {
        AddMessage(IDS_LOAD_FAILED, NULL);
        goto Done;
    }
    if (Page.BaseHref)
    {
        WCHAR Resolved[CP_URL_LENGTH];

        if (ResolveUrl(Page.BaseHref, Resolved, ARRAYSIZE(Resolved)))
            StringCchCopyW(BaseUrl, ARRAYSIZE(BaseUrl), Resolved);
    }

    BuildControls();
    if (ControlCount == 0)
        AddMessage(Page.HasScript ? IDS_SCRIPT : IDS_EMPTY, Page.HasScript ? PageUrl : NULL);

    if (Page.HasRefresh && Page.RefreshUrl && ResolveUrl(Page.RefreshUrl, RefreshUrl, ARRAYSIZE(RefreshUrl)) &&
        lstrcmpiW(RefreshUrl, PageUrl) != 0)
    {
        SetTimer(MainWindow, CP_TIMER_REFRESH, min(Page.RefreshSeconds, CP_MAX_REFRESH_SECONDS) * CP_MS_PER_SECOND, NULL);
    }

Done:
    free(Utf8);
    LayoutPage();
    InvalidateRect(PageWindow, NULL, TRUE);
    UpdateStatus();
    StartProbe();
}

static VOID
StoreControlValues(VOID)
{
    size_t Index;

    for (Index = 0; Index < ControlCount; Index++)
    {
        CP_CONTROL *Control = &Controls[Index];
        HTML_ITEM *Item;

        if (Control->Item >= Page.ItemCount)
            continue;
        Item = &Page.Items[Control->Item];
        switch (Control->Role)
        {
            case CpRoleEdit:
            case CpRoleTextArea:
            {
                int Length = GetWindowTextLengthW(Control->Hwnd);
                PWSTR Text = malloc((Length + 1) * sizeof(WCHAR));
                char *Utf8;

                if (!Text)
                    break;
                GetWindowTextW(Control->Hwnd, Text, Length + 1);
                Utf8 = WideToUtf8(Text);
                if (Utf8)
                    HtmlSetValue(Item, Utf8);
                free(Utf8);
                free(Text);
                break;
            }

            case CpRoleCheck:
            case CpRoleRadio:
                Item->Checked = SendMessageW(Control->Hwnd, BM_GETCHECK, 0, 0) == BST_CHECKED;
                break;

            case CpRoleCombo:
            {
                LRESULT Selected = SendMessageW(Control->Hwnd, CB_GETCURSEL, 0, 0);
                size_t Option;

                for (Option = 0; Option < Item->OptionCount; Option++)
                    Item->Options[Option].Selected = (LRESULT)Option == Selected;
                break;
            }

            default:
                break;
        }
    }
}

static VOID
SubmitForm(size_t Form, size_t Submitter)
{
    WCHAR Action[CP_URL_LENGTH], ContentType[CP_TEXT_LENGTH];
    char Boundary[64], *Body;
    DWORD Tick = GetTickCount(), Process = GetCurrentProcessId();
    size_t Length = 0;
    BOOL Multipart;

    if (Form >= Page.FormCount || Loading)
        return;
    StoreControlValues();
    if (!ResolveUrl(Page.Forms[Form].Action ? Page.Forms[Form].Action : "", Action, ARRAYSIZE(Action)))
        return;

    Multipart = HtmlFormIsMultipart(&Page, Form);
    StringCchPrintfA(Boundary, ARRAYSIZE(Boundary), "----LiberNTFormBoundary%08lX%08lX", Tick, Process);
    Body = HtmlEncodeForm(&Page, Form, Submitter, Multipart ? Boundary : NULL, &Length);
    if (!Body)
        return;

    if (HtmlFormIsPost(&Page, Form))
    {
        if (Multipart)
            StringCchPrintfW(ContentType, ARRAYSIZE(ContentType),
                             L"multipart/form-data; boundary=----LiberNTFormBoundary%08lX%08lX", Tick, Process);
        else
            StringCchCopyW(ContentType, ARRAYSIZE(ContentType), L"application/x-www-form-urlencoded");
        Navigate(Action, Body, (DWORD)Length, ContentType);
    }
    else
    {
        PWSTR Query = Utf8ToWide(Body), Cut;

        free(Body);
        if (!Query)
            return;
        Cut = wcspbrk(Action, L"?#");
        if (Cut)
            *Cut = 0;
        StringCchCatW(Action, ARRAYSIZE(Action), L"?");
        StringCchCatW(Action, ARRAYSIZE(Action), Query);
        free(Query);
        Navigate(Action, NULL, 0, NULL);
    }
}

static CP_CONTROL *
ControlFromId(UINT Id)
{
    if (Id < CP_ID_BASE || Id - CP_ID_BASE >= ControlCount)
        return NULL;
    return &Controls[Id - CP_ID_BASE];
}

static VOID
SubmitFocusedForm(VOID)
{
    HWND Focus = GetFocus();
    CP_CONTROL *Control = Focus ? ControlFromId(GetDlgCtrlID(Focus)) : NULL;
    size_t Index;
    int Form;

    if (!Control || Control->Item >= Page.ItemCount || Control->Hwnd != Focus)
        return;
    if (Control->Role == CpRoleButton)
    {
        SubmitForm((size_t)Page.Items[Control->Item].Form, Control->Item);
        return;
    }
    Form = Page.Items[Control->Item].Form;
    if (Form < 0)
        return;
    for (Index = 0; Index < Page.ItemCount; Index++)
    {
        if (Page.Items[Index].Form == Form && HtmlIsSubmitItem(&Page.Items[Index]) && !Page.Items[Index].Disabled)
        {
            SubmitForm((size_t)Form, Index);
            return;
        }
    }
    SubmitForm((size_t)Form, (size_t)-1);
}

static VOID
OnControlCommand(CP_CONTROL *Control, UINT Code)
{
    HTML_ITEM *Item = Control->Item < Page.ItemCount ? &Page.Items[Control->Item] : NULL;
    size_t Index;

    if ((Control->Role == CpRoleCombo && Code == CBN_SETFOCUS) ||
        ((Control->Role == CpRoleEdit || Control->Role == CpRoleTextArea || Control->Role == CpRoleAddress) &&
         Code == EN_SETFOCUS) ||
        ((Control->Role == CpRoleLink || Control->Role == CpRoleCheck || Control->Role == CpRoleRadio ||
          Control->Role == CpRoleButton) && Code == BN_SETFOCUS))
    {
        EnsureVisible(Control->Hwnd);
        return;
    }
    if (!Item || Code != BN_CLICKED || Loading || Control->Role == CpRoleCombo || Control->Role == CpRoleEdit ||
        Control->Role == CpRoleTextArea)
    {
        return;
    }

    switch (Control->Role)
    {
        case CpRoleLink:
        {
            WCHAR Target[CP_URL_LENGTH];

            if (IsNavigableHref(Item->Href) && ResolveUrl(Item->Href, Target, ARRAYSIZE(Target)))
                Navigate(Target, NULL, 0, NULL);
            break;
        }

        case CpRoleRadio:
            for (Index = 0; Index < ControlCount; Index++)
            {
                CP_CONTROL *Other = &Controls[Index];
                HTML_ITEM *OtherItem;

                if (Other->Role != CpRoleRadio || Other->Item >= Page.ItemCount)
                    continue;
                OtherItem = &Page.Items[Other->Item];
                if (Other == Control || (OtherItem->Form == Item->Form && Item->Name && OtherItem->Name &&
                                         strcmp(OtherItem->Name, Item->Name) == 0))
                {
                    SendMessageW(Other->Hwnd, BM_SETCHECK, Other == Control ? BST_CHECKED : BST_UNCHECKED, 0);
                }
            }
            break;

        case CpRoleButton:
            if (HtmlIsSubmitItem(Item) && Item->Form >= 0)
                SubmitForm((size_t)Item->Form, Control->Item);
            break;

        default:
            break;
    }
}

static VOID
DrawLink(const DRAWITEMSTRUCT *Draw)
{
    WCHAR Text[1024];
    RECT Rect = Draw->rcItem;
    HGDIOBJ Old;
    BOOL Disabled = (Draw->itemState & ODS_DISABLED) != 0;

    FillRect(Draw->hDC, &Draw->rcItem, GetSysColorBrush(COLOR_WINDOW));
    GetWindowTextW(Draw->hwndItem, Text, ARRAYSIZE(Text));
    Old = SelectObject(Draw->hDC, LinkFont);
    SetBkMode(Draw->hDC, TRANSPARENT);
    SetTextColor(Draw->hDC, GetSysColor(Disabled ? COLOR_GRAYTEXT : COLOR_HOTLIGHT));
    InflateRect(&Rect, -Scale(2), -Scale(2));
    DrawTextW(Draw->hDC, Text, -1, &Rect, DT_WORDBREAK | DT_NOPREFIX | DT_EDITCONTROL);
    SelectObject(Draw->hDC, Old);
    if ((Draw->itemState & ODS_FOCUS) && !(Draw->itemState & ODS_NOFOCUSRECT))
        DrawFocusRect(Draw->hDC, &Draw->rcItem);
}

static LRESULT CALLBACK
PageProc(HWND Hwnd, UINT Message, WPARAM WParam, LPARAM LParam)
{
    switch (Message)
    {
        case WM_COMMAND:
        {
            CP_CONTROL *Control = ControlFromId(LOWORD(WParam));

            if (Control && (HWND)LParam == Control->Hwnd)
                OnControlCommand(Control, HIWORD(WParam));
            return 0;
        }

        case WM_DRAWITEM:
            DrawLink((const DRAWITEMSTRUCT *)LParam);
            return TRUE;

        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORBTN:
        {
            HDC Hdc = (HDC)WParam;
            CP_CONTROL *Control = ControlFromId(GetDlgCtrlID((HWND)LParam));

            if (Control && Control->Role == CpRoleAddress)
                break;
            SetBkColor(Hdc, GetSysColor(COLOR_WINDOW));
            SetTextColor(Hdc, GetSysColor(COLOR_WINDOWTEXT));
            return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
        }

        case WM_SETCURSOR:
        {
            CP_CONTROL *Control = ControlFromId(GetDlgCtrlID((HWND)WParam));

            if (Control && Control->Role == CpRoleLink && IsWindowEnabled(Control->Hwnd))
            {
                SetCursor(LoadCursorW(NULL, IDC_HAND));
                return TRUE;
            }
            break;
        }

        case WM_VSCROLL:
        {
            SCROLLINFO Info;
            RECT Client;
            int Line = Scale(24);

            GetClientRect(Hwnd, &Client);
            ZeroMemory(&Info, sizeof(Info));
            Info.cbSize = sizeof(Info);
            Info.fMask = SIF_ALL;
            GetScrollInfo(Hwnd, SB_VERT, &Info);
            switch (LOWORD(WParam))
            {
                case SB_LINEUP: ScrollPageTo(ScrollPos - Line); break;
                case SB_LINEDOWN: ScrollPageTo(ScrollPos + Line); break;
                case SB_PAGEUP: ScrollPageTo(ScrollPos - Client.bottom); break;
                case SB_PAGEDOWN: ScrollPageTo(ScrollPos + Client.bottom); break;
                case SB_TOP: ScrollPageTo(0); break;
                case SB_BOTTOM: ScrollPageTo(ContentHeight); break;
                case SB_THUMBTRACK:
                case SB_THUMBPOSITION: ScrollPageTo(Info.nTrackPos); break;
            }
            return 0;
        }

        case WM_MOUSEWHEEL:
            ScrollPageTo(ScrollPos - MulDiv(GET_WHEEL_DELTA_WPARAM(WParam), Scale(48), WHEEL_DELTA));
            return 0;

        case WM_SIZE:
            if (Hwnd == PageWindow)
                LayoutPage();
            return 0;

        case WM_ERASEBKGND:
        {
            RECT Client;

            GetClientRect(Hwnd, &Client);
            FillRect((HDC)WParam, &Client, GetSysColorBrush(COLOR_WINDOW));
            return TRUE;
        }
    }
    return DefWindowProcW(Hwnd, Message, WParam, LParam);
}

static int
BarHeight(VOID)
{
    RECT Rect;

    GetWindowRect(ActionButton, &Rect);
    return Rect.bottom - Rect.top + Scale(24);
}

static VOID
LayoutMain(VOID)
{
    RECT Client;
    int Bar, ButtonWidth = Scale(96), ButtonHeight, Margin = Scale(12);
    HDC Hdc;
    TEXTMETRICW Metrics;

    GetClientRect(MainWindow, &Client);
    Hdc = GetDC(MainWindow);
    SelectObject(Hdc, TextFont);
    GetTextMetricsW(Hdc, &Metrics);
    ReleaseDC(MainWindow, Hdc);
    ButtonHeight = Metrics.tmHeight + Scale(12);
    Bar = ButtonHeight + 2 * Margin;

    MoveWindow(PageWindow, 0, 0, Client.right, max(Client.bottom - Bar - 1, 0), TRUE);
    MoveWindow(ActionButton, Client.right - Margin - ButtonWidth, Client.bottom - Margin - ButtonHeight, ButtonWidth,
               ButtonHeight, TRUE);
    MoveWindow(StatusWindow, Margin, Client.bottom - Bar + (Bar - Metrics.tmHeight) / 2,
               max(Client.right - 3 * Margin - ButtonWidth, 0), Metrics.tmHeight, TRUE);
}

static VOID
CreateFonts(VOID)
{
    NONCLIENTMETRICSW Metrics;
    LOGFONTW Font;
    static const int HeadingPercent[3] = { 160, 130, 110 };
    int Index;

    ZeroMemory(&Metrics, sizeof(Metrics));
    Metrics.cbSize = sizeof(Metrics);
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(Metrics), &Metrics, 0);
    Font = Metrics.lfMessageFont;
    TextFont = CreateFontIndirectW(&Font);
    Font.lfUnderline = TRUE;
    LinkFont = CreateFontIndirectW(&Font);
    for (Index = 0; Index < 3; Index++)
    {
        Font = Metrics.lfMessageFont;
        Font.lfHeight = MulDiv(Font.lfHeight, HeadingPercent[Index], 100);
        Font.lfWeight = FW_SEMIBOLD;
        HeadingFont[Index] = CreateFontIndirectW(&Font);
    }
}

static LRESULT CALLBACK
MainProc(HWND Hwnd, UINT Message, WPARAM WParam, LPARAM LParam)
{
    switch (Message)
    {
        case WM_CREATE:
        {
            WCHAR Text[CP_TEXT_LENGTH];

            MainWindow = Hwnd;
            PageWindow = CreateWindowExW(WS_EX_CONTROLPARENT, CP_PAGE_CLASS_NAME, NULL,
                                         WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_CLIPCHILDREN | WS_TABSTOP, 0, 0, 0, 0,
                                         Hwnd, NULL, Instance, NULL);
            StatusWindow = CreateWindowExW(0, WC_STATICW, NULL, WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX |
                                           SS_ENDELLIPSIS, 0, 0, 0, 0, Hwnd, (HMENU)CP_ID_STATUS, Instance, NULL);
            LoadText(IDS_CANCEL, Text, ARRAYSIZE(Text));
            ActionButton = CreateWindowExW(0, WC_BUTTONW, Text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0,
                                           0, 0, 0, Hwnd, (HMENU)CP_ID_ACTION, Instance, NULL);
            if (!PageWindow || !StatusWindow || !ActionButton)
                return -1;
            SendMessageW(StatusWindow, WM_SETFONT, (WPARAM)TextFont, FALSE);
            SendMessageW(ActionButton, WM_SETFONT, (WPARAM)TextFont, FALSE);
            UpdateStatus();
            return 0;
        }

        case WM_SIZE:
            LayoutMain();
            return 0;

        case WM_GETMINMAXINFO:
        {
            MINMAXINFO *Info = (MINMAXINFO *)LParam;

            Info->ptMinTrackSize.x = Scale(320);
            Info->ptMinTrackSize.y = Scale(240);
            return 0;
        }

        case WM_PAINT:
        {
            PAINTSTRUCT Paint;
            RECT Client, Line;
            HDC Hdc = BeginPaint(Hwnd, &Paint);

            GetClientRect(Hwnd, &Client);
            Line = Client;
            Line.top = Client.bottom - BarHeight() - 1;
            Line.bottom = Line.top + 1;
            FillRect(Hdc, &Line, GetSysColorBrush(COLOR_3DSHADOW));
            EndPaint(Hwnd, &Paint);
            return 0;
        }

        case WM_COMMAND:
            if (LOWORD(WParam) == CP_ID_ACTION || LOWORD(WParam) == IDCANCEL)
            {
                DestroyWindow(Hwnd);
                return 0;
            }
            if (LOWORD(WParam) == IDOK)
            {
                SubmitFocusedForm();
                return 0;
            }
            break;

        case WM_MOUSEWHEEL:
            return SendMessageW(PageWindow, Message, WParam, LParam);

        case WM_TIMER:
            if (WParam == CP_TIMER_PROBE)
            {
                StartProbe();
            }
            else if (WParam == CP_TIMER_REFRESH)
            {
                KillTimer(Hwnd, CP_TIMER_REFRESH);
                if (!Loading)
                    Navigate(RefreshUrl, NULL, 0, NULL);
            }
            return 0;

        case WM_CP_PAGE:
        {
            CP_RESPONSE *Response = (CP_RESPONSE *)LParam;

            if (Response->Sequence == NavigationSequence)
                ShowResponse(Response);
            free(Response->Body);
            free(Response);
            return 0;
        }

        case WM_CP_PROBE:
            if (WParam && !Online)
            {
                Online = TRUE;
                UpdateStatus();
            }
            else if (!WParam && Online)
            {
                Online = FALSE;
                UpdateStatus();
            }
            return 0;

        case WM_CTLCOLORSTATIC:
            SetBkMode((HDC)WParam, TRANSPARENT);
            return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);

        case WM_DESTROY:
            KillTimer(Hwnd, CP_TIMER_PROBE);
            KillTimer(Hwnd, CP_TIMER_REFRESH);
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(Hwnd, Message, WParam, LParam);
}

int WINAPI
wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nShowCmd)
{
    INITCOMMONCONTROLSEX Common = { sizeof(Common), ICC_STANDARD_CLASSES };
    WNDCLASSEXW Class;
    WCHAR Title[CP_TEXT_LENGTH], Format[CP_TEXT_LENGTH];
    LPWSTR *Arguments;
    HANDLE Mutex;
    HWND Existing;
    RECT Work;
    MSG Message;
    HDC Screen;
    int Count = 0, Width, Height;

    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    Instance = hInstance;
    Mutex = CreateMutexW(NULL, FALSE, CP_MUTEX_NAME);
    if (Mutex && GetLastError() == ERROR_ALREADY_EXISTS)
    {
        Existing = FindWindowW(CP_CLASS_NAME, NULL);
        if (Existing)
        {
            ShowWindow(Existing, SW_RESTORE);
            SetForegroundWindow(Existing);
        }
        CloseHandle(Mutex);
        return 0;
    }

    InitCommonControlsEx(&Common);
    Screen = GetDC(NULL);
    Dpi = GetDeviceCaps(Screen, LOGPIXELSY);
    ReleaseDC(NULL, Screen);
    CreateFonts();
    ReadProbeConfig();

    PageSession = WinHttpOpen(CP_USER_AGENT, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                              WINHTTP_NO_PROXY_BYPASS, 0);
    ProbeSession = WinHttpOpen(CP_USER_AGENT, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                               WINHTTP_NO_PROXY_BYPASS, 0);
    if (!PageSession || !ProbeSession)
        return 1;

    ZeroMemory(&Class, sizeof(Class));
    Class.cbSize = sizeof(Class);
    Class.lpfnWndProc = PageProc;
    Class.hInstance = hInstance;
    Class.hCursor = LoadCursorW(NULL, IDC_ARROW);
    Class.lpszClassName = CP_PAGE_CLASS_NAME;
    if (!RegisterClassExW(&Class))
        return 1;
    Class.lpfnWndProc = MainProc;
    Class.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
    Class.hIcon = LoadIconW(NULL, IDI_APPLICATION);
    Class.lpszClassName = CP_CLASS_NAME;
    if (!RegisterClassExW(&Class))
        return 1;

    Arguments = CommandLineToArgvW(GetCommandLineW(), &Count);
    if (Arguments && Count > 1 && Arguments[1][0])
    {
        LoadText(IDS_TITLE, Format, ARRAYSIZE(Format));
        StringCchPrintfW(Title, ARRAYSIZE(Title), Format, Arguments[1]);
    }
    else
    {
        LoadText(IDS_TITLE_DEFAULT, Title, ARRAYSIZE(Title));
    }
    if (Arguments)
        LocalFree(Arguments);

    SystemParametersInfoW(SPI_GETWORKAREA, 0, &Work, 0);
    Width = min(Scale(520), Work.right - Work.left);
    Height = min(Scale(640), Work.bottom - Work.top);
    if (!CreateWindowExW(WS_EX_CONTROLPARENT, CP_CLASS_NAME, Title, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                         Work.left + (Work.right - Work.left - Width) / 2, Work.top + (Work.bottom - Work.top - Height) / 2,
                         Width, Height, NULL, NULL, hInstance, NULL))
    {
        return 1;
    }
    ShowWindow(MainWindow, nShowCmd);
    UpdateWindow(MainWindow);

    Navigate(Probe.Url, NULL, 0, NULL);
    SetTimer(MainWindow, CP_TIMER_PROBE, CP_PROBE_INTERVAL, NULL);

    while (GetMessageW(&Message, NULL, 0, 0) > 0)
    {
        if (!IsDialogMessageW(MainWindow, &Message))
        {
            TranslateMessage(&Message);
            DispatchMessageW(&Message);
        }
    }

    ClearPage();
    free(Controls);
    WinHttpCloseHandle(PageSession);
    WinHttpCloseHandle(ProbeSession);
    if (Mutex)
        CloseHandle(Mutex);
    return (int)Message.wParam;
}
