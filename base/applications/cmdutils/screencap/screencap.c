/*
 * PROJECT:     LiberNT test tools
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Capture the desktop to a BMP file or an HTTP PUT URL
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <windows.h>
#include <winhttp.h>
#include <stdio.h>
#include <wchar.h>

static BYTE *
CaptureDesktop(DWORD *Size)
{
    int Width = GetSystemMetrics(SM_CXSCREEN);
    int Height = GetSystemMetrics(SM_CYSCREEN);
    DWORD Pixels = (DWORD)Width * (DWORD)Height * 4;
    DWORD Total = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + Pixels;
    BITMAPFILEHEADER *File;
    BITMAPINFOHEADER *Info;
    BITMAPINFO Format;
    HDC Screen, Memory;
    HBITMAP Bitmap;
    void *Bits;
    BYTE *Buffer;

    Buffer = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, Total);
    if (!Buffer)
        return NULL;

    ZeroMemory(&Format, sizeof(Format));
    Format.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    Format.bmiHeader.biWidth = Width;
    Format.bmiHeader.biHeight = Height;
    Format.bmiHeader.biPlanes = 1;
    Format.bmiHeader.biBitCount = 32;
    Format.bmiHeader.biCompression = BI_RGB;

    Screen = GetDC(NULL);
    Memory = CreateCompatibleDC(Screen);
    Bitmap = CreateDIBSection(Screen, &Format, DIB_RGB_COLORS, &Bits, NULL, 0);
    if (!Memory || !Bitmap)
    {
        HeapFree(GetProcessHeap(), 0, Buffer);
        Buffer = NULL;
        goto Cleanup;
    }
    SelectObject(Memory, Bitmap);
    BitBlt(Memory, 0, 0, Width, Height, Screen, 0, 0, SRCCOPY | CAPTUREBLT);
    GdiFlush();

    File = (BITMAPFILEHEADER *)Buffer;
    Info = (BITMAPINFOHEADER *)(File + 1);
    File->bfType = 0x4D42;
    File->bfSize = Total;
    File->bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    *Info = Format.bmiHeader;
    Info->biSizeImage = Pixels;
    CopyMemory(Info + 1, Bits, Pixels);
    *Size = Total;

Cleanup:
    if (Bitmap)
        DeleteObject(Bitmap);
    if (Memory)
        DeleteDC(Memory);
    ReleaseDC(NULL, Screen);
    return Buffer;
}

static BOOL
PutUrl(const WCHAR *Url, const BYTE *Data, DWORD Size)
{
    WCHAR Host[256], Path[1024];
    URL_COMPONENTS Parts;
    HINTERNET Session = NULL, Connection = NULL, Request = NULL;
    DWORD Status = 0, StatusSize = sizeof(Status);
    BOOL Result = FALSE;

    ZeroMemory(&Parts, sizeof(Parts));
    Parts.dwStructSize = sizeof(Parts);
    Parts.lpszHostName = Host;
    Parts.dwHostNameLength = ARRAYSIZE(Host);
    Parts.lpszUrlPath = Path;
    Parts.dwUrlPathLength = ARRAYSIZE(Path);
    if (!WinHttpCrackUrl(Url, 0, 0, &Parts))
        return FALSE;

    Session = WinHttpOpen(L"screencap", WINHTTP_ACCESS_TYPE_NO_PROXY, WINHTTP_NO_PROXY_NAME,
                          WINHTTP_NO_PROXY_BYPASS, 0);
    if (Session)
        Connection = WinHttpConnect(Session, Host, Parts.nPort, 0);
    if (Connection)
        Request = WinHttpOpenRequest(Connection, L"PUT", Path, NULL, WINHTTP_NO_REFERER,
                                     WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
    if (Request &&
        WinHttpSendRequest(Request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, (LPVOID)Data, Size, Size, 0) &&
        WinHttpReceiveResponse(Request, NULL) &&
        WinHttpQueryHeaders(Request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &Status, &StatusSize, WINHTTP_NO_HEADER_INDEX))
    {
        Result = Status >= 200 && Status < 300;
    }

    if (Request)
        WinHttpCloseHandle(Request);
    if (Connection)
        WinHttpCloseHandle(Connection);
    if (Session)
        WinHttpCloseHandle(Session);
    return Result;
}

typedef struct _PLACE_SEARCH
{
    const WCHAR *Title;
    HWND Window;
} PLACE_SEARCH;

static BOOL CALLBACK
FindTitle(HWND Window, LPARAM Parameter)
{
    PLACE_SEARCH *Search = (PLACE_SEARCH *)Parameter;
    WCHAR Text[512];

    if (Window == GetConsoleWindow() || !IsWindowVisible(Window) || GetWindow(Window, GW_OWNER) ||
        !GetWindowTextW(Window, Text, ARRAYSIZE(Text)))
    {
        return TRUE;
    }
    _wcslwr(Text);
    OutputDebugStringW(L"SCREENCAP step title ");
    OutputDebugStringW(Text);
    OutputDebugStringW(L"\n");
    if (wcsstr(Text, Search->Title))
    {
        Search->Window = Window;
        return FALSE;
    }
    return TRUE;
}

static BOOL
PlaceWindow(const WCHAR *Title, int X, int Y, int Width, int Height)
{
    WCHAR Lower[256];
    PLACE_SEARCH Search;

    wcsncpy(Lower, Title, ARRAYSIZE(Lower) - 1);
    Lower[ARRAYSIZE(Lower) - 1] = 0;
    _wcslwr(Lower);
    Search.Title = Lower;
    Search.Window = NULL;
    OutputDebugStringW(L"SCREENCAP step enum\n");
    EnumWindows(FindTitle, (LPARAM)&Search);
    if (!Search.Window)
    {
        wprintf(L"SCREENCAP no window titled %ls\n", Title);
        return FALSE;
    }
    OutputDebugStringW(L"SCREENCAP step show\n");
    ShowWindowAsync(Search.Window, SW_RESTORE);
    OutputDebugStringW(L"SCREENCAP step pos\n");
    SetWindowPos(Search.Window, HWND_TOP, X, Y, Width, Height, SWP_SHOWWINDOW | SWP_ASYNCWINDOWPOS);
    OutputDebugStringW(L"SCREENCAP step placed\n");
    return TRUE;
}

int
wmain(int argc, WCHAR **argv)
{
    DWORD Size = 0, Written = 0, Delay = 0;
    const WCHAR *Target = NULL;
    BYTE *Data = NULL;
    HANDLE File;
    BOOL Result;
    int Index;

    SetProcessDPIAware();
    for (Index = 1; Index < argc; Index++)
    {
        if (!wcscmp(argv[Index], L"--hide-console"))
        {
            if (GetConsoleWindow())
                ShowWindow(GetConsoleWindow(), SW_MINIMIZE);
        }
        else if (!wcscmp(argv[Index], L"--place") && Index + 5 < argc)
        {
            PlaceWindow(argv[Index + 1], _wtoi(argv[Index + 2]), _wtoi(argv[Index + 3]),
                        _wtoi(argv[Index + 4]), _wtoi(argv[Index + 5]));
            Index += 5;
        }
        else if (!wcscmp(argv[Index], L"--delay") && Index + 1 < argc)
        {
            Delay = (DWORD)_wtoi(argv[++Index]);
        }
        else if (!wcscmp(argv[Index], L"--put-file") && Index + 2 < argc)
        {
            LARGE_INTEGER FileSize;

            File = CreateFileW(argv[Index + 1], GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                               NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
            Result = File != INVALID_HANDLE_VALUE && GetFileSizeEx(File, &FileSize) && FileSize.HighPart == 0 &&
                     (Data = HeapAlloc(GetProcessHeap(), 0, FileSize.LowPart ? FileSize.LowPart : 1)) != NULL &&
                     ReadFile(File, Data, FileSize.LowPart, &Size, NULL) && PutUrl(argv[Index + 2], Data, Size);
            if (File != INVALID_HANDLE_VALUE)
                CloseHandle(File);
            wprintf(L"SCREENCAP %ls %lu bytes %ls\n", Result ? L"OK" : L"FAILED", Size, argv[Index + 2]);
            if (Data)
                HeapFree(GetProcessHeap(), 0, Data);
            return Result ? 0 : 1;
        }
        else
        {
            Target = argv[Index];
        }
    }
    if (!Target)
    {
        fwprintf(stderr, L"usage: screencap [--hide-console] [--place TITLE X Y W H]... [--delay MS] <file.bmp | http://host:port/name.bmp>\n");
        return 2;
    }
    if (Delay)
        Sleep(Delay);

    Data = CaptureDesktop(&Size);
    if (!Data)
        return 1;

    if (!_wcsnicmp(Target, L"http://", 7))
    {
        Result = PutUrl(Target, Data, Size);
    }
    else
    {
        File = CreateFileW(Target, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        Result = File != INVALID_HANDLE_VALUE && WriteFile(File, Data, Size, &Written, NULL) && Written == Size;
        if (File != INVALID_HANDLE_VALUE)
            CloseHandle(File);
    }

    wprintf(L"SCREENCAP %ls %lu bytes %ls\n", Result ? L"OK" : L"FAILED", Size, Target);
    HeapFree(GetProcessHeap(), 0, Data);
    return Result ? 0 : 1;
}
