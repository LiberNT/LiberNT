/*
 * Shell Menu Site
 *
 * Copyright 2014 David Quintana
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
#pragma once

#include <shdeprecated.h>

class CEnumMergedFolder;

class CMergedFolder :
    public CComCoClass<CMergedFolder, &CLSID_MergedFolder>,
    public CComObjectRootEx<CComMultiThreadModelNoCS>,
    public IShellFolder2,
    public IPersistFolder2,
    public IAugmentedShellFolder,
    public IFolderType,
    public IObjectWithBackReferences,
    public IShellService,
    public IStorage
    //public IShellService,              // DEPRECATED IE4 interface: https://learn.microsoft.com/en-us/windows/win32/api/shdeprecated/nn-shdeprecated-ishellservice
    //public ITranslateShellChangeNotify,// -- undocumented
    //public IStorage,
    //public IPersistPropertyBag,
    //public IShellIconOverlay,          // -- undocumented
    //public ICompositeFolder,           // -- undocumented
    //public IItemNameLimits,            // https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nn-shobjidl_core-iitemnamelimits
{
private:
    CComPtr<IShellFolder> m_UserLocal;
    CComPtr<IShellFolder> m_AllUsers;
    CComPtr<CEnumMergedFolder> m_EnumSource;

    LPITEMIDLIST m_UserLocalPidl;
    LPITEMIDLIST m_AllUsersPidl;
    LPITEMIDLIST m_shellPidl;
    GUID m_UserLocalGuid;
    GUID m_AllUsersGuid;
    CComPtr<IUnknown> m_pOwner;

    HRESULT _GetNameSpaceStorage(ULONG uNameSpace, IStorage **ppstg);

public:
    CMergedFolder();
    virtual ~CMergedFolder();

    HRESULT _SetSources(IShellFolder* userLocal, IShellFolder* allUSers);

    DECLARE_REGISTRY_RESOURCEID(IDR_MERGEDFOLDER)
    DECLARE_NOT_AGGREGATABLE(CMergedFolder)
    DECLARE_PROTECT_FINAL_CONSTRUCT()

    BEGIN_COM_MAP(CMergedFolder)
        COM_INTERFACE_ENTRY2_IID(IID_IShellFolder, IShellFolder, IShellFolder2)
        COM_INTERFACE_ENTRY_IID(IID_IShellFolder2,   IShellFolder2)
        COM_INTERFACE_ENTRY_IID(IID_IPersist,        IPersist)
        COM_INTERFACE_ENTRY_IID(IID_IPersistFolder,  IPersistFolder)
        COM_INTERFACE_ENTRY_IID(IID_IPersistFolder2, IPersistFolder2)
        COM_INTERFACE_ENTRY_IID(IID_IAugmentedShellFolder, IAugmentedShellFolder)
        COM_INTERFACE_ENTRY_IID(IID_IFolderType, IFolderType)
        COM_INTERFACE_ENTRY_IID(IID_IObjectWithBackReferences, IObjectWithBackReferences)
        COM_INTERFACE_ENTRY_IID(IID_IShellService, IShellService)
        COM_INTERFACE_ENTRY_IID(IID_IStorage, IStorage)
    END_COM_MAP()

    // IShellFolder
    STDMETHOD(ParseDisplayName)(
        HWND hwndOwner,
        LPBC pbcReserved,
        LPOLESTR lpszDisplayName,
        ULONG *pchEaten,
        LPITEMIDLIST *ppidl,
        ULONG *pdwAttributes) override;

    STDMETHOD(EnumObjects)(
        HWND hwndOwner,
        SHCONTF grfFlags,
        IEnumIDList **ppenumIDList) override;

    STDMETHOD(BindToObject)(
        LPCITEMIDLIST pidl,
        LPBC pbcReserved,
        REFIID riid,
        void **ppvOut) override;

    STDMETHOD(BindToStorage)(
        LPCITEMIDLIST pidl,
        LPBC pbcReserved,
        REFIID riid,
        void **ppvObj) override;

    STDMETHOD(CompareIDs)(
        LPARAM lParam,
        LPCITEMIDLIST pidl1,
        LPCITEMIDLIST pidl2) override;

    STDMETHOD(CreateViewObject)(
        HWND hwndOwner,
        REFIID riid,
        void **ppvOut) override;

    STDMETHOD(GetAttributesOf)(
        UINT cidl,
        PCUITEMID_CHILD_ARRAY apidl,
        SFGAOF *rgfInOut) override;

    STDMETHOD(GetUIObjectOf)(
        HWND hwndOwner,
        UINT cidl,
        PCUITEMID_CHILD_ARRAY apidl,
        REFIID riid,
        UINT *prgfInOut,
        void **ppvOut) override;

    STDMETHOD(GetDisplayNameOf)(
        LPCITEMIDLIST pidl,
        SHGDNF uFlags,
        STRRET *lpName) override;

    STDMETHOD(SetNameOf)(
        HWND hwnd,
        LPCITEMIDLIST pidl,
        LPCOLESTR lpszName,
        SHGDNF uFlags,
        LPITEMIDLIST *ppidlOut) override;

    // IShellFolder2
    STDMETHOD(GetDefaultSearchGUID)(
        GUID *lpguid) override;

    STDMETHOD(EnumSearches)(
        IEnumExtraSearch **ppenum) override;

    STDMETHOD(GetDefaultColumn)(
        DWORD dwReserved,
        ULONG *pSort,
        ULONG *pDisplay) override;

    STDMETHOD(GetDefaultColumnState)(
        UINT iColumn,
        SHCOLSTATEF *pcsFlags) override;

    STDMETHOD(GetDetailsEx)(
        LPCITEMIDLIST pidl,
        const SHCOLUMNID *pscid,
        VARIANT *pv) override;

    STDMETHOD(GetDetailsOf)(
        LPCITEMIDLIST pidl,
        UINT iColumn,
        SHELLDETAILS *psd) override;

    STDMETHOD(MapColumnToSCID)(
        UINT iColumn,
        SHCOLUMNID *pscid) override;

    // IPersist
    STDMETHOD(GetClassID)(CLSID *lpClassId) override;

    // IPersistFolder
    STDMETHOD(Initialize)(PCIDLIST_ABSOLUTE pidl) override;

    // IPersistFolder2
    STDMETHOD(GetCurFolder)(PIDLIST_ABSOLUTE * pidl) override;

    // IAugmentedShellFolder
    STDMETHOD(AddNameSpace)(const GUID *pguidObject, IShellFolder *psf, PCIDLIST_ABSOLUTE pidl, ULONG dwFlags, UINT uGroupId) override;
    STDMETHOD(GetNameSpaceID)(PCUITEMID_CHILD pidl, GUID *pguidOut) override;
    STDMETHOD(QueryNameSpace)(ULONG uNameSpace, GUID *pguidOut, IShellFolder **ppsf) override;
    STDMETHOD(EnumNameSpace)(ULONG uNameSpace, ULONG *pdwID) override;
    STDMETHOD(UnWrapIDList)(PCUITEMID_CHILD pidl, LONG cPidls, IShellFolder **ppsf, PIDLIST_ABSOLUTE *ppidlFolder, PIDLIST_ABSOLUTE *ppidlItem, LONG *pcPidls) override;
    STDMETHOD(QueryNameSpace2)(ULONG uNameSpace, QUERYNAMESPACEINFO *pqnsi) override;
    STDMETHOD(GetNameSpaceGroupID)(PCUITEMID_CHILD pidl, UINT *puGroupId) override;
    STDMETHOD(GetFlagsForChild)(PCUITEMID_CHILD pidl, REFGUID guidNameSpace, DWORD *pdwFlags) override;

    // IFolderType
    STDMETHOD(GetFolderType)(FOLDERTYPEID *pftid) override;

    // IObjectWithBackReferences
    STDMETHOD(RemoveBackReferences)() override;

    // IShellService
    STDMETHOD(SetOwner)(IUnknown *punkOwner) override;

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
};
