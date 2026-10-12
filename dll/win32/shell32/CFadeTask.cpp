/*
 * PROJECT:     LiberNT shell32
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Fades out a captured screen rectangle
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

#define FADE_DURATION_MS 350
#define FADE_STEP_MS 10

struct FADE_DATA
{
    RECT rc;
    HBITMAP hbm;
    HMODULE hModule;
};

static LRESULT CALLBACK FadeWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
        case WM_NCHITTEST:
            return HTTRANSPARENT;
        case WM_ERASEBKGND:
            return TRUE;
        case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HBITMAP hbm = (HBITMAP)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
            HDC hdc = BeginPaint(hwnd, &ps);
            HDC hdcMem = CreateCompatibleDC(hdc);
            if (hdcMem && hbm)
            {
                RECT rc;
                HGDIOBJ hbmOld = SelectObject(hdcMem, hbm);
                GetClientRect(hwnd, &rc);
                BitBlt(hdc, 0, 0, rc.right, rc.bottom, hdcMem, 0, 0, SRCCOPY);
                SelectObject(hdcMem, hbmOld);
            }
            if (hdcMem)
                DeleteDC(hdcMem);
            EndPaint(hwnd, &ps);
            return 0;
        }
    }
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

static DWORD WINAPI FadeThreadProc(LPVOID lpParameter)
{
    FADE_DATA *pData = static_cast<FADE_DATA *>(lpParameter);
    HMODULE hModule = pData->hModule;
    WNDCLASSEXW wc = { sizeof(wc) };
    HWND hwnd;
    DWORD dwStart, dwElapsed;
    MSG msg;

    wc.lpfnWndProc = FadeWndProc;
    wc.hInstance = shell32_hInstance;
    wc.lpszClassName = L"SysFader";
    RegisterClassExW(&wc);

    hwnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE,
                           L"SysFader", L"", WS_POPUP,
                           pData->rc.left, pData->rc.top,
                           pData->rc.right - pData->rc.left, pData->rc.bottom - pData->rc.top,
                           NULL, NULL, shell32_hInstance, NULL);
    if (hwnd)
    {
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)pData->hbm);
        SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA);
        ShowWindow(hwnd, SW_SHOWNOACTIVATE);
        UpdateWindow(hwnd);
        dwStart = GetTickCount();
        for (;;)
        {
            dwElapsed = GetTickCount() - dwStart;
            if (dwElapsed >= FADE_DURATION_MS)
                break;
            SetLayeredWindowAttributes(hwnd, 0, (BYTE)(255 - (255 * dwElapsed) / FADE_DURATION_MS), LWA_ALPHA);
            MsgWaitForMultipleObjects(0, NULL, FALSE, FADE_STEP_MS, QS_ALLINPUT);
            while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
                DispatchMessageW(&msg);
        }
        DestroyWindow(hwnd);
    }
    DeleteObject(pData->hbm);
    HeapFree(GetProcessHeap(), 0, pData);
    FreeLibraryAndExitThread(hModule, 0);
    return 0;
}

class CFadeTask :
    public CComCoClass<CFadeTask, &CLSID_FadeTask>,
    public CComObjectRootEx<CComMultiThreadModelNoCS>,
    public IFadeTask
{
public:
    STDMETHOD(FadeRect)(const RECT *prc) override
    {
        BITMAPINFO bmi = {};
        FADE_DATA *pData;
        HDC hdcScreen, hdcMem;
        HGDIOBJ hbmOld;
        HANDLE hThread;
        DWORD *pBits = NULL;
        LONG cx, cy;

        if (!prc)
            return E_INVALIDARG;
        cx = prc->right - prc->left;
        cy = prc->bottom - prc->top;
        if (cx <= 0 || cy <= 0)
            return E_INVALIDARG;

        pData = static_cast<FADE_DATA *>(HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*pData)));
        if (!pData)
            return E_OUTOFMEMORY;
        pData->rc = *prc;

        bmi.bmiHeader.biSize = sizeof(bmi.bmiHeader);
        bmi.bmiHeader.biWidth = cx;
        bmi.bmiHeader.biHeight = -cy;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        hdcScreen = GetDC(NULL);
        hdcMem = CreateCompatibleDC(hdcScreen);
        pData->hbm = CreateDIBSection(hdcScreen, &bmi, DIB_RGB_COLORS, (void **)&pBits, NULL, 0);
        if (!pData->hbm || !hdcMem)
        {
            if (pData->hbm)
                DeleteObject(pData->hbm);
            if (hdcMem)
                DeleteDC(hdcMem);
            ReleaseDC(NULL, hdcScreen);
            HeapFree(GetProcessHeap(), 0, pData);
            return E_OUTOFMEMORY;
        }
        hbmOld = SelectObject(hdcMem, pData->hbm);
        BitBlt(hdcMem, 0, 0, cx, cy, hdcScreen, prc->left, prc->top, SRCCOPY);
        SelectObject(hdcMem, hbmOld);
        DeleteDC(hdcMem);
        ReleaseDC(NULL, hdcScreen);
        GdiFlush();

        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, (LPCWSTR)FadeThreadProc, &pData->hModule))
        {
            DeleteObject(pData->hbm);
            HeapFree(GetProcessHeap(), 0, pData);
            return HRESULT_FROM_WIN32(GetLastError());
        }
        hThread = CreateThread(NULL, 0, FadeThreadProc, pData, 0, NULL);
        if (!hThread)
        {
            DWORD dwError = GetLastError();
            FreeLibrary(pData->hModule);
            DeleteObject(pData->hbm);
            HeapFree(GetProcessHeap(), 0, pData);
            return HRESULT_FROM_WIN32(dwError);
        }
        CloseHandle(hThread);
        return S_OK;
    }

    DECLARE_REGISTRY_RESOURCEID(IDR_FADETASK)
    DECLARE_NOT_AGGREGATABLE(CFadeTask)
    DECLARE_PROTECT_FINAL_CONSTRUCT()

    BEGIN_COM_MAP(CFadeTask)
        COM_INTERFACE_ENTRY_IID(IID_IFadeTask, IFadeTask)
    END_COM_MAP()
};

OBJECT_ENTRY_AUTO(CLSID_FadeTask, CFadeTask)
