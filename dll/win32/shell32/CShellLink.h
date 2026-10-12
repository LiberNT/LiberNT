/*
 *
 *      Copyright 1997  Marcus Meissner
 *      Copyright 1998  Juergen Schmied
 *      Copyright 2005  Mike McCormack
 *      Copyright 2009  Andrew Hill
 *      Copyright 2017  Hermes Belusca-Maito
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
 *
 */

#ifndef _SHELLLINK_H_
#define _SHELLLINK_H_

class CShellLink :
    public CComCoClass<CShellLink, &CLSID_ShellLink>,
    public CComObjectRootEx<CComMultiThreadModelNoCS>,
    public IShellLinkA,
    public IShellLinkW,
    public IPersistStream,
    public IPersistFile,
    public IShellExtInit,
    public IContextMenu3,
    public IDropTarget,
    public IQueryInfo,
    public IShellLinkDataList,
    public IExtractIconA,
    public IExtractIconW,
    public IExtractImage2,
    public IPersistPropertyBag,
    public IServiceProvider,
    public IObjectWithSite,
    public ICustomizeInfoTip,
    public IMarshal,
    public IPropertyStore,
    public ISLTracker
{
public:
    /* Link file formats */

    #pragma pack(push, 1)
    struct volume_info
    {
        DWORD type;
        DWORD serial;
        WCHAR label[12];  /* assume 8.3 */
    };
    #pragma pack(pop)

    enum IDCMD
    {
        IDCMD_OPEN = 0,
        IDCMD_OPENFILELOCATION,
        IDCMD_RUNAS
    };

private:
    /* Cached link header */
    SHELL_LINK_HEADER m_Header;

    /* Cached data set according to m_Header.dwFlags (SHELL_LINK_DATA_FLAGS) */

    LPITEMIDLIST  m_pPidl;

    /* Link tracker information */
    LPWSTR        m_sPath;
    volume_info   volume;

    LPWSTR        m_sDescription;
    LPWSTR        m_sPathRel;
    LPWSTR        m_sWorkDir;
    LPWSTR        m_sArgs;
    LPWSTR        m_sIcoPath;
    BOOL          m_bRunAs;
    BOOL          m_bDirty;
    LPDBLIST      m_pDBList; /* Optional data block list (in the extra data section) */

    /* Pointers to strings inside Logo3/Darwin info blocks, cached for debug info purposes only */
    LPWSTR sProduct;
    LPWSTR sComponent;

    LPWSTR        m_sLinkPath;

    CComPtr<IUnknown>    m_site;
    CComPtr<IDropTarget> m_DropTarget;
    CComPtr<IExtractImage> m_pExtractImage;
    CComHeapPtr<WCHAR>   m_pszInfoTipPrefix;
    CComPtr<IPropertyStore> m_pPropertyStore;
    CComHeapPtr<BYTE>    m_pHiddenStorages;
    ULONG                m_cbHiddenStorages;

    VOID Reset();

    HRESULT GetAdvertiseInfo(LPWSTR *str, DWORD dwSig);
    HRESULT SetAdvertiseInfo(LPCWSTR str);
    HRESULT WriteAdvertiseInfo(LPCWSTR string, DWORD dwSig);
    HRESULT SetTargetFromPIDLOrPath(LPCITEMIDLIST pidl, LPCWSTR pszFile);

    HRESULT EnsurePropertyStore();
    HRESULT DoOpen(LPCMINVOKECOMMANDINFO lpici, PCWSTR pszVerb = NULL);
    HRESULT DoOpenFileLocation();

public:
    CShellLink();
    ~CShellLink();

    // IPersistFile
    STDMETHOD(GetClassID)(CLSID *pclsid) override;
    STDMETHOD(IsDirty)() override;
    STDMETHOD(Load)(LPCOLESTR pszFileName, DWORD dwMode) override;
    STDMETHOD(Save)(LPCOLESTR pszFileName, BOOL fRemember) override;
    STDMETHOD(SaveCompleted)(LPCOLESTR pszFileName) override;
    STDMETHOD(GetCurFile)(LPOLESTR *ppszFileName) override;

    // IPersistStream
    // STDMETHOD(GetClassID)(CLSID *pclsid) override;
    // STDMETHOD(IsDirty)() override;
    STDMETHOD(Load)(IStream *stm) override;
    STDMETHOD(Save)(IStream *stm, BOOL fClearDirty) override;
    STDMETHOD(GetSizeMax)(ULARGE_INTEGER *pcbSize) override;

    // IShellLinkA
    STDMETHOD(GetPath)(LPSTR pszFile, INT cchMaxPath, WIN32_FIND_DATAA *pfd, DWORD fFlags) override;
    STDMETHOD(GetIDList)(PIDLIST_ABSOLUTE *ppidl) override;
    STDMETHOD(SetIDList)(PCIDLIST_ABSOLUTE pidl) override;
    STDMETHOD(GetDescription)(LPSTR pszName, INT cchMaxName) override;
    STDMETHOD(SetDescription)(LPCSTR pszName) override;
    STDMETHOD(GetWorkingDirectory)(LPSTR pszDir, INT cchMaxPath) override;
    STDMETHOD(SetWorkingDirectory)(LPCSTR pszDir) override;
    STDMETHOD(GetArguments)(LPSTR pszArgs, INT cchMaxPath) override;
    STDMETHOD(SetArguments)(LPCSTR pszArgs) override;
    STDMETHOD(GetHotkey)(WORD *pwHotkey) override;
    STDMETHOD(SetHotkey)(WORD wHotkey) override;
    STDMETHOD(GetShowCmd)(INT *piShowCmd) override;
    STDMETHOD(SetShowCmd)(INT iShowCmd) override;
    STDMETHOD(GetIconLocation)(LPSTR pszIconPath, INT cchIconPath, INT *piIcon) override;
    STDMETHOD(SetIconLocation)(LPCSTR pszIconPath, INT iIcon) override;
    STDMETHOD(SetRelativePath)(LPCSTR pszPathRel, DWORD dwReserved) override;
    STDMETHOD(Resolve)(HWND hwnd, DWORD fFlags) override;
    STDMETHOD(SetPath)(LPCSTR pszFile) override;

    // IShellLinkW
    STDMETHOD(GetPath)(LPWSTR pszFile, INT cchMaxPath, WIN32_FIND_DATAW *pfd, DWORD fFlags) override;
    // STDMETHOD(GetIDList)(PIDLIST_ABSOLUTE *ppidl) override;
    // STDMETHOD(SetIDList)(PCIDLIST_ABSOLUTE pidl) override;
    STDMETHOD(GetDescription)(LPWSTR pszName, INT cchMaxName) override;
    STDMETHOD(SetDescription)(LPCWSTR pszName) override;
    STDMETHOD(GetWorkingDirectory)(LPWSTR pszDir, INT cchMaxPath) override;
    STDMETHOD(SetWorkingDirectory)(LPCWSTR pszDir) override;
    STDMETHOD(GetArguments)(LPWSTR pszArgs, INT cchMaxPath) override;
    STDMETHOD(SetArguments)(LPCWSTR pszArgs) override;
    // STDMETHOD(GetHotkey)(WORD *pwHotkey) override;
    // STDMETHOD(SetHotkey)(WORD wHotkey) override;
    // STDMETHOD(GetShowCmd)(INT *piShowCmd) override;
    // STDMETHOD(SetShowCmd)(INT iShowCmd) override;
    STDMETHOD(GetIconLocation)(LPWSTR pszIconPath, INT cchIconPath, INT *piIcon) override;
    STDMETHOD(SetIconLocation)(LPCWSTR pszIconPath, INT iIcon) override;
    STDMETHOD(SetRelativePath)(LPCWSTR pszPathRel, DWORD dwReserved) override;
    // STDMETHOD(Resolve)(HWND hwnd, DWORD fFlags) override;
    STDMETHOD(SetPath)(LPCWSTR pszFile) override;

    // IShellLinkDataList
    STDMETHOD(AddDataBlock)(void *pDataBlock) override;
    STDMETHOD(CopyDataBlock)(DWORD dwSig, void **ppDataBlock) override;
    STDMETHOD(RemoveDataBlock)(DWORD dwSig) override;
    STDMETHOD(GetFlags)(DWORD *pdwFlags) override;
    STDMETHOD(SetFlags)(DWORD dwFlags) override;

    // IExtractIconA
    STDMETHOD(Extract)(PCSTR pszFile, UINT nIconIndex, HICON *phiconLarge, HICON *phiconSmall, UINT nIconSize) override;
    STDMETHOD(GetIconLocation)(UINT uFlags, PSTR pszIconFile, UINT cchMax, int *piIndex, UINT *pwFlags) override;

    // IExtractIconW
    STDMETHOD(Extract)(PCWSTR pszFile, UINT nIconIndex, HICON *phiconLarge, HICON *phiconSmall, UINT nIconSize) override;
    STDMETHOD(GetIconLocation)(UINT uFlags, PWSTR pszIconFile, UINT cchMax, int *piIndex, UINT *pwFlags) override;

    // IShellExtInit
    STDMETHOD(Initialize)(PCIDLIST_ABSOLUTE pidlFolder, IDataObject *pdtobj, HKEY hkeyProgID) override;

    // IContextMenu
    STDMETHOD(QueryContextMenu)(HMENU hmenu, UINT indexMenu, UINT idCmdFirst, UINT idCmdLast, UINT uFlags) override;
    STDMETHOD(InvokeCommand)(LPCMINVOKECOMMANDINFO lpici) override;
    STDMETHOD(GetCommandString)(UINT_PTR idCmd, UINT uType, UINT *pwReserved, LPSTR pszName, UINT cchMax) override;
    STDMETHOD(HandleMenuMsg)(UINT uMsg, WPARAM wParam, LPARAM lParam) override;
    STDMETHOD(HandleMenuMsg2)(UINT uMsg, WPARAM wParam, LPARAM lParam, LRESULT *plResult) override;

    STDMETHOD(GetInfoTip)(DWORD dwFlags, WCHAR **ppwszTip) override;
    STDMETHOD(GetInfoFlags)(DWORD *pdwFlags) override;

    STDMETHOD(SetPrefixText)(LPCWSTR pszPrefix) override;
    STDMETHOD(SetExtraProperties)(const PROPERTYKEY *pkey, UINT cProps) override;

    STDMETHOD(GetLocation)(LPWSTR pszPathBuffer, DWORD cch, DWORD *pdwPriority, const SIZE *prgSize, DWORD dwRecClrDepth, DWORD *pdwFlags) override;
    STDMETHOD(Extract)(HBITMAP *phBmpThumbnail) override;
    STDMETHOD(GetDateStamp)(FILETIME *pDateStamp) override;

    STDMETHOD(InitNew)() override;
    STDMETHOD(Load)(IPropertyBag *pPropBag, IErrorLog *pErrorLog) override;
    STDMETHOD(Save)(IPropertyBag *pPropBag, BOOL fClearDirty, BOOL fSaveAllProperties) override;

    STDMETHOD(QueryService)(REFGUID guidService, REFIID riid, void **ppvObject) override;

    STDMETHOD(GetUnmarshalClass)(REFIID riid, void *pv, DWORD dwDestContext, void *pvDestContext, DWORD mshlflags, CLSID *pCid) override;
    STDMETHOD(GetMarshalSizeMax)(REFIID riid, void *pv, DWORD dwDestContext, void *pvDestContext, DWORD mshlflags, DWORD *pSize) override;
    STDMETHOD(MarshalInterface)(IStream *pStm, REFIID riid, void *pv, DWORD dwDestContext, void *pvDestContext, DWORD mshlflags) override;
    STDMETHOD(UnmarshalInterface)(IStream *pStm, REFIID riid, void **ppv) override;
    STDMETHOD(ReleaseMarshalData)(IStream *pStm) override;
    STDMETHOD(DisconnectObject)(DWORD dwReserved) override;

    STDMETHOD(GetCount)(DWORD *cProps) override;
    STDMETHOD(GetAt)(DWORD iProp, PROPERTYKEY *pkey) override;
    STDMETHOD(GetValue)(REFPROPERTYKEY key, PROPVARIANT *pv) override;
    STDMETHOD(SetValue)(REFPROPERTYKEY key, REFPROPVARIANT propvar) override;
    STDMETHOD(Commit)() override;

    STDMETHOD(Resolve)(HWND hwnd, ULONG fFlags, ULONG fRestrictions) override;
    STDMETHOD(GetIDs)(DOMAIN_RELATIVE_OBJECTID *pdroidCurrent, DOMAIN_RELATIVE_OBJECTID *pdroidBirth, MACHINE_ID *pmcid) override;
    STDMETHOD(CancelSearch)() override;

    // IObjectWithSite
    STDMETHOD(SetSite)(IUnknown *punk) override;
    STDMETHOD(GetSite)(REFIID iid, void **ppvSite) override;

    // IDropTarget
    STDMETHOD(DragEnter)(IDataObject *pDataObject, DWORD dwKeyState, POINTL pt, DWORD *pdwEffect) override;
    STDMETHOD(DragOver)(DWORD dwKeyState, POINTL pt, DWORD *pdwEffect) override;
    STDMETHOD(DragLeave)() override;
    STDMETHOD(Drop)(IDataObject *pDataObject, DWORD dwKeyState, POINTL pt, DWORD *pdwEffect) override;

DECLARE_REGISTRY_RESOURCEID(IDR_SHELLLINK)
DECLARE_NOT_AGGREGATABLE(CShellLink)

DECLARE_PROTECT_FINAL_CONSTRUCT()

BEGIN_COM_MAP(CShellLink)
    COM_INTERFACE_ENTRY_IID(IID_IShellLinkA, IShellLinkA)
    COM_INTERFACE_ENTRY_IID(IID_IShellLinkW, IShellLinkW)
    COM_INTERFACE_ENTRY2_IID(IID_IPersist, IPersist, IPersistFile)
    COM_INTERFACE_ENTRY_IID(IID_IPersistFile, IPersistFile)
    COM_INTERFACE_ENTRY_IID(IID_IPersistStream, IPersistStream)
    COM_INTERFACE_ENTRY_IID(IID_IShellExtInit, IShellExtInit)
    COM_INTERFACE_ENTRY_IID(IID_IContextMenu3, IContextMenu3)
    COM_INTERFACE_ENTRY_IID(IID_IContextMenu2, IContextMenu2)
    COM_INTERFACE_ENTRY_IID(IID_IContextMenu, IContextMenu)
    COM_INTERFACE_ENTRY_IID(IID_IDropTarget, IDropTarget)
    COM_INTERFACE_ENTRY_IID(IID_IQueryInfo, IQueryInfo)
    COM_INTERFACE_ENTRY_IID(IID_IShellLinkDataList, IShellLinkDataList)
    COM_INTERFACE_ENTRY_IID(IID_IExtractIconA, IExtractIconA)
    COM_INTERFACE_ENTRY_IID(IID_IExtractIconW, IExtractIconW)
    COM_INTERFACE_ENTRY_IID(IID_IExtractImage2, IExtractImage2)
    COM_INTERFACE_ENTRY_IID(IID_IExtractImage, IExtractImage)
    COM_INTERFACE_ENTRY_IID(IID_IPersistPropertyBag, IPersistPropertyBag)
    COM_INTERFACE_ENTRY_IID(IID_IServiceProvider, IServiceProvider)
    COM_INTERFACE_ENTRY_IID(IID_IObjectWithSite, IObjectWithSite)
    COM_INTERFACE_ENTRY_IID(IID_ICustomizeInfoTip, ICustomizeInfoTip)
    COM_INTERFACE_ENTRY_IID(IID_IMarshal, IMarshal)
    COM_INTERFACE_ENTRY_IID(IID_IPropertyStore, IPropertyStore)
    COM_INTERFACE_ENTRY_IID(IID_ISLTracker, ISLTracker)
END_COM_MAP()
};

#endif /* _SHELLLINK_H_ */
