/*
 * Trash virtual folder support. The trashing engine is implemented in trash.c
 *
 * Copyright (C) 2006 Mikolaj Zalewski
 * Copyright (C) 2009 Andrew Hill
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#ifndef _SHFLDR_RECYCLEBIN_H_
#define _SHFLDR_RECYCLEBIN_H_

BOOL TRASH_CanTrashFile(LPCWSTR wszPath);
BOOL TRASH_TrashFile(LPCWSTR wszPath);
HRESULT CRecyclerDropTarget_CreateInstance(REFIID riid, LPVOID * ppvOut);

class CRecycleBin :
    public CComCoClass<CRecycleBin, &CLSID_RecycleBin>,
    public CComObjectRootEx<CComMultiThreadModelNoCS>,
    public IShellFolder2,
    public IPersistFolder2,
    public IContextMenuCB,
    public IShellPropSheetExt,
    public IShellExtInit,
    public IObjectWithBackReferences
{
    private:
        LPITEMIDLIST pidl;
        IShellFolder *m_pFSFolders[RECYCLEBINMAXDRIVECOUNT];

        IShellFolder* GetFSFolderForItem(LPCITEMIDLIST pidl);

    public:
        CRecycleBin();
        ~CRecycleBin();
        static inline REFCLSID GetClassID() { return CLSID_RecycleBin; }

        HRESULT ParseRecycleBinPath(LPCWSTR lpszPath, LPBC pbc, PIDLIST_RELATIVE *ppidl, DWORD *pdwAttributes);

        // IPersistFolder
        STDMETHOD(GetClassID)(CLSID *pClassID) override;
        STDMETHOD(Initialize)(PCIDLIST_ABSOLUTE pidl) override;

        // IPersistFolder2
        STDMETHOD(GetCurFolder)(PIDLIST_ABSOLUTE * pidl) override;

        // IShellFolder
        STDMETHOD(ParseDisplayName)(HWND hwndOwner, LPBC pbc, LPOLESTR lpszDisplayName, DWORD *pchEaten, PIDLIST_RELATIVE *ppidl, DWORD *pdwAttributes) override;
        STDMETHOD(EnumObjects)(HWND hwndOwner, DWORD dwFlags, LPENUMIDLIST *ppEnumIDList) override;
        STDMETHOD(BindToObject)(PCUIDLIST_RELATIVE pidl, LPBC pbcReserved, REFIID riid, LPVOID *ppvOut) override;
        STDMETHOD(BindToStorage)(PCUIDLIST_RELATIVE pidl, LPBC pbcReserved, REFIID riid, LPVOID *ppvOut) override;
        STDMETHOD(CompareIDs)(LPARAM lParam, PCUIDLIST_RELATIVE pidl1, PCUIDLIST_RELATIVE pidl2) override;
        STDMETHOD(CreateViewObject)(HWND hwndOwner, REFIID riid, LPVOID *ppvOut) override;
        STDMETHOD(GetAttributesOf)(UINT cidl, PCUITEMID_CHILD_ARRAY apidl, DWORD *rgfInOut) override;
        STDMETHOD(GetUIObjectOf)(HWND hwndOwner, UINT cidl, PCUITEMID_CHILD_ARRAY apidl, REFIID riid, UINT * prgfInOut, LPVOID * ppvOut) override;
        STDMETHOD(GetDisplayNameOf)(PCUITEMID_CHILD pidl, DWORD dwFlags, LPSTRRET strRet) override;
        STDMETHOD(SetNameOf)(HWND hwndOwner, PCUITEMID_CHILD pidl, LPCOLESTR lpName, DWORD dwFlags, PITEMID_CHILD *pPidlOut) override;

        // IShellFolder2
        STDMETHOD(GetDefaultSearchGUID)(GUID *pguid) override;
        STDMETHOD(EnumSearches)(IEnumExtraSearch **ppenum) override;
        STDMETHOD(GetDefaultColumn)(DWORD dwRes, ULONG *pSort, ULONG *pDisplay) override;
        STDMETHOD(GetDefaultColumnState)(UINT iColumn, DWORD *pcsFlags) override;
        STDMETHOD(GetDetailsEx)(PCUITEMID_CHILD pidl, const SHCOLUMNID *pscid, VARIANT *pv) override;
        STDMETHOD(GetDetailsOf)(PCUITEMID_CHILD pidl, UINT iColumn, SHELLDETAILS *psd) override;
        STDMETHOD(MapColumnToSCID)(UINT column, SHCOLUMNID *pscid) override;

        // IContextMenuCB
        STDMETHOD(CallBack)(IShellFolder *psf, HWND hwndOwner, IDataObject *pdtobj, UINT uMsg, WPARAM wParam, LPARAM lParam) override;

        // IShellPropSheetExt
        STDMETHOD(AddPages)(LPFNSVADDPROPSHEETPAGE pfnAddPage, LPARAM lParam) override;
        STDMETHOD(ReplacePage)(EXPPS uPageID, LPFNSVADDPROPSHEETPAGE pfnReplaceWith, LPARAM lParam) override;

        // IShellExtInit
        STDMETHOD(Initialize)(PCIDLIST_ABSOLUTE pidlFolder, IDataObject *pdtobj, HKEY hkeyProgID) override;

        // IObjectWithBackReferences
        STDMETHOD(RemoveBackReferences)() override;

        DECLARE_REGISTRY_RESOURCEID(IDR_RECYCLEBIN)
        DECLARE_NOT_AGGREGATABLE(CRecycleBin)

        DECLARE_PROTECT_FINAL_CONSTRUCT()

        BEGIN_COM_MAP(CRecycleBin)
        COM_INTERFACE_ENTRY_IID(IID_IPersistFolder, IPersistFolder)
        COM_INTERFACE_ENTRY_IID(IID_IPersistFolder2, IPersistFolder2)
        COM_INTERFACE_ENTRY2_IID(IID_IPersist, IPersist, IPersistFolder2)
        COM_INTERFACE_ENTRY_IID(IID_IShellFolder, IShellFolder)
        COM_INTERFACE_ENTRY_IID(IID_IShellFolder2, IShellFolder2)
        COM_INTERFACE_ENTRY_IID(IID_IContextMenuCB, IContextMenuCB)
        COM_INTERFACE_ENTRY_IID(IID_IShellPropSheetExt, IShellPropSheetExt)
        COM_INTERFACE_ENTRY_IID(IID_IShellExtInit, IShellExtInit)
        COM_INTERFACE_ENTRY_IID(IID_IObjectWithBackReferences, IObjectWithBackReferences)
        END_COM_MAP()
};

class CRecycleBinEmptyCommand :
    public CComCoClass<CRecycleBinEmptyCommand, &CLSID_RecycleBinEmptyCommand>,
    public CComObjectRootEx<CComMultiThreadModelNoCS>,
    public IExecuteCommand,
    public IObjectWithSelection,
    public IObjectWithSite
{
    private:
        CComPtr<IShellItemArray> m_pSelection;
        CComPtr<IUnknown> m_pSite;

    public:
        // IExecuteCommand
        STDMETHOD(SetKeyState)(DWORD grfKeyState) override;
        STDMETHOD(SetParameters)(LPCWSTR pszParameters) override;
        STDMETHOD(SetPosition)(POINT pt) override;
        STDMETHOD(SetShowWindow)(int nShow) override;
        STDMETHOD(SetNoShowUI)(BOOL fNoShowUI) override;
        STDMETHOD(SetDirectory)(LPCWSTR pszDirectory) override;
        STDMETHOD(Execute)() override;

        // IObjectWithSelection
        STDMETHOD(SetSelection)(IShellItemArray *psia) override;
        STDMETHOD(GetSelection)(REFIID riid, void **ppv) override;

        // IObjectWithSite
        STDMETHOD(SetSite)(IUnknown *pUnkSite) override;
        STDMETHOD(GetSite)(REFIID riid, void **ppvSite) override;

        DECLARE_REGISTRY_RESOURCEID(IDR_RECYCLEBINEMPTYCMD)
        DECLARE_NOT_AGGREGATABLE(CRecycleBinEmptyCommand)
        DECLARE_PROTECT_FINAL_CONSTRUCT()

        BEGIN_COM_MAP(CRecycleBinEmptyCommand)
        COM_INTERFACE_ENTRY_IID(IID_IExecuteCommand, IExecuteCommand)
        COM_INTERFACE_ENTRY_IID(IID_IObjectWithSelection, IObjectWithSelection)
        COM_INTERFACE_ENTRY_IID(IID_IObjectWithSite, IObjectWithSite)
        END_COM_MAP()
};

class CRecycleBinEmptyState :
    public CComCoClass<CRecycleBinEmptyState, &CLSID_RecycleBinEmptyState>,
    public CComObjectRootEx<CComMultiThreadModelNoCS>,
    public IExplorerCommandState
{
    public:
        // IExplorerCommandState
        STDMETHOD(GetState)(IShellItemArray *psiItemArray, BOOL fOkToBeSlow, EXPCMDSTATE *pCmdState) override;

        DECLARE_REGISTRY_RESOURCEID(IDR_RECYCLEBINEMPTYSTATE)
        DECLARE_NOT_AGGREGATABLE(CRecycleBinEmptyState)
        DECLARE_PROTECT_FINAL_CONSTRUCT()

        BEGIN_COM_MAP(CRecycleBinEmptyState)
        COM_INTERFACE_ENTRY_IID(IID_IExplorerCommandState, IExplorerCommandState)
        END_COM_MAP()
};

#endif /* _SHFLDR_RECYCLEBIN_H_ */
