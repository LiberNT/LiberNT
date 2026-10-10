/*
 * PROJECT:     LiberNT Zip Shell Extension
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Drop handler of compressed folders
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

class CZipFolderDropHandler :
    public CComCoClass<CZipFolderDropHandler, &CLSID_ZipFolderDropHandler>,
    public CComObjectRootEx<CComMultiThreadModelNoCS>,
    public IDropTarget,
    public IInitializeWithItem
{
    CStringW m_ZipFile;
    CStringW m_ZipDir;
    CComHeapPtr<ITEMIDLIST> m_Pidl;
    CComPtr<IShellFolder> m_pFolder;
    CZipFolder *m_pZipFolder = nullptr;

public:
    CZipFolderDropHandler()
    {
        InterlockedIncrement(&g_ModuleRefCnt);
    }

    virtual ~CZipFolderDropHandler()
    {
        InterlockedDecrement(&g_ModuleRefCnt);
    }

    HRESULT InitializeFolder(CZipFolder *pZipFolder, PCWSTR zipFile, PCWSTR zipDir, PCIDLIST_ABSOLUTE pidl);

    // *** IInitializeWithItem methods ***
    STDMETHODIMP Initialize(IShellItem *psi, DWORD grfMode) override;

    // *** IDropTarget methods ***
    STDMETHODIMP DragEnter(IDataObject *pDataObj, DWORD grfKeyState, POINTL pt, DWORD *pdwEffect) override;
    STDMETHODIMP DragOver(DWORD grfKeyState, POINTL pt, DWORD *pdwEffect) override;
    STDMETHODIMP DragLeave() override;
    STDMETHODIMP Drop(IDataObject *pDataObj, DWORD grfKeyState, POINTL pt, DWORD *pdwEffect) override;

public:
    DECLARE_NO_REGISTRY()
    DECLARE_NOT_AGGREGATABLE(CZipFolderDropHandler)
    DECLARE_PROTECT_FINAL_CONSTRUCT()

    BEGIN_COM_MAP(CZipFolderDropHandler)
        COM_INTERFACE_ENTRY_IID(IID_IDropTarget, IDropTarget)
        COM_INTERFACE_ENTRY_IID(IID_IInitializeWithItem, IInitializeWithItem)
    END_COM_MAP()
};
