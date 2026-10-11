/*
 * PROJECT:     LiberNT Shell
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Shortcut property page
 * COPYRIGHT:   Copyright 2005 Mike McCormack
 *              Copyright 2009 Andrew Hill
 *              Copyright 2013 Dominik Hornung
 *              Copyright 2017 Hermes Belusca-Maito
 *              Copyright 2018-2024 Katayama Hirofumi MZ <katayama.hirofumi.mz@gmail.com>
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

WINE_DEFAULT_DEBUG_CHANNEL(shell);

class CShellLinkPropPage
{
    LONG m_cRef;
    CComPtr<IShellLinkW> m_pLink;
    CComPtr<IShellLinkDataList> m_pDataList;
    CComPtr<IPersistFile> m_pFile;
    WCHAR m_szLinkPath[MAX_PATH];
    HICON m_hIcon;
    BOOL m_bInInit;
    BOOL m_bRunAs;

    BOOL OnInitDialog(HWND hwndDlg);
    void OnCommand(HWND hwndDlg, int id, UINT codeNotify);
    LRESULT OnNotify(HWND hwndDlg, LPNMHDR pnmhdr);
    void OnDestroy();

public:
    CShellLinkPropPage() : m_cRef(1), m_hIcon(NULL), m_bInInit(FALSE), m_bRunAs(FALSE)
    {
        m_szLinkPath[0] = UNICODE_NULL;
    }

    ~CShellLinkPropPage()
    {
        if (m_hIcon)
            DestroyIcon(m_hIcon);
    }

    ULONG AddRef()
    {
        return InterlockedIncrement(&m_cRef);
    }

    ULONG Release()
    {
        ULONG cRef = InterlockedDecrement(&m_cRef);
        if (!cRef)
            delete this;
        return cRef;
    }

    HRESULT Initialize(PCWSTR pszLinkPath);
    static INT_PTR CALLBACK DlgProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);
};

static INT_PTR CALLBACK
ExtendedShortcutProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch(uMsg)
    {
        case WM_INITDIALOG:
            if (lParam)
            {
                HWND hDlgCtrl = GetDlgItem(hwndDlg, IDC_SHORTEX_RUN_DIFFERENT);
                SendMessage(hDlgCtrl, BM_SETCHECK, BST_CHECKED, 0);
            }
            return TRUE;
        case WM_COMMAND:
        {
            HWND hDlgCtrl = GetDlgItem(hwndDlg, IDC_SHORTEX_RUN_DIFFERENT);
            if (LOWORD(wParam) == IDOK)
            {
                if (SendMessage(hDlgCtrl, BM_GETCHECK, 0, 0) == BST_CHECKED)
                    EndDialog(hwndDlg, 1);
                else
                    EndDialog(hwndDlg, 0);
            }
            else if (LOWORD(wParam) == IDCANCEL)
            {
                EndDialog(hwndDlg, -1);
            }
            else if (LOWORD(wParam) == IDC_SHORTEX_RUN_DIFFERENT)
            {
                if (SendMessage(hDlgCtrl, BM_GETCHECK, 0, 0) == BST_CHECKED)
                    SendMessage(hDlgCtrl, BM_SETCHECK, BST_UNCHECKED, 0);
                else
                    SendMessage(hDlgCtrl, BM_SETCHECK, BST_CHECKED, 0);
            }
        }
    }
    return FALSE;
}

static void GetTypeDescriptionByPath(PCWSTR pszFullPath, DWORD fAttributes, PWSTR szBuf, UINT cchBuf)
{
    if (fAttributes == INVALID_FILE_ATTRIBUTES && !PathFileExistsAndAttributesW(pszFullPath, &fAttributes))
        fAttributes = 0;

    SHFILEINFOW fi;
    if (!SHGetFileInfoW(pszFullPath, fAttributes, &fi, sizeof(fi), SHGFI_TYPENAME | SHGFI_USEFILEATTRIBUTES))
    {
        ERR("SHGetFileInfoW failed for %ls (%lu)\n", pszFullPath, GetLastError());
        fi.szTypeName[0] = UNICODE_NULL;
    }

    BOOL fFolder = (fAttributes & FILE_ATTRIBUTE_DIRECTORY);
    LPCWSTR pwszExt = fFolder ? L"" : PathFindExtensionW(pszFullPath);
    if (pwszExt[0])
    {
        if (!fi.szTypeName[0])
            StringCchPrintfW(szBuf, cchBuf, L"%s", pwszExt + 1);
        else
            StringCchPrintfW(szBuf, cchBuf, L"%s (%s)", fi.szTypeName, pwszExt);
    }
    else
    {
        StringCchPrintfW(szBuf, cchBuf, L"%s", fi.szTypeName);
    }
}

static HICON CreateShortcutIcon(LPCWSTR wszIconPath, INT IconIndex)
{
    const INT cx = GetSystemMetrics(SM_CXICON), cy = GetSystemMetrics(SM_CYICON);
    const COLORREF crMask = GetSysColor(COLOR_3DFACE);
    WCHAR wszLnkIcon[MAX_PATH];
    int lnk_idx;
    HDC hDC;
    HIMAGELIST himl = ImageList_Create(cx, cy, ILC_COLOR32 | ILC_MASK, 1, 1);
    HICON hIcon = NULL, hNewIcon = NULL, hShortcut;

    if (HLM_GetIconW(IDI_SHELL_SHORTCUT - 1, wszLnkIcon, _countof(wszLnkIcon), &lnk_idx))
    {
        ::ExtractIconExW(wszLnkIcon, lnk_idx, &hShortcut, NULL, 1);
    }
    else
    {
        hShortcut = (HICON)LoadImageW(shell32_hInstance, MAKEINTRESOURCE(IDI_SHELL_SHORTCUT),
                                      IMAGE_ICON, cx, cy, 0);
    }

    ::ExtractIconExW(wszIconPath, IconIndex, &hIcon, NULL, 1);
    if (!hIcon || !hShortcut || !himl)
        goto cleanup;

    hDC = CreateCompatibleDC(NULL);
    if (hDC)
    {
        BITMAPINFO bi;
        ZeroMemory(&bi, sizeof(bi));
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = cx;
        bi.bmiHeader.biHeight = cy;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        LPVOID pvBits;
        HBITMAP hbm = CreateDIBSection(hDC, &bi, DIB_RGB_COLORS, &pvBits, NULL, 0);
        if (hbm)
        {
            HGDIOBJ hbmOld = SelectObject(hDC, hbm);
            {
                HBRUSH hbr = CreateSolidBrush(crMask);
                RECT rc = { 0, 0, cx, cy };
                FillRect(hDC, &rc, hbr);
                DeleteObject(hbr);

                DrawIconEx(hDC, 0, 0, hIcon, cx, cy, 0, NULL, DI_NORMAL);
                DrawIconEx(hDC, 0, 0, hShortcut, cx, cy, 0, NULL, DI_NORMAL);
            }
            SelectObject(hDC, hbmOld);

            INT iAdded = ImageList_AddMasked(himl, hbm, crMask);
            hNewIcon = ImageList_GetIcon(himl, iAdded, ILD_NORMAL | ILD_TRANSPARENT);

            DeleteObject(hbm);
        }
        DeleteDC(hDC);
    }

cleanup:
    if (hIcon)
        DestroyIcon(hIcon);
    if (hShortcut)
        DestroyIcon(hShortcut);
    if (himl)
        ImageList_Destroy(himl);

    return hNewIcon;
}

HRESULT CShellLinkPropPage::Initialize(PCWSTR pszLinkPath)
{
    DWORD dwFlags = 0;
    HRESULT hr;

    hr = SHCoCreateInstance(NULL, &CLSID_ShellLink, NULL, IID_PPV_ARG(IShellLinkW, &m_pLink));
    if (FAILED(hr))
        return hr;
    hr = m_pLink->QueryInterface(IID_PPV_ARG(IPersistFile, &m_pFile));
    if (FAILED(hr))
        return hr;
    hr = m_pFile->Load(pszLinkPath, STGM_READ);
    if (FAILED(hr))
        return hr;
    hr = m_pLink->QueryInterface(IID_PPV_ARG(IShellLinkDataList, &m_pDataList));
    if (FAILED(hr))
        return hr;

    m_pDataList->GetFlags(&dwFlags);
    m_bRunAs = (dwFlags & SLDF_RUNAS_USER) != 0;
    return StringCchCopyW(m_szLinkPath, _countof(m_szLinkPath), pszLinkPath);
}

BOOL CShellLinkPropPage::OnInitDialog(HWND hwndDlg)
{
    WCHAR szPath[MAX_PATH], szText[INFOTIPSIZE];
    WIN32_FIND_DATAW wfd;
    DWORD dwFlags = 0;
    WORD wHotKey = 0;
    INT iShowCmd = SW_SHOWNORMAL;

    m_pLink->Resolve(NULL, SLR_NO_UI | SLR_NOUPDATE | SLR_NOSEARCH | SLR_NOTRACK);

    m_bInInit = TRUE;
    m_pDataList->GetFlags(&dwFlags);
    UINT darwin = dwFlags & SLDF_HAS_DARWINID;

    SHFILEINFOW fi;
    if (!SHGetFileInfoW(m_szLinkPath, 0, &fi, sizeof(fi), SHGFI_TYPENAME | SHGFI_ICON))
    {
        ERR("SHGetFileInfoW failed for %ls (%lu)\n", m_szLinkPath, GetLastError());
        fi.szTypeName[0] = L'\0';
        fi.hIcon = NULL;
    }

    if (fi.hIcon)
    {
        if (m_hIcon)
            DestroyIcon(m_hIcon);
        m_hIcon = fi.hIcon;
        SendDlgItemMessageW(hwndDlg, IDC_SHORTCUT_ICON, STM_SETICON, (WPARAM)m_hIcon, 0);
    }

    if (!SHGetFileInfoW(m_szLinkPath, 0, &fi, sizeof(fi), SHGFI_DISPLAYNAME))
        fi.szDisplayName[0] = UNICODE_NULL;
    SetDlgItemTextW(hwndDlg, IDC_SHORTCUT_TEXT, fi.szDisplayName);

    szPath[0] = UNICODE_NULL;
    m_pLink->GetPath(szPath, _countof(szPath), &wfd, 0);
    if (szPath[0])
    {
        WCHAR buf[MAX_PATH];
        GetTypeDescriptionByPath(szPath, wfd.dwFileAttributes, buf, _countof(buf));
        SetDlgItemTextW(hwndDlg, IDC_SHORTCUT_TYPE_EDIT, buf);

        WCHAR target[MAX_PATH];
        StringCchCopyW(target, _countof(target), szPath);
        PathRemoveFileSpecW(target);
        SetDlgItemTextW(hwndDlg, IDC_SHORTCUT_LOCATION_EDIT, PathFindFileNameW(target));

        WCHAR newpath[MAX_PATH * 2];
        newpath[0] = UNICODE_NULL;
        if (wcschr(szPath, ' '))
            StringCchPrintfExW(newpath, _countof(newpath), NULL, NULL, 0, L"\"%ls\"", szPath);
        else
            StringCchCopyExW(newpath, _countof(newpath), szPath, NULL, NULL, 0);

        szText[0] = UNICODE_NULL;
        if (SUCCEEDED(m_pLink->GetArguments(szText, _countof(szText))) && szText[0])
        {
            StringCchCatW(newpath, _countof(newpath), L" ");
            StringCchCatW(newpath, _countof(newpath), szText);
        }
        SetDlgItemTextW(hwndDlg, IDC_SHORTCUT_TARGET_TEXT, newpath);
    }

    szText[0] = UNICODE_NULL;
    if (SUCCEEDED(m_pLink->GetWorkingDirectory(szText, _countof(szText))) && szText[0])
        SetDlgItemTextW(hwndDlg, IDC_SHORTCUT_START_IN_EDIT, szText);

    szText[0] = UNICODE_NULL;
    if (SUCCEEDED(m_pLink->GetDescription(szText, _countof(szText))) && szText[0])
        SetDlgItemTextW(hwndDlg, IDC_SHORTCUT_COMMENT_EDIT, szText);

    m_pLink->GetHotkey(&wHotKey);
    SendDlgItemMessageW(hwndDlg, IDC_SHORTCUT_KEY_HOTKEY, HKM_SETHOTKEY, wHotKey, 0);

    m_pLink->GetShowCmd(&iShowCmd);
    const WORD runstrings[] = { IDS_SHORTCUT_RUN_NORMAL, IDS_SHORTCUT_RUN_MIN, IDS_SHORTCUT_RUN_MAX };
    const DWORD runshowcmd[] = { SW_SHOWNORMAL, SW_SHOWMINNOACTIVE, SW_SHOWMAXIMIZED };
    HWND hRunCombo = GetDlgItem(hwndDlg, IDC_SHORTCUT_RUN_COMBO);
    for (UINT i = 0; i < _countof(runstrings); ++i)
    {
        WCHAR buf[MAX_PATH];
        if (!LoadStringW(shell32_hInstance, runstrings[i], buf, _countof(buf)))
            break;

        int index = SendMessageW(hRunCombo, CB_ADDSTRING, 0, (LPARAM)buf);
        if (index < 0)
            continue;
        SendMessageW(hRunCombo, CB_SETITEMDATA, index, runshowcmd[i]);
        if (!i || (DWORD)iShowCmd == runshowcmd[i])
            SendMessageW(hRunCombo, CB_SETCURSEL, index, 0);
    }

    BOOL disablecontrols = FALSE;
    if (darwin)
    {
        disablecontrols = TRUE;
        EnableWindow(GetDlgItem(hwndDlg, IDC_SHORTCUT_FIND), FALSE);
        EnableWindow(GetDlgItem(hwndDlg, IDC_SHORTCUT_CHANGE_ICON), FALSE);
    }
    else
    {
        WCHAR path[MAX_PATH * 2];
        path[0] = UNICODE_NULL;
        HRESULT hr = m_pLink->GetPath(path, _countof(path), NULL, SLGP_RAWPATH);
        if (FAILED(hr))
            hr = m_pLink->GetPath(path, _countof(path), NULL, 0);
#if DBG
        if (GetKeyState(VK_CONTROL) < 0)
        {
            hr = S_OK;
            path[0] = UNICODE_NULL;
        }
#endif
        if (hr != S_OK)
        {
            disablecontrols = TRUE;
            LPITEMIDLIST pidl;
            if (m_pLink->GetIDList(&pidl) == S_OK)
            {
                path[0] = UNICODE_NULL;
                SHGetNameAndFlagsW(pidl, SHGDN_FORADDRESSBAR | SHGDN_FORPARSING, path, _countof(path), NULL);
                SetDlgItemTextW(hwndDlg, IDC_SHORTCUT_TYPE_EDIT, path);
                SetDlgItemTextW(hwndDlg, IDC_SHORTCUT_TARGET_TEXT, path);
                ILRemoveLastID(pidl);
                path[0] = UNICODE_NULL;
                SetDlgItemTextW(hwndDlg, IDC_SHORTCUT_START_IN_EDIT, path);
                SHGetNameAndFlagsW(pidl, SHGDN_NORMAL, path, _countof(path), NULL);
                SetDlgItemTextW(hwndDlg, IDC_SHORTCUT_LOCATION_EDIT, path);
                ILFree(pidl);
            }
            EnableWindow(GetDlgItem(hwndDlg, IDC_SHORTCUT_ADVANCED), FALSE);
            EnableWindow(GetDlgItem(hwndDlg, IDC_SHORTCUT_START_IN_EDIT), FALSE);
        }
        else
        {
            ASSERT(FAILED(hr) || !(path[0] == ':' && path[1] == ':' && path[2] == '{'));
        }
    }

    HWND hWndTarget = GetDlgItem(hwndDlg, IDC_SHORTCUT_TARGET_TEXT);
    EnableWindow(hWndTarget, !disablecontrols);
    PostMessage(hWndTarget, EM_SETSEL, 0, -1);

    SHAutoComplete(hWndTarget, SHACF_DEFAULT);
    SHAutoComplete(GetDlgItem(hwndDlg, IDC_SHORTCUT_START_IN_EDIT), SHACF_DEFAULT);

    m_bInInit = FALSE;

    return TRUE;
}

void CShellLinkPropPage::OnCommand(HWND hwndDlg, int id, UINT codeNotify)
{
    switch (id)
    {
        case IDC_SHORTCUT_FIND:
        {
            LPITEMIDLIST pidl;
            if (m_pLink->GetIDList(&pidl) == S_OK)
            {
                SHOpenFolderAndSelectItems(pidl, 0, NULL, 0);
                ILFree(pidl);
            }
            return;
        }

        case IDC_SHORTCUT_CHANGE_ICON:
        {
            SHFILEINFOW fi;
            INT IconIndex = 0;
            WCHAR wszIcoPath[MAX_PATH], wszPath[MAX_PATH];
            *wszIcoPath = UNICODE_NULL;
            *wszPath = UNICODE_NULL;

            m_pLink->GetIconLocation(wszIcoPath, _countof(wszIcoPath), &IconIndex);
            if (!StrIsNullOrEmpty(wszIcoPath))
            {
                PWSTR pszPath = wszIcoPath;
                if (*wszIcoPath == '.')
                {
                    if (SUCCEEDED(StringCchPrintfW(wszPath, _countof(wszPath), L"x:\\x%s", wszIcoPath)) &&
                        SHGetFileInfoW(wszPath, 0, &fi, sizeof(fi), SHGFI_ICONLOCATION | SHGFI_USEFILEATTRIBUTES))
                    {
                        pszPath = fi.szDisplayName;
                        IconIndex = fi.iIcon;
                    }
                }

                if (FAILED(StringCchCopyW(wszPath, _countof(wszPath), pszPath)))
                    *wszPath = UNICODE_NULL;
            }
            else
            {
                WCHAR wszTarget[MAX_PATH];
                *wszTarget = UNICODE_NULL;
                m_pLink->GetPath(wszTarget, _countof(wszTarget), NULL, 0);
                if (*wszTarget)
                    FindExecutableW(wszTarget, NULL, wszPath);
            }

            LPITEMIDLIST pidl;
            if (!*wszPath && m_pLink->GetIDList(&pidl) == S_OK)
            {
                if (SHGetFileInfoW((PWSTR)pidl, 0, &fi, sizeof(fi), SHGFI_ICONLOCATION | SHGFI_PIDL) &&
                    SUCCEEDED(StringCchCopyW(wszPath, _countof(wszPath), fi.szDisplayName)))
                {
                    IconIndex = fi.iIcon;
                }
                ILFree(pidl);
            }

            if (PickIconDlg(hwndDlg, wszPath, _countof(wszPath), &IconIndex))
            {
                m_pLink->SetIconLocation(wszPath, IconIndex);
                PropSheet_Changed(GetParent(hwndDlg), hwndDlg);

                HICON hIconLarge = CreateShortcutIcon(wszPath, IconIndex);
                if (hIconLarge)
                {
                    if (m_hIcon)
                        DestroyIcon(m_hIcon);
                    m_hIcon = hIconLarge;
                    SendDlgItemMessageW(hwndDlg, IDC_SHORTCUT_ICON, STM_SETICON, (WPARAM)m_hIcon, 0);
                }
            }
            return;
        }

        case IDC_SHORTCUT_ADVANCED:
        {
            INT_PTR result = DialogBoxParamW(shell32_hInstance, MAKEINTRESOURCEW(IDD_SHORTCUT_EXTENDED_PROPERTIES), hwndDlg, ExtendedShortcutProc, (LPARAM)m_bRunAs);
            if (result == 1 || result == 0)
            {
                if (m_bRunAs != result)
                {
                    PropSheet_Changed(GetParent(hwndDlg), hwndDlg);
                }

                m_bRunAs = result;
            }
            return;
        }
    }
    if (codeNotify == EN_CHANGE || codeNotify == CBN_SELCHANGE)
    {
        if (!m_bInInit)
            PropSheet_Changed(GetParent(hwndDlg), hwndDlg);
    }
}

LRESULT CShellLinkPropPage::OnNotify(HWND hwndDlg, LPNMHDR pnmhdr)
{
    WCHAR wszBuf[MAX_PATH];
    LPPSHNOTIFY lppsn = (LPPSHNOTIFY)pnmhdr;

    if (lppsn->hdr.code == PSN_APPLY)
    {
        GetDlgItemTextW(hwndDlg, IDC_SHORTCUT_START_IN_EDIT, wszBuf, _countof(wszBuf));
        m_pLink->SetWorkingDirectory(wszBuf);

        HWND hWndTarget = GetDlgItem(hwndDlg, IDC_SHORTCUT_TARGET_TEXT);
        GetWindowTextW(hWndTarget, wszBuf, _countof(wszBuf));
        if (IsWindowEnabled(hWndTarget))
        {
            LPWSTR lpszArgs = NULL;
            LPWSTR unquoted = wszBuf;
            StrTrimW(unquoted, L" ");

            if (!PathFileExistsW(unquoted))
            {
                lpszArgs = PathGetArgsW(unquoted);
                PathRemoveArgsW(unquoted);
                StrTrimW(lpszArgs, L" ");
            }
            if (unquoted[0] == '"' && unquoted[wcslen(unquoted) - 1] == '"')
                PathUnquoteSpacesW(unquoted);

            WCHAR *pwszExt = PathFindExtensionW(unquoted);
            if (!_wcsicmp(pwszExt, L".lnk"))
            {
                MessageBoxW(hwndDlg, L"You cannot create a link to a shortcut", NULL, MB_ICONERROR);
                SetWindowLongPtr(hwndDlg, DWLP_MSGRESULT, PSNRET_INVALID_NOCHANGEPAGE);
                return TRUE;
            }

            if (!PathFileExistsW(unquoted))
            {
                MessageBoxW(hwndDlg, L"The specified file name in the target box is invalid", NULL, MB_ICONERROR);
                SetWindowLongPtr(hwndDlg, DWLP_MSGRESULT, PSNRET_INVALID_NOCHANGEPAGE);
                return TRUE;
            }

            m_pLink->SetPath(unquoted);
            m_pLink->SetArguments(lpszArgs ? lpszArgs : L"\0");
        }

        m_pLink->SetHotkey((WORD)SendDlgItemMessageW(hwndDlg, IDC_SHORTCUT_KEY_HOTKEY, HKM_GETHOTKEY, 0, 0));

        int index = (int)SendDlgItemMessageW(hwndDlg, IDC_SHORTCUT_RUN_COMBO, CB_GETCURSEL, 0, 0);
        if (index != CB_ERR)
        {
            m_pLink->SetShowCmd((INT)SendDlgItemMessageW(hwndDlg, IDC_SHORTCUT_RUN_COMBO, CB_GETITEMDATA, index, 0));
        }

        DWORD dwFlags = 0;
        m_pDataList->GetFlags(&dwFlags);
        dwFlags = m_bRunAs ? (dwFlags | SLDF_RUNAS_USER) : (dwFlags & ~SLDF_RUNAS_USER);
        m_pDataList->SetFlags(dwFlags);

        TRACE("This %p m_szLinkPath %S\n", this, m_szLinkPath);
        m_pFile->Save(m_szLinkPath, TRUE);
        SHChangeNotify(SHCNE_UPDATEITEM, SHCNF_PATHW, m_szLinkPath, NULL);
        SetWindowLongPtr(hwndDlg, DWLP_MSGRESULT, PSNRET_NOERROR);
        return TRUE;
    }
    return FALSE;
}

void CShellLinkPropPage::OnDestroy()
{
    if (m_hIcon)
    {
        DestroyIcon(m_hIcon);
        m_hIcon = NULL;
    }
}

INT_PTR CALLBACK
CShellLinkPropPage::DlgProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    LPPROPSHEETPAGEW ppsp;
    CShellLinkPropPage *pThis = reinterpret_cast<CShellLinkPropPage *>(GetWindowLongPtr(hwndDlg, DWLP_USER));

    switch (uMsg)
    {
        case WM_INITDIALOG:
            ppsp = (LPPROPSHEETPAGEW)lParam;
            if (ppsp == NULL)
                break;

            pThis = reinterpret_cast<CShellLinkPropPage *>(ppsp->lParam);
            SetWindowLongPtr(hwndDlg, DWLP_USER, (LONG_PTR)pThis);
            return pThis->OnInitDialog(hwndDlg);

        case WM_NOTIFY:
            if (pThis)
                return pThis->OnNotify(hwndDlg, (NMHDR *)lParam);
            break;

        case WM_COMMAND:
            if (pThis)
                pThis->OnCommand(hwndDlg, LOWORD(wParam), HIWORD(wParam));
            break;

        case WM_DESTROY:
            if (pThis)
                pThis->OnDestroy();
            break;

        default:
            break;
    }

    return FALSE;
}

static BOOL IsShellLinkFile(PCWSTR pszPath)
{
    WCHAR szClsid[40];
    DWORD cbClsid = sizeof(szClsid);
    CLSID clsid;
    HKEY hKey;

    if (FAILED(HCR_GetProgIdKeyOfExtension(PathFindExtensionW(pszPath), &hKey, FALSE)))
        return FALSE;
    LSTATUS error = RegGetValueW(hKey, L"CLSID", NULL, RRF_RT_REG_SZ, NULL, szClsid, &cbClsid);
    RegCloseKey(hKey);
    return error == ERROR_SUCCESS && SUCCEEDED(CLSIDFromString(szClsid, &clsid)) &&
           IsEqualCLSID(clsid, CLSID_ShellLink);
}

HRESULT
SHELL32_AddShellLinkPropPage(PCWSTR pszLinkPath, LPFNADDPROPSHEETPAGE pfnAddPage, LPARAM lParam)
{
    if (!IsShellLinkFile(pszLinkPath))
        return S_FALSE;

    CShellLinkPropPage *pPage = new CShellLinkPropPage();
    if (!pPage)
        return E_OUTOFMEMORY;

    HRESULT hr = pPage->Initialize(pszLinkPath);
    if (FAILED(hr))
    {
        pPage->Release();
        return hr;
    }

    pPage->AddRef();
    HPROPSHEETPAGE hPage = SH_CreatePropertySheetPageEx(IDD_SHORTCUT_PROPERTIES, CShellLinkPropPage::DlgProc,
                                                        (LPARAM)pPage, NULL,
                                                        &PropSheetPageLifetimeCallback<CShellLinkPropPage>);
    if (!hPage)
        pPage->Release();
    hr = AddPropSheetPage(hPage, pfnAddPage, lParam);
    pPage->Release();
    return hr;
}
