/*
 * PROJECT:     LiberNT Web Browser
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Minimal web browser on the LiberNT web view, installed as iexplore.exe
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <stdarg.h>
#include <stdlib.h>
#include <windef.h>
#include <winbase.h>
#include <winnls.h>
#include <wingdi.h>
#include <winuser.h>
#include <winhttp.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <strsafe.h>

#include <libernt/lntweb.h>

#include "browser.h"
#include "resource.h"

#define WB_CLASS_NAME L"LiberNTWebBrowser"
#define WB_USER_AGENT_TEXT "Mozilla/5.0 (Windows NT 10.0; Win64; x64) LiberNT-WebBrowser/1.0"
#define WB_WIDEN_(Text) L##Text
#define WB_WIDEN(Text) WB_WIDEN_(Text)
#define WB_USER_AGENT WB_WIDEN(WB_USER_AGENT_TEXT)
#define WB_URL_LENGTH 4096
#define WB_TEXT_LENGTH 512
#define WB_TITLE_LENGTH 1024
#define WB_MAX_BODY (16 * 1024 * 1024)
#define WB_READ_CHUNK (64 * 1024)
#define WB_HISTORY 64
#define WM_WB_PAGE (WM_APP + 1)
#define WB_ID_BACK 101
#define WB_ID_FORWARD 102
#define WB_ID_RELOAD 103
#define WB_ID_ADDRESS 104
#define WB_ID_VIEW 105
#define WB_ID_STATUS 106

typedef struct _WB_REQUEST
{
    LONG Sequence;
    BOOL Push;
    PWSTR Url;
    PWSTR Method;
    char *Body;
    DWORD BodyLength;
    PWSTR Headers;
} WB_REQUEST;

typedef struct _WB_RESPONSE
{
    LONG Sequence;
    BOOL Push;
    DWORD Error;
    DWORD Status;
    WCHAR Url[WB_URL_LENGTH];
    WCHAR ContentType[WB_TEXT_LENGTH];
    WCHAR Disposition[WB_TEXT_LENGTH];
    char *Body;
    SIZE_T Length;
    BOOL WantHeaders;
    char *Headers;
    BOOL Download;
    WCHAR TempFile[MAX_PATH];
} WB_RESPONSE;

static HINSTANCE Instance;
static HWND MainWindow;
static HWND AddressBox;
static HWND BackButton;
static HWND ForwardButton;
static HWND ReloadButton;
static HWND StatusText;
static HWND WebWindow;
static HFONT UiFont;
static PFN_LNTWEB_LOAD WebLoad;
static HINTERNET Session;
static LONG NavigationSequence;
static PWSTR History[WB_HISTORY];
static int HistoryCount;
static int HistoryIndex = -1;
static WCHAR CurrentUrl[WB_URL_LENGTH];
static int Dpi = 96;

static int
Scale(int Value)
{
    return MulDiv(Value, Dpi, 96);
}

static PWSTR
Utf8ToWide(const char *Text)
{
    int Length = MultiByteToWideChar(CP_UTF8, 0, Text, -1, NULL, 0);
    PWSTR Wide = Length > 0 ? malloc(Length * sizeof(WCHAR)) : NULL;

    if (Wide && !MultiByteToWideChar(CP_UTF8, 0, Text, -1, Wide, Length))
    {
        free(Wide);
        Wide = NULL;
    }
    return Wide;
}

static char *
WideToUtf8(PCWSTR Text)
{
    int Length = WideCharToMultiByte(CP_UTF8, 0, Text, -1, NULL, 0, NULL, NULL);
    char *Utf8 = Length > 0 ? malloc(Length) : NULL;

    if (Utf8 && !WideCharToMultiByte(CP_UTF8, 0, Text, -1, Utf8, Length, NULL, NULL))
    {
        free(Utf8);
        Utf8 = NULL;
    }
    return Utf8;
}

static char *
ToUtf8(const char *Body, SIZE_T Length, UINT CodePage, SIZE_T *OutLength)
{
    char *Result;
    PWSTR Wide;
    int WideLength, Utf8Length;

    if (Length > MAXINT)
        return NULL;
    if (!CodePage || !Length)
    {
        Result = malloc(Length + 1);
        if (Result)
        {
            memcpy(Result, Body, Length);
            Result[Length] = 0;
            *OutLength = Length;
        }
        return Result;
    }
    WideLength = MultiByteToWideChar(CodePage, 0, Body, (int)Length, NULL, 0);
    Wide = WideLength > 0 ? malloc(WideLength * sizeof(WCHAR)) : NULL;
    if (!Wide)
        return ToUtf8(Body, Length, 0, OutLength);
    MultiByteToWideChar(CodePage, 0, Body, (int)Length, Wide, WideLength);
    Utf8Length = WideCharToMultiByte(CP_UTF8, 0, Wide, WideLength, NULL, 0, NULL, NULL);
    Result = Utf8Length > 0 ? malloc((size_t)Utf8Length + 1) : NULL;
    if (Result)
    {
        WideCharToMultiByte(CP_UTF8, 0, Wide, WideLength, Result, Utf8Length, NULL, NULL);
        Result[Utf8Length] = 0;
        *OutLength = (SIZE_T)Utf8Length;
    }
    free(Wide);
    return Result;
}

static void
LoadText(UINT Id, PWSTR Buffer, int Size)
{
    if (!LoadStringW(Instance, Id, Buffer, Size))
        Buffer[0] = 0;
}

static void
SetStatus(UINT Id, PCWSTR Argument)
{
    WCHAR Format[WB_TEXT_LENGTH], Text[WB_TEXT_LENGTH + MAX_PATH];

    LoadText(Id, Format, ARRAYSIZE(Format));
    StringCchPrintfW(Text, ARRAYSIZE(Text), Format, Argument ? Argument : L"");
    SetWindowTextW(StatusText, Text);
}

static BOOL
QueryText(HINTERNET Request, DWORD Level, PWSTR Buffer, DWORD Characters)
{
    DWORD Size = Characters * sizeof(WCHAR);

    if (WinHttpQueryHeaders(Request, Level, WINHTTP_HEADER_NAME_BY_INDEX, Buffer, &Size, WINHTTP_NO_HEADER_INDEX))
        return TRUE;
    Buffer[0] = 0;
    return FALSE;
}

static BOOL
ShouldDownload(const WB_RESPONSE *Response)
{
    char ContentType[WB_TEXT_LENGTH];

    if (Response->Status < 200 || Response->Status >= 300)
        return FALSE;
    if (!_wcsnicmp(Response->Disposition, L"attachment", 10))
        return TRUE;
    if (!WideCharToMultiByte(CP_UTF8, 0, Response->ContentType, -1, ContentType, sizeof(ContentType), NULL, NULL))
        ContentType[0] = 0;
    return BrowserContentKind(ContentType) == BrowserContentDownload;
}

static DWORD
ReadToFile(HINTERNET Request, WB_RESPONSE *Response)
{
    WCHAR Folder[MAX_PATH];
    HANDLE File;
    DWORD Error = NO_ERROR;
    char *Buffer = malloc(WB_READ_CHUNK);

    if (!Buffer)
        return ERROR_NOT_ENOUGH_MEMORY;
    if (!GetTempPathW(ARRAYSIZE(Folder), Folder) || !GetTempFileNameW(Folder, L"web", 0, Response->TempFile))
    {
        free(Buffer);
        return GetLastError();
    }
    File = CreateFileW(Response->TempFile, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, NULL);
    if (File == INVALID_HANDLE_VALUE)
    {
        Error = GetLastError();
        DeleteFileW(Response->TempFile);
        Response->TempFile[0] = 0;
        free(Buffer);
        return Error;
    }
    for (;;)
    {
        DWORD Read = 0, Written = 0;
        if (!WinHttpReadData(Request, Buffer, WB_READ_CHUNK, &Read))
        {
            Error = GetLastError();
            break;
        }
        if (!Read)
            break;
        if (!WriteFile(File, Buffer, Read, &Written, NULL) || Written != Read)
        {
            Error = GetLastError();
            break;
        }
        Response->Length += Read;
    }
    CloseHandle(File);
    free(Buffer);
    if (Error != NO_ERROR)
    {
        DeleteFileW(Response->TempFile);
        Response->TempFile[0] = 0;
    }
    else
    {
        Response->Download = TRUE;
    }
    return Error;
}

static DWORD
ReadToMemory(HINTERNET Request, WB_RESPONSE *Response)
{
    for (;;)
    {
        DWORD Available = 0, Read = 0;
        char *Grown;

        if (!WinHttpQueryDataAvailable(Request, &Available))
            return GetLastError();
        if (!Available)
            return NO_ERROR;
        if (Available > WB_READ_CHUNK)
            Available = WB_READ_CHUNK;
        if (Response->Length + Available > WB_MAX_BODY)
            return NO_ERROR;
        Grown = realloc(Response->Body, Response->Length + Available + 1);
        if (!Grown)
            return ERROR_NOT_ENOUGH_MEMORY;
        Response->Body = Grown;
        if (!WinHttpReadData(Request, Response->Body + Response->Length, Available, &Read))
            return GetLastError();
        if (!Read)
            return NO_ERROR;
        Response->Length += Read;
        Response->Body[Response->Length] = 0;
    }
}

static DWORD
HttpFetch(PCWSTR Url, PCWSTR Method, const char *Body, DWORD BodyLength, PCWSTR Headers, BOOL AllowDownload,
          WB_RESPONSE *Response)
{
    URL_COMPONENTSW Parts;
    WCHAR Host[256];
    PWSTR Path;
    HINTERNET Connect = NULL, Request = NULL;
    DWORD Error = NO_ERROR, Size, Option, PathLength;
    PWSTR Hash;

    ZeroMemory(&Parts, sizeof(Parts));
    Parts.dwStructSize = sizeof(Parts);
    Parts.dwHostNameLength = (DWORD)-1;
    Parts.dwUrlPathLength = (DWORD)-1;
    Parts.dwExtraInfoLength = (DWORD)-1;
    if (!WinHttpCrackUrl(Url, 0, 0, &Parts) || !Parts.dwHostNameLength || Parts.dwHostNameLength >= ARRAYSIZE(Host) ||
        (Parts.nScheme != INTERNET_SCHEME_HTTP && Parts.nScheme != INTERNET_SCHEME_HTTPS))
    {
        return ERROR_WINHTTP_INVALID_URL;
    }
    CopyMemory(Host, Parts.lpszHostName, Parts.dwHostNameLength * sizeof(WCHAR));
    Host[Parts.dwHostNameLength] = 0;
    PathLength = Parts.dwUrlPathLength + Parts.dwExtraInfoLength;
    Path = malloc((PathLength + 2) * sizeof(WCHAR));
    if (!Path)
        return ERROR_NOT_ENOUGH_MEMORY;
    Path[0] = L'/';
    Path[1] = 0;
    if (PathLength)
    {
        PCWSTR Source = Parts.dwUrlPathLength ? Parts.lpszUrlPath : Parts.lpszExtraInfo;
        BOOL Rooted = Source[0] == L'/';
        CopyMemory(Path + (Rooted ? 0 : 1), Source, PathLength * sizeof(WCHAR));
        Path[PathLength + (Rooted ? 0 : 1)] = 0;
        Hash = wcschr(Path, L'#');
        if (Hash)
            *Hash = 0;
    }

    Connect = WinHttpConnect(Session, Host, Parts.nPort, 0);
    if (Connect)
    {
        Request = WinHttpOpenRequest(Connect, Method, Path, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                     Parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0);
    }
    free(Path);
    if (!Request)
    {
        Error = GetLastError();
        goto Cleanup;
    }
    Option = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
    WinHttpSetOption(Request, WINHTTP_OPTION_REDIRECT_POLICY, &Option, sizeof(Option));
    Option = WINHTTP_DECOMPRESSION_FLAG_ALL;
    WinHttpSetOption(Request, WINHTTP_OPTION_DECOMPRESSION, &Option, sizeof(Option));
    if (!WinHttpSendRequest(Request, Headers && Headers[0] ? Headers : WINHTTP_NO_ADDITIONAL_HEADERS,
                            Headers && Headers[0] ? (DWORD)-1 : 0, (LPVOID)Body, BodyLength, BodyLength, 0) ||
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
    QueryText(Request, WINHTTP_QUERY_CONTENT_TYPE, Response->ContentType, ARRAYSIZE(Response->ContentType));
    Size = sizeof(Response->Disposition);
    if (!WinHttpQueryHeaders(Request, WINHTTP_QUERY_CUSTOM, L"Content-Disposition", Response->Disposition, &Size,
                             WINHTTP_NO_HEADER_INDEX))
    {
        Response->Disposition[0] = 0;
    }
    Size = sizeof(Response->Url);
    if (!WinHttpQueryOption(Request, WINHTTP_OPTION_URL, Response->Url, &Size))
        StringCchCopyW(Response->Url, ARRAYSIZE(Response->Url), Url);
    if (Response->WantHeaders)
    {
        PWSTR Raw;
        Size = 0;
        WinHttpQueryHeaders(Request, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX, NULL, &Size,
                            WINHTTP_NO_HEADER_INDEX);
        Raw = Size ? malloc(Size + sizeof(WCHAR)) : NULL;
        if (Raw && WinHttpQueryHeaders(Request, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX, Raw, &Size,
                                       WINHTTP_NO_HEADER_INDEX))
        {
            Raw[Size / sizeof(WCHAR)] = 0;
            Response->Headers = WideToUtf8(Raw);
        }
        free(Raw);
    }
    Error = AllowDownload && ShouldDownload(Response) ? ReadToFile(Request, Response) : ReadToMemory(Request, Response);

Cleanup:
    if (Request)
        WinHttpCloseHandle(Request);
    if (Connect)
        WinHttpCloseHandle(Connect);
    return Error;
}

static void
FreeResponse(WB_RESPONSE *Response)
{
    if (!Response)
        return;
    if (Response->TempFile[0])
        DeleteFileW(Response->TempFile);
    free(Response->Body);
    free(Response->Headers);
    free(Response);
}

static DWORD WINAPI
NavigateThread(LPVOID Context)
{
    WB_REQUEST *Request = Context;
    WB_RESPONSE *Response = calloc(1, sizeof(*Response));

    if (Response)
    {
        Response->Sequence = Request->Sequence;
        Response->Push = Request->Push;
        StringCchCopyW(Response->Url, ARRAYSIZE(Response->Url), Request->Url);
        Response->Error = HttpFetch(Request->Url, Request->Method, Request->Body, Request->BodyLength, Request->Headers,
                                    TRUE, Response);
        if (!PostMessageW(MainWindow, WM_WB_PAGE, 0, (LPARAM)Response))
            FreeResponse(Response);
    }
    free(Request->Url);
    free(Request->Method);
    free(Request->Body);
    free(Request->Headers);
    free(Request);
    return 0;
}

static void
UpdateButtons(void)
{
    EnableWindow(BackButton, HistoryIndex > 0);
    EnableWindow(ForwardButton, HistoryIndex >= 0 && HistoryIndex < HistoryCount - 1);
}

static void
PushHistory(PCWSTR Url)
{
    PWSTR Copy = _wcsdup(Url);

    if (!Copy)
        return;
    while (HistoryCount > HistoryIndex + 1)
        free(History[--HistoryCount]);
    if (HistoryCount == WB_HISTORY)
    {
        free(History[0]);
        MoveMemory(History, History + 1, (WB_HISTORY - 1) * sizeof(History[0]));
        HistoryCount--;
    }
    History[HistoryCount++] = Copy;
    HistoryIndex = HistoryCount - 1;
}

static void
SetPageTitle(const char *Title)
{
    WCHAR Application[WB_TEXT_LENGTH], Format[WB_TEXT_LENGTH], Text[WB_TITLE_LENGTH + WB_TEXT_LENGTH];
    PWSTR Wide = Title && Title[0] ? Utf8ToWide(Title) : NULL;

    LoadText(IDS_APP_TITLE, Application, ARRAYSIZE(Application));
    LoadText(IDS_TITLE_FORMAT, Format, ARRAYSIZE(Format));
    if (Wide)
        StringCchPrintfW(Text, ARRAYSIZE(Text), Format, Wide);
    else
        StringCchCopyW(Text, ARRAYSIZE(Text), Application);
    SetWindowTextW(MainWindow, Text);
    free(Wide);
}

static void
ShowPage(const char *Html, SIZE_T Length, PCWSTR Url)
{
    char *Utf8Url = WideToUtf8(Url);

    if (Utf8Url && WebLoad)
        WebLoad(WebWindow, Html, Length, Utf8Url);
    free(Utf8Url);
}

static void
ShowMessage(UINT HeadingId, PCWSTR Detail, PCWSTR Url)
{
    WCHAR Heading[WB_TEXT_LENGTH];
    char *Utf8Heading, *Utf8Detail, *Page;
    size_t PageLength = 0;

    LoadText(HeadingId, Heading, ARRAYSIZE(Heading));
    Utf8Heading = WideToUtf8(Heading);
    Utf8Detail = WideToUtf8(Detail ? Detail : L"");
    Page = Utf8Heading && Utf8Detail ? BrowserMessagePage(Utf8Heading, Utf8Detail, &PageLength) : NULL;
    if (Page)
        ShowPage(Page, PageLength, Url);
    SetPageTitle(Utf8Heading);
    free(Page);
    free(Utf8Heading);
    free(Utf8Detail);
}

static void
SaveDownload(WB_RESPONSE *Response)
{
    WCHAR Filter[WB_TEXT_LENGTH + 8], Target[MAX_PATH] = L"";
    char Disposition[WB_TEXT_LENGTH], *Url = WideToUtf8(Response->Url), Name[MAX_PATH];
    PWSTR WideName;
    OPENFILENAMEW Dialog;
    size_t Length;

    if (!WideCharToMultiByte(CP_UTF8, 0, Response->Disposition, -1, Disposition, sizeof(Disposition), NULL, NULL))
        Disposition[0] = 0;
    BrowserFileName(Disposition, Url, Name, sizeof(Name));
    free(Url);
    WideName = Utf8ToWide(Name);
    if (WideName)
        StringCchCopyW(Target, ARRAYSIZE(Target), WideName);
    free(WideName);

    LoadText(IDS_ALL_FILES, Filter, WB_TEXT_LENGTH);
    Length = wcslen(Filter);
    StringCchCopyW(Filter + Length + 1, ARRAYSIZE(Filter) - Length - 1, L"*.*");
    Filter[Length + 1 + 3 + 1] = 0;

    ZeroMemory(&Dialog, sizeof(Dialog));
    Dialog.lStructSize = sizeof(Dialog);
    Dialog.hwndOwner = MainWindow;
    Dialog.lpstrFilter = Filter;
    Dialog.lpstrFile = Target;
    Dialog.nMaxFile = ARRAYSIZE(Target);
    Dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetSaveFileNameW(&Dialog))
    {
        SetStatus(IDS_DONE, NULL);
        return;
    }
    if (MoveFileExW(Response->TempFile, Target, MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED))
    {
        Response->TempFile[0] = 0;
        SetStatus(IDS_SAVED, Target);
    }
    else
    {
        SetStatus(IDS_SAVE_FAILED, NULL);
    }
}

static void
ShowResponse(WB_RESPONSE *Response)
{
    char ContentType[WB_TEXT_LENGTH], Charset[64], Title[WB_TITLE_LENGTH], *Utf8Url;
    char *Page = NULL, *Text = NULL;
    size_t PageLength = 0;
    SIZE_T TextLength = 0;

    if (Response->Error != NO_ERROR)
    {
        ShowMessage(Response->Error == ERROR_WINHTTP_SECURE_FAILURE ? IDS_CERTIFICATE : IDS_LOAD_FAILED, Response->Url,
                    Response->Url);
        SetStatus(IDS_DONE, NULL);
        return;
    }
    if (Response->Download)
    {
        SetWindowTextW(AddressBox, CurrentUrl);
        SaveDownload(Response);
        return;
    }

    if (!WideCharToMultiByte(CP_UTF8, 0, Response->ContentType, -1, ContentType, sizeof(ContentType), NULL, NULL))
        ContentType[0] = 0;
    BrowserCharset(ContentType, Response->Body, Response->Length, Charset, sizeof(Charset));
    Title[0] = 0;
    switch (BrowserContentKind(ContentType))
    {
        case BrowserContentHtml:
            Text = ToUtf8(Response->Body ? Response->Body : "", Response->Length, BrowserCodePage(Charset), &TextLength);
            if (Text)
            {
                BrowserTitle(Text, TextLength, Title, sizeof(Title));
                ShowPage(Text, TextLength, Response->Url);
            }
            break;

        case BrowserContentText:
            Text = ToUtf8(Response->Body ? Response->Body : "", Response->Length, BrowserCodePage(Charset), &TextLength);
            Page = Text ? BrowserTextPage(Text, TextLength, &PageLength) : NULL;
            if (Page)
                ShowPage(Page, PageLength, Response->Url);
            break;

        case BrowserContentImage:
            Utf8Url = WideToUtf8(Response->Url);
            Page = Utf8Url ? BrowserImagePage(Utf8Url, &PageLength) : NULL;
            if (Page)
                ShowPage(Page, PageLength, Response->Url);
            free(Utf8Url);
            break;

        case BrowserContentDownload:
            ShowMessage(IDS_LOAD_FAILED, Response->Url, Response->Url);
            break;
    }
    free(Page);
    free(Text);

    StringCchCopyW(CurrentUrl, ARRAYSIZE(CurrentUrl), Response->Url);
    SetWindowTextW(AddressBox, CurrentUrl);
    if (Response->Push)
        PushHistory(CurrentUrl);
    else if (HistoryIndex >= 0 && HistoryIndex < HistoryCount && wcscmp(History[HistoryIndex], CurrentUrl))
    {
        PWSTR Copy = _wcsdup(CurrentUrl);
        if (Copy)
        {
            free(History[HistoryIndex]);
            History[HistoryIndex] = Copy;
        }
    }
    UpdateButtons();
    if (Title[0])
    {
        SetPageTitle(Title);
    }
    else
    {
        char *Utf8 = WideToUtf8(CurrentUrl);
        SetPageTitle(Utf8);
        free(Utf8);
    }
    SetStatus(IDS_DONE, NULL);
}

static void
Navigate(PCWSTR Url, const char *Body, SIZE_T BodyLength, PCWSTR ContentType, BOOL Push)
{
    WB_REQUEST *Request;
    HANDLE Thread;

    if (!_wcsicmp(Url, L"about:blank"))
    {
        InterlockedIncrement(&NavigationSequence);
        ShowPage("<!DOCTYPE html><html><body></body></html>", 41, L"about:blank");
        StringCchCopyW(CurrentUrl, ARRAYSIZE(CurrentUrl), L"about:blank");
        SetWindowTextW(AddressBox, L"");
        if (Push)
            PushHistory(CurrentUrl);
        UpdateButtons();
        SetPageTitle(NULL);
        SetStatus(IDS_DONE, NULL);
        return;
    }
    if (BodyLength > MAXDWORD)
        return;
    Request = calloc(1, sizeof(*Request));
    if (!Request)
        return;
    Request->Sequence = InterlockedIncrement(&NavigationSequence);
    Request->Push = Push;
    Request->Url = _wcsdup(Url);
    Request->Method = _wcsdup(Body ? L"POST" : L"GET");
    if (Body)
    {
        Request->Body = malloc(BodyLength + 1);
        if (Request->Body)
        {
            memcpy(Request->Body, Body, BodyLength);
            Request->BodyLength = (DWORD)BodyLength;
        }
    }
    if (Body && ContentType && ContentType[0])
    {
        size_t Length = wcslen(ContentType) + 20;
        Request->Headers = malloc(Length * sizeof(WCHAR));
        if (Request->Headers)
            StringCchPrintfW(Request->Headers, Length, L"Content-Type: %s\r\n", ContentType);
    }
    if (!Request->Url || !Request->Method || (Body && !Request->Body))
        goto Failed;

    SetWindowTextW(AddressBox, Url);
    SetStatus(IDS_LOADING, NULL);
    Thread = CreateThread(NULL, 0, NavigateThread, Request, 0, NULL);
    if (Thread)
    {
        CloseHandle(Thread);
        return;
    }

Failed:
    free(Request->Url);
    free(Request->Method);
    free(Request->Body);
    free(Request->Headers);
    free(Request);
}

static void
NavigateUtf8(const char *Url, BOOL Push)
{
    PWSTR Wide = Utf8ToWide(Url);

    if (Wide)
        Navigate(Wide, NULL, 0, NULL, Push);
    free(Wide);
}

static void
GoToAddress(void)
{
    int Length = GetWindowTextLengthW(AddressBox);
    PWSTR Text = malloc(((size_t)Length + 1) * sizeof(WCHAR));
    char *Utf8, Url[WB_URL_LENGTH];

    if (!Text)
        return;
    GetWindowTextW(AddressBox, Text, Length + 1);
    Utf8 = WideToUtf8(Text);
    if (Utf8 && BrowserNormalizeUrl(Utf8, Url, sizeof(Url)))
    {
        NavigateUtf8(Url, TRUE);
        SetFocus(WebWindow);
    }
    else
    {
        ShowMessage(IDS_UNSUPPORTED, Text, L"about:blank");
    }
    free(Utf8);
    free(Text);
}

static void
GoHistory(int Delta)
{
    int Target = HistoryIndex + Delta;

    if (Target < 0 || Target >= HistoryCount)
        return;
    HistoryIndex = Target;
    UpdateButtons();
    Navigate(History[HistoryIndex], NULL, 0, NULL, FALSE);
}

static void
Reload(void)
{
    if (CurrentUrl[0])
        Navigate(CurrentUrl, NULL, 0, NULL, FALSE);
}

static BOOL CALLBACK
HostFetch(PVOID Context, PCSTR Url, PSTR *Data, SIZE_T *Length)
{
    WB_RESPONSE *Response = calloc(1, sizeof(*Response));
    PWSTR Wide = Utf8ToWide(Url);
    BOOL Fetched = FALSE;

    UNREFERENCED_PARAMETER(Context);

    if (Response && Wide && HttpFetch(Wide, L"GET", NULL, 0, NULL, FALSE, Response) == NO_ERROR &&
        Response->Status >= 200 && Response->Status < 300 && Response->Body)
    {
        *Data = Response->Body;
        *Length = Response->Length;
        Response->Body = NULL;
        Fetched = TRUE;
    }
    FreeResponse(Response);
    free(Wide);
    return Fetched;
}

static BOOL CALLBACK
HostRequest(PVOID Context, PCSTR Method, PCSTR Url, PCSTR Headers, PCSTR Body, SIZE_T BodyLength, ULONG *Status,
            PSTR *ResponseHeaders, PSTR *Data, SIZE_T *Length)
{
    WB_RESPONSE *Response = calloc(1, sizeof(*Response));
    PWSTR WideUrl = Utf8ToWide(Url), WideMethod = Utf8ToWide(Method);
    PWSTR WideHeaders = Headers && Headers[0] ? Utf8ToWide(Headers) : NULL;
    BOOL Done = FALSE;

    UNREFERENCED_PARAMETER(Context);

    if (Response && WideUrl && WideMethod && WideMethod[0] && (!Headers || !Headers[0] || WideHeaders) &&
        BodyLength <= WB_MAX_BODY)
    {
        Response->WantHeaders = TRUE;
        if (HttpFetch(WideUrl, WideMethod, Body, (DWORD)BodyLength, WideHeaders, FALSE, Response) == NO_ERROR)
        {
            *Status = Response->Status;
            *ResponseHeaders = Response->Headers;
            *Data = Response->Body;
            *Length = Response->Length;
            Response->Headers = NULL;
            Response->Body = NULL;
            Done = TRUE;
        }
    }
    FreeResponse(Response);
    free(WideUrl);
    free(WideMethod);
    free(WideHeaders);
    return Done;
}

static VOID CALLBACK
HostFree(PVOID Context, PSTR Data)
{
    UNREFERENCED_PARAMETER(Context);
    free(Data);
}

static VOID CALLBACK
HostNavigate(PVOID Context, PCSTR Url, PCSTR Body, SIZE_T BodyLength, PCSTR ContentType)
{
    PWSTR WideUrl = Utf8ToWide(Url), WideType = ContentType ? Utf8ToWide(ContentType) : NULL;

    UNREFERENCED_PARAMETER(Context);

    if (WideUrl)
        Navigate(WideUrl, Body, BodyLength, WideType, TRUE);
    free(WideUrl);
    free(WideType);
}

static LRESULT CALLBACK
AddressProc(HWND Hwnd, UINT Message, WPARAM WParam, LPARAM LParam, UINT_PTR Id, DWORD_PTR Data)
{
    UNREFERENCED_PARAMETER(Id);
    UNREFERENCED_PARAMETER(Data);

    if (Message == WM_KEYDOWN && WParam == VK_RETURN)
    {
        GoToAddress();
        return 0;
    }
    if (Message == WM_CHAR && WParam == L'\r')
        return 0;
    if (Message == WM_GETDLGCODE)
        return DefSubclassProc(Hwnd, Message, WParam, LParam) | DLGC_WANTALLKEYS;
    return DefSubclassProc(Hwnd, Message, WParam, LParam);
}

static HWND
CreateChild(PCWSTR Class, UINT TextId, DWORD Style, DWORD ExStyle, int Id)
{
    WCHAR Text[WB_TEXT_LENGTH] = L"";
    HWND Child;

    if (TextId)
        LoadText(TextId, Text, ARRAYSIZE(Text));
    Child = CreateWindowExW(ExStyle, Class, Text, WS_CHILD | WS_VISIBLE | Style, 0, 0, 0, 0, MainWindow,
                            (HMENU)(INT_PTR)Id, Instance, NULL);
    if (Child)
        SendMessageW(Child, WM_SETFONT, (WPARAM)UiFont, FALSE);
    return Child;
}

static void
Layout(void)
{
    RECT Client;
    int Bar = Scale(34), Button = Scale(76), Gap = Scale(4), Status = Scale(22), X = Gap;

    GetClientRect(MainWindow, &Client);
    MoveWindow(BackButton, X, Gap, Button, Bar - 2 * Gap, TRUE);
    X += Button + Gap;
    MoveWindow(ForwardButton, X, Gap, Button, Bar - 2 * Gap, TRUE);
    X += Button + Gap;
    MoveWindow(ReloadButton, X, Gap, Button, Bar - 2 * Gap, TRUE);
    X += Button + Gap;
    MoveWindow(AddressBox, X, Gap, max(Client.right - X - Gap, 0), Bar - 2 * Gap, TRUE);
    if (WebWindow)
        MoveWindow(WebWindow, 0, Bar, Client.right, max(Client.bottom - Bar - Status, 0), TRUE);
    MoveWindow(StatusText, Gap * 2, Client.bottom - Status + Scale(3), max(Client.right - 4 * Gap, 0), Status - Scale(4),
               TRUE);
}

static LRESULT CALLBACK
MainProc(HWND Hwnd, UINT Message, WPARAM WParam, LPARAM LParam)
{
    switch (Message)
    {
        case WM_CREATE:
        {
            static const LNTWEB_HOST Host = { sizeof(LNTWEB_HOST), NULL,          HostFetch,
                                              HostFree,            HostNavigate,  HostRequest,
                                              WB_USER_AGENT_TEXT };
            HMODULE Web = LoadLibraryW(L"lntweb.dll");
            PFN_LNTWEB_CREATE CreateView = Web ? (PFN_LNTWEB_CREATE)GetProcAddress(Web, "LntWebCreate") : NULL;

            MainWindow = Hwnd;
            WebLoad = Web ? (PFN_LNTWEB_LOAD)GetProcAddress(Web, "LntWebLoad") : NULL;
            BackButton = CreateChild(WC_BUTTONW, IDS_BACK, WS_TABSTOP | BS_PUSHBUTTON | WS_DISABLED, 0, WB_ID_BACK);
            ForwardButton = CreateChild(WC_BUTTONW, IDS_FORWARD, WS_TABSTOP | BS_PUSHBUTTON | WS_DISABLED, 0,
                                        WB_ID_FORWARD);
            ReloadButton = CreateChild(WC_BUTTONW, IDS_RELOAD, WS_TABSTOP | BS_PUSHBUTTON, 0, WB_ID_RELOAD);
            AddressBox = CreateChild(WC_EDITW, 0, WS_TABSTOP | ES_AUTOHSCROLL, WS_EX_CLIENTEDGE, WB_ID_ADDRESS);
            StatusText = CreateChild(WC_STATICW, 0, SS_LEFT | SS_ENDELLIPSIS | SS_NOPREFIX, 0, WB_ID_STATUS);
            if (AddressBox)
                SetWindowSubclass(AddressBox, AddressProc, 1, 0);
            WebWindow = CreateView && WebLoad ? CreateView(Hwnd, WB_ID_VIEW, &Host) : NULL;
            if (!WebWindow || !BackButton || !ForwardButton || !ReloadButton || !AddressBox || !StatusText)
                return -1;
            Layout();
            return 0;
        }

        case WM_SIZE:
            Layout();
            return 0;

        case WM_SETFOCUS:
            if (WebWindow)
                SetFocus(WebWindow);
            return 0;

        case WM_COMMAND:
            switch (LOWORD(WParam))
            {
                case WB_ID_BACK: GoHistory(-1); return 0;
                case WB_ID_FORWARD: GoHistory(1); return 0;
                case WB_ID_RELOAD: Reload(); return 0;
            }
            break;

        case WM_WB_PAGE:
        {
            WB_RESPONSE *Response = (WB_RESPONSE *)LParam;
            if (Response->Sequence == NavigationSequence)
                ShowResponse(Response);
            FreeResponse(Response);
            return 0;
        }

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(Hwnd, Message, WParam, LParam);
}

static BOOL
HandleShortcut(const MSG *Message)
{
    BOOL Control = GetKeyState(VK_CONTROL) < 0;

    if (Message->message == WM_KEYDOWN && Message->wParam == VK_F5)
    {
        Reload();
        return TRUE;
    }
    if (Message->message == WM_SYSKEYDOWN && (Message->wParam == VK_LEFT || Message->wParam == VK_RIGHT))
    {
        GoHistory(Message->wParam == VK_LEFT ? -1 : 1);
        return TRUE;
    }
    if ((Message->message == WM_KEYDOWN && (Message->wParam == VK_F6 || (Control && Message->wParam == 'L'))) ||
        (Message->message == WM_SYSKEYDOWN && Message->wParam == 'D'))
    {
        SetFocus(AddressBox);
        SendMessageW(AddressBox, EM_SETSEL, 0, -1);
        return TRUE;
    }
    return FALSE;
}

int WINAPI
wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nShowCmd)
{
    INITCOMMONCONTROLSEX Controls = { sizeof(Controls), ICC_STANDARD_CLASSES };
    NONCLIENTMETRICSW Metrics = { sizeof(Metrics) };
    WNDCLASSEXW Class = { sizeof(Class) };
    WCHAR Title[WB_TEXT_LENGTH];
    RECT Work = { 0, 0, 1024, 720 };
    int Width, Height;
    PWSTR *Arguments, Url = NULL;
    char *Utf8, Normalized[WB_URL_LENGTH];
    int Count = 0;
    HDC Screen;
    MSG Message;

    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    Instance = hInstance;
    InitCommonControlsEx(&Controls);
    Screen = GetDC(NULL);
    if (Screen)
    {
        Dpi = GetDeviceCaps(Screen, LOGPIXELSY);
        ReleaseDC(NULL, Screen);
    }
    if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(Metrics), &Metrics, 0))
        UiFont = CreateFontIndirectW(&Metrics.lfMessageFont);

    Session = WinHttpOpen(WB_USER_AGENT, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                          WINHTTP_NO_PROXY_BYPASS, 0);
    if (Session)
        WinHttpSetTimeouts(Session, 0, 30000, 30000, 60000);

    Class.lpfnWndProc = MainProc;
    Class.hInstance = hInstance;
    Class.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    Class.hIcon = LoadIconW(NULL, (LPCWSTR)IDI_APPLICATION);
    Class.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    Class.lpszClassName = WB_CLASS_NAME;
    LoadText(IDS_APP_TITLE, Title, ARRAYSIZE(Title));
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &Work, 0);
    Width = min(Scale(1024), (int)(Work.right - Work.left));
    Height = min(Scale(720), (int)(Work.bottom - Work.top));
    if (!Session || !RegisterClassExW(&Class) ||
        !CreateWindowExW(0, WB_CLASS_NAME, Title, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                         Work.left + (Work.right - Work.left - Width) / 2, Work.top + (Work.bottom - Work.top - Height) / 2,
                         Width, Height, NULL, NULL, hInstance, NULL))
    {
        WCHAR Text[WB_TEXT_LENGTH];
        LoadText(IDS_NO_VIEW, Text, ARRAYSIZE(Text));
        MessageBoxW(NULL, Text, Title, MB_ICONERROR);
        return 1;
    }

    Arguments = CommandLineToArgvW(GetCommandLineW(), &Count);
    for (int Index = 1; Arguments && Index < Count; Index++)
    {
        if (Arguments[Index][0] != L'-' && Arguments[Index][0] != L'/')
        {
            Url = Arguments[Index];
            break;
        }
    }
    Utf8 = Url ? WideToUtf8(Url) : NULL;
    ShowWindow(MainWindow, nShowCmd);
    UpdateWindow(MainWindow);
    if (Utf8 && BrowserNormalizeUrl(Utf8, Normalized, sizeof(Normalized)))
    {
        NavigateUtf8(Normalized, TRUE);
    }
    else
    {
        Navigate(L"about:blank", NULL, 0, NULL, TRUE);
        if (Url)
            ShowMessage(IDS_UNSUPPORTED, Url, L"about:blank");
        SetFocus(AddressBox);
    }
    free(Utf8);
    LocalFree(Arguments);

    while (GetMessageW(&Message, NULL, 0, 0) > 0)
    {
        if (HandleShortcut(&Message) || IsDialogMessageW(MainWindow, &Message))
            continue;
        TranslateMessage(&Message);
        DispatchMessageW(&Message);
    }
    WinHttpCloseHandle(Session);
    if (UiFont)
        DeleteObject(UiFont);
    return (int)Message.wParam;
}
