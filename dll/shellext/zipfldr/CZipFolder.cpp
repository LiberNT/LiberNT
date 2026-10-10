/*
 * PROJECT:     ReactOS Zip Shell Extension
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     Main class
 * COPYRIGHT:   Copyright 2017 Mark Jansen (mark.jansen@reactos.org)
 *              Copyright 2023-2026 Katayama Hirofumi MZ (katayama.hirofumi.mz@gmail.com)
 */

#include "precomp.h"
#include <ntquery.h> // PID_STG_*

static const GUID FmtIdZipFolder = { 0xE88DCCE0, 0xB7B3, 0x11D1, { 0xA9,0xF0,0x00,0xAA,0x00,0x60,0xFA,0x31 } };

static const FolderViewColumn g_ColumnDefs[] =
{
    { IDS_COL_NAME,      SHCOLSTATE_TYPE_STR | SHCOLSTATE_ONBYDEFAULT,   25, LVCFMT_LEFT,  &FMTID_Storage, PID_STG_NAME },
    { IDS_COL_TYPE,      SHCOLSTATE_TYPE_STR | SHCOLSTATE_ONBYDEFAULT,   20, LVCFMT_LEFT,  &FMTID_Storage, PID_STG_STORAGETYPE },
    { IDS_COL_COMPRSIZE, SHCOLSTATE_TYPE_INT | SHCOLSTATE_ONBYDEFAULT,   10, LVCFMT_RIGHT, &FmtIdZipFolder, 6 },
    { IDS_COL_PASSWORD,  SHCOLSTATE_TYPE_STR | SHCOLSTATE_ONBYDEFAULT,   10, LVCFMT_LEFT,  &FmtIdZipFolder, 2 },
    { IDS_COL_SIZE,      SHCOLSTATE_TYPE_INT | SHCOLSTATE_ONBYDEFAULT,   10, LVCFMT_RIGHT, &FMTID_Storage, PID_STG_SIZE },
    { IDS_COL_RATIO,     SHCOLSTATE_TYPE_STR | SHCOLSTATE_ONBYDEFAULT,   10, LVCFMT_LEFT,  &FmtIdZipFolder, 4 },
    { IDS_COL_DATE_MOD,  SHCOLSTATE_TYPE_DATE | SHCOLSTATE_ONBYDEFAULT,  15, LVCFMT_LEFT,  &FMTID_Storage, PID_STG_WRITETIME },
    // IDS_COL_METHOD,    SHCOLSTATE_TYPE_STR, ... &FmtIdZipFolder, 3 },
    // IDS_COL_CRC32,     SHCOLSTATE_TYPE_STR, ... &FmtIdZipFolder, 5 },
};

static int MapScidToColumn(const SHCOLUMNID &scid)
{
    for (UINT i = 0; i < _countof(g_ColumnDefs); ++i)
    {
        if (IsEqual(scid, g_ColumnDefs[i]))
            return i;
    }
    return -1;
}

static PIDLIST_RELATIVE FindItem(IEnumIDList &List, PCWSTR Path)
{
    for (PITEMID_CHILD pidl; List.Next(1, &pidl, NULL) == S_OK;)
    {
        ZipPidlEntry *p = (ZipPidlEntry*)pidl;
        if (!lstrcmpiW(p->Name, Path))
            return pidl;
        CoTaskMemFree(pidl);
    }
    return NULL;
}

CZipFolder::CZipFolder()
{
}

CZipFolder::~CZipFolder()
{
    Close();
}

void CZipFolder::Close()
{
    if (m_UnzipFile)
        unzClose(m_UnzipFile);
    m_UnzipFile = NULL;
}

STDMETHODIMP_(unzFile) CZipFolder::getZip()
{
    if (!m_UnzipFile)
        m_UnzipFile = unzOpen2_64(m_ZipFile, &g_FFunc);
    return m_UnzipFile;
}

HRESULT CZipFolder::Initialize(PCWSTR zipFile, PCWSTR zipDir, PCUIDLIST_ABSOLUTE curDir, PCUIDLIST_RELATIVE pidl)
{
    m_ZipFile = zipFile;
    m_ZipDir = zipDir;

    m_CurDir.Attach(ILCombine(curDir, pidl));
    return S_OK;
}

DWORD WINAPI CZipFolder::s_ExtractProc(LPVOID arg)
{
    CComBSTR ZipFile;
    ZipFile.Attach((BSTR)arg);

    _CZipExtract_runWizard(ZipFile);

    InterlockedDecrement(&g_ModuleRefCnt);
    return 0;
}

// Adapted from CFileDefExt::GetFileTimeString
BOOL CZipFolder::_GetFileTimeString(LPFILETIME lpFileTime, PWSTR pwszResult, UINT cchResult)
{
    SYSTEMTIME st;

    if (!FileTimeToSystemTime(lpFileTime, &st))
        return FALSE;

    size_t cchRemaining = cchResult;
    PWSTR pwszEnd = pwszResult;
    int cchWritten = GetDateFormatW(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &st, NULL, pwszEnd, cchRemaining);
    if (cchWritten)
        --cchWritten; // GetDateFormatW returns count with terminating zero
    else
        return FALSE;
    cchRemaining -= cchWritten;
    pwszEnd += cchWritten;

    StringCchCopyExW(pwszEnd, cchRemaining, L" ", &pwszEnd, &cchRemaining, 0);

    cchWritten = GetTimeFormatW(LOCALE_USER_DEFAULT, 0, &st, NULL, pwszEnd, cchRemaining);
    if (cchWritten)
        --cchWritten; // GetTimeFormatW returns count with terminating zero
    else
        return FALSE;

    return TRUE;
}

HRESULT CZipFolder::DoDeleteItems(CComPtr<IDataObject> pDataObj)
{
    CStringW message(MAKEINTRESOURCEW(IDS_CONFIRMDELETE_TEXT));
    CStringW title(MAKEINTRESOURCEW(IDS_FRIENDLYNAME));
    if (MessageBoxW(m_hwnd, message, title, MB_ICONWARNING | MB_YESNOCANCEL) != IDYES)
        return S_FALSE;

    HRESULT hr = DeleteItems(pDataObj);
    if (FAILED_UNEXPECTEDLY(hr))
    {
        message.LoadString(IDS_CANTDELETEFILE);
        MessageBoxW(m_hwnd, message, title, MB_ICONERROR);
    }

    return hr;
}

HRESULT CZipFolder::DeleteItems(CComPtr<IDataObject> pDataObj)
{

    CDataObjectHIDA cida(pDataObj);
    if (!cida || cida->cidl <= 0)
        return E_FAIL;

    // Get the target paths
    CAtlList<CStringW> targetPaths;
    for (UINT iFile = 0; iFile < cida->cidl; ++iFile)
    {
        PCUIDLIST_RELATIVE pidlRelative = HIDA_GetPIDLItem(cida, iFile);
        const ZipPidlEntry* pEntry = _ZipFromIL(pidlRelative);
        if (pEntry)
        {
            CStringW fullPath = m_ZipDir + pEntry->Name;
            // For folders, end with a slash
            if (pEntry->ZipType == ZIP_PIDL_DIRECTORY && fullPath.Right(1) != L"/")
                fullPath += L"/";
            targetPaths.AddTail(fullPath);
        }
    }

    return DeleteEntries(targetPaths);
}

HRESULT CZipFolder::DeleteEntries(const CAtlList<CStringW>& targetPaths)
{
    // Create a temporary file
    WCHAR szTempPath[MAX_PATH], szTempFile[MAX_PATH];
    GetTempPathW(MAX_PATH, szTempPath);
    GetTempFileNameW(szTempPath, L"ZIP", 0, szTempFile);

    // Close the current handle to work with the ZIP file
    Close();

    zlib_filefunc64_def ffunc = {};
    fill_win32_filefunc64W(&ffunc);

    HRESULT hr = S_OK;
    unzFile uf = unzOpen2_64(m_ZipFile, &ffunc);
    zipFile zf = zipOpen2_64(szTempFile, APPEND_STATUS_CREATE, NULL, &ffunc);

    if (!uf || !zf)
    {
        DPRINT1("Cannot open file\n");
        if (uf) unzClose(uf);
        if (zf) zipClose(zf, NULL);
        return E_FAIL;
    }

    // Scan all entries in the original ZIP
    if (unzGoToFirstFile(uf) == UNZ_OK)
    {
        do
        {
            // Read file entry
            unz_file_info64 info;
            char szNameA[MAX_PATH];
            if (unzGetCurrentFileInfo64(uf, &info, szNameA, sizeof(szNameA), NULL, 0, NULL, 0) != UNZ_OK)
                continue;

            // Read extra field
            CAtlArray<BYTE> extra;
            if (info.size_file_extra > 0)
            {
                extra.SetCount(info.size_file_extra);
                unzGetCurrentFileInfo64(uf, NULL, NULL, 0, extra.GetData(), info.size_file_extra, NULL, 0);
            }

            CStringA utf8Name = CZipEnumerator::GetUtf8Name(szNameA, extra.GetData(), (DWORD)extra.GetCount());
            CStringW currentEntryName;
            if (utf8Name.GetLength() > 0)
                currentEntryName = (LPWSTR)CA2WEX<MAX_PATH>(utf8Name, CP_UTF8);
            else if (info.flag & MINIZIP_UTF8_FLAG)
                currentEntryName = (LPWSTR)CA2WEX<MAX_PATH>(szNameA, CP_UTF8);
            else
                currentEntryName = (LPWSTR)CA2WEX<MAX_PATH>(szNameA, CP_ACP);

            currentEntryName.Replace(L'\\', L'/');

            // Check if it is on the deletion target list
            bool bSkip = false;
            POSITION pos = targetPaths.GetHeadPosition();
            while (pos)
            {
                const CStringW& target = targetPaths.GetNext(pos);
                // Check for an exact match (file) or a prefix match (folder)
                if (currentEntryName == target || currentEntryName.Left(target.GetLength()) == target)
                {
                    bSkip = true;
                    break;
                }
            }

            if (!bSkip)
            {
                // If not to be deleted, copy to new ZIP
                hr = CopyZipEntry(uf, zf, &info, szNameA);
                if (FAILED_UNEXPECTEDLY(hr))
                    break;
            }
        } while (unzGoToNextFile(uf) == UNZ_OK);
    }

    unzClose(uf);
    zipClose(zf, NULL);

    // Replace the original file with the temporary file
    if (SUCCEEDED(hr) && ReplaceFileW(m_ZipFile, szTempFile, NULL, 0, NULL, NULL))
    {
        // Notify the shell that the folder contents have changed
        SHChangeNotify(SHCNE_UPDATEDIR, SHCNF_IDLIST, m_CurDir, NULL);
    }
    else
    {
        DPRINT1("Failed to replace file\n");
        DeleteFileW(szTempFile);
        hr = E_FAIL;
    }

    return hr;
}

HRESULT CZipFolder::CopyZipEntry(unzFile uf, zipFile zf, unz_file_info64* info, LPCSTR nameA)
{
    // Get extra field
    CAtlArray<BYTE> extra;
    if (info->size_file_extra > 0)
    {
        extra.SetCount(info->size_file_extra);
        if (unzGetCurrentFileInfo64(uf, NULL, NULL, 0, extra.GetData(),
                                    info->size_file_extra, NULL, 0) != UNZ_OK)
        {
            DPRINT1("Cannot get extra fields\n");
            return E_FAIL;
        }
    }

    if (unzOpenCurrentFile(uf) != UNZ_OK)
    {
        DPRINT1("Cannot open current file\n");
        return E_FAIL;
    }

    zip_fileinfo zi = {0};
    zi.dosDate = info->dosDate;
    zi.internal_fa = info->internal_fa;
    zi.external_fa = info->external_fa;

    INT err = zipOpenNewFileInZip3_64(zf, nameA, &zi,
                                      extra.GetData(), (UINT)extra.GetCount(),
                                      extra.GetData(), (UINT)extra.GetCount(),
                                      NULL, Z_DEFLATED, Z_DEFAULT_COMPRESSION, 0,
                                      -MAX_WBITS, DEF_MEM_LEVEL, Z_DEFAULT_STRATEGY,
                                      NULL, 0, info->flag);
    if (err)
    {
        DPRINT1("err: %d\n", err);
        unzCloseCurrentFile(uf);
        return E_FAIL;
    }

    BYTE buffer[4096];
    INT read;
    while ((read = unzReadCurrentFile(uf, buffer, sizeof(buffer))) > 0)
        zipWriteInFileInZip(zf, buffer, read);

    zipCloseFileInZip(zf);
    unzCloseCurrentFile(uf);
    return S_OK;
}

STDMETHODIMP CZipFolder::GetDefaultColumnState(UINT iColumn, DWORD *pcsFlags)
{
    if (!pcsFlags || iColumn >= _countof(g_ColumnDefs))
        return E_INVALIDARG;
    *pcsFlags = g_ColumnDefs[iColumn].ColumnFlags;
    return S_OK;
}

STDMETHODIMP CZipFolder::GetDetailsEx(PCUITEMID_CHILD pidl, const SHCOLUMNID *pscid, VARIANT *pv)
{
    if (!pidl || !pscid || !pv)
        return E_INVALIDARG;

    V_VT(pv) = VT_EMPTY;

    PCUIDLIST_RELATIVE curpidl = ILGetNext(pidl);
    if (curpidl->mkid.cb != 0)
    {
        DPRINT1("ERROR, unhandled PIDL!\n");
        return E_FAIL;
    }
    const ZipPidlEntry* zipEntry = _ZipFromIL(pidl);
    if (!zipEntry)
        return E_INVALIDARG;
    bool isDir = zipEntry->IsDirectory();

    // Handle the non-string columns here so the caller gets the correct variant type
    if (!isDir && IsEqual(*pscid, g_ColumnDefs[COL_SIZE]))
    {
        V_VT(pv) = VT_UI8;
        V_UI8(pv) = zipEntry->UncompressedSize;
        return S_OK;
    }
    else if (!isDir && IsEqual(*pscid, g_ColumnDefs[COL_COMPRSIZE]))
    {
        V_VT(pv) = VT_UI8;
        V_UI8(pv) = zipEntry->CompressedSize;
        return S_OK;
    }
    else if (!isDir && IsEqual(*pscid, g_ColumnDefs[COL_DATE_MOD]))
    {
        if (DosDateTimeToVariantTime(HIWORD(zipEntry->DosDate), LOWORD(zipEntry->DosDate), &V_DATE(pv)))
        {
            V_VT(pv) = VT_DATE;
            return S_OK;
        }
    }

    HRESULT hr = E_FAIL;
    int col = MapScidToColumn(*pscid);
    if (col >= 0)
    {
        SHELLDETAILS sd;
        if (SUCCEEDED(hr = GetDetailsOf(pidl, col, &sd)))
        {
            CComHeapPtr<WCHAR> str;
            if (SUCCEEDED(hr = StrRetToStrW(&sd.str, pidl, &str)))
            {
                hr = (V_BSTR(pv) = SysAllocString(str)) != NULL ? S_OK : E_OUTOFMEMORY;
                if (SUCCEEDED(hr))
                    V_VT(pv) = VT_BSTR;
            }
        }
    }
    return hr;
}

STDMETHODIMP CZipFolder::GetDetailsOf(PCUITEMID_CHILD pidl, UINT iColumn, SHELLDETAILS *psd)
{
    if (iColumn >= _countof(g_ColumnDefs))
        return E_FAIL;

    psd->cxChar = g_ColumnDefs[iColumn].cxChar;
    psd->fmt = g_ColumnDefs[iColumn].fmt;

    if (pidl == NULL)
    {
        return SHSetStrRet(&psd->str, _AtlBaseModule.GetResourceInstance(), g_ColumnDefs[iColumn].iResource);
    }

    PCUIDLIST_RELATIVE curpidl = ILGetNext(pidl);
    if (curpidl->mkid.cb != 0)
    {
        DPRINT1("ERROR, unhandled PIDL!\n");
        return E_FAIL;
    }

    const ZipPidlEntry* zipEntry = _ZipFromIL(pidl);
    if (!zipEntry)
        return E_INVALIDARG;

    WCHAR Buffer[100];
    bool isDir = zipEntry->ZipType == ZIP_PIDL_DIRECTORY;
    switch (iColumn)
    {
        case COL_NAME:
            return GetDisplayNameOf(pidl, SHGDN_INFOLDER, &psd->str);
        case COL_TYPE:
        {
            SHFILEINFOW shfi;
            DWORD dwAttributes = isDir ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
            ULONG_PTR firet = SHGetFileInfoW(zipEntry->Name, dwAttributes, &shfi, sizeof(shfi), SHGFI_USEFILEATTRIBUTES | SHGFI_TYPENAME);
            if (!firet)
                return E_FAIL;
            return SHSetStrRet(&psd->str, shfi.szTypeName);
        }
        case COL_COMPRSIZE:
        case COL_SIZE:
        {
            if (isDir)
                return SHSetStrRet(&psd->str, L"");

            ULONG64 Size = iColumn == COL_COMPRSIZE ? zipEntry->CompressedSize : zipEntry->UncompressedSize;
            if (!StrFormatByteSizeW(Size, Buffer, _countof(Buffer)))
                return E_FAIL;
            return SHSetStrRet(&psd->str, Buffer);
        }
        case COL_PASSWORD:
            if (isDir)
                return SHSetStrRet(&psd->str, L"");
            return SHSetStrRet(&psd->str, _AtlBaseModule.GetResourceInstance(), zipEntry->Password ? IDS_YES : IDS_NO);
        case COL_RATIO:
        {
            if (isDir)
                return SHSetStrRet(&psd->str, L"");

            int ratio = 0;
            if (zipEntry->UncompressedSize)
                ratio = 100 - (int)((zipEntry->CompressedSize*100)/zipEntry->UncompressedSize);
            StringCchPrintfW(Buffer, _countof(Buffer), L"%d%%", ratio);
            return SHSetStrRet(&psd->str, Buffer);
        }
        case COL_DATE_MOD:
        {
            if (isDir)
                return SHSetStrRet(&psd->str, L"");
            FILETIME ftLocal;
            DosDateTimeToFileTime((WORD)(zipEntry->DosDate>>16), (WORD)zipEntry->DosDate, &ftLocal);
            if (!_GetFileTimeString(&ftLocal, Buffer, _countof(Buffer)))
                return E_FAIL;
            return SHSetStrRet(&psd->str, Buffer);
        }
    }

    UNIMPLEMENTED;
    return E_NOTIMPL;
}

STDMETHODIMP CZipFolder::MapColumnToSCID(UINT column, SHCOLUMNID *pscid)
{
    if (column < _countof(g_ColumnDefs) && g_ColumnDefs[column].pkg)
    {
        pscid->fmtid = *g_ColumnDefs[column].pkg;
        pscid->pid = g_ColumnDefs[column].pki;
        return S_OK;
    }
    return E_FAIL;
}

STDMETHODIMP CZipFolder::ParseDisplayName(HWND hwndOwner, LPBC pbc, LPOLESTR lpszDisplayName, ULONG *pchEaten, PIDLIST_RELATIVE *ppidl, ULONG *pdwAttributes)
{
    if (pchEaten)
        *pchEaten = 0;

    DWORD dwFlags = SHCONTF_FOLDERS | SHCONTF_NONFOLDERS | SHCONTF_INCLUDEHIDDEN | SHCONTF_INCLUDESUPERHIDDEN;
    CComPtr<IEnumIDList> pEnum;
    HRESULT hr = EnumObjects(hwndOwner, dwFlags, &pEnum);
    if (FAILED(hr))
        return hr;

    PIDLIST_RELATIVE pidl = FindItem(*pEnum, lpszDisplayName);
    if (!pidl)
        return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);

    *ppidl = pidl;
    if (pchEaten)
        *pchEaten = lstrlenW(lpszDisplayName);
    if (pdwAttributes)
        GetAttributesOf(1, ppidl, (SFGAOF*)pdwAttributes);
    return S_OK;
}

STDMETHODIMP CZipFolder::BindToObject(PCUIDLIST_RELATIVE pidl, LPBC pbcReserved, REFIID riid, LPVOID *ppvOut)
{
    if (riid == IID_IShellFolder)
    {
        CStringW newZipDir = m_ZipDir;
        PCUIDLIST_RELATIVE curpidl = pidl;
        while (curpidl->mkid.cb)
        {
            const ZipPidlEntry* zipEntry = _ZipFromIL(curpidl);
            if (!zipEntry)
            {
                return E_FAIL;
            }
            newZipDir += zipEntry->Name;
            newZipDir += L'/';

            curpidl = ILGetNext(curpidl);
        }
        return ShellObjectCreatorInit<CZipFolder>(m_ZipFile, newZipDir, m_CurDir, pidl, riid, ppvOut);
    }
    DbgPrint("%s(%S) UNHANDLED\n", __FUNCTION__, guid2string(riid));
    return E_NOTIMPL;
}

STDMETHODIMP CZipFolder::CompareIDs(LPARAM lParam, PCUIDLIST_RELATIVE pidl1, PCUIDLIST_RELATIVE pidl2)
{
    const ZipPidlEntry* zipEntry1 = _ZipFromIL(pidl1);
    const ZipPidlEntry* zipEntry2 = _ZipFromIL(pidl2);

    if (!zipEntry1 || !zipEntry2)
        return E_INVALIDARG;

    int result = 0;
    if (zipEntry1->ZipType != zipEntry2->ZipType)
        result = zipEntry1->ZipType - zipEntry2->ZipType;
    else
        result = StrCmpIW(zipEntry1->Name, zipEntry2->Name);

    if (!result && zipEntry1->ZipType == ZIP_PIDL_DIRECTORY)
    {
        PCUIDLIST_RELATIVE child1 = ILGetNext(pidl1);
        PCUIDLIST_RELATIVE child2 = ILGetNext(pidl2);

        if (child1->mkid.cb && child2->mkid.cb)
            return CompareIDs(lParam, child1, child2);
        else if (child1->mkid.cb)
            result = 1;
        else if (child2->mkid.cb)
            result = -1;
    }

    return MAKE_COMPARE_HRESULT(result);
}

HRESULT CZipFolder::CreateDropTarget(PCWSTR zipDir, REFIID riid, LPVOID *ppvOut)
{
    CComPtr<IDropTarget> pDropTarget;
    CZipFolderDropHandler *pHandler;
    HRESULT hr = ShellObjectCreator<CZipFolderDropHandler>(IID_PPV_ARG(IDropTarget, &pDropTarget));
    if (FAILED_UNEXPECTEDLY(hr))
        return hr;

    pHandler = static_cast<CZipFolderDropHandler *>(pDropTarget.p);
    hr = pHandler->InitializeFolder(this, m_ZipFile, zipDir, m_CurDir);
    if (FAILED_UNEXPECTEDLY(hr))
        return hr;

    return pDropTarget->QueryInterface(riid, ppvOut);
}

STDMETHODIMP CZipFolder::CreateViewObject(HWND hwndOwner, REFIID riid, LPVOID *ppvOut)
{
    m_hwnd = hwndOwner ? hwndOwner : m_hwnd;

    static const GUID UnknownIID = // {93F81976-6A0D-42C3-94DD-AA258A155470}
    {0x93F81976, 0x6A0D, 0x42C3, {0x94, 0xDD, 0xAA, 0x25, 0x8A, 0x15, 0x54, 0x70}};
    if (riid == IID_IShellView)
    {
        SFV_CREATE sfvparams = {sizeof(SFV_CREATE), this};
        CComPtr<IShellFolderViewCB> pcb;

        HRESULT hr = _CFolderViewCB_CreateInstance(IID_PPV_ARG(IShellFolderViewCB, &pcb));
        if (FAILED_UNEXPECTEDLY(hr))
            return hr;

        sfvparams.psfvcb = pcb;
        hr = SHCreateShellFolderView(&sfvparams, (IShellView**)ppvOut);

        return hr;
    }
    else if (riid == IID_IExplorerCommandProvider)
    {
        return _CExplorerCommandProvider_CreateInstance(this, riid, ppvOut);
    }
    else if (riid == IID_IContextMenu)
    {
        // Folder context menu
        return QueryInterface(riid, ppvOut);
    }
    else if (riid == IID_IDropTarget)
    {
        return CreateDropTarget(m_ZipDir, riid, ppvOut);
    }
    if (UnknownIID != riid)
        DbgPrint("%s(%S) UNHANDLED\n", __FUNCTION__, guid2string(riid));
    return E_NOTIMPL;
}

STDMETHODIMP CZipFolder::GetAttributesOf(UINT cidl, PCUITEMID_CHILD_ARRAY apidl, DWORD *rgfInOut)
{
    if (!rgfInOut || !cidl || !apidl)
        return E_INVALIDARG;

    *rgfInOut = 0;

    //static DWORD dwFileAttrs = SFGAO_STREAM | SFGAO_HASPROPSHEET | SFGAO_CANDELETE | SFGAO_CANCOPY | SFGAO_CANMOVE;
    //static DWORD dwFolderAttrs = SFGAO_FOLDER | SFGAO_DROPTARGET | SFGAO_HASPROPSHEET | SFGAO_CANDELETE | SFGAO_STORAGE | SFGAO_CANCOPY | SFGAO_CANMOVE;
    static DWORD dwFileAttrs = SFGAO_CANCOPY | SFGAO_CANDELETE | SFGAO_STREAM;
    static DWORD dwFolderAttrs = SFGAO_CANCOPY | SFGAO_CANDELETE | SFGAO_FOLDER | SFGAO_HASSUBFOLDER | SFGAO_BROWSABLE | SFGAO_DROPTARGET;

    while (cidl > 0 && *apidl)
    {
        const ZipPidlEntry* zipEntry = _ZipFromIL(*apidl);

        if (zipEntry)
        {
            if (zipEntry->ZipType == ZIP_PIDL_FILE)
                *rgfInOut |= dwFileAttrs;
            else
                *rgfInOut |= dwFolderAttrs;
        }
        else
        {
            *rgfInOut = 0;
        }

        apidl++;
        cidl--;
    }

    *rgfInOut &= ~SFGAO_FILESYSTEM;
    *rgfInOut &= ~SFGAO_VALIDATE;
    return S_OK;
}

HRESULT CALLBACK CZipFolder::ZipFolderMenuCallback(
    IShellFolder *psf, HWND hwnd, IDataObject *pdtobj,
    UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    CZipFolder* pThis = static_cast<CZipFolder*>(psf);
    if (!pThis)
        return E_FAIL;

    pThis->m_pDataObj = pdtobj;

    switch (uMsg)
    {
        case DFM_MERGECONTEXTMENU:
        {
            CComQIIDPtr<I_ID(IContextMenu)> spContextMenu(psf);
            if (!spContextMenu)
            {
                DPRINT1("E_NOINTERFACE\n");
                return E_NOINTERFACE;
            }

            QCMINFO *pqcminfo = (QCMINFO *)lParam;
            HRESULT hr = spContextMenu->QueryContextMenu(pqcminfo->hmenu,
                                                 pqcminfo->indexMenu,
                                                 pqcminfo->idCmdFirst,
                                                 pqcminfo->idCmdLast,
                                                 CMF_NORMAL);
            if (FAILED_UNEXPECTEDLY(hr))
                return hr;

            pqcminfo->idCmdFirst += HRESULT_CODE(hr);
            return S_OK;
        }
        case DFM_INVOKECOMMANDEX:
            return E_NOTIMPL;
        case DFM_INVOKECOMMAND:
        {
            if (wParam == DFM_CMD_DELETE)
                return pThis->DoDeleteItems(pdtobj);

            CComQIIDPtr<I_ID(IContextMenu)> spContextMenu(psf);
            if (!spContextMenu)
            {
                DPRINT1("E_NOINTERFACE\n");
                return E_NOINTERFACE;
            }

            CMINVOKECOMMANDINFO ici = { sizeof(ici) };
            ici.hwnd = hwnd;
            ici.lpVerb = (LPSTR)wParam;
            ici.nShow = SW_SHOWNORMAL;

            return spContextMenu->InvokeCommand(&ici);
        }
        case DFM_GETDEFSTATICID: // Required for Windows 7 to pick a default
            return S_FALSE;
        case DFM_WM_INITMENUPOPUP: // FIXME: Make it effective in `CDefViewBckgrndMenu`
        {
            // Disable [Paste] / [Paste link] menu items
            ::EnableMenuItem((HMENU)wParam, FCIDM_SHVIEW_INSERT, MF_BYCOMMAND | MF_GRAYED);
            ::EnableMenuItem((HMENU)wParam, FCIDM_SHVIEW_INSERTLINK, MF_BYCOMMAND | MF_GRAYED);
            break;
        }
    }
    return E_NOTIMPL;
}

STDMETHODIMP CZipFolder::GetUIObjectOf(HWND hwndOwner, UINT cidl, PCUITEMID_CHILD_ARRAY apidl, REFIID riid, UINT * prgfInOut, LPVOID * ppvOut)
{
    m_hwnd = hwndOwner ? hwndOwner : m_hwnd;
    if ((riid == IID_IExtractIconA || riid == IID_IExtractIconW) && cidl == 1)
    {
        const ZipPidlEntry* zipEntry = _ZipFromIL(*apidl);
        if (zipEntry)
        {
            DWORD dwAttributes = (zipEntry->ZipType == ZIP_PIDL_DIRECTORY) ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
            return SHCreateFileExtractIconW(zipEntry->Name, dwAttributes, riid, ppvOut);
        }
    }
    else if (riid == IID_IContextMenu && cidl >= 0)
    {
        // Context menu of an object inside the zip
        const ZipPidlEntry* zipEntry = _ZipFromIL(*apidl);
        if (zipEntry)
        {
            HKEY keys[1] = {0};
            int nkeys = 0;
            if (zipEntry->ZipType == ZIP_PIDL_DIRECTORY)
            {
                LSTATUS res = RegOpenKeyExW(HKEY_CLASSES_ROOT, L"Folder", 0, KEY_READ | KEY_QUERY_VALUE, keys);
                if (res != ERROR_SUCCESS)
                    return E_FAIL;
                nkeys++;
            }
            return CDefFolderMenu_Create2(NULL, hwndOwner, cidl, apidl, this, ZipFolderMenuCallback, nkeys, keys, (IContextMenu**)ppvOut);
        }
    }
    else if (riid == IID_IDataObject && cidl >= 1)
    {
        return _CZipDataObject_CreateInstance(m_ZipFile, m_ZipDir, m_hwnd, m_CurDir, cidl, apidl, riid, ppvOut);
    }
    else if (riid == IID_IDropTarget && cidl == 1)
    {
        const ZipPidlEntry* zipEntry = _ZipFromIL(*apidl);
        if (!zipEntry || !zipEntry->IsDirectory())
            return E_NOINTERFACE;

        CStringW subDir = m_ZipDir;
        subDir += zipEntry->Name;
        subDir += L'/';
        return CreateDropTarget(subDir, riid, ppvOut);
    }

    DbgPrint("%s(%S) UNHANDLED\n", __FUNCTION__ , guid2string(riid));
    return E_NOINTERFACE;
}

STDMETHODIMP CZipFolder::GetDisplayNameOf(PCUITEMID_CHILD pidl, DWORD dwFlags, LPSTRRET strRet)
{
    if (!pidl)
        return S_FALSE;

    PCUIDLIST_RELATIVE curpidl = ILGetNext(pidl);
    if (curpidl->mkid.cb != 0)
    {
        DPRINT1("ERROR, unhandled PIDL!\n");
        return E_FAIL;
    }

    const ZipPidlEntry* zipEntry = _ZipFromIL(pidl);
    if (!zipEntry)
        return E_FAIL;

    if (dwFlags & SHGDN_FORPARSING)
    {
        if (dwFlags & SHGDN_INFOLDER)
            return SHSetStrRet(strRet, zipEntry->Name);

        WCHAR parent[MAX_PATH];
        if (!SHGetPathFromIDListW(m_CurDir, parent))
            return E_FAIL;
        UINT cch = lstrlenW(parent) + 1 + lstrlenW(zipEntry->Name) + 1;
        strRet->uType = STRRET_WSTR;
        strRet->pOleStr = (LPWSTR)SHAlloc(cch * sizeof(WCHAR));
        if (!strRet->pOleStr)
            return E_OUTOFMEMORY;
        lstrcpyW(strRet->pOleStr, parent);
        PathAppendW(strRet->pOleStr, zipEntry->Name);
        return S_OK;
    }

    SHFILEINFOW fi;
    DWORD attr = zipEntry->IsDirectory() ? FILE_ATTRIBUTE_DIRECTORY : 0;
    if (SHGetFileInfoW(zipEntry->Name, attr, &fi, sizeof(fi), SHGFI_DISPLAYNAME | SHGFI_USEFILEATTRIBUTES))
        return SHSetStrRet(strRet, fi.szDisplayName);
    return SHSetStrRet(strRet, zipEntry->Name);
}

STDMETHODIMP CZipFolder::GetCommandString(UINT_PTR idCmd, UINT uFlags, UINT *pwReserved, LPSTR pszName, UINT cchMax)
{
    if (idCmd != 0)
        return E_INVALIDARG;

    switch (uFlags)
    {
        case GCS_VERBA:
            return StringCchCopyA(pszName, cchMax, EXTRACT_VERBA);
        case GCS_VERBW:
            return StringCchCopyW((PWSTR)pszName, cchMax, EXTRACT_VERBW);
        case GCS_HELPTEXTA:
        {
            CStringA helpText(MAKEINTRESOURCEA(IDS_HELPTEXT));
            return StringCchCopyA(pszName, cchMax, helpText);
        }
        case GCS_HELPTEXTW:
        {
            CStringW helpText(MAKEINTRESOURCEA(IDS_HELPTEXT));
            return StringCchCopyW((PWSTR)pszName, cchMax, helpText);
        }
        case GCS_VALIDATEA:
        case GCS_VALIDATEW:
            return S_OK;
    }

    return E_INVALIDARG;
}

STDMETHODIMP CZipFolder::InvokeCommand(LPCMINVOKECOMMANDINFO pici)
{
    if (!pici || (pici->cbSize != sizeof(CMINVOKECOMMANDINFO) && pici->cbSize != sizeof(CMINVOKECOMMANDINFOEX)))
        return E_INVALIDARG;

    if (pici->lpVerb == MAKEINTRESOURCEA(0) ||
        (!IS_INTRESOURCE(pici->lpVerb) && !strcmp(pici->lpVerb, EXTRACT_VERBA)))
    {
        BSTR ZipFile = m_ZipFile.AllocSysString();
        InterlockedIncrement(&g_ModuleRefCnt);

        DWORD tid;
        HANDLE hThread = CreateThread(NULL, 0, s_ExtractProc, ZipFile, NULL, &tid);
        if (hThread)
        {
            CloseHandle(hThread);
            return S_OK;
        }
    }

    if (pici->lpVerb == MAKEINTRESOURCEA(DFM_CMD_DELETE) ||
        (!IS_INTRESOURCE(pici->lpVerb) && !strcmp(pici->lpVerb, "delete")))
    {
        return DoDeleteItems(m_pDataObj);
    }

    return E_INVALIDARG;
}

STDMETHODIMP CZipFolder::QueryContextMenu(HMENU hmenu, UINT indexMenu, UINT idCmdFirst, UINT idCmdLast, UINT uFlags)
{
    UINT idCmd = idCmdFirst;

    if (!(uFlags & CMF_DEFAULTONLY))
    {
        CStringW menuText(MAKEINTRESOURCEW(IDS_MENUITEM));

        if (indexMenu)
            InsertMenuW(hmenu, indexMenu++, MF_BYPOSITION | MF_SEPARATOR, 0, NULL);
        InsertMenuW(hmenu, indexMenu++, MF_BYPOSITION | MF_STRING, idCmd++, menuText); // Command 0
    }

    return MAKE_HRESULT(SEVERITY_SUCCESS, FACILITY_NULL, idCmd - idCmdFirst);
}

STDMETHODIMP CZipFolder::Initialize(PCIDLIST_ABSOLUTE pidlFolder, LPDATAOBJECT pDataObj, HKEY hkeyProgID)
{
    FORMATETC etc = { CF_HDROP, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
    STGMEDIUM stg;

    HRESULT hr = pDataObj->GetData(&etc, &stg);
    if (FAILED_UNEXPECTEDLY(hr))
        return hr;

    hr = E_FAIL;
    HDROP hdrop = (HDROP)GlobalLock(stg.hGlobal);
    if (hdrop)
    {
        UINT uNumFiles = DragQueryFileW(hdrop, 0xFFFFFFFF, NULL, 0);
        if (uNumFiles == 1)
        {
            WCHAR szFile[MAX_PATH * 2];
            if (DragQueryFileW(hdrop, 0, szFile, _countof(szFile)))
            {
                CComHeapPtr<ITEMIDLIST> pidl;
                hr = SHParseDisplayName(szFile, NULL, &pidl, 0, NULL);
                if (!FAILED_UNEXPECTEDLY(hr))
                {
                    hr = Initialize(pidl);
                }
            }
            else
            {
                DbgPrint("Failed to query the file.\r\n");
            }
        }
        else
        {
            DbgPrint("Invalid number of files: %d\r\n", uNumFiles);
        }
        GlobalUnlock(stg.hGlobal);
    }
    else
    {
        DbgPrint("Could not lock stg.hGlobal\r\n");
    }
    ReleaseStgMedium(&stg);
    return hr;
}

STDMETHODIMP CZipFolder::Initialize(PCIDLIST_ABSOLUTE pidl)
{
    WCHAR tmpPath[MAX_PATH];

    if (SHGetPathFromIDListW(pidl, tmpPath))
    {
        m_ZipFile = tmpPath;
        m_CurDir.Attach(ILClone(pidl));
        return S_OK;
    }
    DbgPrint("%s() => Unable to parse pidl\n", __FUNCTION__);
    return E_INVALIDARG;
}

class CEnumZipStatStg :
    public CComObjectRootEx<CComMultiThreadModelNoCS>,
    public IEnumSTATSTG
{
    struct Entry
    {
        CStringW Name;
        DWORD Type;
        ULONGLONG Size;
        FILETIME Time;
    };
    CAtlArray<Entry> m_Entries;
    size_t m_Index = 0;

public:
    void Add(PCWSTR Name, DWORD Type, ULONGLONG Size, const FILETIME &Time)
    {
        Entry entry;
        entry.Name = Name;
        entry.Type = Type;
        entry.Size = Size;
        entry.Time = Time;
        m_Entries.Add(entry);
    }

    void CopyFrom(const CEnumZipStatStg *pOther)
    {
        for (size_t i = 0; i < pOther->m_Entries.GetCount(); ++i)
            m_Entries.Add(pOther->m_Entries[i]);
        m_Index = pOther->m_Index;
    }

    STDMETHODIMP Next(ULONG celt, STATSTG *rgelt, ULONG *pceltFetched) override
    {
        ULONG fetched = 0;

        if (!rgelt || (celt > 1 && !pceltFetched))
            return STG_E_INVALIDPOINTER;

        while (fetched < celt && m_Index < m_Entries.GetCount())
        {
            const Entry &entry = m_Entries[m_Index];
            STATSTG *st = &rgelt[fetched];
            SIZE_T cb = (entry.Name.GetLength() + 1) * sizeof(WCHAR);

            ZeroMemory(st, sizeof(*st));
            st->pwcsName = (LPOLESTR)CoTaskMemAlloc(cb);
            if (!st->pwcsName)
                break;
            CopyMemory(st->pwcsName, entry.Name.GetString(), cb);
            st->type = entry.Type;
            st->cbSize.QuadPart = entry.Size;
            st->mtime = entry.Time;
            ++fetched;
            ++m_Index;
        }

        if (pceltFetched)
            *pceltFetched = fetched;
        return fetched == celt ? S_OK : S_FALSE;
    }

    STDMETHODIMP Skip(ULONG celt) override
    {
        while (celt-- > 0)
        {
            if (m_Index >= m_Entries.GetCount())
                return S_FALSE;
            ++m_Index;
        }
        return S_OK;
    }

    STDMETHODIMP Reset() override
    {
        m_Index = 0;
        return S_OK;
    }

    STDMETHODIMP Clone(IEnumSTATSTG **ppenum) override
    {
        CComPtr<IEnumSTATSTG> pEnum;
        HRESULT hr = ShellObjectCreator<CEnumZipStatStg>(IID_PPV_ARG(IEnumSTATSTG, &pEnum));
        if (FAILED_UNEXPECTEDLY(hr))
            return hr;
        static_cast<CEnumZipStatStg *>(pEnum.p)->CopyFrom(this);
        *ppenum = pEnum.Detach();
        return S_OK;
    }

    DECLARE_NOT_AGGREGATABLE(CEnumZipStatStg)
    DECLARE_PROTECT_FINAL_CONSTRUCT()

    BEGIN_COM_MAP(CEnumZipStatStg)
        COM_INTERFACE_ENTRY_IID(IID_IEnumSTATSTG, IEnumSTATSTG)
    END_COM_MAP()
};

HRESULT CZipFolder::FindEntry(LPCOLESTR pwcsName, ZipDataItem *pItem)
{
    CZipEnumerator zipEnum;
    CStringW path = m_ZipDir + pwcsName;
    CStringW dirPath = path;
    dirPath += L'/';
    CStringW name;
    unz_file_info64 info;
    bool found = false;

    if (!zipEnum.Initialize(this))
        return STG_E_FILENOTFOUND;

    pItem->Directory = false;
    while (zipEnum.Next(name, info))
    {
        if (!name.CompareNoCase(path))
        {
            pItem->Name = name;
            pItem->Directory = false;
            pItem->Password = (info.flag & MINIZIP_PASSWORD_FLAG) != 0;
            pItem->HasDate = true;
            pItem->DosDate = info.dosDate;
            pItem->Size = info.uncompressed_size;
            if (unzGetFilePos64(getZip(), &pItem->Pos) != UNZ_OK)
                return STG_E_READFAULT;
            return S_OK;
        }
        if (!found && name.GetLength() >= dirPath.GetLength() &&
            !StrCmpNIW(name, dirPath, dirPath.GetLength()))
        {
            found = true;
        }
    }

    if (!found)
        return STG_E_FILENOTFOUND;

    pItem->Name = dirPath;
    pItem->Directory = true;
    return S_OK;
}

STDMETHODIMP CZipFolder::CreateStream(LPCOLESTR pwcsName, DWORD grfMode, DWORD reserved1, DWORD reserved2, IStream **ppstm)
{
    return E_NOTIMPL;
}

STDMETHODIMP CZipFolder::OpenStream(LPCOLESTR pwcsName, void *reserved1, DWORD grfMode, DWORD reserved2, IStream **ppstm)
{
    ZipDataItem item;
    HRESULT hr;

    if (!pwcsName || !ppstm)
        return STG_E_INVALIDPOINTER;
    *ppstm = NULL;

    if (grfMode & (STGM_WRITE | STGM_READWRITE))
        return E_NOTIMPL;

    hr = FindEntry(pwcsName, &item);
    if (FAILED(hr))
        return hr;
    if (item.Directory)
        return STG_E_FILENOTFOUND;

    return _CZipStream_CreateInstance(m_ZipFile, &item, "", IID_PPV_ARG(IStream, ppstm));
}

STDMETHODIMP CZipFolder::CreateStorage(LPCOLESTR pwcsName, DWORD grfMode, DWORD dwStgFmt, DWORD reserved2, IStorage **ppstg)
{
    return E_FAIL;
}

STDMETHODIMP CZipFolder::OpenStorage(LPCOLESTR pwcsName, IStorage *pstgPriority, DWORD grfMode, SNB snbExclude, DWORD reserved, IStorage **ppstg)
{
    ZipDataItem item;
    unz_file_info64 info = {};
    HRESULT hr;

    if (!pwcsName || !ppstg)
        return STG_E_INVALIDPOINTER;
    *ppstg = NULL;

    hr = FindEntry(pwcsName, &item);
    if (FAILED(hr))
        return hr;
    if (!item.Directory)
        return E_NOTIMPL;

    CComHeapPtr<ITEMIDLIST> pidl(_ILCreateZipItem(ZIP_PIDL_DIRECTORY, pwcsName, info));
    if (!pidl)
        return E_OUTOFMEMORY;

    return ShellObjectCreatorInit<CZipFolder>(m_ZipFile, item.Name, m_CurDir, pidl, IID_PPV_ARG(IStorage, ppstg));
}

STDMETHODIMP CZipFolder::CopyTo(DWORD ciidExclude, const IID *rgiidExclude, SNB snbExclude, IStorage *pstgDest)
{
    return E_NOTIMPL;
}

STDMETHODIMP CZipFolder::MoveElementTo(LPCOLESTR pwcsName, IStorage *pstgDest, LPCOLESTR pwcsNewName, DWORD grfFlags)
{
    return E_NOTIMPL;
}

STDMETHODIMP CZipFolder::Commit(DWORD grfCommitFlags)
{
    return S_OK;
}

STDMETHODIMP CZipFolder::Revert()
{
    return E_NOTIMPL;
}

STDMETHODIMP CZipFolder::EnumElements(DWORD reserved1, void *reserved2, DWORD reserved3, IEnumSTATSTG **ppenum)
{
    CComPtr<IEnumSTATSTG> pEnum;
    CEnumZipStatStg *pImpl;
    CZipEnumerator zipEnum;
    CStringW name;
    bool folder;
    unz_file_info64 info;
    HRESULT hr;

    if (!ppenum)
        return STG_E_INVALIDPOINTER;
    *ppenum = NULL;

    hr = ShellObjectCreator<CEnumZipStatStg>(IID_PPV_ARG(IEnumSTATSTG, &pEnum));
    if (FAILED_UNEXPECTEDLY(hr))
        return hr;
    pImpl = static_cast<CEnumZipStatStg *>(pEnum.p);

    if (zipEnum.Initialize(this))
    {
        while (zipEnum.NextUnique(m_ZipDir, name, folder, info))
        {
            FILETIME time = {};
            if (!folder)
                ZipDateToFileTime(info.dosDate, &time);
            pImpl->Add(name, folder ? STGTY_STORAGE : STGTY_STREAM, folder ? 0 : info.uncompressed_size, time);
        }
    }

    *ppenum = pEnum.Detach();
    return S_OK;
}

STDMETHODIMP CZipFolder::DestroyElement(LPCOLESTR pwcsName)
{
    CAtlList<CStringW> targetPaths;
    ZipDataItem item;
    HRESULT hr;

    if (!pwcsName)
        return STG_E_INVALIDPOINTER;

    hr = FindEntry(pwcsName, &item);
    if (FAILED(hr))
        return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);

    targetPaths.AddTail(item.Name);
    return DeleteEntries(targetPaths);
}

STDMETHODIMP CZipFolder::RenameElement(LPCOLESTR pwcsOldName, LPCOLESTR pwcsNewName)
{
    return E_NOTIMPL;
}

STDMETHODIMP CZipFolder::SetElementTimes(LPCOLESTR pwcsName, const FILETIME *pctime, const FILETIME *patime, const FILETIME *pmtime)
{
    return E_NOTIMPL;
}

STDMETHODIMP CZipFolder::SetClass(REFCLSID clsid)
{
    return E_NOTIMPL;
}

STDMETHODIMP CZipFolder::SetStateBits(DWORD grfStateBits, DWORD grfMask)
{
    return E_NOTIMPL;
}

STDMETHODIMP CZipFolder::Stat(STATSTG *pstatstg, DWORD grfStatFlag)
{
    if (!pstatstg)
        return STG_E_INVALIDPOINTER;

    ZeroMemory(pstatstg, sizeof(*pstatstg));
    if (!(grfStatFlag & STATFLAG_NONAME))
    {
        pstatstg->pwcsName = (LPOLESTR)CoTaskMemAlloc(sizeof(WCHAR));
        if (!pstatstg->pwcsName)
            return E_OUTOFMEMORY;
        pstatstg->pwcsName[0] = UNICODE_NULL;
    }
    pstatstg->type = STGTY_STORAGE;
    return S_OK;
}

STDMETHODIMP CZipFolder::GetFolderType(FOLDERTYPEID *pftid)
{
    if (!pftid)
        return E_POINTER;
    *pftid = FOLDERTYPEID_CompressedFolder;
    return S_OK;
}
