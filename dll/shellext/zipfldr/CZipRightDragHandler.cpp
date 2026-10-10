/*
 * PROJECT:     LiberNT Zip Shell Extension
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Right-drag handler of compressed folders
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

STDMETHODIMP CZipRightDragHandler::Initialize(PCIDLIST_ABSOLUTE pidlFolder, LPDATAOBJECT pDataObj, HKEY hkeyProgID)
{
    FORMATETC etc = { CF_HDROP, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
    WCHAR szPath[MAX_PATH];
    STGMEDIUM stg;
    HRESULT hr;

    m_ZipFiles.RemoveAll();
    m_TargetDir.Empty();

    if (!pidlFolder || !pDataObj)
        return E_INVALIDARG;
    if (!SHGetPathFromIDListW(pidlFolder, szPath))
        return E_FAIL;
    m_TargetDir = szPath;

    hr = pDataObj->GetData(&etc, &stg);
    if (FAILED_UNEXPECTEDLY(hr))
        return hr;

    HDROP hDrop = (HDROP)GlobalLock(stg.hGlobal);
    if (hDrop)
    {
        UINT count = DragQueryFileW(hDrop, 0xFFFFFFFF, NULL, 0);
        for (UINT i = 0; i < count; i++)
        {
            if (DragQueryFileW(hDrop, i, szPath, _countof(szPath)) &&
                !_wcsicmp(PathFindExtensionW(szPath), L".zip"))
            {
                m_ZipFiles.AddTail(szPath);
            }
        }
        GlobalUnlock(stg.hGlobal);
    }
    ReleaseStgMedium(&stg);
    return S_OK;
}

STDMETHODIMP CZipRightDragHandler::QueryContextMenu(HMENU hmenu, UINT indexMenu, UINT idCmdFirst, UINT idCmdLast, UINT uFlags)
{
    if ((uFlags & CMF_DEFAULTONLY) || m_ZipFiles.IsEmpty() || idCmdFirst > idCmdLast)
        return MAKE_HRESULT(SEVERITY_SUCCESS, FACILITY_NULL, 0);

    CStringW text(MAKEINTRESOURCEW(IDS_DRAGEXTRACT));
    InsertMenuW(hmenu, indexMenu, MF_BYPOSITION | MF_STRING, idCmdFirst, text);
    InsertMenuW(hmenu, indexMenu + 1, MF_BYPOSITION | MF_SEPARATOR, 0, NULL);
    return MAKE_HRESULT(SEVERITY_SUCCESS, FACILITY_NULL, 1);
}

STDMETHODIMP CZipRightDragHandler::InvokeCommand(LPCMINVOKECOMMANDINFO pici)
{
    if (!pici || !IS_INTRESOURCE(pici->lpVerb) || LOWORD(pici->lpVerb) != 0)
        return E_INVALIDARG;

    POSITION pos = m_ZipFiles.GetHeadPosition();
    while (pos)
    {
        const CStringW &zipFile = m_ZipFiles.GetNext(pos);
        CStringW name = PathFindFileNameW(zipFile);
        PWSTR buffer = name.GetBuffer();
        PathRemoveExtensionW(buffer);
        name.ReleaseBuffer();

        CStringW directory = m_TargetDir;
        PWSTR dirBuffer = directory.GetBuffer(MAX_PATH);
        PathAppendW(dirBuffer, name);
        directory.ReleaseBuffer();

        _CZipExtract_runWizardTo(zipFile, directory);
    }
    return S_OK;
}

STDMETHODIMP CZipRightDragHandler::GetCommandString(UINT_PTR idCmd, UINT uFlags, UINT *pwReserved, LPSTR pszName, UINT cchMax)
{
    return E_NOTIMPL;
}
