/*
 *    Virtual Workplace folder
 *
 *    Copyright 1997                Marcus Meissner
 *    Copyright 1998, 1999, 2002    Juergen Schmied
 *    Copyright 2009                Andrew Hill
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

#ifndef _CDRIVESFOLDER_H_
#define _CDRIVESFOLDER_H_

class CDrivesFolder :
    public CComCoClass<CDrivesFolder, &CLSID_MyComputer>,
    public CComObjectRootEx<CComMultiThreadModelNoCS>,
    public IShellFolder2,
    public IPersistFolder2,
    public IContextMenuCB,
    public IFolderFilter,
    public IPersistPropertyBag,
    public IItemNameLimits,
    public IShellIcon,
    public IShellIconOverlay,
    public IStorage,
    public IObjectWithSite,
    public IObjectWithBackReferences,
    public IBackReferencedObject,
    public IDelegateHostItemContainer
{
    private:
        /* both paths are parsible from the desktop */
        LPITEMIDLIST pidlRoot;    /* absolute pidl */
        CComPtr<IShellFolder2> m_regFolder;
        INT8 m_DriveDisplayMode;
        CComPtr<IUnknown> m_site;
        LONG m_cBackReferences;
        CComHeapPtr<WCHAR> m_pszRemoteComputer;
        BOOL m_bHideNonRemovableDrives;
        BOOL m_bForceAllDrivesRemovable;

    public:
        CDrivesFolder();
        ~CDrivesFolder();
        HRESULT WINAPI FinalConstruct();

        static HRESULT SetDriveLabel(HWND hwndOwner, PCWSTR DrivePath, PCWSTR Label);
        BOOL ShouldEnumDrive(PCWSTR DrivePath);

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

        // IContextMenuCB
        STDMETHOD(CallBack)(IShellFolder *psf, HWND hwndOwner, IDataObject *pdtobj, UINT uMsg, WPARAM wParam, LPARAM lParam) override;

        HRESULT WINAPI MessageSFVCB(UINT uMsg, WPARAM wParam, LPARAM lParam);

        // IFolderFilter
        STDMETHOD(ShouldShow)(IShellFolder *psf, PCIDLIST_ABSOLUTE pidlFolder, PCUITEMID_CHILD pidlItem) override;
        STDMETHOD(GetEnumFlags)(IShellFolder *psf, PCIDLIST_ABSOLUTE pidlFolder, HWND *phwnd, DWORD *pgrfFlags) override;

        STDMETHOD(InitNew)() override;
        STDMETHOD(Load)(IPropertyBag *pPropBag, IErrorLog *pErrorLog) override;
        STDMETHOD(Save)(IPropertyBag *pPropBag, BOOL fClearDirty, BOOL fSaveAllProperties) override;

        STDMETHOD(GetValidCharacters)(LPWSTR *ppwszValidChars, LPWSTR *ppwszInvalidChars) override;
        STDMETHOD(GetMaxLength)(LPCWSTR pszName, int *piMaxNameLen) override;

        STDMETHOD(GetIconOf)(PCUITEMID_CHILD pidl, UINT flags, int *pIconIndex) override;

        STDMETHOD(GetOverlayIndex)(PCUITEMID_CHILD pidl, int *pIndex) override;
        STDMETHOD(GetOverlayIconIndex)(PCUITEMID_CHILD pidl, int *pIconIndex) override;

        STDMETHOD(CreateStream)(LPCOLESTR pwcsName, DWORD grfMode, DWORD reserved1, DWORD reserved2, IStream **ppstm) override;
        STDMETHOD(OpenStream)(LPCOLESTR pwcsName, void *reserved1, DWORD grfMode, DWORD reserved2, IStream **ppstm) override;
        STDMETHOD(CreateStorage)(LPCOLESTR pwcsName, DWORD grfMode, DWORD dwStgFmt, DWORD reserved2, IStorage **ppstg) override;
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

        STDMETHOD(SetSite)(IUnknown *pUnkSite) override;
        STDMETHOD(GetSite)(REFIID riid, void **ppvSite) override;

        STDMETHOD(RemoveBackReferences)() override;

        STDMETHOD(AddRefBackReference)() override;
        STDMETHOD(ReleaseBackReference)() override;

        STDMETHOD(SetRemoteComputer)(LPCWSTR pszComputer) override;

        DECLARE_REGISTRY_RESOURCEID(IDR_MYCOMPUTER)
        DECLARE_CENTRAL_INSTANCE_NOT_AGGREGATABLE(CDrivesFolder)

        DECLARE_PROTECT_FINAL_CONSTRUCT()

        BEGIN_COM_MAP(CDrivesFolder)
        COM_INTERFACE_ENTRY_IID(IID_IShellFolder2, IShellFolder2)
        COM_INTERFACE_ENTRY_IID(IID_IShellFolder, IShellFolder)
        COM_INTERFACE_ENTRY_IID(IID_IPersistFolder, IPersistFolder)
        COM_INTERFACE_ENTRY_IID(IID_IPersistFolder2, IPersistFolder2)
        COM_INTERFACE_ENTRY2_IID(IID_IPersist, IPersist, IPersistFolder2)
        COM_INTERFACE_ENTRY_IID(IID_IContextMenuCB, IContextMenuCB)
        COM_INTERFACE_ENTRY_IID(IID_IFolderFilter, IFolderFilter)
        COM_INTERFACE_ENTRY_IID(IID_IPersistPropertyBag, IPersistPropertyBag)
        COM_INTERFACE_ENTRY_IID(IID_IItemNameLimits, IItemNameLimits)
        COM_INTERFACE_ENTRY_IID(IID_IShellIcon, IShellIcon)
        COM_INTERFACE_ENTRY_IID(IID_IShellIconOverlay, IShellIconOverlay)
        COM_INTERFACE_ENTRY_IID(IID_IStorage, IStorage)
        COM_INTERFACE_ENTRY_IID(IID_IObjectWithSite, IObjectWithSite)
        COM_INTERFACE_ENTRY_IID(IID_IObjectWithBackReferences, IObjectWithBackReferences)
        COM_INTERFACE_ENTRY_IID(IID_IBackReferencedObject, IBackReferencedObject)
        COM_INTERFACE_ENTRY_IID(IID_IDelegateHostItemContainer, IDelegateHostItemContainer)
        END_COM_MAP()
};

class CDrivesFolderViewCB :
    public CComObjectRootEx<CComMultiThreadModelNoCS>,
    public IShellFolderViewCB,
    public IFolderFilter
{
        CComPtr<IShellFolder2> m_pFolder;
        CDrivesFolder *m_pDrivesFolder;

    public:
        CDrivesFolderViewCB() : m_pDrivesFolder(NULL) {}
        void Initialize(CDrivesFolder *pFolder)
        {
            m_pDrivesFolder = pFolder;
            m_pFolder = static_cast<IShellFolder2 *>(pFolder);
        }

        STDMETHOD(MessageSFVCB)(UINT uMsg, WPARAM wParam, LPARAM lParam) override
        {
            return m_pDrivesFolder ? m_pDrivesFolder->MessageSFVCB(uMsg, wParam, lParam) : E_NOTIMPL;
        }

        STDMETHOD(ShouldShow)(IShellFolder *psf, PCIDLIST_ABSOLUTE pidlFolder, PCUITEMID_CHILD pidlItem) override
        {
            return m_pDrivesFolder ? m_pDrivesFolder->ShouldShow(psf, pidlFolder, pidlItem) : S_OK;
        }
        STDMETHODIMP GetEnumFlags(IShellFolder*, PCIDLIST_ABSOLUTE, HWND*, DWORD*) override { return E_NOTIMPL; }

        DECLARE_NO_REGISTRY()
        DECLARE_NOT_AGGREGATABLE(CDrivesFolderViewCB)
        BEGIN_COM_MAP(CDrivesFolderViewCB)
        COM_INTERFACE_ENTRY_IID(IID_IShellFolderViewCB, IShellFolderViewCB)
        COM_INTERFACE_ENTRY_IID(IID_IFolderFilter, IFolderFilter)
        END_COM_MAP()
};

#endif /* _CDRIVESFOLDER_H_ */
