/*
 * PROJECT:     LiberNT Windows Storage
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Recent and frequent document lists of applications
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

#define AUTODEST_MAGIC   0x4C4A4441
#define AUTODEST_VERSION 1
#define AUTODEST_FOLDER  L"AutomaticDestinations"
#define AUTODEST_EXT     L"automaticDestinations-ms"

struct AUTODEST_ENTRY
{
    FILETIME LastUsed;
    DWORD UseCount;
    PIDLIST_ABSOLUTE Pidl;
};

static HRESULT
ResolveAppId(LPCWSTR pszAppID, CStringW &AppId, BOOL *pbExplicit)
{
    WCHAR Module[MAX_PATH];
    PWSTR Explicit = NULL;

    *pbExplicit = TRUE;
    if (pszAppID && *pszAppID)
    {
        AppId = pszAppID;
    }
    else if (SUCCEEDED(GetCurrentProcessExplicitAppUserModelID(&Explicit)) && Explicit && *Explicit)
    {
        AppId = Explicit;
    }
    else if (GetModuleFileNameW(NULL, Module, _countof(Module)))
    {
        AppId = Module;
        *pbExplicit = FALSE;
    }
    else
    {
        AppId.Empty();
    }

    CoTaskMemFree(Explicit);
    return AppId.IsEmpty() ? E_FAIL : S_OK;
}

HRESULT
JumpListPath(LPCWSTR pszAppID, LPCWSTR pszFolder, LPCWSTR pszExtension, BOOL bCreate, CStringW &Path)
{
    WCHAR Recent[MAX_PATH];
    BYTE Hash[8];
    CStringW AppId;
    BOOL bExplicit;
    HRESULT hr;

    hr = ResolveAppId(pszAppID, AppId, &bExplicit);
    if (FAILED(hr))
        return hr;
    AppId.MakeLower();

    hr = SHGetFolderPathW(NULL, CSIDL_RECENT | (bCreate ? CSIDL_FLAG_CREATE : 0), NULL, SHGFP_TYPE_CURRENT, Recent);
    if (FAILED(hr))
        return hr;
    Path = Recent;
    Path.AppendFormat(L"\\%s", pszFolder);
    if (bCreate && !CreateDirectoryW(Path, NULL) && GetLastError() != ERROR_ALREADY_EXISTS)
        return HRESULT_FROM_WIN32(GetLastError());

    hr = HashData((LPBYTE)AppId.GetString(), AppId.GetLength() * sizeof(WCHAR), Hash, sizeof(Hash));
    if (FAILED(hr))
        return hr;
    Path.AppendFormat(L"\\%02x%02x%02x%02x%02x%02x%02x%02x.%s",
                      Hash[0], Hash[1], Hash[2], Hash[3], Hash[4], Hash[5], Hash[6], Hash[7], pszExtension);
    return S_OK;
}

static HRESULT
OpenStore(LPCWSTR pszPath, BOOL bWrite, BOOL bCreate, IStream **ppStream)
{
    DWORD Mode = bWrite ? (STGM_READWRITE | STGM_SHARE_EXCLUSIVE) : (STGM_READ | STGM_SHARE_DENY_WRITE);
    HRESULT hr;

    for (UINT Try = 0; ; Try++)
    {
        hr = SHCreateStreamOnFileEx(pszPath, Mode, FILE_ATTRIBUTE_NORMAL, bCreate, NULL, ppStream);
        if ((hr != STG_E_SHAREVIOLATION && hr != HRESULT_FROM_WIN32(ERROR_SHARING_VIOLATION)) || Try >= 100)
            return hr;
        Sleep(10);
    }
}

static HRESULT
ReadDword(IStream *Stream, DWORD *Value)
{
    ULONG Read = 0;
    HRESULT hr = Stream->Read(Value, sizeof(*Value), &Read);

    if (SUCCEEDED(hr) && Read != sizeof(*Value))
        hr = STG_E_READFAULT;
    return hr;
}

static HRESULT
WriteDword(IStream *Stream, DWORD Value)
{
    ULONG Written = 0;
    HRESULT hr = Stream->Write(&Value, sizeof(Value), &Written);

    if (SUCCEEDED(hr) && Written != sizeof(Value))
        hr = STG_E_MEDIUMFULL;
    return hr;
}

class CAutoDestList
{
public:
    CSimpleArray<AUTODEST_ENTRY> m_Entries;

    ~CAutoDestList()
    {
        for (int i = 0; i < m_Entries.GetSize(); i++)
            ILFree(m_Entries[i].Pidl);
    }

    void Load(IStream *Stream)
    {
        DWORD Magic, Version, Count;

        if (FAILED(ReadDword(Stream, &Magic)) || Magic != AUTODEST_MAGIC ||
            FAILED(ReadDword(Stream, &Version)) || Version != AUTODEST_VERSION ||
            FAILED(ReadDword(Stream, &Count)))
        {
            return;
        }

        for (DWORD i = 0; i < Count; i++)
        {
            AUTODEST_ENTRY Entry;
            LPITEMIDLIST Pidl = NULL;

            if (FAILED(ReadDword(Stream, &Entry.LastUsed.dwLowDateTime)) ||
                FAILED(ReadDword(Stream, &Entry.LastUsed.dwHighDateTime)) ||
                FAILED(ReadDword(Stream, &Entry.UseCount)) ||
                FAILED(ILLoadFromStream(Stream, &Pidl)) || !Pidl)
            {
                ILFree(Pidl);
                return;
            }
            Entry.Pidl = (PIDLIST_ABSOLUTE)Pidl;
            if (!m_Entries.Add(Entry))
            {
                ILFree(Entry.Pidl);
                return;
            }
        }
    }

    HRESULT Save(IStream *Stream)
    {
        LARGE_INTEGER Zero = { { 0, 0 } };
        ULARGE_INTEGER End;
        HRESULT hr;

        hr = Stream->Seek(Zero, STREAM_SEEK_SET, NULL);
        if (SUCCEEDED(hr))
            hr = WriteDword(Stream, AUTODEST_MAGIC);
        if (SUCCEEDED(hr))
            hr = WriteDword(Stream, AUTODEST_VERSION);
        if (SUCCEEDED(hr))
            hr = WriteDword(Stream, m_Entries.GetSize());
        for (int i = 0; SUCCEEDED(hr) && i < m_Entries.GetSize(); i++)
        {
            hr = WriteDword(Stream, m_Entries[i].LastUsed.dwLowDateTime);
            if (SUCCEEDED(hr))
                hr = WriteDword(Stream, m_Entries[i].LastUsed.dwHighDateTime);
            if (SUCCEEDED(hr))
                hr = WriteDword(Stream, m_Entries[i].UseCount);
            if (SUCCEEDED(hr))
                hr = ILSaveToStream(Stream, m_Entries[i].Pidl);
        }
        if (SUCCEEDED(hr))
            hr = Stream->Seek(Zero, STREAM_SEEK_CUR, &End);
        if (SUCCEEDED(hr))
            hr = Stream->SetSize(End);
        if (SUCCEEDED(hr))
            hr = Stream->Commit(STGC_DEFAULT);
        return hr;
    }

    int Find(PCIDLIST_ABSOLUTE Pidl)
    {
        for (int i = 0; i < m_Entries.GetSize(); i++)
        {
            if (ILIsEqual(m_Entries[i].Pidl, Pidl))
                return i;
        }
        return -1;
    }

    BOOL Before(int First, int Second, APPDOCLISTTYPE ListType)
    {
        const AUTODEST_ENTRY &A = m_Entries[First], &B = m_Entries[Second];

        if (ListType == ADLT_FREQUENT && A.UseCount != B.UseCount)
            return A.UseCount > B.UseCount;
        return CompareFileTime(&A.LastUsed, &B.LastUsed) > 0;
    }
};

static BOOL
ProgIdHandlesApp(LPCWSTR pszProgId, LPCWSTR pszAppId, BOOL bExplicit)
{
    WCHAR Value[MAX_PATH];
    DWORD Size = sizeof(Value);

    if (bExplicit)
    {
        return RegGetValueW(HKEY_CLASSES_ROOT, pszProgId, L"AppUserModelID", RRF_RT_REG_SZ, NULL,
                            Value, &Size) == ERROR_SUCCESS && !_wcsicmp(Value, pszAppId);
    }

    Size = _countof(Value);
    return SUCCEEDED(AssocQueryStringW(ASSOCF_NOTRUNCATE, ASSOCSTR_EXECUTABLE, pszProgId, NULL, Value, &Size)) &&
           !_wcsicmp(Value, pszAppId);
}

static BOOL
IsRegisteredHandler(PCIDLIST_ABSOLUTE Pidl, LPCWSTR pszAppId, BOOL bExplicit)
{
    WCHAR Path[MAX_PATH], ProgId[MAX_PATH];
    CStringW Key;
    LPCWSTR Ext;
    DWORD Size = sizeof(ProgId);
    HKEY hKey;
    BOOL bFound = FALSE;

    if (!SHGetPathFromIDListW(Pidl, Path))
        return FALSE;
    Ext = PathFindExtensionW(Path);
    if (!*Ext)
        return FALSE;

    if (RegGetValueW(HKEY_CLASSES_ROOT, Ext, NULL, RRF_RT_REG_SZ, NULL, ProgId, &Size) == ERROR_SUCCESS &&
        ProgIdHandlesApp(ProgId, pszAppId, bExplicit))
    {
        return TRUE;
    }

    Key.Format(L"%s\\OpenWithProgids", Ext);
    if (RegOpenKeyExW(HKEY_CLASSES_ROOT, Key, 0, KEY_READ, &hKey) != ERROR_SUCCESS)
        return FALSE;
    for (DWORD i = 0; !bFound; i++)
    {
        DWORD cch = _countof(ProgId);
        if (RegEnumValueW(hKey, i, ProgId, &cch, NULL, NULL, NULL, NULL) != ERROR_SUCCESS)
            break;
        bFound = ProgIdHandlesApp(ProgId, pszAppId, bExplicit);
    }
    RegCloseKey(hKey);
    return bFound;
}

EXTERN_C HRESULT
SHELL_AddAutomaticDestination(LPCWSTR pszAppID, PCIDLIST_ABSOLUTE pidl)
{
    CComPtr<IStream> Stream;
    CAutoDestList List;
    CStringW AppId, Path;
    AUTODEST_ENTRY Entry;
    BOOL bExplicit;
    int Index;
    HRESULT hr;

    hr = ResolveAppId(pszAppID, AppId, &bExplicit);
    if (FAILED(hr))
        return hr;
    if (!IsRegisteredHandler(pidl, AppId, bExplicit))
        return S_FALSE;

    hr = JumpListPath(AppId, AUTODEST_FOLDER, AUTODEST_EXT, TRUE, Path);
    if (SUCCEEDED(hr))
        hr = OpenStore(Path, TRUE, TRUE, &Stream);
    if (FAILED(hr))
        return hr;
    List.Load(Stream);

    Index = List.Find(pidl);
    if (Index >= 0)
    {
        Entry = List.m_Entries[Index];
        List.m_Entries.RemoveAt(Index);
    }
    else
    {
        Entry.UseCount = 0;
        Entry.Pidl = ILClone(pidl);
        if (!Entry.Pidl)
            return E_OUTOFMEMORY;
    }
    Entry.UseCount++;
    GetSystemTimeAsFileTime(&Entry.LastUsed);
    if (!List.m_Entries.Add(Entry))
    {
        ILFree(Entry.Pidl);
        return E_OUTOFMEMORY;
    }
    return List.Save(Stream);
}

STDMETHODIMP
CApplicationDocumentLists::SetAppID(LPCWSTR pszAppID)
{
    if (!pszAppID || !*pszAppID)
        return E_INVALIDARG;
    m_AppId = pszAppID;
    return S_OK;
}

STDMETHODIMP
CApplicationDocumentLists::GetList(APPDOCLISTTYPE listtype, UINT cItemsDesired, REFIID riid, void **ppv)
{
    CComPtr<IObjectCollection> Collection;
    CComPtr<IStream> Stream;
    CSimpleArray<int> Order;
    CAutoDestList List;
    CStringW Path;
    HRESULT hr;

    if (!ppv)
        return E_POINTER;
    *ppv = NULL;
    if (listtype != ADLT_RECENT && listtype != ADLT_FREQUENT)
        return E_INVALIDARG;

    hr = ShellObjectCreator<CEnumerableObjectCollection>(IID_PPV_ARG(IObjectCollection, &Collection));
    if (FAILED(hr))
        return hr;
    if (SUCCEEDED(JumpListPath(m_AppId, AUTODEST_FOLDER, AUTODEST_EXT, FALSE, Path)) &&
        SUCCEEDED(OpenStore(Path, FALSE, FALSE, &Stream)))
    {
        List.Load(Stream);
    }

    for (int i = 0; i < List.m_Entries.GetSize(); i++)
    {
        int Pos = Order.GetSize();

        if (!Order.Add(i))
            return E_OUTOFMEMORY;
        while (Pos > 0 && List.Before(i, Order[Pos - 1], listtype))
        {
            Order[Pos] = Order[Pos - 1];
            Pos--;
        }
        Order[Pos] = i;
    }

    for (int i = 0; i < Order.GetSize() && (!cItemsDesired || (UINT)i < cItemsDesired); i++)
    {
        CComPtr<IShellItem> Item;

        if (SUCCEEDED(SHCreateItemFromIDList(List.m_Entries[Order[i]].Pidl, IID_PPV_ARG(IShellItem, &Item))))
        {
            hr = Collection->AddObject(Item);
            if (FAILED(hr))
                return hr;
        }
    }
    return Collection->QueryInterface(riid, ppv);
}

STDMETHODIMP
CApplicationDestinations::SetAppID(LPCWSTR pszAppID)
{
    if (!pszAppID || !*pszAppID)
        return E_INVALIDARG;
    m_AppId = pszAppID;
    return S_OK;
}

STDMETHODIMP
CApplicationDestinations::RemoveDestination(IUnknown *punk)
{
    CComHeapPtr<ITEMIDLIST_ABSOLUTE> Pidl;
    CComPtr<IShellLinkW> Link;
    CComPtr<IStream> Stream;
    CAutoDestList List;
    CStringW Path;
    int Index;
    HRESULT hr;

    if (!punk)
        return E_INVALIDARG;
    if (SUCCEEDED(punk->QueryInterface(IID_PPV_ARG(IShellLinkW, &Link))))
        hr = Link->GetIDList(&Pidl);
    else
        hr = SHGetIDListFromObject(punk, &Pidl);
    if (FAILED(hr))
        return hr;

    hr = JumpListPath(m_AppId, AUTODEST_FOLDER, AUTODEST_EXT, FALSE, Path);
    if (FAILED(hr))
        return hr;
    hr = OpenStore(Path, TRUE, FALSE, &Stream);
    if (hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) || hr == HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND))
        return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    if (FAILED(hr))
        return hr;
    List.Load(Stream);

    Index = List.Find(Pidl);
    if (Index < 0)
        return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    ILFree(List.m_Entries[Index].Pidl);
    List.m_Entries.RemoveAt(Index);
    return List.Save(Stream);
}

STDMETHODIMP
CApplicationDestinations::RemoveAllDestinations()
{
    CStringW Path;
    HRESULT hr;

    hr = JumpListPath(m_AppId, AUTODEST_FOLDER, AUTODEST_EXT, FALSE, Path);
    if (FAILED(hr))
        return hr;
    if (!DeleteFileW(Path) &&
        GetLastError() != ERROR_FILE_NOT_FOUND && GetLastError() != ERROR_PATH_NOT_FOUND)
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    return S_OK;
}
