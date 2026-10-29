/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests that the fonts in the Fonts folder are loaded at boot
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include "precomp.h"

#define TTCF_TAG 0x66637474

typedef struct _FONT_SIZES
{
    HDC hDC;
    DWORD *pSizes;
    SIZE_T cSizes;
    SIZE_T cMax;
    BOOL fRaster;
} FONT_SIZES;

static void AddSize(FONT_SIZES *pData, DWORD cb)
{
    SIZE_T i;

    if (cb == GDI_ERROR || !cb)
        return;
    for (i = 0; i < pData->cSizes; i++)
    {
        if (pData->pSizes[i] == cb)
            return;
    }
    if (pData->cSizes == pData->cMax)
    {
        SIZE_T cMax = pData->cMax ? pData->cMax * 2 : 256;
        DWORD *pSizes = HeapAlloc(GetProcessHeap(), 0, cMax * sizeof(DWORD));
        if (!pSizes)
            return;
        if (pData->pSizes)
        {
            CopyMemory(pSizes, pData->pSizes, pData->cSizes * sizeof(DWORD));
            HeapFree(GetProcessHeap(), 0, pData->pSizes);
        }
        pData->pSizes = pSizes;
        pData->cMax = cMax;
    }
    pData->pSizes[pData->cSizes++] = cb;
}

static int CALLBACK EnumStyleProc(const LOGFONTW *plf, const TEXTMETRICW *ptm, DWORD FontType, LPARAM lParam)
{
    FONT_SIZES *pData = (FONT_SIZES *)lParam;
    HFONT hFont, hOldFont;

    if (FontType & RASTER_FONTTYPE)
    {
        pData->fRaster = TRUE;
        return 1;
    }
    hFont = CreateFontIndirectW(plf);
    if (!hFont)
        return 1;
    hOldFont = SelectObject(pData->hDC, hFont);
    AddSize(pData, GetFontData(pData->hDC, 0, 0, NULL, 0));
    AddSize(pData, GetFontData(pData->hDC, TTCF_TAG, 0, NULL, 0));
    SelectObject(pData->hDC, hOldFont);
    DeleteObject(hFont);
    return 1;
}

static int CALLBACK EnumFaceProc(const LOGFONTW *plf, const TEXTMETRICW *ptm, DWORD FontType, LPARAM lParam)
{
    FONT_SIZES *pData = (FONT_SIZES *)lParam;
    LOGFONTW lf = { 0 };

    lf.lfCharSet = DEFAULT_CHARSET;
    StringCchCopyW(lf.lfFaceName, _countof(lf.lfFaceName), plf->lfFaceName);
    EnumFontFamiliesExW(pData->hDC, &lf, EnumStyleProc, lParam, 0);
    return 1;
}

static BOOL HasSize(FONT_SIZES *pData, DWORD cb)
{
    SIZE_T i;

    for (i = 0; i < pData->cSizes; i++)
    {
        if (pData->pSizes[i] == cb)
            return TRUE;
    }
    return FALSE;
}

static UINT CheckFiles(FONT_SIZES *pData, LPCWSTR pszFolder, LPCWSTR pszPattern)
{
    WCHAR szPath[MAX_PATH];
    WIN32_FIND_DATAW fd;
    HANDLE hFind;
    UINT cFiles = 0;

    StringCchPrintfW(szPath, _countof(szPath), L"%s\\%s", pszFolder, pszPattern);
    hFind = FindFirstFileW(szPath, &fd);
    if (hFind == INVALID_HANDLE_VALUE)
        return 0;
    do
    {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            continue;
        cFiles++;
        ok(HasSize(pData, fd.nFileSizeLow), "%ls is not loaded\n", fd.cFileName);
    } while (FindNextFileW(hFind, &fd));
    FindClose(hFind);
    return cFiles;
}

static UINT CountFiles(LPCWSTR pszFolder, LPCWSTR pszPattern)
{
    WCHAR szPath[MAX_PATH];
    WIN32_FIND_DATAW fd;
    HANDLE hFind;
    UINT cFiles = 0;

    StringCchPrintfW(szPath, _countof(szPath), L"%s\\%s", pszFolder, pszPattern);
    hFind = FindFirstFileW(szPath, &fd);
    if (hFind == INVALID_HANDLE_VALUE)
        return 0;
    do
    {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
            cFiles++;
    } while (FindNextFileW(hFind, &fd));
    FindClose(hFind);
    return cFiles;
}

START_TEST(FontFolder)
{
    FONT_SIZES data = { 0 };
    LOGFONTW lf = { 0 };
    WCHAR szFolder[MAX_PATH];
    UINT cOtf, cTtc, cFon;

    GetWindowsDirectoryW(szFolder, _countof(szFolder));
    StringCchCatW(szFolder, _countof(szFolder), L"\\Fonts");

    data.hDC = CreateCompatibleDC(NULL);
    ok(data.hDC != NULL, "CreateCompatibleDC failed\n");
    lf.lfCharSet = DEFAULT_CHARSET;
    EnumFontFamiliesExW(data.hDC, &lf, EnumFaceProc, (LPARAM)&data, 0);

    cOtf = CheckFiles(&data, szFolder, L"*.otf");
    cTtc = CheckFiles(&data, szFolder, L"*.ttc");
    cFon = CountFiles(szFolder, L"*.fon");
    trace("%u font files loaded, %u .otf, %u .ttc and %u .fon files in %ls\n",
          (UINT)data.cSizes, cOtf, cTtc, cFon, szFolder);
    ok(cOtf + cTtc + cFon != 0, "no .otf, .ttc or .fon file in %ls\n", szFolder);
    if (cFon)
        ok(data.fRaster, "no font of the %u .fon files is loaded\n", cFon);

    HeapFree(GetProcessHeap(), 0, data.pSizes);
    DeleteDC(data.hDC);
}
