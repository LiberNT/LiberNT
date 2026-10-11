/*
 * Control panel folder
 *
 * Copyright 2003 Martin Fuchs
 * Copyright 2009 Andrew Hill
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

#ifndef _SHFLDR_CPANEL_H_
#define _SHFLDR_CPANEL_H_

class CControlPanelFolder :
    public CComCoClass<CControlPanelFolder, &CLSID_ControlPanel>,
    public CComObjectRootEx<CComMultiThreadModelNoCS>,
    public IShellFolder2,
    public IPersistFolder2,
    public IShellIconOverlay,
    public IShellIcon,
    public IStorage,
    public IContextMenuCB,
    public IFolderType,
    public IRegItemCustomAttributes,
    public IObjectWithBackReferences,
    public IBackReferencedObject,
    public IDelegateHostItemContainer
{
    private:
        /* both paths are parsible from the desktop */
        LPITEMIDLIST pidlRoot;  /* absolute pidl */
        int dwAttributes;       /* attributes returned by GetAttributesOf FIXME: use it */
        CComPtr<IShellFolder2> m_regFolder;
        LONG m_cBackReferences;
        CComHeapPtr<WCHAR> m_pszRemoteComputer;

        HRESULT WINAPI ExecuteFromIdList(LPCITEMIDLIST pidl);

    public:
        CControlPanelFolder();
        ~CControlPanelFolder();

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

        /* ShellFolder2 */
        STDMETHOD(GetDefaultSearchGUID)(GUID *pguid) override;
        STDMETHOD(EnumSearches)(IEnumExtraSearch **ppenum) override;
        STDMETHOD(GetDefaultColumn)(DWORD dwRes, ULONG *pSort, ULONG *pDisplay) override;
        STDMETHOD(GetDefaultColumnState)(UINT iColumn, DWORD *pcsFlags) override;
        STDMETHOD(GetDetailsEx)(PCUITEMID_CHILD pidl, const SHCOLUMNID *pscid, VARIANT *pv) override;
        STDMETHOD(GetDetailsOf)(PCUITEMID_CHILD pidl, UINT iColumn, SHELLDETAILS *psd) override;
        STDMETHOD(MapColumnToSCID)(UINT column, SHCOLUMNID *pscid) override;

        // IPersist
        STDMETHOD(GetClassID)(CLSID *lpClassId) override;

        // IPersistFolder
        STDMETHOD(Initialize)(PCIDLIST_ABSOLUTE pidl) override;

        // IPersistFolder2
        STDMETHOD(GetCurFolder)(PIDLIST_ABSOLUTE * pidl) override;

        // IShellIconOverlay
        STDMETHOD(GetOverlayIndex)(PCUITEMID_CHILD pidl, int *pIndex) override;
        STDMETHOD(GetOverlayIconIndex)(PCUITEMID_CHILD pidl, int *pIconIndex) override;

        // IShellIcon
        STDMETHOD(GetIconOf)(PCUITEMID_CHILD pidl, UINT flags, int *pIconIndex) override;

        // IStorage
        STDMETHOD(CreateStream)(LPCOLESTR pwcsName, DWORD grfMode, DWORD reserved1, DWORD reserved2, IStream **ppstm) override;
        STDMETHOD(OpenStream)(LPCOLESTR pwcsName, void *reserved1, DWORD grfMode, DWORD reserved2, IStream **ppstm) override;
        STDMETHOD(CreateStorage)(LPCOLESTR pwcsName, DWORD grfMode, DWORD reserved1, DWORD reserved2, IStorage **ppstg) override;
        STDMETHOD(OpenStorage)(LPCOLESTR pwcsName, IStorage *pstgPriority, DWORD grfMode, SNB snbExclude, DWORD reserved, IStorage **ppstg) override;
        STDMETHOD(CopyTo)(DWORD ciidExclude, const IID *rgiidExclude, SNB snbExclude, IStorage *pstgDest) override;
        STDMETHOD(MoveElementTo)(LPCOLESTR pwcsName, IStorage *pstgDest, LPCOLESTR pwcsNewName, DWORD grfFlags) override;
        STDMETHOD(Commit)(DWORD grfCommitFlags) override;
        STDMETHOD(Revert)() override;
        STDMETHOD(EnumElements)(DWORD reserved1, void *reserved2, DWORD reserved3, IEnumSTATSTG **ppenum) override;
        STDMETHOD(DestroyElement)(LPCOLESTR pwcsName) override;
        STDMETHOD(RenameElement)(LPCOLESTR pwcsOldName, LPCOLESTR pwcsNewName) override;
        STDMETHOD(SetElementTimes)(LPCOLESTR pwcsName, const FILETIME *pctime, const FILETIME *patime, const FILETIME *pmtime) override;
        STDMETHOD(SetClass)(REFCLSID clsid) override;
        STDMETHOD(SetStateBits)(DWORD grfStateBits, DWORD grfMask) override;
        STDMETHOD(Stat)(STATSTG *pstatstg, DWORD grfStatFlag) override;

        // IContextMenuCB
        STDMETHOD(CallBack)(IShellFolder *psf, HWND hwndOwner, IDataObject *pdtobj, UINT uMsg, WPARAM wParam, LPARAM lParam) override;

        // IFolderType
        STDMETHOD(GetFolderType)(FOLDERTYPEID *pftid) override;

        STDMETHOD(GetCustomAttributes)(PCUITEMID_CHILD pidl, DWORD dwFlags, ControlPanelCategoryInfo *pInfo, DWORD *pdwAttributes) override;

        // IObjectWithBackReferences
        STDMETHOD(RemoveBackReferences)() override;

        // IBackReferencedObject
        STDMETHOD(AddRefBackReference)() override;
        STDMETHOD(ReleaseBackReference)() override;

        // IDelegateHostItemContainer
        STDMETHOD(SetRemoteComputer)(LPCWSTR pszComputer) override;

        DECLARE_REGISTRY_RESOURCEID(IDR_CONTROLPANEL)
        DECLARE_NOT_AGGREGATABLE(CControlPanelFolder)

        DECLARE_PROTECT_FINAL_CONSTRUCT()

        BEGIN_COM_MAP(CControlPanelFolder)
        COM_INTERFACE_ENTRY_IID(IID_IShellFolder2, IShellFolder2)
        COM_INTERFACE_ENTRY_IID(IID_IShellFolder, IShellFolder)
        COM_INTERFACE_ENTRY_IID(IID_IPersistFolder, IPersistFolder)
        COM_INTERFACE_ENTRY_IID(IID_IPersistFolder2, IPersistFolder2)
        COM_INTERFACE_ENTRY_IID(IID_IPersist, IPersist)
        COM_INTERFACE_ENTRY_IID(IID_IShellIconOverlay, IShellIconOverlay)
        COM_INTERFACE_ENTRY_IID(IID_IShellIcon, IShellIcon)
        COM_INTERFACE_ENTRY_IID(IID_IStorage, IStorage)
        COM_INTERFACE_ENTRY_IID(IID_IContextMenuCB, IContextMenuCB)
        COM_INTERFACE_ENTRY_IID(IID_IFolderType, IFolderType)
        COM_INTERFACE_ENTRY_IID(IID_IRegItemCustomAttributes, IRegItemCustomAttributes)
        COM_INTERFACE_ENTRY_IID(IID_IObjectWithBackReferences, IObjectWithBackReferences)
        COM_INTERFACE_ENTRY_IID(IID_IBackReferencedObject, IBackReferencedObject)
        COM_INTERFACE_ENTRY_IID(IID_IDelegateHostItemContainer, IDelegateHostItemContainer)
        END_COM_MAP()
};

class CControlPanelFolderViewCB :
    public CComObjectRootEx<CComMultiThreadModelNoCS>,
    public IShellFolderViewCB
{
    public:
        // IShellFolderViewCB
        STDMETHODIMP MessageSFVCB(UINT uMsg, WPARAM wParam, LPARAM lParam) override;

        DECLARE_NO_REGISTRY()
        DECLARE_NOT_AGGREGATABLE(CControlPanelFolderViewCB)
        BEGIN_COM_MAP(CControlPanelFolderViewCB)
        COM_INTERFACE_ENTRY_IID(IID_IShellFolderViewCB, IShellFolderViewCB)
        END_COM_MAP()
};

class CCPLItemMenu:
    public CComObjectRootEx<CComMultiThreadModelNoCS>,
    public IContextMenu2
{
private:
    PITEMID_CHILD *m_apidl;
    UINT m_cidl;

public:
    CCPLItemMenu();
    ~CCPLItemMenu();
    HRESULT WINAPI Initialize(UINT cidl, PCUITEMID_CHILD_ARRAY apidl);

    // IContextMenu
    STDMETHOD(QueryContextMenu)(HMENU hMenu, UINT indexMenu, UINT idCmdFirst, UINT idCmdLast, UINT uFlags) override;
    STDMETHOD(InvokeCommand)(LPCMINVOKECOMMANDINFO lpcmi) override;
    STDMETHOD(GetCommandString)(UINT_PTR idCommand, UINT uFlags, UINT *lpReserved, LPSTR lpszName, UINT uMaxNameLen) override;

    // IContextMenu2
    STDMETHOD(HandleMenuMsg)(UINT uMsg, WPARAM wParam, LPARAM lParam) override;

    BEGIN_COM_MAP(CCPLItemMenu)
    COM_INTERFACE_ENTRY_IID(IID_IContextMenu, IContextMenu)
    COM_INTERFACE_ENTRY_IID(IID_IContextMenu2, IContextMenu2)
    END_COM_MAP()
};

class COpenControlPanel :
    public CComCoClass<COpenControlPanel, &CLSID_OpenControlPanel>,
    public CComObjectRootEx<CComMultiThreadModelNoCS>,
    public IOpenControlPanel
{
    public:
        // IOpenControlPanel
        STDMETHOD(Open)(LPCWSTR pszName, LPCWSTR pszPage, IUnknown *punkSite) override;
        STDMETHOD(GetPath)(LPCWSTR pszName, LPWSTR pszPath, UINT cchPath) override;
        STDMETHOD(GetCurrentView)(CPVIEW *pView) override;

        static HRESULT WINAPI UpdateRegistry(BOOL bRegister) { return S_OK; } // CControlPanelFolder does it for us
        DECLARE_NOT_AGGREGATABLE(COpenControlPanel)

        DECLARE_PROTECT_FINAL_CONSTRUCT()

        BEGIN_COM_MAP(COpenControlPanel)
        COM_INTERFACE_ENTRY_IID(IID_IOpenControlPanel, IOpenControlPanel)
        END_COM_MAP()
};

#endif /* _SHFLDR_CPANEL_H_ */
