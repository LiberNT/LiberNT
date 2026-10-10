/*
 * PROJECT:     LiberNT Zip Shell Extension
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Drop handler of compressed folders
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

HRESULT CZipFolderDropHandler::InitializeFolder(CZipFolder *pZipFolder, PCWSTR zipFile, PCWSTR zipDir, PCIDLIST_ABSOLUTE pidl)
{
    m_pZipFolder = pZipFolder;
    m_pFolder = pZipFolder ? static_cast<IShellFolder *>(pZipFolder) : NULL;
    m_ZipFile = zipFile;
    m_ZipDir = zipDir;
    m_Pidl.Free();
    if (pidl)
    {
        m_Pidl.Attach(ILClone(pidl));
        if (!m_Pidl)
            return E_OUTOFMEMORY;
    }
    return S_OK;
}

STDMETHODIMP CZipFolderDropHandler::Initialize(IShellItem *psi, DWORD grfMode)
{
    CComHeapPtr<WCHAR> path;
    CComHeapPtr<ITEMIDLIST> pidl;
    HRESULT hr;

    if (!psi)
        return E_INVALIDARG;

    hr = psi->GetDisplayName(SIGDN_FILESYSPATH, &path);
    if (FAILED_UNEXPECTEDLY(hr))
        return hr;

    hr = SHGetIDListFromObject(psi, &pidl);
    if (FAILED_UNEXPECTEDLY(hr))
        return hr;

    return InitializeFolder(NULL, path, L"", pidl);
}

STDMETHODIMP CZipFolderDropHandler::DragEnter(IDataObject *pDataObj, DWORD grfKeyState, POINTL pt, DWORD *pdwEffect)
{
    *pdwEffect &= DROPEFFECT_COPY;
    return S_OK;
}

STDMETHODIMP CZipFolderDropHandler::DragOver(DWORD grfKeyState, POINTL pt, DWORD *pdwEffect)
{
    *pdwEffect &= DROPEFFECT_COPY;
    return S_OK;
}

STDMETHODIMP CZipFolderDropHandler::DragLeave()
{
    return S_OK;
}

STDMETHODIMP CZipFolderDropHandler::Drop(IDataObject *pDataObj, DWORD grfKeyState, POINTL pt, DWORD *pdwEffect)
{
    STGMEDIUM sm;
    FORMATETC fe = { CF_HDROP, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };

    if (m_ZipFile.IsEmpty())
        return E_UNEXPECTED;

    HRESULT hr = pDataObj->GetData(&fe, &sm);
    if (FAILED_UNEXPECTEDLY(hr))
        return hr;

    HDROP hDrop = (HDROP)GlobalLock(sm.hGlobal);
    if (hDrop)
    {
        if (m_pZipFolder)
            m_pZipFolder->Close();

        CZipCreator *pCreator = CZipCreator::DoCreate(m_ZipFile, m_ZipDir);
        pCreator->SetNotifyPidl(m_Pidl);

        UINT fileCount = DragQueryFileW(hDrop, 0xFFFFFFFF, NULL, 0);
        for (UINT i = 0; i < fileCount; i++)
        {
            WCHAR szFilePath[MAX_PATH];
            DragQueryFileW(hDrop, i, szFilePath, MAX_PATH);
            pCreator->DoAddItem(szFilePath);
        }

        CZipCreator::runThread(pCreator);

        GlobalUnlock(sm.hGlobal);
        *pdwEffect = DROPEFFECT_COPY;
    }
    ReleaseStgMedium(&sm);

    return S_OK;
}
