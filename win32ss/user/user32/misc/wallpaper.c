/*
 * PROJECT:     LiberNT user32
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Desktop wallpaper decoding for image formats win32k cannot load
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <user32.h>
#include <objbase.h>
#include <gdiplus.h>
#include <strsafe.h>

typedef GpStatus (WINGDIPAPI *PGDIPLUS_STARTUP)(ULONG_PTR*, const struct GdiplusStartupInput*, struct GdiplusStartupOutput*);
typedef VOID (WINGDIPAPI *PGDIPLUS_SHUTDOWN)(ULONG_PTR);
typedef GpStatus (WINGDIPAPI *PGDIP_CREATE_BITMAP_FROM_FILE)(GDIPCONST WCHAR*, GpBitmap**);
typedef GpStatus (WINGDIPAPI *PGDIP_DISPOSE_IMAGE)(GpImage*);
typedef GpStatus (WINGDIPAPI *PGDIP_GET_IMAGE_SIZE)(GpImage*, UINT*);
typedef GpStatus (WINGDIPAPI *PGDIP_LOCK_BITS)(GpBitmap*, GDIPCONST GpRect*, UINT, PixelFormat, BitmapData*);
typedef GpStatus (WINGDIPAPI *PGDIP_UNLOCK_BITS)(GpBitmap*, BitmapData*);
typedef GpStatus (WINGDIPAPI *PGDIP_GET_RAW_FORMAT)(GpImage*, GUID*);
typedef GpStatus (WINGDIPAPI *PGDIP_ENCODERS_SIZE)(UINT*, UINT*);
typedef GpStatus (WINGDIPAPI *PGDIP_ENCODERS)(UINT, UINT, ImageCodecInfo*);
typedef GpStatus (WINGDIPAPI *PGDIP_SAVE_IMAGE)(GpImage*, GDIPCONST WCHAR*, GDIPCONST CLSID*, GDIPCONST EncoderParameters*);

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
HBITMAP
UserCreateWallpaperBitmap(
    _In_ const BitmapData* Data)
{
    BITMAPINFO Info = { { 0 } };
    HBITMAP Bitmap;
    HDC Dc;

    Info.bmiHeader.biSize = sizeof(Info.bmiHeader);
    Info.bmiHeader.biWidth = Data->Width;
    Info.bmiHeader.biHeight = -(LONG)Data->Height;
    Info.bmiHeader.biPlanes = 1;
    Info.bmiHeader.biBitCount = 32;
    Info.bmiHeader.biCompression = BI_RGB;

    Dc = GetDC(NULL);
    if (!Dc)
        return NULL;
    Bitmap = CreateDIBitmap(Dc, &Info.bmiHeader, CBM_INIT, Data->Scan0, &Info, DIB_RGB_COLORS);
    ReleaseDC(NULL, Dc);
    return Bitmap;
}

static
VOID
UserSaveTranscodedWallpaper(
    _In_ HMODULE Gdiplus,
    _In_ GpImage* Image,
    _In_ PCWSTR Transcoded)
{
    PGDIP_GET_RAW_FORMAT GetRawFormat = (PGDIP_GET_RAW_FORMAT)GetProcAddress(Gdiplus, "GdipGetImageRawFormat");
    PGDIP_ENCODERS_SIZE GetSize = (PGDIP_ENCODERS_SIZE)GetProcAddress(Gdiplus, "GdipGetImageEncodersSize");
    PGDIP_ENCODERS GetEncoders = (PGDIP_ENCODERS)GetProcAddress(Gdiplus, "GdipGetImageEncoders");
    PGDIP_SAVE_IMAGE SaveImage = (PGDIP_SAVE_IMAGE)GetProcAddress(Gdiplus, "GdipSaveImageToFile");
    ImageCodecInfo* Codecs;
    UINT Count = 0, Size = 0, Index;
    GUID Format;

    if (!GetRawFormat || !GetSize || !GetEncoders || !SaveImage ||
        GetRawFormat(Image, &Format) != Ok || GetSize(&Count, &Size) != Ok || Size == 0)
    {
        return;
    }

    Codecs = HeapAlloc(GetProcessHeap(), 0, Size);
    if (!Codecs)
        return;

    if (GetEncoders(Count, Size, Codecs) == Ok)
    {
        for (Index = 0; Index < Count; Index++)
        {
            if (IsEqualGUID(&Codecs[Index].FormatID, &Format))
            {
                SetFileAttributesW(Transcoded, FILE_ATTRIBUTE_NORMAL);
                SaveImage(Image, Transcoded, &Codecs[Index].Clsid, NULL);
                break;
            }
        }
    }

    HeapFree(GetProcessHeap(), 0, Codecs);
}

HBITMAP
UserLoadWallpaperBitmap(
    _In_ PCWSTR Source,
    _In_ UINT WinIni)
{
    PGDIPLUS_STARTUP Startup;
    PGDIPLUS_SHUTDOWN Shutdown;
    PGDIP_CREATE_BITMAP_FROM_FILE CreateBitmap;
    PGDIP_DISPOSE_IMAGE DisposeImage;
    PGDIP_GET_IMAGE_SIZE GetWidth, GetHeight;
    PGDIP_LOCK_BITS LockBits;
    PGDIP_UNLOCK_BITS UnlockBits;
    struct GdiplusStartupInput Input = { 1, NULL, FALSE, FALSE };
    WCHAR Transcoded[MAX_PATH];
    ULONG_PTR Token = 0;
    GpBitmap* Image = NULL;
    BitmapData Data;
    GpRect Rect;
    HMODULE Gdiplus;
    HBITMAP Bitmap = NULL;
    UINT Width, Height;
    BOOL Readable;

    if (!*Source || UserIsBitmapFile(Source, &Readable) || !Readable)
        return NULL;

    Gdiplus = LoadLibraryW(L"gdiplus.dll");
    if (!Gdiplus)
        return NULL;

    Startup = (PGDIPLUS_STARTUP)GetProcAddress(Gdiplus, "GdiplusStartup");
    Shutdown = (PGDIPLUS_SHUTDOWN)GetProcAddress(Gdiplus, "GdiplusShutdown");
    CreateBitmap = (PGDIP_CREATE_BITMAP_FROM_FILE)GetProcAddress(Gdiplus, "GdipCreateBitmapFromFile");
    DisposeImage = (PGDIP_DISPOSE_IMAGE)GetProcAddress(Gdiplus, "GdipDisposeImage");
    GetWidth = (PGDIP_GET_IMAGE_SIZE)GetProcAddress(Gdiplus, "GdipGetImageWidth");
    GetHeight = (PGDIP_GET_IMAGE_SIZE)GetProcAddress(Gdiplus, "GdipGetImageHeight");
    LockBits = (PGDIP_LOCK_BITS)GetProcAddress(Gdiplus, "GdipBitmapLockBits");
    UnlockBits = (PGDIP_UNLOCK_BITS)GetProcAddress(Gdiplus, "GdipBitmapUnlockBits");

    if (Startup && Shutdown && CreateBitmap && DisposeImage && GetWidth && GetHeight && LockBits && UnlockBits &&
        Startup(&Token, &Input, NULL) == Ok)
    {
        if (CreateBitmap(Source, &Image) == Ok)
        {
            if (GetWidth((GpImage*)Image, &Width) == Ok && GetHeight((GpImage*)Image, &Height) == Ok &&
                Width && Height && Width <= MAXLONG / 4 && Height <= MAXLONG)
            {
                Rect.X = 0;
                Rect.Y = 0;
                Rect.Width = Width;
                Rect.Height = Height;
                if (LockBits(Image, &Rect, ImageLockModeRead, PixelFormat32bppRGB, &Data) == Ok)
                {
                    if (Data.Stride == (INT)(Width * 4))
                        Bitmap = UserCreateWallpaperBitmap(&Data);
                    UnlockBits(Image, &Data);
                }
            }
            if (Bitmap && (WinIni & SPIF_UPDATEINIFILE) && UserGetTranscodedWallpaperPath(Transcoded, TRUE) &&
                lstrcmpiW(Source, Transcoded) != 0)
            {
                UserSaveTranscodedWallpaper(Gdiplus, (GpImage*)Image, Transcoded);
            }
            DisposeImage((GpImage*)Image);
        }
        Shutdown(Token);
    }

    FreeLibrary(Gdiplus);
    return Bitmap;
}
