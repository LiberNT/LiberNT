/*
 * PROJECT:     LiberNT Shell
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     File system item property store
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <precomp.h>
#include <propkey.h>
#include <propvarutil.h>
#include <aclapi.h>

static void
StoreValue(IPropertyStore *pStore, REFPROPERTYKEY key, PROPVARIANT &pv, HRESULT hr)
{
    if (SUCCEEDED(hr))
    {
        pStore->SetValue(key, pv);
        PropVariantClear(&pv);
    }
}

static void
StoreString(IPropertyStore *pStore, REFPROPERTYKEY key, PCWSTR psz)
{
    PROPVARIANT pv;
    if (psz)
        StoreValue(pStore, key, pv, InitPropVariantFromString(psz, &pv));
}

static void
StoreUInt32(IPropertyStore *pStore, REFPROPERTYKEY key, ULONG value)
{
    PROPVARIANT pv;
    StoreValue(pStore, key, pv, InitPropVariantFromUInt32(value, &pv));
}

static void
StoreBoolean(IPropertyStore *pStore, REFPROPERTYKEY key, BOOL value)
{
    PROPVARIANT pv;
    StoreValue(pStore, key, pv, InitPropVariantFromBoolean(value, &pv));
}

static void
StoreFileTime(IPropertyStore *pStore, REFPROPERTYKEY key, const FILETIME &ft)
{
    PROPVARIANT pv;
    StoreValue(pStore, key, pv, InitPropVariantFromFileTime(&ft, &pv));
}

static void
StoreNamedBoolean(IPropertyStore *pStore, PCWSTR pszName, BOOL value)
{
    PROPERTYKEY key;
    if (SUCCEEDED(PSGetPropertyKeyFromName(pszName, &key)))
        StoreBoolean(pStore, key, value);
}

static void
StoreNarrowPath(IPropertyStore *pStore, REFPROPERTYKEY key, PCWSTR pszName, PCWSTR pszFolder)
{
    WCHAR szText[MAX_PATH * 2];

    if (pszFolder && *pszFolder)
        StringCchPrintfW(szText, _countof(szText), L"%s (%s)", pszName, pszFolder);
    else
        StringCchCopyW(szText, _countof(szText), pszName);
    StoreString(pStore, key, szText);
}

static HRESULT
GetOwnerName(PCWSTR pszPath, PWSTR pszOwner, UINT cchOwner)
{
    PSECURITY_DESCRIPTOR pSD = NULL;
    PSID pOwner = NULL;
    WCHAR szName[256], szDomain[256];
    DWORD cchName = _countof(szName), cchDomain = _countof(szDomain);
    SID_NAME_USE Use;
    DWORD dwError;
    BOOL bFound;

    dwError = GetNamedSecurityInfoW(const_cast<PWSTR>(pszPath), SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION,
                                    &pOwner, NULL, NULL, NULL, &pSD);
    if (dwError != ERROR_SUCCESS)
        return HRESULT_FROM_WIN32(dwError);
    bFound = pOwner && LookupAccountSidW(NULL, pOwner, szName, &cchName, szDomain, &cchDomain, &Use);
    dwError = bFound ? ERROR_SUCCESS : GetLastError();
    LocalFree(pSD);
    if (!bFound)
        return HRESULT_FROM_WIN32(dwError);
    if (*szDomain)
        return StringCchPrintfW(pszOwner, cchOwner, L"%s\\%s", szDomain, szName);
    return StringCchCopyW(pszOwner, cchOwner, szName);
}

static HRESULT
GetVolumeId(PCWSTR pszPath, GUID *pVolumeId)
{
    WCHAR szRoot[MAX_PATH], szVolume[MAX_PATH];
    PWSTR pszOpen, pszClose;

    if (!GetVolumePathNameW(pszPath, szRoot, _countof(szRoot)) ||
        !GetVolumeNameForVolumeMountPointW(szRoot, szVolume, _countof(szVolume)))
        return HRESULT_FROM_WIN32(GetLastError());
    pszOpen = wcschr(szVolume, L'{');
    pszClose = pszOpen ? wcschr(pszOpen, L'}') : NULL;
    if (!pszClose)
        return E_FAIL;
    pszClose[1] = UNICODE_NULL;
    return CLSIDFromString(pszOpen, pVolumeId);
}

static void
StoreKind(IPropertyStore *pStore, PCWSTR pszExt, BOOL bFolder)
{
    WCHAR szKinds[MAX_PATH];
    PCWSTR apszKinds[16];
    ULONG cKinds = 0;
    DWORD cbKinds = sizeof(szKinds);
    PROPVARIANT pv;
    LPWSTR pszText = NULL;

    if (bFolder)
    {
        StringCchCopyW(szKinds, _countof(szKinds), L"folder");
    }
    else if (!*pszExt ||
             RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\KindMap",
                          pszExt, RRF_RT_REG_SZ, NULL, szKinds, &cbKinds) != ERROR_SUCCESS)
    {
        return;
    }
    for (PWSTR psz = szKinds; psz && *psz && cKinds < _countof(apszKinds); )
    {
        PWSTR pszNext = wcschr(psz, L';');
        if (pszNext)
            *pszNext++ = UNICODE_NULL;
        if (*psz)
            apszKinds[cKinds++] = psz;
        psz = pszNext;
    }
    if (!cKinds || FAILED(InitPropVariantFromStringVector(apszKinds, cKinds, &pv)))
        return;
    if (SUCCEEDED(PSFormatForDisplayAlloc(PKEY_Kind, pv, PDFF_DEFAULT, &pszText)))
    {
        StoreString(pStore, PKEY_KindText, pszText);
        CoTaskMemFree(pszText);
    }
    StoreValue(pStore, PKEY_Kind, pv, S_OK);
}

static void
StoreAttributeStrings(IPropertyStore *pStore, SFGAOF Attributes)
{
    static const struct { SFGAOF Flag; PCWSTR Name; } Names[] =
    {
        { SFGAO_FILESYSTEM, L"filesys" },
        { SFGAO_FILESYSANCESTOR, L"fileanc" },
        { SFGAO_STORAGEANCESTOR, L"storageanc" },
        { SFGAO_STREAM, L"stream" },
        { SFGAO_LINK, L"link" },
        { SFGAO_HIDDEN, L"hidden" },
        { SFGAO_FOLDER, L"folder" },
        { SFGAO_NONENUMERATED, L"nonenum" },
        { SFGAO_BROWSABLE, L"browsable" },
        { SFGAO_SYSTEM, L"system" },
    };
    PCWSTR apsz[_countof(Names)];
    ULONG c = 0;
    PROPVARIANT pv;

    for (SIZE_T i = 0; i < _countof(Names); ++i)
    {
        if (Attributes & Names[i].Flag)
            apsz[c++] = Names[i].Name;
    }
    StoreValue(pStore, PKEY_Shell_SFGAOFlagsStrings, pv, InitPropVariantFromStringVector(apsz, c, &pv));
}

class CFSPropertyStoreFactory :
    public CComObjectRootEx<CComMultiThreadModelNoCS>,
    public IPropertyStoreFactory
{
private:
    CComPtr<IShellFolder2> m_psf;
    CComHeapPtr<ITEMIDLIST_ABSOLUTE> m_pidlFolder;
    CComHeapPtr<ITEMID_CHILD> m_pidl;
    WCHAR m_szPath[MAX_PATH];

    HRESULT AddFileSystemProperties(IPropertyStore *pStore);
    HRESULT CreateHandlerStore(BOOL bReadWrite, REFIID riid, void **ppv);

public:
    HRESULT Initialize(PCIDLIST_ABSOLUTE pidlFolder, IShellFolder2 *psf, PCUITEMID_CHILD pidl, PCWSTR pszPath);

    STDMETHOD(GetPropertyStore)(GETPROPERTYSTOREFLAGS flags, IUnknown *pUnkFactory, REFIID riid, void **ppv) override;
    STDMETHOD(GetPropertyStoreForKeys)(const PROPERTYKEY *rgKeys, UINT cKeys, GETPROPERTYSTOREFLAGS flags,
                                       REFIID riid, void **ppv) override;

    DECLARE_NOT_AGGREGATABLE(CFSPropertyStoreFactory)
    DECLARE_PROTECT_FINAL_CONSTRUCT()

    BEGIN_COM_MAP(CFSPropertyStoreFactory)
        COM_INTERFACE_ENTRY_IID(IID_IPropertyStoreFactory, IPropertyStoreFactory)
    END_COM_MAP()
};

HRESULT
CFSPropertyStoreFactory::Initialize(PCIDLIST_ABSOLUTE pidlFolder, IShellFolder2 *psf, PCUITEMID_CHILD pidl, PCWSTR pszPath)
{
    m_pidlFolder.Attach(ILClone(pidlFolder));
    m_pidl.Attach(ILClone(pidl));
    if (!m_pidlFolder || !m_pidl)
        return E_OUTOFMEMORY;
    m_psf = psf;
    return StringCchCopyW(m_szPath, _countof(m_szPath), pszPath);
}

HRESULT
CFSPropertyStoreFactory::CreateHandlerStore(BOOL bReadWrite, REFIID riid, void **ppv)
{
    CComPtr<IPropertyStore> pHandler;
    CComPtr<IInitializeWithStream> pInitStream;
    CComPtr<IInitializeWithFile> pInitFile;
    DWORD grfMode = bReadWrite ? STGM_READWRITE : STGM_READ;
    CLSID clsid;
    HRESULT hr;

    *ppv = NULL;
    if (FAILED(PSLookupPropertyHandlerCLSID(m_szPath, &clsid)))
        return E_FAIL;
    hr = CoCreateInstance(clsid, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARG(IPropertyStore, &pHandler));
    if (FAILED(hr))
        return hr;
    if (SUCCEEDED(pHandler->QueryInterface(IID_PPV_ARG(IInitializeWithStream, &pInitStream))))
    {
        CComPtr<IStream> pStream;
        hr = SHCreateStreamOnFileEx(m_szPath, grfMode | (bReadWrite ? STGM_SHARE_DENY_WRITE : STGM_SHARE_DENY_NONE),
                                    FILE_ATTRIBUTE_NORMAL, FALSE, NULL, &pStream);
        if (SUCCEEDED(hr))
            hr = pInitStream->Initialize(pStream, grfMode);
    }
    else if (SUCCEEDED(pHandler->QueryInterface(IID_PPV_ARG(IInitializeWithFile, &pInitFile))))
    {
        hr = pInitFile->Initialize(m_szPath, grfMode);
    }
    else
    {
        hr = E_NOINTERFACE;
    }
    if (FAILED(hr))
        return hr;
    return pHandler->QueryInterface(riid, ppv);
}

HRESULT
CFSPropertyStoreFactory::AddFileSystemProperties(IPropertyStore *pStore)
{
    WIN32_FILE_ATTRIBUTE_DATA fad;
    WCHAR szFolder[MAX_PATH], szGrandParent[MAX_PATH], szBase[MAX_PATH], szText[MAX_PATH];
    WCHAR szDisplay[MAX_PATH] = L"", szParsing[MAX_PATH] = L"";
    CComHeapPtr<WCHAR> pszFolderDisplay;
    PCWSTR pszFileName, pszExt;
    STRRET str;
    BOOL bFolder;
    PROPVARIANT pv;

    if (!GetFileAttributesExW(m_szPath, GetFileExInfoStandard, &fad))
        return HRESULT_FROM_WIN32(GetLastError());
    bFolder = (fad.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    pszFileName = PathFindFileNameW(m_szPath);
    pszExt = bFolder ? L"" : PathFindExtensionW(pszFileName);
    StringCchCopyW(szFolder, _countof(szFolder), m_szPath);
    PathRemoveFileSpecW(szFolder);
    StringCchCopyW(szGrandParent, _countof(szGrandParent), szFolder);
    if (!PathRemoveFileSpecW(szGrandParent) || !wcscmp(szGrandParent, szFolder))
        *szGrandParent = UNICODE_NULL;
    StringCchCopyW(szBase, _countof(szBase), pszFileName);
    if (!bFolder)
        PathRemoveExtensionW(szBase);
    if (SUCCEEDED(m_psf->GetDisplayNameOf(m_pidl, SHGDN_INFOLDER, &str)))
        StrRetToBufW(&str, m_pidl, szDisplay, _countof(szDisplay));
    if (SUCCEEDED(m_psf->GetDisplayNameOf(m_pidl, SHGDN_INFOLDER | SHGDN_FORPARSING, &str)))
        StrRetToBufW(&str, m_pidl, szParsing, _countof(szParsing));
    SHGetNameFromIDList(m_pidlFolder, SIGDN_NORMALDISPLAY, &pszFolderDisplay);

    StoreString(pStore, PKEY_ItemFolderNameDisplay, pszFolderDisplay);
    {
        SHCOLUMNID scid = { FMTID_Storage, PID_STG_STORAGETYPE };
        VARIANT var;
        VariantInit(&var);
        if (SUCCEEDED(m_psf->GetDetailsEx(m_pidl, &scid, &var)) && V_VT(&var) == VT_BSTR)
            StoreString(pStore, PKEY_ItemTypeText, V_BSTR(&var));
        VariantClear(&var);
    }
    StoreString(pStore, PKEY_ItemNameDisplay, szDisplay);
    if (!bFolder)
    {
        ULARGE_INTEGER Size;
        Size.LowPart = fad.nFileSizeLow;
        Size.HighPart = fad.nFileSizeHigh;
        StoreValue(pStore, PKEY_Size, pv, InitPropVariantFromUInt64(Size.QuadPart, &pv));
    }
    StoreUInt32(pStore, PKEY_FileAttributes, fad.dwFileAttributes);
    StoreFileTime(pStore, PKEY_DateModified, fad.ftLastWriteTime);
    StoreFileTime(pStore, PKEY_DateCreated, fad.ftCreationTime);
    StoreFileTime(pStore, PKEY_DateAccessed, fad.ftLastAccessTime);
    StoreString(pStore, PKEY_ItemNameDisplayWithoutExtension, szBase);
    if (bFolder)
    {
        StoreString(pStore, PKEY_FolderNameDisplay, szDisplay);
    }
    else
    {
        DWORD cbText = sizeof(szText);
        if (*pszExt && RegGetValueW(HKEY_CLASSES_ROOT, pszExt, L"Content Type", RRF_RT_REG_SZ, NULL,
                                    szText, &cbText) == ERROR_SUCCESS)
            StoreString(pStore, PKEY_ContentType, szText);
    }
    StoreFileTime(pStore, PKEY_Document_DateCreated, fad.ftLastWriteTime);
    StoreFileTime(pStore, PKEY_Document_DateSaved, fad.ftLastWriteTime);
    if (SUCCEEDED(GetOwnerName(m_szPath, szText, _countof(szText))))
        StoreString(pStore, PKEY_FileOwner, szText);
    if (PathIsUNCW(m_szPath))
    {
        StringCchCopyW(szText, _countof(szText), m_szPath + 2);
        PWSTR pszEnd = wcschr(szText, L'\\');
        if (pszEnd)
            *pszEnd = UNICODE_NULL;
        StoreString(pStore, PKEY_ComputerName, szText);
    }
    else
    {
        DWORD cchText = _countof(szText);
        if (GetComputerNameW(szText, &cchText))
            StoreString(pStore, PKEY_ComputerName, szText);
    }
    StoreNarrowPath(pStore, PKEY_ItemPathDisplayNarrow, szBase, szFolder);
    {
        PERCEIVED Perceived = PERCEIVED_TYPE_UNKNOWN;
        PERCEIVEDFLAG PerceivedFlags;
        if (!bFolder && *pszExt)
            AssocGetPerceivedType(pszExt, &Perceived, &PerceivedFlags, NULL);
        StoreValue(pStore, PKEY_PerceivedType, pv, InitPropVariantFromInt32(Perceived, &pv));
    }
    StoreString(pStore, PKEY_ItemType, bFolder ? L"Directory" : pszExt);
    StoreString(pStore, PKEY_ParsingName, szParsing);
    {
        SFGAOF Attributes = SFGAO_CAPABILITYMASK | SFGAO_DISPLAYATTRMASK | SFGAO_STORAGECAPMASK |
                            SFGAO_NONENUMERATED | SFGAO_BROWSABLE | SFGAO_SYSTEM;
        PCUITEMID_CHILD pidl = m_pidl;
        if (SUCCEEDED(m_psf->GetAttributesOf(1, &pidl, &Attributes)))
        {
            StoreUInt32(pStore, PKEY_SFGAOFlags, Attributes);
            StoreAttributeStrings(pStore, Attributes);
        }
    }
    StoreString(pStore, PKEY_ParsingPath, m_szPath);
    if (*pszExt)
        StoreString(pStore, PKEY_FileExtension, pszExt);
    StoreFileTime(pStore, PKEY_ItemDate, bFolder ? fad.ftCreationTime : fad.ftLastWriteTime);
    StoreKind(pStore, pszExt, bFolder);
    StoreBoolean(pStore, PKEY_IsShared, FALSE);
    StoreUInt32(pStore, PKEY_SharingStatus, 2);
    StoreString(pStore, PKEY_ItemName, pszFileName);
    if (bFolder)
    {
        StoreUInt32(pStore, PKEY_ZoneIdentifier, 0);
    }
    else
    {
        StringCchPrintfW(szText, _countof(szText), L"%s:Zone.Identifier", m_szPath);
        StoreUInt32(pStore, PKEY_ZoneIdentifier, GetPrivateProfileIntW(L"ZoneTransfer", L"ZoneId", 0, szText));
    }
    StoreNarrowPath(pStore, PKEY_ItemFolderPathDisplayNarrow, PathFindFileNameW(szFolder), szGrandParent);
    StoreString(pStore, PKEY_FileName, pszFileName);
    {
        GUID VolumeId;
        if (SUCCEEDED(GetVolumeId(m_szPath, &VolumeId)))
            StoreValue(pStore, PKEY_VolumeId, pv, InitPropVariantFromCLSID(VolumeId, &pv));
    }
    if (PathIsRootW(m_szPath))
    {
        ULARGE_INTEGER FreeBytes, TotalBytes;

        if (GetDiskFreeSpaceExW(m_szPath, &FreeBytes, &TotalBytes, NULL))
        {
            StoreValue(pStore, PKEY_Capacity, pv, InitPropVariantFromUInt64(TotalBytes.QuadPart, &pv));
            StoreValue(pStore, PKEY_FreeSpace, pv, InitPropVariantFromUInt64(FreeBytes.QuadPart, &pv));
            if (TotalBytes.QuadPart != 0 && FreeBytes.QuadPart <= TotalBytes.QuadPart)
                StoreUInt32(pStore, PKEY_PercentFull,
                            (ULONG)((TotalBytes.QuadPart - FreeBytes.QuadPart) * 100 / TotalBytes.QuadPart));
        }
        if (GetVolumeInformationW(m_szPath, NULL, 0, NULL, NULL, NULL, szText, _countof(szText)))
            StoreString(pStore, PKEY_Volume_FileSystem, szText);
    }
    StoreString(pStore, PKEY_ItemFolderPathDisplay, szFolder);
    StoreString(pStore, PKEY_ItemPathDisplay, m_szPath);
    StoreNamedBoolean(pStore, L"System.IsFolder", bFolder);
    StoreNamedBoolean(pStore, L"System.NotUserContent", FALSE);
    StoreFileTime(pStore, PKEY_DateImported, fad.ftLastWriteTime);
    StoreUInt32(pStore, PKEY_FilePlaceholderStatus, PS_FULL_PRIMARY_STREAM_AVAILABLE | PS_CREATE_FILE_ACCESSIBLE);
    return S_OK;
}

STDMETHODIMP
CFSPropertyStoreFactory::GetPropertyStore(GETPROPERTYSTOREFLAGS flags, IUnknown *pUnkFactory, REFIID riid, void **ppv)
{
    CComPtr<IPropertyStore> pStore, pHandler;
    HRESULT hr;

    if (!ppv)
        return E_POINTER;
    *ppv = NULL;
    if (flags & (GPS_HANDLERPROPERTIESONLY | GPS_READWRITE))
        return CreateHandlerStore(!!(flags & GPS_READWRITE), riid, ppv);

    hr = PSCreateMemoryPropertyStore(IID_PPV_ARG(IPropertyStore, &pStore));
    if (FAILED(hr))
        return hr;
    hr = AddFileSystemProperties(pStore);
    if (FAILED(hr))
        return hr;
    if (!(flags & GPS_FASTPROPERTIESONLY) &&
        SUCCEEDED(CreateHandlerStore(FALSE, IID_PPV_ARG(IPropertyStore, &pHandler))))
    {
        DWORD cProps = 0;
        pHandler->GetCount(&cProps);
        for (DWORD i = 0; i < cProps; ++i)
        {
            PROPERTYKEY key;
            PROPVARIANT pv;
            if (FAILED(pHandler->GetAt(i, &key)))
                continue;
            PropVariantInit(&pv);
            StoreValue(pStore, key, pv, pHandler->GetValue(key, &pv));
        }
    }
    return pStore->QueryInterface(riid, ppv);
}

STDMETHODIMP
CFSPropertyStoreFactory::GetPropertyStoreForKeys(const PROPERTYKEY *rgKeys, UINT cKeys, GETPROPERTYSTOREFLAGS flags,
                                                 REFIID riid, void **ppv)
{
    return GetPropertyStore(flags, NULL, riid, ppv);
}

EXTERN_C HRESULT
CFSPropertyStoreFactory_CreateInstance(PCIDLIST_ABSOLUTE pidlFolder, IShellFolder2 *psf, PCUITEMID_CHILD pidl,
                                       PCWSTR pszPath, REFIID riid, void **ppv)
{
    CComPtr<IPropertyStoreFactory> pFactory;
    HRESULT hr;

    hr = ShellObjectCreatorInit<CFSPropertyStoreFactory>(pidlFolder, psf, pidl, pszPath,
                                                         IID_PPV_ARG(IPropertyStoreFactory, &pFactory));
    if (FAILED(hr))
        return hr;
    if (IsEqualIID(riid, IID_IPropertyStore))
        return pFactory->GetPropertyStore(GPS_DEFAULT, NULL, riid, ppv);
    return pFactory->QueryInterface(riid, ppv);
}
