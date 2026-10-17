/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for SPI_SETDESKWALLPAPER and the transcoded wallpaper cache
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"
#include <winreg.h>
#include <objbase.h>
#include <gdiplus.h>

static const WCHAR DesktopKey[] = L"Control Panel\\Desktop";

static
BOOL
SaveImage(
    _In_ GpBitmap* Bitmap,
    _In_ PCWSTR MimeType,
    _In_ PCWSTR Path)
{
    ImageCodecInfo* Codecs;
    UINT Count = 0, Size = 0, Index;
    BOOL Saved = FALSE;

    if (GdipGetImageEncodersSize(&Count, &Size) != Ok || Size == 0)
        return FALSE;
    Codecs = HeapAlloc(GetProcessHeap(), 0, Size);
    if (!Codecs)
        return FALSE;
    if (GdipGetImageEncoders(Count, Size, Codecs) == Ok)
    {
        for (Index = 0; Index < Count; Index++)
        {
            if (lstrcmpiW(Codecs[Index].MimeType, MimeType) == 0)
            {
                Saved = GdipSaveImageToFile((GpImage*)Bitmap, Path, &Codecs[Index].Clsid, NULL) == Ok;
                break;
            }
        }
    }
    HeapFree(GetProcessHeap(), 0, Codecs);
    return Saved;
}

static
BOOL
CreateImages(
    _In_ PCWSTR Png,
    _In_ PCWSTR Jpg,
    _In_ PCWSTR Bmp)
{
    struct GdiplusStartupInput Input = { 1, NULL, FALSE, FALSE };
    ULONG_PTR Token;
    GpBitmap* Bitmap = NULL;
    GpGraphics* Graphics = NULL;
    BOOL Result = FALSE;

    if (GdiplusStartup(&Token, &Input, NULL) != Ok)
        return FALSE;
    if (GdipCreateBitmapFromScan0(64, 48, 0, PixelFormat32bppRGB, NULL, &Bitmap) == Ok &&
        GdipGetImageGraphicsContext((GpImage*)Bitmap, &Graphics) == Ok)
    {
        GdipGraphicsClear(Graphics, 0xFFFF0000);
        GdipDeleteGraphics(Graphics);
        Result = SaveImage(Bitmap, L"image/png", Png) &&
                 SaveImage(Bitmap, L"image/jpeg", Jpg) &&
                 SaveImage(Bitmap, L"image/bmp", Bmp);
    }
    if (Bitmap)
        GdipDisposeImage((GpImage*)Bitmap);
    GdiplusShutdown(Token);
    return Result;
}

static
DWORD
ReadHead(
    _In_ PCWSTR Path,
    _Out_ PDWORD FileSize)
{
    HANDLE File;
    DWORD Head = 0, Read = 0;

    *FileSize = 0;
    File = CreateFileW(Path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                       NULL, OPEN_EXISTING, 0, NULL);
    if (File == INVALID_HANDLE_VALUE)
        return 0;
    *FileSize = GetFileSize(File, NULL);
    if (!ReadFile(File, &Head, sizeof(Head), &Read, NULL) || Read != sizeof(Head))
        Head = 0;
    CloseHandle(File);
    return Head;
}

static
DWORD
QueryTranscodedCount(VOID)
{
    DWORD Count = 0, Size = sizeof(Count), Type = 0;

    if (RegGetValueW(HKEY_CURRENT_USER, DesktopKey, L"TranscodedImageCount", RRF_RT_REG_DWORD, &Type, &Count, &Size) != ERROR_SUCCESS)
        Count = 0;
    return Count;
}

static
VOID
CheckWallpaper(
    _In_ PCWSTR Path,
    _In_ PCWSTR Transcoded,
    _In_ DWORD ExpectedHead,
    _In_ DWORD ExpectedMask,
    _In_ int Line)
{
    WCHAR Current[MAX_PATH + 1], Stored[MAX_PATH + 1];
    DWORD Size = sizeof(Stored), Head, FileSize, SourceSize;
    BOOL Ret;

    SetLastError(0xdeadbeef);
    Ret = SystemParametersInfoW(SPI_SETDESKWALLPAPER, 0, (PVOID)Path, SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);
    ok_(__FILE__, Line)(Ret, "SPI_SETDESKWALLPAPER(%S) failed, error %lu\n", Path, GetLastError());

    Current[0] = UNICODE_NULL;
    Ret = SystemParametersInfoW(SPI_GETDESKWALLPAPER, ARRAYSIZE(Current), Current, 0);
    ok_(__FILE__, Line)(Ret, "SPI_GETDESKWALLPAPER failed, error %lu\n", GetLastError());
    ok_(__FILE__, Line)(lstrcmpiW(Current, Path) == 0, "SPI_GETDESKWALLPAPER returned %S, expected %S\n", Current, Path);

    Stored[0] = UNICODE_NULL;
    RegGetValueW(HKEY_CURRENT_USER, DesktopKey, L"Wallpaper", RRF_RT_REG_SZ, NULL, Stored, &Size);
    ok_(__FILE__, Line)(lstrcmpiW(Stored, Path) == 0, "Wallpaper value is %S, expected %S\n", Stored, Path);

    Head = ReadHead(Transcoded, &FileSize);
    ok_(__FILE__, Line)((Head & ExpectedMask) == ExpectedHead, "TranscodedWallpaper starts with 0x%08lx, expected 0x%08lx\n", Head, ExpectedHead);
    ReadHead(Path, &SourceSize);
    printf("%S: transcoded size %lu, source size %lu\n", Path, FileSize, SourceSize);
}

START_TEST(DeskWallpaper)
{
    WCHAR Original[MAX_PATH + 1], Temp[MAX_PATH], Png[MAX_PATH], Jpg[MAX_PATH], Bmp[MAX_PATH], Missing[MAX_PATH], Transcoded[MAX_PATH];
    DWORD Length, CountBefore, CountAfter;

    Original[0] = UNICODE_NULL;
    SystemParametersInfoW(SPI_GETDESKWALLPAPER, ARRAYSIZE(Original), Original, 0);

    Length = GetTempPathW(ARRAYSIZE(Temp), Temp);
    ok(Length != 0 && Length < ARRAYSIZE(Temp), "GetTempPathW failed\n");
    Length = GetEnvironmentVariableW(L"APPDATA", Transcoded, ARRAYSIZE(Transcoded));
    ok(Length != 0 && Length < ARRAYSIZE(Transcoded), "APPDATA is not set\n");
    lstrcatW(Transcoded, L"\\Microsoft\\Windows\\Themes\\TranscodedWallpaper");
    _snwprintf(Png, ARRAYSIZE(Png), L"%swp_test.png", Temp);
    _snwprintf(Jpg, ARRAYSIZE(Jpg), L"%swp_test.jpg", Temp);
    _snwprintf(Bmp, ARRAYSIZE(Bmp), L"%swp_test.bmp", Temp);
    _snwprintf(Missing, ARRAYSIZE(Missing), L"%swp_missing.png", Temp);
    DeleteFileW(Missing);

    if (!CreateImages(Png, Jpg, Bmp))
    {
        skip("Cannot create the test images\n");
        return;
    }

    CountBefore = QueryTranscodedCount();
    CheckWallpaper(Png, Transcoded, 0x474E5089, 0xFFFFFFFF, __LINE__);
    CheckWallpaper(Jpg, Transcoded, 0x00FFD8FF, 0x00FFFFFF, __LINE__);
    CheckWallpaper(Bmp, Transcoded, 0x00FFD8FF, 0x00FFFFFF, __LINE__);
    CheckWallpaper(Missing, Transcoded, 0x00FFD8FF, 0x00FFFFFF, __LINE__);
    CountAfter = QueryTranscodedCount();
    printf("TranscodedImageCount went from %lu to %lu\n", CountBefore, CountAfter);

    SystemParametersInfoW(SPI_SETDESKWALLPAPER, 0, Original, SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);
    DeleteFileW(Png);
    DeleteFileW(Jpg);
    DeleteFileW(Bmp);
}
