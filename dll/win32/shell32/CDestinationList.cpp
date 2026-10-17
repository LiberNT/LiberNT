/*
 * PROJECT:     LiberNT shell32
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Custom jump list builder
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

#define DESTLIST_MAGIC          0x4C4A4443
#define DESTLIST_VERSION        1
#define DESTLIST_SECTION_CUSTOM 1
#define DESTLIST_SECTION_KNOWN  2
#define DESTLIST_SECTION_TASKS  3
#define DESTLIST_ITEM_IDLIST    1
#define DESTLIST_ITEM_LINK      2
#define DESTLIST_DEFAULT_SLOTS  10

static HRESULT
DestListWrite(IStream *Stream, const void *Data, ULONG Size)
{
    ULONG Written = 0;
    HRESULT hr = Stream->Write(Data, Size, &Written);

    if (SUCCEEDED(hr) && Written != Size)
        hr = STG_E_MEDIUMFULL;
    return hr;
}

static HRESULT
DestListWriteDword(IStream *Stream, DWORD Value)
{
    return DestListWrite(Stream, &Value, sizeof(Value));
}

static HRESULT
DestListCreateEmpty(REFIID riid, void **ppv)
{
    return ShellObjectCreator<CEnumerableObjectCollection>(riid, ppv);
}

HRESULT
CDestinationList::ResolveAppId(LPCWSTR pszAppID, CStringW &AppId)
{
    WCHAR Module[MAX_PATH];
    PWSTR Explicit = NULL;

    if (pszAppID && *pszAppID)
        AppId = pszAppID;
    else if (!m_AppId.IsEmpty())
        AppId = m_AppId;
    else if (SUCCEEDED(GetCurrentProcessExplicitAppUserModelID(&Explicit)) && Explicit && *Explicit)
        AppId = Explicit;
    else if (GetModuleFileNameW(NULL, Module, _countof(Module)))
        AppId = Module;
    else
        AppId.Empty();

    if (Explicit)
        CoTaskMemFree(Explicit);
    if (AppId.IsEmpty())
        return E_FAIL;
    AppId.MakeLower();
    return S_OK;
}

HRESULT
CDestinationList::ListPath(LPCWSTR pszAppID, BOOL Create, CStringW &Path)
{
    WCHAR Recent[MAX_PATH];
    BYTE Hash[8];
    CStringW AppId;
    HRESULT hr;

    hr = ResolveAppId(pszAppID, AppId);
    if (FAILED(hr))
        return hr;

    hr = SHGetFolderPathW(NULL, CSIDL_RECENT | (Create ? CSIDL_FLAG_CREATE : 0), NULL, SHGFP_TYPE_CURRENT, Recent);
    if (FAILED(hr))
        return hr;
    Path = Recent;
    Path += L"\\CustomDestinations";
    if (Create && !CreateDirectoryW(Path, NULL) && GetLastError() != ERROR_ALREADY_EXISTS)
        return HRESULT_FROM_WIN32(GetLastError());

    hr = HashData((LPBYTE)AppId.GetString(), AppId.GetLength() * sizeof(WCHAR), Hash, sizeof(Hash));
    if (FAILED(hr))
        return hr;
    Path.AppendFormat(L"\\%02x%02x%02x%02x%02x%02x%02x%02x.customDestinations-ms",
                      Hash[0], Hash[1], Hash[2], Hash[3], Hash[4], Hash[5], Hash[6], Hash[7]);
    return S_OK;
}

HRESULT
CDestinationList::WriteSection(DWORD Type, LPCWSTR Name, DWORD Category, IObjectArray *poa)
{
    ULARGE_INTEGER Start;
    LARGE_INTEGER Zero = { { 0, 0 } };
    UINT Count = 0, i;
    HRESULT hr;

    if (!m_Building)
        return E_UNEXPECTED;

    if (poa)
    {
        hr = poa->GetCount(&Count);
        if (FAILED(hr))
            return hr;
        for (i = 0; i < Count; i++)
        {
            CComPtr<IShellLinkW> Link;
            CComPtr<IShellItem> Item;

            if (FAILED(poa->GetAt(i, IID_PPV_ARG(IShellLinkW, &Link))) &&
                FAILED(poa->GetAt(i, IID_PPV_ARG(IShellItem, &Item))))
            {
                return E_INVALIDARG;
            }
        }
    }

    hr = m_Pending->Seek(Zero, STREAM_SEEK_CUR, &Start);
    if (FAILED(hr))
        return hr;

    hr = DestListWriteDword(m_Pending, Type);
    if (SUCCEEDED(hr) && Type == DESTLIST_SECTION_CUSTOM)
    {
        DWORD Length = (DWORD)wcslen(Name);
        hr = DestListWriteDword(m_Pending, Length);
        if (SUCCEEDED(hr))
            hr = DestListWrite(m_Pending, Name, Length * sizeof(WCHAR));
    }
    else if (SUCCEEDED(hr) && Type == DESTLIST_SECTION_KNOWN)
    {
        hr = DestListWriteDword(m_Pending, Category);
    }
    if (SUCCEEDED(hr))
        hr = DestListWriteDword(m_Pending, Count);

    for (i = 0; SUCCEEDED(hr) && i < Count; i++)
    {
        CComPtr<IShellLinkW> Link;
        CComPtr<IPersistStream> Persist;
        CComPtr<IShellItem> Item;
        CComHeapPtr<ITEMIDLIST> Pidl;

        if (SUCCEEDED(poa->GetAt(i, IID_PPV_ARG(IShellLinkW, &Link))) &&
            SUCCEEDED(Link->QueryInterface(IID_PPV_ARG(IPersistStream, &Persist))))
        {
            hr = DestListWriteDword(m_Pending, DESTLIST_ITEM_LINK);
            if (SUCCEEDED(hr))
                hr = Persist->Save(m_Pending, FALSE);
        }
        else
        {
            hr = poa->GetAt(i, IID_PPV_ARG(IShellItem, &Item));
            if (SUCCEEDED(hr))
                hr = SHGetIDListFromObject(Item, &Pidl);
            if (SUCCEEDED(hr))
                hr = DestListWriteDword(m_Pending, DESTLIST_ITEM_IDLIST);
            if (SUCCEEDED(hr))
                hr = ILSaveToStream(m_Pending, Pidl);
        }
    }

    if (FAILED(hr))
    {
        LARGE_INTEGER Back;

        Back.QuadPart = (LONGLONG)Start.QuadPart;
        m_Pending->Seek(Back, STREAM_SEEK_SET, NULL);
        m_Pending->SetSize(Start);
        return hr;
    }

    m_Sections++;
    return S_OK;
}

STDMETHODIMP
CDestinationList::SetAppID(LPCWSTR pszAppID)
{
    if (!pszAppID || !*pszAppID)
        return E_INVALIDARG;

    CComCritSecLock<CComAutoCriticalSection> lock(m_Lock);
    if (m_Building)
        return E_UNEXPECTED;
    m_AppId = pszAppID;
    return S_OK;
}

STDMETHODIMP
CDestinationList::BeginList(UINT *pcMinSlots, REFIID riid, void **ppv)
{
    DWORD Slots = DESTLIST_DEFAULT_SLOTS, Size = sizeof(Slots);
    CComPtr<IStream> Pending;
    HRESULT hr;

    if (ppv)
        *ppv = NULL;
    if (!pcMinSlots || !ppv)
        return E_POINTER;

    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced",
                     L"JumpListItems_Maximum", RRF_RT_REG_DWORD, NULL, &Slots, &Size) != ERROR_SUCCESS)
    {
        Slots = DESTLIST_DEFAULT_SLOTS;
    }

    hr = CreateStreamOnHGlobal(NULL, TRUE, &Pending);
    if (FAILED(hr))
        return hr;
    hr = DestListCreateEmpty(riid, ppv);
    if (FAILED(hr))
        return hr;

    CComCritSecLock<CComAutoCriticalSection> lock(m_Lock);
    m_Pending = Pending;
    m_Sections = 0;
    m_Building = TRUE;
    *pcMinSlots = Slots;
    return S_OK;
}

STDMETHODIMP
CDestinationList::AppendCategory(LPCWSTR pszCategory, IObjectArray *poa)
{
    if (!pszCategory || !*pszCategory || !poa)
        return E_INVALIDARG;

    CComCritSecLock<CComAutoCriticalSection> lock(m_Lock);
    return WriteSection(DESTLIST_SECTION_CUSTOM, pszCategory, 0, poa);
}

STDMETHODIMP
CDestinationList::AppendKnownCategory(KNOWNDESTCATEGORY category)
{
    if (category != KDC_FREQUENT && category != KDC_RECENT)
        return E_INVALIDARG;

    CComCritSecLock<CComAutoCriticalSection> lock(m_Lock);
    return WriteSection(DESTLIST_SECTION_KNOWN, NULL, category, NULL);
}

STDMETHODIMP
CDestinationList::AddUserTasks(IObjectArray *poa)
{
    if (!poa)
        return E_INVALIDARG;

    CComCritSecLock<CComAutoCriticalSection> lock(m_Lock);
    return WriteSection(DESTLIST_SECTION_TASKS, NULL, 0, poa);
}

STDMETHODIMP
CDestinationList::CommitList()
{
    CComPtr<IStream> File;
    ULARGE_INTEGER All;
    LARGE_INTEGER Zero = { { 0, 0 } };
    CStringW Path;
    HRESULT hr;

    CComCritSecLock<CComAutoCriticalSection> lock(m_Lock);
    if (!m_Building)
        return E_UNEXPECTED;

    hr = ListPath(NULL, TRUE, Path);
    if (SUCCEEDED(hr))
        hr = SHCreateStreamOnFileEx(Path, STGM_CREATE | STGM_WRITE | STGM_SHARE_DENY_WRITE,
                                    FILE_ATTRIBUTE_NORMAL, TRUE, NULL, &File);
    if (SUCCEEDED(hr))
        hr = DestListWriteDword(File, DESTLIST_MAGIC);
    if (SUCCEEDED(hr))
        hr = DestListWriteDword(File, DESTLIST_VERSION);
    if (SUCCEEDED(hr))
        hr = DestListWriteDword(File, m_Sections);
    if (SUCCEEDED(hr))
        hr = m_Pending->Seek(Zero, STREAM_SEEK_SET, NULL);
    if (SUCCEEDED(hr))
    {
        All.QuadPart = ~0ULL;
        hr = m_Pending->CopyTo(File, All, NULL, NULL);
    }
    if (SUCCEEDED(hr))
        hr = File->Commit(STGC_DEFAULT);
    if (FAILED(hr))
        return hr;

    m_Pending.Release();
    m_Sections = 0;
    m_Building = FALSE;
    return S_OK;
}

STDMETHODIMP
CDestinationList::GetRemovedDestinations(REFIID riid, void **ppv)
{
    if (!ppv)
        return E_POINTER;
    *ppv = NULL;
    return DestListCreateEmpty(riid, ppv);
}

STDMETHODIMP
CDestinationList::DeleteList(LPCWSTR pszAppID)
{
    CStringW Path;
    HRESULT hr;

    CComCritSecLock<CComAutoCriticalSection> lock(m_Lock);
    hr = ListPath(pszAppID, FALSE, Path);
    if (FAILED(hr))
        return hr;
    if (!DeleteFileW(Path) &&
        GetLastError() != ERROR_FILE_NOT_FOUND && GetLastError() != ERROR_PATH_NOT_FOUND)
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    return S_OK;
}

STDMETHODIMP
CDestinationList::AbortList()
{
    CComCritSecLock<CComAutoCriticalSection> lock(m_Lock);

    m_Pending.Release();
    m_Sections = 0;
    m_Building = FALSE;
    return S_OK;
}
