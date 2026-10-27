/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test icon arrangement and positions of folder views and the desktop
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "shelltest.h"
#include <exdisp.h>
#include <shlguid.h>
#include <shellutils.h>
#include <strsafe.h>

#define DESKTOP_ITEM L"IconPositions.txt"
#define WAIT_MS 5000

class CIconPositionsBrowser : public IShellBrowser
{
public:
    HWND m_hwnd;

    CIconPositionsBrowser() : m_hwnd(NULL)
    {
        WNDCLASSW wc = {};
        wc.lpfnWndProc = DefWindowProcW;
        wc.hInstance = GetModuleHandleW(NULL);
        wc.lpszClassName = L"IconPositionsBrowser";
        RegisterClassW(&wc);
        m_hwnd = CreateWindowExW(0, wc.lpszClassName, L"IconPositions", WS_OVERLAPPEDWINDOW,
                                 0, 0, 400, 300, NULL, NULL, wc.hInstance, NULL);
    }

    ~CIconPositionsBrowser()
    {
        if (m_hwnd)
            DestroyWindow(m_hwnd);
    }

    STDMETHOD(QueryInterface)(REFIID riid, void **ppv) override
    {
        if (riid == IID_IShellBrowser || riid == IID_IOleWindow || riid == IID_IUnknown)
        {
            *ppv = static_cast<IShellBrowser *>(this);
            return S_OK;
        }
        *ppv = NULL;
        return E_NOINTERFACE;
    }
    STDMETHOD_(ULONG, AddRef)() override { return 2; }
    STDMETHOD_(ULONG, Release)() override { return 1; }
    STDMETHOD(GetWindow)(HWND *phwnd) override { *phwnd = m_hwnd; return S_OK; }
    STDMETHOD(ContextSensitiveHelp)(BOOL fEnterMode) override { return E_NOTIMPL; }
    STDMETHOD(InsertMenusSB)(HMENU hmenuShared, LPOLEMENUGROUPWIDTHS lpMenuWidths) override { return S_OK; }
    STDMETHOD(SetMenuSB)(HMENU hmenuShared, HOLEMENU holemenuRes, HWND hwndActiveObject) override { return S_OK; }
    STDMETHOD(RemoveMenusSB)(HMENU hmenuShared) override { return S_OK; }
    STDMETHOD(SetStatusTextSB)(LPCWSTR pszStatusText) override { return S_OK; }
    STDMETHOD(EnableModelessSB)(BOOL fEnable) override { return S_OK; }
    STDMETHOD(TranslateAcceleratorSB)(MSG *pmsg, WORD wID) override { return S_FALSE; }
    STDMETHOD(BrowseObject)(PCUIDLIST_RELATIVE pidl, UINT wFlags) override { return E_NOTIMPL; }
    STDMETHOD(GetViewStateStream)(DWORD grfMode, IStream **ppStrm) override { *ppStrm = NULL; return E_NOTIMPL; }
    STDMETHOD(GetControlWindow)(UINT id, HWND *phwnd) override { *phwnd = NULL; return E_NOTIMPL; }
    STDMETHOD(SendControlMsg)(UINT id, UINT uMsg, WPARAM wParam, LPARAM lParam, LRESULT *pret) override
    {
        if (pret)
            *pret = 0;
        return E_NOTIMPL;
    }
    STDMETHOD(QueryActiveShellView)(IShellView **ppshv) override { *ppshv = NULL; return E_NOTIMPL; }
    STDMETHOD(OnViewWindowActive)(IShellView *pshv) override { return S_OK; }
    STDMETHOD(SetToolbarItems)(LPTBBUTTONSB lpButtons, UINT nButtons, UINT uFlags) override { return S_OK; }
};

static BOOL CreateTestFile(LPCWSTR pszDir, LPCWSTR pszName, LPWSTR pszPath, UINT cchPath)
{
    if (FAILED(StringCchPrintfW(pszPath, cchPath, L"%s\\%s", pszDir, pszName)))
        return FALSE;
    HANDLE hFile = CreateFileW(pszPath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
        return FALSE;
    CloseHandle(hFile);
    SHChangeNotify(SHCNE_CREATE, SHCNF_PATHW | SHCNF_FLUSH, pszPath, NULL);
    return TRUE;
}

static void DeleteTestFile(LPCWSTR pszPath)
{
    if (DeleteFileW(pszPath))
        SHChangeNotify(SHCNE_DELETE, SHCNF_PATHW | SHCNF_FLUSH, pszPath, NULL);
}

static void CheckAutoArrange(IFolderView2 *pfv, HRESULT hrExpected, int line)
{
    DWORD flags = 0;
    ok_(__FILE__, line)(pfv->GetAutoArrange() == hrExpected, "GetAutoArrange() is not 0x%lx\n", hrExpected);
    ok_(__FILE__, line)(SUCCEEDED(pfv->GetCurrentFolderFlags(&flags)), "GetCurrentFolderFlags failed\n");
    ok_(__FILE__, line)(!!(flags & FWF_AUTOARRANGE) == (hrExpected == S_OK),
                        "Folder flags 0x%lx do not match the arrangement\n", flags);
}

static void TestHostedView(IShellFolder *psf, DWORD fFlags)
{
    CComPtr<IShellView> psv;
    SFV_CREATE sfvc = { sizeof(sfvc), psf };
    HRESULT hr = SHCreateShellFolderView(&sfvc, &psv);
    ok_hex(hr, S_OK);
    if (FAILED(hr))
        return;

    CIconPositionsBrowser browser;
    FOLDERSETTINGS fs = { FVM_ICON, fFlags };
    RECT rc = { 0, 0, 400, 300 };
    HWND hwndView = NULL;
    hr = psv->CreateViewWindow(NULL, &fs, &browser, &rc, &hwndView);
    ok_hex(hr, S_OK);
    if (FAILED(hr))
        return;

    CComPtr<IFolderView2> pfv;
    hr = psv->QueryInterface(IID_PPV_ARG(IFolderView2, &pfv));
    ok_hex(hr, S_OK);
    if (SUCCEEDED(hr))
    {
        HRESULT hrArranged = (fFlags & FWF_AUTOARRANGE) ? S_OK : S_FALSE;
        UINT mode = 0;
        ok_hex(pfv->GetCurrentViewMode(&mode), S_OK);
        ok_int(mode, FVM_ICON);
        CheckAutoArrange(pfv, hrArranged, __LINE__);

        ok_hex(pfv->SetCurrentViewMode(FVM_SMALLICON), S_OK);
        CheckAutoArrange(pfv, hrArranged, __LINE__);
        ok_hex(pfv->SetCurrentViewMode(FVM_ICON), S_OK);
        CheckAutoArrange(pfv, hrArranged, __LINE__);

        ok_hex(pfv->SetCurrentFolderFlags(FWF_AUTOARRANGE, FWF_AUTOARRANGE), S_OK);
        CheckAutoArrange(pfv, S_OK, __LINE__);
        ok_hex(pfv->SetCurrentFolderFlags(FWF_AUTOARRANGE, 0), S_OK);
        CheckAutoArrange(pfv, S_FALSE, __LINE__);
    }

    psv->UIActivate(SVUIA_DEACTIVATE);
    psv->DestroyViewWindow();
}

static HRESULT ViewFromDispatch(IDispatch *pDispatch, IFolderView2 **ppfv)
{
    CComPtr<IShellBrowser> pBrowser;
    HRESULT hr = IUnknown_QueryService(pDispatch, SID_STopLevelBrowser, IID_PPV_ARG(IShellBrowser, &pBrowser));
    if (FAILED(hr))
        return hr;

    CComPtr<IShellView> pView;
    hr = pBrowser->QueryActiveShellView(&pView);
    if (FAILED(hr))
        return hr;

    return pView->QueryInterface(IID_PPV_ARG(IFolderView2, ppfv));
}

static IFileDialog *s_pFileDialog;
static DWORD s_dwFileDialogStart;
static BOOL s_bFileDialogChecked;

static void CALLBACK FileDialogTimer(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime)
{
    CComPtr<IServiceProvider> psp;
    CComPtr<IFolderView2> pfv;
    int count = 0;
    if (FAILED(s_pFileDialog->QueryInterface(IID_PPV_ARG(IServiceProvider, &psp))) ||
        FAILED(psp->QueryService(SID_SFolderView, IID_PPV_ARG(IFolderView2, &pfv))) ||
        FAILED(pfv->ItemCount(SVGIO_ALLVIEW, &count)) || count < 3)
    {
        if (GetTickCount() - s_dwFileDialogStart < WAIT_MS)
            return;
        KillTimer(NULL, idEvent);
        s_pFileDialog->Close(S_OK);
        return;
    }

    KillTimer(NULL, idEvent);
    s_bFileDialogChecked = TRUE;
    CheckAutoArrange(pfv, S_OK, __LINE__);
    ok_hex(pfv->SetCurrentViewMode(FVM_ICON), S_OK);
    CheckAutoArrange(pfv, S_OK, __LINE__);
    s_pFileDialog->Close(S_OK);
}

static void TestFileDialog(PCIDLIST_ABSOLUTE pidlFolder)
{
    CComPtr<IFileDialog> pfd;
    CComPtr<IShellItem> psi;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARG(IFileDialog, &pfd));
    ok_hex(hr, S_OK);
    if (FAILED(hr))
        return;
    hr = SHCreateItemFromIDList(pidlFolder, IID_PPV_ARG(IShellItem, &psi));
    ok_hex(hr, S_OK);
    if (FAILED(hr))
        return;
    ok_hex(pfd->SetFolder(psi), S_OK);

    s_pFileDialog = pfd;
    s_dwFileDialogStart = GetTickCount();
    s_bFileDialogChecked = FALSE;
    UINT_PTR idTimer = SetTimer(NULL, 0, 100, FileDialogTimer);
    ok(idTimer != 0, "SetTimer failed (%lu)\n", GetLastError());
    if (idTimer)
        pfd->Show(NULL);
    ok(s_bFileDialogChecked, "The file dialog view did not show the test folder\n");
    s_pFileDialog = NULL;
}

static void TestFolderViews(void)
{
    WCHAR szDir[MAX_PATH], szFiles[3][MAX_PATH];
    GetTempPathW(_countof(szDir), szDir);
    PathAppendW(szDir, L"IconPositionsFolder");
    CreateDirectoryW(szDir, NULL);
    for (UINT i = 0; i < _countof(szFiles); ++i)
    {
        WCHAR szName[32];
        StringCchPrintfW(szName, _countof(szName), L"item%u.txt", i);
        ok(CreateTestFile(szDir, szName, szFiles[i], _countof(szFiles[i])), "CreateTestFile failed (%lu)\n", GetLastError());
    }

    CComPtr<IShellFolder> pDesktop;
    CComHeapPtr<ITEMIDLIST> pidl;
    CComPtr<IShellFolder> psf;
    HRESULT hr = SHGetDesktopFolder(&pDesktop);
    ok_hex(hr, S_OK);
    if (SUCCEEDED(hr))
        hr = SHParseDisplayName(szDir, NULL, &pidl, 0, NULL);
    ok_hex(hr, S_OK);
    if (SUCCEEDED(hr))
        hr = pDesktop->BindToObject(pidl, NULL, IID_PPV_ARG(IShellFolder, &psf));
    ok_hex(hr, S_OK);
    if (SUCCEEDED(hr))
    {
        TestHostedView(psf, 0);
        TestHostedView(psf, FWF_AUTOARRANGE);
        TestFileDialog(pidl);
    }

    for (UINT i = 0; i < _countof(szFiles); ++i)
        DeleteTestFile(szFiles[i]);
    RemoveDirectoryW(szDir);
}

static BOOL FindViewItem(IFolderView *pfv, IShellFolder *psf, LPCWSTR pszName, PITEMID_CHILD *ppidl)
{
    *ppidl = NULL;
    int count = 0;
    if (FAILED(pfv->ItemCount(SVGIO_ALLVIEW, &count)))
        return FALSE;

    for (int i = 0; i < count; ++i)
    {
        PITEMID_CHILD pidl = NULL;
        if (FAILED(pfv->Item(i, &pidl)) || !pidl)
            continue;
        STRRET sr;
        WCHAR szName[MAX_PATH];
        if (SUCCEEDED(psf->GetDisplayNameOf(pidl, SHGDN_INFOLDER | SHGDN_FORPARSING, &sr)) &&
            SUCCEEDED(StrRetToBufW(&sr, pidl, szName, _countof(szName))) &&
            !lstrcmpiW(szName, pszName))
        {
            *ppidl = pidl;
            return TRUE;
        }
        CoTaskMemFree(pidl);
    }
    return FALSE;
}

static void TestDesktop(void)
{
    CComPtr<IShellWindows> pShellWindows;
    HRESULT hr = CoCreateInstance(CLSID_ShellWindows, NULL, CLSCTX_LOCAL_SERVER,
                                  IID_PPV_ARG(IShellWindows, &pShellWindows));
    if (FAILED(hr))
    {
        skip("No shell windows (0x%lx)\n", hr);
        return;
    }

    VARIANT vLocation, vRoot;
    V_VT(&vLocation) = VT_I4;
    V_I4(&vLocation) = CSIDL_DESKTOP;
    VariantInit(&vRoot);
    long hwnd = 0;
    CComPtr<IDispatch> pDispatch;
    CComPtr<IFolderView2> pfv;
    hr = pShellWindows->FindWindowSW(&vLocation, &vRoot, SWC_DESKTOP, &hwnd, SWFO_NEEDDISPATCH, &pDispatch);
    if (hr == S_OK && pDispatch)
        hr = ViewFromDispatch(pDispatch, &pfv);
    if (hr != S_OK || !pfv)
    {
        skip("No desktop folder view (0x%lx)\n", hr);
        return;
    }

    DWORD flags = 0;
    ok_hex(pfv->GetAutoArrange(), S_FALSE);
    ok_hex(pfv->GetCurrentFolderFlags(&flags), S_OK);
    ok(flags & FWF_DESKTOP, "Desktop folder flags 0x%lx lack FWF_DESKTOP\n", flags);
    ok(flags & FWF_SNAPTOGRID, "Desktop folder flags 0x%lx lack FWF_SNAPTOGRID\n", flags);
    ok(!(flags & FWF_AUTOARRANGE), "Desktop folder flags 0x%lx have FWF_AUTOARRANGE\n", flags);

    CComPtr<IShellView> psv;
    CComPtr<IShellFolder> psf;
    WCHAR szDir[MAX_PATH], szPath[MAX_PATH];
    ok_hex(pfv->QueryInterface(IID_PPV_ARG(IShellView, &psv)), S_OK);
    ok_hex(SHGetDesktopFolder(&psf), S_OK);
    if (!psv || !psf || FAILED(SHGetFolderPathW(NULL, CSIDL_DESKTOPDIRECTORY, NULL, SHGFP_TYPE_CURRENT, szDir)))
        return;
    if (!CreateTestFile(szDir, DESKTOP_ITEM, szPath, _countof(szPath)))
    {
        ok(FALSE, "Creating %S on the desktop failed (%lu)\n", DESKTOP_ITEM, GetLastError());
        return;
    }

    ok_hex(psv->Refresh(), S_OK);
    CComHeapPtr<ITEMID_CHILD> pidl;
    for (DWORD start = GetTickCount(); !pidl && GetTickCount() - start < WAIT_MS; Sleep(100))
        FindViewItem(pfv, psf, DESKTOP_ITEM, &pidl);
    ok(pidl != NULL, "%S did not appear on the desktop\n", DESKTOP_ITEM);
    if (pidl)
    {
        POINT spacing = { 0, 0 };
        if (FAILED(pfv->GetSpacing(&spacing)) || spacing.x <= 0 || spacing.y <= 0)
            spacing.x = spacing.y = 100;

        POINT target = { spacing.x * 4, spacing.y }, pt = { -1, -1 };
        PCUITEMID_CHILD apidl[1] = { pidl };
        ok_hex(pfv->SelectAndPositionItems(1, apidl, &target, SVSI_POSITIONITEM), S_OK);
        ok_hex(pfv->GetItemPosition(pidl, &pt), S_OK);
        ok(pt.x == target.x && pt.y == target.y, "%S is at (%ld,%ld) instead of (%ld,%ld)\n",
           DESKTOP_ITEM, pt.x, pt.y, target.x, target.y);
        ok_hex(pfv->GetAutoArrange(), S_FALSE);
    }

    DeleteTestFile(szPath);
}

START_TEST(IconPositions)
{
    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    ok_hex(hr, S_OK);

    TestFolderViews();
    TestDesktop();

    if (SUCCEEDED(hr))
        CoUninitialize();
}
