/*
 * PROJECT:     LiberNT Certificates Snap-in
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Snap-in information object
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

static HBITMAP CertMgr_CreateIconBitmap(HICON hIcon, int cx)
{
    BITMAPINFO bmi = { { sizeof(BITMAPINFOHEADER), cx, cx, 1, 32, BI_RGB } };
    HDC hdcScreen, hdcMem;
    HBITMAP hBitmap, hOld;
    void *pvBits;
    RECT rc = { 0, 0, cx, cx };

    hdcScreen = GetDC(NULL);
    hdcMem = CreateCompatibleDC(hdcScreen);
    ReleaseDC(NULL, hdcScreen);
    if (!hdcMem)
        return NULL;

    hBitmap = CreateDIBSection(hdcMem, &bmi, DIB_RGB_COLORS, &pvBits, NULL, 0);
    if (hBitmap)
    {
        hOld = (HBITMAP)SelectObject(hdcMem, hBitmap);
        FillRect(hdcMem, &rc, (HBRUSH)GetStockObject(BLACK_BRUSH));
        DrawIconEx(hdcMem, 0, 0, hIcon, cx, cx, 0, NULL, DI_NORMAL);
        SelectObject(hdcMem, hOld);
    }
    DeleteDC(hdcMem);
    return hBitmap;
}

class CCertMgrAbout :
    public CComCoClass<CCertMgrAbout, &CLSID_CertificateManagerAbout>,
    public CComObjectRootEx<CComMultiThreadModel>,
    public ISnapinAbout
{
public:
    STDMETHOD(GetSnapinDescription)(LPOLESTR *lpDescription) override
    {
        if (!lpDescription)
            return E_POINTER;
        return CertMgr_LoadString(IDS_SNAPIN_DESCRIPTION, lpDescription);
    }

    STDMETHOD(GetProvider)(LPOLESTR *lpName) override
    {
        if (!lpName)
            return E_POINTER;
        return CertMgr_LoadString(IDS_SNAPIN_PROVIDER, lpName);
    }

    STDMETHOD(GetSnapinVersion)(LPOLESTR *lpVersion) override
    {
        WCHAR szModule[MAX_PATH], szVersion[64];
        VS_FIXEDFILEINFO *pInfo;
        DWORD cbInfo, dwHandle;
        UINT cbFixed;
        BYTE *pData;
        HRESULT hr;

        if (!lpVersion)
            return E_POINTER;
        *lpVersion = NULL;

        if (!GetModuleFileNameW(certmgr_hInstance, szModule, _countof(szModule)))
            return HRESULT_FROM_WIN32(GetLastError());
        cbInfo = GetFileVersionInfoSizeW(szModule, &dwHandle);
        if (!cbInfo)
            return HRESULT_FROM_WIN32(GetLastError());
        pData = (BYTE *)CoTaskMemAlloc(cbInfo);
        if (!pData)
            return E_OUTOFMEMORY;

        if (!GetFileVersionInfoW(szModule, 0, cbInfo, pData) ||
            !VerQueryValueW(pData, L"\\", (void **)&pInfo, &cbFixed) || cbFixed < sizeof(*pInfo))
        {
            CoTaskMemFree(pData);
            return E_FAIL;
        }

        StringCchPrintfW(szVersion, _countof(szVersion), L"%u.%u.%u.%u",
                         HIWORD(pInfo->dwFileVersionMS), LOWORD(pInfo->dwFileVersionMS),
                         HIWORD(pInfo->dwFileVersionLS), LOWORD(pInfo->dwFileVersionLS));
        CoTaskMemFree(pData);

        hr = SHStrDupW(szVersion, lpVersion);
        return hr;
    }

    STDMETHOD(GetSnapinImage)(HICON *hAppIcon) override
    {
        if (!hAppIcon)
            return E_POINTER;
        *hAppIcon = (HICON)LoadImageW(certmgr_hInstance, MAKEINTRESOURCEW(IDI_CERTMGR), IMAGE_ICON, 32, 32, 0);
        return *hAppIcon ? S_OK : HRESULT_FROM_WIN32(GetLastError());
    }

    STDMETHOD(GetStaticFolderImage)(HBITMAP *hSmallImage, HBITMAP *hSmallImageOpen, HBITMAP *hLargeImage,
                                    COLORREF *cMask) override
    {
        HICON hSmall, hLarge;

        if (!hSmallImage || !hSmallImageOpen || !hLargeImage || !cMask)
            return E_POINTER;

        hSmall = (HICON)LoadImageW(certmgr_hInstance, MAKEINTRESOURCEW(IDI_CERTMGR), IMAGE_ICON, 16, 16, 0);
        hLarge = (HICON)LoadImageW(certmgr_hInstance, MAKEINTRESOURCEW(IDI_CERTMGR), IMAGE_ICON, 32, 32, 0);
        *hSmallImage = hSmall ? CertMgr_CreateIconBitmap(hSmall, 16) : NULL;
        *hSmallImageOpen = hSmall ? CertMgr_CreateIconBitmap(hSmall, 16) : NULL;
        *hLargeImage = hLarge ? CertMgr_CreateIconBitmap(hLarge, 32) : NULL;
        *cMask = RGB(0, 0, 0);
        if (hSmall)
            DestroyIcon(hSmall);
        if (hLarge)
            DestroyIcon(hLarge);

        if (!*hSmallImage || !*hSmallImageOpen || !*hLargeImage)
        {
            if (*hSmallImage)
                DeleteObject(*hSmallImage);
            if (*hSmallImageOpen)
                DeleteObject(*hSmallImageOpen);
            if (*hLargeImage)
                DeleteObject(*hLargeImage);
            *hSmallImage = *hSmallImageOpen = *hLargeImage = NULL;
            return E_FAIL;
        }
        return S_OK;
    }

    DECLARE_REGISTRY_RESOURCEID(IDR_CERTMGRABOUT)
    DECLARE_NOT_AGGREGATABLE(CCertMgrAbout)
    DECLARE_PROTECT_FINAL_CONSTRUCT()

    BEGIN_COM_MAP(CCertMgrAbout)
        COM_INTERFACE_ENTRY_IID(IID_ISnapinAbout, ISnapinAbout)
    END_COM_MAP()
};

OBJECT_ENTRY_AUTO(CLSID_CertificateManagerAbout, CCertMgrAbout)
