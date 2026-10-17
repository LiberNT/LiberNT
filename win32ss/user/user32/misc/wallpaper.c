/*
 * PROJECT:     LiberNT user32
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Desktop wallpaper transcoding for image formats win32k cannot load
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <user32.h>
#include <objbase.h>
#include <gdiplus.h>
#include <strsafe.h>

typedef GpStatus (WINGDIPAPI *PGDIPLUS_STARTUP)(ULONG_PTR*, const struct GdiplusStartupInput*, struct GdiplusStartupOutput*);
typedef VOID (WINGDIPAPI *PGDIPLUS_SHUTDOWN)(ULONG_PTR);
typedef GpStatus (WINGDIPAPI *PGDIP_LOAD_IMAGE)(GDIPCONST WCHAR*, GpImage**);
typedef GpStatus (WINGDIPAPI *PGDIP_DISPOSE_IMAGE)(GpImage*);
typedef GpStatus (WINGDIPAPI *PGDIP_ENCODERS_SIZE)(UINT*, UINT*);
typedef GpStatus (WINGDIPAPI *PGDIP_ENCODERS)(UINT, UINT, ImageCodecInfo*);
typedef GpStatus (WINGDIPAPI *PGDIP_SAVE_IMAGE)(GpImage*, GDIPCONST WCHAR*, GDIPCONST CLSID*, GDIPCONST EncoderParameters*);

static const WCHAR DesktopKey[] = L"Control Panel\\Desktop";

static
BOOL
UserGetTranscodedWallpaperPath(
    _Out_writes_(MAX_PATH) PWSTR Path,
    _In_ BOOL Create)
{
    static const PCWSTR Parts[] = { L"\\Microsoft", L"\\Windows", L"\\Themes" };
    DWORD Length;
    ULONG Index;

    Length = GetEnvironmentVariableW(L"APPDATA", Path, MAX_PATH);
    if (Length == 0 || Length >= MAX_PATH)
        return FALSE;

    for (Index = 0; Index < ARRAYSIZE(Parts); Index++)
    {
        if (FAILED(StringCchCatW(Path, MAX_PATH, Parts[Index])))
            return FALSE;
        if (Create && !CreateDirectoryW(Path, NULL) && GetLastError() != ERROR_ALREADY_EXISTS)
            return FALSE;
    }

    return SUCCEEDED(StringCchCatW(Path, MAX_PATH, L"\\TranscodedWallpaper"));
}

static
BOOL
UserIsBitmapFile(
    _In_ PCWSTR Source,
    _Out_ PBOOL Readable)
{
    HANDLE File;
    WORD Signature = 0;
    DWORD Read = 0;

    *Readable = FALSE;
    File = CreateFileW(Source, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                       NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (File == INVALID_HANDLE_VALUE)
        return FALSE;

    *Readable = ReadFile(File, &Signature, sizeof(Signature), &Read, NULL) && Read == sizeof(Signature);
    CloseHandle(File);
    return *Readable && Signature == 0x4D42;
}

static
BOOL
UserGetBitmapEncoder(
    _In_ HMODULE Gdiplus,
    _Out_ CLSID* Encoder)
{
    PGDIP_ENCODERS_SIZE GetSize = (PGDIP_ENCODERS_SIZE)GetProcAddress(Gdiplus, "GdipGetImageEncodersSize");
    PGDIP_ENCODERS GetEncoders = (PGDIP_ENCODERS)GetProcAddress(Gdiplus, "GdipGetImageEncoders");
    ImageCodecInfo* Codecs;
    UINT Count = 0, Size = 0, Index;
    BOOL Found = FALSE;

    if (!GetSize || !GetEncoders || GetSize(&Count, &Size) != Ok || Size == 0)
        return FALSE;

    Codecs = HeapAlloc(GetProcessHeap(), 0, Size);
    if (!Codecs)
        return FALSE;

    if (GetEncoders(Count, Size, Codecs) == Ok)
    {
        for (Index = 0; Index < Count; Index++)
        {
            if (Codecs[Index].MimeType && lstrcmpiW(Codecs[Index].MimeType, L"image/bmp") == 0)
            {
                *Encoder = Codecs[Index].Clsid;
                Found = TRUE;
                break;
            }
        }
    }

    HeapFree(GetProcessHeap(), 0, Codecs);
    return Found;
}

static
VOID
UserCountTranscodedWallpaper(VOID)
{
    HKEY Key;
    DWORD Count = 0, Size = sizeof(Count), Type;

    if (RegOpenKeyExW(HKEY_CURRENT_USER, DesktopKey, 0, KEY_QUERY_VALUE | KEY_SET_VALUE, &Key) != ERROR_SUCCESS)
        return;
    if (RegQueryValueExW(Key, L"TranscodedImageCount", NULL, &Type, (PBYTE)&Count, &Size) != ERROR_SUCCESS ||
        Type != REG_DWORD)
    {
        Count = 0;
    }
    Count++;
    RegSetValueExW(Key, L"TranscodedImageCount", 0, REG_DWORD, (PBYTE)&Count, sizeof(Count));
    RegCloseKey(Key);
}

BOOL
UserTranscodeWallpaper(
    _In_ PCWSTR Source,
    _Out_writes_(MAX_PATH) PWSTR Transcoded)
{
    PGDIPLUS_STARTUP Startup;
    PGDIPLUS_SHUTDOWN Shutdown;
    PGDIP_LOAD_IMAGE LoadImage;
    PGDIP_DISPOSE_IMAGE DisposeImage;
    PGDIP_SAVE_IMAGE SaveImage;
    struct GdiplusStartupInput Input = { 1, NULL, FALSE, FALSE };
    ULONG_PTR Token = 0;
    GpImage* Image = NULL;
    HMODULE Gdiplus;
    CLSID Encoder;
    BOOL Readable, Saved = FALSE;

    if (!Source || !*Source || UserIsBitmapFile(Source, &Readable) || !Readable)
        return FALSE;
    if (!UserGetTranscodedWallpaperPath(Transcoded, TRUE) || lstrcmpiW(Source, Transcoded) == 0)
        return FALSE;

    Gdiplus = LoadLibraryW(L"gdiplus.dll");
    if (!Gdiplus)
        return FALSE;

    Startup = (PGDIPLUS_STARTUP)GetProcAddress(Gdiplus, "GdiplusStartup");
    Shutdown = (PGDIPLUS_SHUTDOWN)GetProcAddress(Gdiplus, "GdiplusShutdown");
    LoadImage = (PGDIP_LOAD_IMAGE)GetProcAddress(Gdiplus, "GdipLoadImageFromFile");
    DisposeImage = (PGDIP_DISPOSE_IMAGE)GetProcAddress(Gdiplus, "GdipDisposeImage");
    SaveImage = (PGDIP_SAVE_IMAGE)GetProcAddress(Gdiplus, "GdipSaveImageToFile");

    if (Startup && Shutdown && LoadImage && DisposeImage && SaveImage &&
        Startup(&Token, &Input, NULL) == Ok)
    {
        if (LoadImage(Source, &Image) == Ok)
        {
            Saved = UserGetBitmapEncoder(Gdiplus, &Encoder) &&
                    SaveImage(Image, Transcoded, &Encoder, NULL) == Ok;
            DisposeImage(Image);
        }
        Shutdown(Token);
    }

    FreeLibrary(Gdiplus);
    if (Saved)
        UserCountTranscodedWallpaper();
    return Saved;
}

VOID
UserStoreWallpaperSource(
    _In_ PCWSTR Source,
    _In_ UINT WinIni)
{
    HKEY Key;

    if (!(WinIni & SPIF_UPDATEINIFILE))
        return;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, DesktopKey, 0, KEY_SET_VALUE, &Key) != ERROR_SUCCESS)
        return;
    RegSetValueExW(Key, L"Wallpaper", 0, REG_SZ, (const BYTE*)Source,
                   (DWORD)(wcslen(Source) + 1) * sizeof(WCHAR));
    RegCloseKey(Key);
}

VOID
UserMapWallpaperSource(
    _Inout_updates_(Capacity) PWSTR Wallpaper,
    _In_ UINT Capacity)
{
    WCHAR Transcoded[MAX_PATH];
    WCHAR Source[MAX_PATH];
    DWORD Size = sizeof(Source), Type;
    HKEY Key;

    if (!Wallpaper || !Capacity || !UserGetTranscodedWallpaperPath(Transcoded, FALSE) ||
        lstrcmpiW(Wallpaper, Transcoded) != 0)
    {
        return;
    }
    if (RegOpenKeyExW(HKEY_CURRENT_USER, DesktopKey, 0, KEY_QUERY_VALUE, &Key) != ERROR_SUCCESS)
        return;
    if (RegQueryValueExW(Key, L"Wallpaper", NULL, &Type, (PBYTE)Source, &Size) == ERROR_SUCCESS &&
        (Type == REG_SZ || Type == REG_EXPAND_SZ) && Size >= sizeof(WCHAR))
    {
        Source[ARRAYSIZE(Source) - 1] = UNICODE_NULL;
        if (Source[0] && lstrcmpiW(Source, Transcoded) != 0)
            StringCchCopyW(Wallpaper, Capacity, Source);
    }
    RegCloseKey(Key);
}
