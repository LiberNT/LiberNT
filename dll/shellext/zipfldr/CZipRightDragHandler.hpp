/*
 * PROJECT:     LiberNT Zip Shell Extension
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Right-drag handler of compressed folders
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

class CZipRightDragHandler :
    public CComCoClass<CZipRightDragHandler, &CLSID_ZipFolderRightDragHandler>,
    public CComObjectRootEx<CComMultiThreadModelNoCS>,
    public IShellExtInit,
    public IContextMenu
{
    CStringW m_TargetDir;
    CAtlList<CStringW> m_ZipFiles;

public:
    CZipRightDragHandler()
    {
        InterlockedIncrement(&g_ModuleRefCnt);
    }

    virtual ~CZipRightDragHandler()
    {
        InterlockedDecrement(&g_ModuleRefCnt);
    }

    // *** IShellExtInit methods ***
    STDMETHODIMP Initialize(PCIDLIST_ABSOLUTE pidlFolder, LPDATAOBJECT pDataObj, HKEY hkeyProgID) override;

    // *** IContextMenu methods ***
    STDMETHODIMP QueryContextMenu(HMENU hmenu, UINT indexMenu, UINT idCmdFirst, UINT idCmdLast, UINT uFlags) override;
    STDMETHODIMP InvokeCommand(LPCMINVOKECOMMANDINFO pici) override;
    STDMETHODIMP GetCommandString(UINT_PTR idCmd, UINT uFlags, UINT *pwReserved, LPSTR pszName, UINT cchMax) override;

public:
    DECLARE_NO_REGISTRY()
    DECLARE_NOT_AGGREGATABLE(CZipRightDragHandler)
    DECLARE_PROTECT_FINAL_CONSTRUCT()

    BEGIN_COM_MAP(CZipRightDragHandler)
        COM_INTERFACE_ENTRY_IID(IID_IShellExtInit, IShellExtInit)
        COM_INTERFACE_ENTRY_IID(IID_IContextMenu, IContextMenu)
    END_COM_MAP()
};
