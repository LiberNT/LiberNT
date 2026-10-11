/*
 * PROJECT:     LiberNT ieframe
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Shell Name Space ActiveX control
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "objects.h"
#include <commctrl.h>
#include <shldisp.h>
#include <atlsimpcoll.h>

#include <wine/debug.h>
WINE_DEFAULT_DEBUG_CHANNEL(ieframe);

#define SNS_DISPID_SELECTIONCHANGE   2
#define SNS_DISPID_DOUBLECLICK       3
#define SNS_DISPID_INITIALIZED       4

#define SNS_DEFAULT_EXTENT           5080
#define SNS_STREAM_VERSION           0x0301
#define SNS_DEFAULT_NSCMODE          5
#define SNS_DEFAULT_FLAGS            3

class CShellNameSpace;

static HRESULT SNS_CreateFolderItem(PCIDLIST_ABSOLUTE pidlItem, FolderItem **ppItem)
{
    CComPtr<IShellDispatch> pShell;
    CComPtr<Folder> pFolder;
    CComHeapPtr<ITEMIDLIST_ABSOLUTE> pidlParent;
    CComPtr<IShellFolder> psfParent;
    PCUITEMID_CHILD pidlLast;
    WCHAR szParent[MAX_PATH], szName[MAX_PATH];
    STRRET strret;
    HRESULT hr;

    *ppItem = NULL;
    hr = SHBindToParent(pidlItem, IID_PPV_ARG(IShellFolder, &psfParent), &pidlLast);
    if (FAILED(hr))
        return hr;
    hr = psfParent->GetDisplayNameOf(pidlLast, SHGDN_INFOLDER | SHGDN_FORPARSING, &strret);
    if (FAILED(hr))
        return hr;
    hr = StrRetToBufW(&strret, pidlLast, szName, _countof(szName));
    if (FAILED(hr))
        return hr;

    pidlParent.Attach(ILClone(pidlItem));
    if (!pidlParent)
        return E_OUTOFMEMORY;
    ILRemoveLastID(pidlParent);
    if (!SHGetPathFromIDListW(pidlParent, szParent))
        return S_FALSE;

    hr = CoCreateInstance(CLSID_Shell, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARG(IShellDispatch, &pShell));
    if (FAILED(hr))
        return hr;
    CComVariant varDir(szParent);
    hr = pShell->NameSpace(varDir, &pFolder);
    if (hr != S_OK || !pFolder)
        return S_FALSE;
    hr = pFolder->ParseName(CComBSTR(szName), ppItem);
    return (hr == S_OK && *ppItem) ? S_OK : S_FALSE;
}

class CSelectedFolderItemsEnum :
    public CComObjectRootEx<CComMultiThreadModelNoCS>,
    public IEnumVARIANT
{
public:
    CSimpleArray<CComPtr<IDispatch> > m_Items;
    int m_iPos;

    CSelectedFolderItemsEnum() : m_iPos(0) {}

    STDMETHOD(Next)(ULONG celt, VARIANT *rgVar, ULONG *pCeltFetched) override
    {
        ULONG cFetched = 0;

        if (!rgVar)
            return E_POINTER;
        while (cFetched < celt && m_iPos < m_Items.GetSize())
        {
            VariantInit(&rgVar[cFetched]);
            V_VT(&rgVar[cFetched]) = VT_DISPATCH;
            V_DISPATCH(&rgVar[cFetched]) = m_Items[m_iPos];
            V_DISPATCH(&rgVar[cFetched])->AddRef();
            cFetched++;
            m_iPos++;
        }
        if (pCeltFetched)
            *pCeltFetched = cFetched;
        return cFetched == celt ? S_OK : S_FALSE;
    }

    STDMETHOD(Skip)(ULONG celt) override
    {
        m_iPos += celt;
        if (m_iPos > m_Items.GetSize())
        {
            m_iPos = m_Items.GetSize();
            return S_FALSE;
        }
        return S_OK;
    }

    STDMETHOD(Reset)() override
    {
        m_iPos = 0;
        return S_OK;
    }

    STDMETHOD(Clone)(IEnumVARIANT **ppEnum) override
    {
        CComObject<CSelectedFolderItemsEnum> *pEnum;
        HRESULT hr;

        if (!ppEnum)
            return E_POINTER;
        *ppEnum = NULL;
        hr = CComObject<CSelectedFolderItemsEnum>::CreateInstance(&pEnum);
        if (FAILED(hr))
            return hr;
        for (int i = 0; i < m_Items.GetSize(); i++)
            pEnum->m_Items.Add(m_Items[i]);
        pEnum->m_iPos = m_iPos;
        return pEnum->QueryInterface(IID_PPV_ARG(IEnumVARIANT, ppEnum));
    }

    BEGIN_COM_MAP(CSelectedFolderItemsEnum)
        COM_INTERFACE_ENTRY_IID(IID_IEnumVARIANT, IEnumVARIANT)
    END_COM_MAP()
};

class CSelectedFolderItems :
    public CComObjectRootEx<CComMultiThreadModelNoCS>,
    public IDispatchImpl<FolderItems, &IID_FolderItems, &LIBID_Shell32, 1, 0>
{
public:
    CSimpleArray<CComPtr<IDispatch> > m_Items;

    HRESULT AddItem(PCIDLIST_ABSOLUTE pidl)
    {
        CComPtr<FolderItem> pItem;
        CComPtr<IDispatch> pDispatch;
        HRESULT hr = SNS_CreateFolderItem(pidl, &pItem);

        if (hr != S_OK)
            return hr;
        hr = pItem->QueryInterface(IID_PPV_ARG(IDispatch, &pDispatch));
        if (FAILED(hr))
            return hr;
        return m_Items.Add(pDispatch) ? S_OK : E_OUTOFMEMORY;
    }

    STDMETHOD(get_Count)(long *plCount) override
    {
        if (!plCount)
            return E_POINTER;
        *plCount = m_Items.GetSize();
        return S_OK;
    }

    STDMETHOD(get_Application)(IDispatch **ppid) override
    {
        if (!ppid)
            return E_POINTER;
        return CoCreateInstance(CLSID_Shell, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARG(IDispatch, ppid));
    }

    STDMETHOD(get_Parent)(IDispatch **ppid) override
    {
        if (ppid)
            *ppid = NULL;
        return E_NOTIMPL;
    }

    STDMETHOD(Item)(VARIANT index, FolderItem **ppid) override
    {
        CComVariant varIndex;

        if (!ppid)
            return E_POINTER;
        *ppid = NULL;
        if (FAILED(varIndex.ChangeType(VT_I4, &index)) || V_I4(&varIndex) < 0 ||
            V_I4(&varIndex) >= m_Items.GetSize())
        {
            return S_FALSE;
        }
        return m_Items[V_I4(&varIndex)]->QueryInterface(IID_PPV_ARG(FolderItem, ppid));
    }

    STDMETHOD(_NewEnum)(IUnknown **ppunk) override
    {
        CComObject<CSelectedFolderItemsEnum> *pEnum;
        HRESULT hr;

        if (!ppunk)
            return E_POINTER;
        *ppunk = NULL;
        hr = CComObject<CSelectedFolderItemsEnum>::CreateInstance(&pEnum);
        if (FAILED(hr))
            return hr;
        for (int i = 0; i < m_Items.GetSize(); i++)
            pEnum->m_Items.Add(m_Items[i]);
        return pEnum->QueryInterface(IID_PPV_ARG(IUnknown, ppunk));
    }

    BEGIN_COM_MAP(CSelectedFolderItems)
        COM_INTERFACE_ENTRY_IID(IID_FolderItems, FolderItems)
        COM_INTERFACE_ENTRY_IID(IID_IDispatch, FolderItems)
    END_COM_MAP()
};

class CShellNameSpaceWnd :
    public CWindowImpl<CShellNameSpaceWnd>
{
public:
    CShellNameSpace *m_pOwner;

    CShellNameSpaceWnd() : m_pOwner(NULL) {}

    DECLARE_WND_CLASS_EX(L"LiberNT ShellNameSpace", CS_DBLCLKS, COLOR_WINDOW)

    LRESULT OnSize(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled);
    LRESULT OnSetFocus(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled);

    BEGIN_MSG_MAP(CShellNameSpaceWnd)
        MESSAGE_HANDLER(WM_SIZE, OnSize)
        MESSAGE_HANDLER(WM_SETFOCUS, OnSetFocus)
    END_MSG_MAP()
};

class CShellNameSpaceEvents :
    public CComObjectRootEx<CComMultiThreadModelNoCS>,
    public INameSpaceTreeControlEvents
{
public:
    CShellNameSpace *m_pOwner;

    CShellNameSpaceEvents() : m_pOwner(NULL) {}

    STDMETHOD(OnItemClick)(IShellItem *psi, NSTCEHITTEST nstceHitTest, NSTCECLICKTYPE nstceClickType) override;
    STDMETHOD(OnPropertyItemCommit)(IShellItem *psi) override { return E_NOTIMPL; }
    STDMETHOD(OnItemStateChanging)(IShellItem *psi, NSTCITEMSTATE nstcisMask, NSTCITEMSTATE nstcisState) override { return E_NOTIMPL; }
    STDMETHOD(OnItemStateChanged)(IShellItem *psi, NSTCITEMSTATE nstcisMask, NSTCITEMSTATE nstcisState) override { return E_NOTIMPL; }
    STDMETHOD(OnSelectionChanged)(IShellItemArray *psiaSelection) override;
    STDMETHOD(OnKeyboardInput)(UINT uMsg, WPARAM wParam, LPARAM lParam) override { return E_NOTIMPL; }
    STDMETHOD(OnBeforeExpand)(IShellItem *psi) override { return E_NOTIMPL; }
    STDMETHOD(OnAfterExpand)(IShellItem *psi) override { return E_NOTIMPL; }
    STDMETHOD(OnBeginLabelEdit)(IShellItem *psi) override { return E_NOTIMPL; }
    STDMETHOD(OnEndLabelEdit)(IShellItem *psi) override { return E_NOTIMPL; }
    STDMETHOD(OnGetToolTip)(IShellItem *psi, LPWSTR pszTip, int cchTip) override { return E_NOTIMPL; }
    STDMETHOD(OnBeforeItemDelete)(IShellItem *psi) override { return E_NOTIMPL; }
    STDMETHOD(OnItemAdded)(IShellItem *psi, BOOL fIsRoot) override { return E_NOTIMPL; }
    STDMETHOD(OnItemDeleted)(IShellItem *psi, BOOL fIsRoot) override { return E_NOTIMPL; }
    STDMETHOD(OnBeforeContextMenu)(IShellItem *psi, REFIID riid, void **ppv) override
    {
        *ppv = NULL;
        return E_NOTIMPL;
    }
    STDMETHOD(OnAfterContextMenu)(IShellItem *psi, IContextMenu *pcmIn, REFIID riid, void **ppv) override
    {
        *ppv = NULL;
        return E_NOTIMPL;
    }
    STDMETHOD(OnBeforeStateImageChange)(IShellItem *psi) override { return E_NOTIMPL; }
    STDMETHOD(OnGetDefaultIconIndex)(IShellItem *psi, int *piDefaultIcon, int *piOpenIcon) override { return E_NOTIMPL; }

    BEGIN_COM_MAP(CShellNameSpaceEvents)
        COM_INTERFACE_ENTRY_IID(IID_INameSpaceTreeControlEvents, INameSpaceTreeControlEvents)
    END_COM_MAP()
};

class CShellNameSpace :
    public CComCoClass<CShellNameSpace, &CLSID_ShellNameSpace>,
    public CComObjectRootEx<CComMultiThreadModelNoCS>,
    public IDispatchImpl<IShellNameSpace, &IID_IShellNameSpace, &LIBID_SHDocVw, 1, 1>,
    public IProvideClassInfo2,
    public IPersistStreamInit,
    public IPersistPropertyBag,
    public IQuickActivate,
    public IOleControl,
    public IOleObject,
    public IOleInPlaceActiveObject,
    public IViewObjectEx,
    public IOleInPlaceObjectWindowless,
    public IConnectionPointContainerImpl<CShellNameSpace>,
    public IConnectionPointImpl<CShellNameSpace, &DIID_DShellNameSpaceEvents>,
    public IDropTarget,
    public CObjectWithSiteBase,
    public INscTree,
    public IShellBrowser,
    public IFolderFilterSite,
    public INewMenuClient,
    public IServiceProvider,
    public INameSpaceTreeControl,
    public IVisualProperties,
    public IWinEventHandler
{
    CShellNameSpaceWnd m_Wnd;
    CComPtr<INameSpaceTreeControl> m_pTree;
    CComObject<CShellNameSpaceEvents> *m_pEvents;
    DWORD m_dwTreeCookie;

    CComPtr<IOleClientSite> m_pClientSite;
    CComPtr<IOleInPlaceSite> m_pInPlaceSite;
    CComPtr<IOleAdviseHolder> m_pOleAdviseHolder;
    CComPtr<IAdviseSink> m_pViewAdviseSink;
    DWORD m_dwViewAdviseAspects;
    DWORD m_dwViewAdviseFlags;
    CComPtr<IUnknown> m_pFilter;
    SIZEL m_sizeExtent;
    BOOL m_bUIActive;

    LONG m_lEnumOptions;
    int m_iDepth;
    UINT m_uMode;
    ULONG m_ulFlags;
    ULONG m_ulTVFlags;
    CComVariant m_Root;
    CComHeapPtr<ITEMIDLIST_ABSOLUTE> m_pidlRoot;
    UINT m_uNscMode;
    COLORREF m_crText;
    COLORREF m_crBackground;
    int m_cyItem;
    HFONT m_hFont;

    HRESULT GetRootIDList(PIDLIST_ABSOLUTE *ppidl);
    HRESULT ApplyRoot();
    HRESULT CreateTreeWindow(HWND hwndParent, LPCRECT prc, DWORD dwStyle, DWORD dwExStyle);
    VOID DestroyTreeWindow();
    HRESULT GetSelectedShellItem(int nItem, IShellItem **ppsi);
    HRESULT GetSelectedIDList(int nItem, PIDLIST_ABSOLUTE *ppidl);
    HRESULT SelectIDList(PCIDLIST_ABSOLUTE pidl);
    HWND GetTreeViewWindow();

public:
    CShellNameSpace();

    HRESULT FinalConstruct();
    void FinalRelease();

    VOID FireEvent(DISPID dispid);
    VOID ResizeTree();
    VOID FocusTree();

    STDMETHOD(MoveSelectionUp)() override;
    STDMETHOD(MoveSelectionDown)() override;
    STDMETHOD(ResetSort)() override;
    STDMETHOD(NewFolder)() override;
    STDMETHOD(Synchronize)() override;
    STDMETHOD(Import)() override;
    STDMETHOD(Export)() override;
    STDMETHOD(InvokeContextMenuCommand)(BSTR strCommand) override;
    STDMETHOD(MoveSelectionTo)() override;
    STDMETHOD(get_SubscriptionsEnabled)(VARIANT_BOOL *pBool) override;
    STDMETHOD(CreateSubscriptionForSelection)(VARIANT_BOOL *pBool) override;
    STDMETHOD(DeleteSubscriptionForSelection)(VARIANT_BOOL *pBool) override;
    STDMETHOD(SetRoot)(BSTR bstrFullPath) override;
    STDMETHOD(get_EnumOptions)(LONG *pgrfEnumFlags) override;
    STDMETHOD(put_EnumOptions)(LONG lVal) override;
    STDMETHOD(get_SelectedItem)(IDispatch **pItem) override;
    STDMETHOD(put_SelectedItem)(IDispatch *pItem) override;
    STDMETHOD(get_Root)(VARIANT *pvar) override;
    STDMETHOD(put_Root)(VARIANT var) override;
    STDMETHOD(get_Depth)(int *piDepth) override;
    STDMETHOD(put_Depth)(int iDepth) override;
    STDMETHOD(get_Mode)(UINT *puMode) override;
    STDMETHOD(put_Mode)(UINT uMode) override;
    STDMETHOD(get_Flags)(ULONG *pdwFlags) override;
    STDMETHOD(put_Flags)(ULONG dwFlags) override;
    STDMETHOD(put_TVFlags)(ULONG dwFlags) override;
    STDMETHOD(get_TVFlags)(ULONG *dwFlags) override;
    STDMETHOD(get_Columns)(BSTR *bstrColumns) override;
    STDMETHOD(put_Columns)(BSTR bstrColumns) override;
    STDMETHOD(get_CountViewTypes)(int *piTypes) override;
    STDMETHOD(SetViewType)(int iType) override;
    STDMETHOD(SelectedItems)(IDispatch **ppid) override;
    STDMETHOD(Expand)(VARIANT var, int iDepth) override;
    STDMETHOD(UnselectAll)() override;

    STDMETHOD(GetClassInfo)(ITypeInfo **ppTI) override;
    STDMETHOD(GetGUID)(DWORD dwGuidKind, GUID *pGUID) override;

    STDMETHOD(GetClassID)(CLSID *pClassID) override;
    STDMETHOD(IsDirty)() override;
    STDMETHOD(Load)(LPSTREAM pStm) override;
    STDMETHOD(Save)(LPSTREAM pStm, BOOL fClearDirty) override;
    STDMETHOD(GetSizeMax)(ULARGE_INTEGER *pCbSize) override;
    STDMETHOD(InitNew)() override;
    STDMETHOD(Load)(IPropertyBag *pPropBag, IErrorLog *pErrorLog) override;
    STDMETHOD(Save)(IPropertyBag *pPropBag, BOOL fClearDirty, BOOL fSaveAllProperties) override;

    STDMETHOD(QuickActivate)(QACONTAINER *pQaContainer, QACONTROL *pQaControl) override;
    STDMETHOD(SetContentExtent)(LPSIZEL pSizel) override;
    STDMETHOD(GetContentExtent)(LPSIZEL pSizel) override;

    STDMETHOD(GetControlInfo)(CONTROLINFO *pCI) override;
    STDMETHOD(OnMnemonic)(MSG *pMsg) override;
    STDMETHOD(OnAmbientPropertyChange)(DISPID dispID) override;
    STDMETHOD(FreezeEvents)(BOOL bFreeze) override;

    STDMETHOD(SetClientSite)(IOleClientSite *pClientSite) override;
    STDMETHOD(GetClientSite)(IOleClientSite **ppClientSite) override;
    STDMETHOD(SetHostNames)(LPCOLESTR szContainerApp, LPCOLESTR szContainerObj) override;
    STDMETHOD(Close)(DWORD dwSaveOption) override;
    STDMETHOD(SetMoniker)(DWORD dwWhichMoniker, IMoniker *pmk) override;
    STDMETHOD(GetMoniker)(DWORD dwAssign, DWORD dwWhichMoniker, IMoniker **ppmk) override;
    STDMETHOD(InitFromData)(IDataObject *pDataObject, BOOL fCreation, DWORD dwReserved) override;
    STDMETHOD(GetClipboardData)(DWORD dwReserved, IDataObject **ppDataObject) override;
    STDMETHOD(DoVerb)(LONG iVerb, LPMSG lpmsg, IOleClientSite *pActiveSite, LONG lindex, HWND hwndParent, LPCRECT lprcPosRect) override;
    STDMETHOD(EnumVerbs)(IEnumOLEVERB **ppEnumOleVerb) override;
    STDMETHOD(Update)() override;
    STDMETHOD(IsUpToDate)() override;
    STDMETHOD(GetUserClassID)(CLSID *pClsid) override;
    STDMETHOD(GetUserType)(DWORD dwFormOfType, LPOLESTR *pszUserType) override;
    STDMETHOD(SetExtent)(DWORD dwDrawAspect, SIZEL *psizel) override;
    STDMETHOD(GetExtent)(DWORD dwDrawAspect, SIZEL *psizel) override;
    STDMETHOD(Advise)(IAdviseSink *pAdvSink, DWORD *pdwConnection) override;
    STDMETHOD(Unadvise)(DWORD dwConnection) override;
    STDMETHOD(EnumAdvise)(IEnumSTATDATA **ppenumAdvise) override;
    STDMETHOD(GetMiscStatus)(DWORD dwAspect, DWORD *pdwStatus) override;
    STDMETHOD(SetColorScheme)(LOGPALETTE *pLogpal) override;

    STDMETHOD(GetWindow)(HWND *phwnd) override;
    STDMETHOD(ContextSensitiveHelp)(BOOL fEnterMode) override;

    STDMETHOD(TranslateAccelerator)(LPMSG lpmsg) override;
    STDMETHOD(OnFrameWindowActivate)(BOOL fActivate) override;
    STDMETHOD(OnDocWindowActivate)(BOOL fActivate) override;
    STDMETHOD(ResizeBorder)(LPCRECT prcBorder, IOleInPlaceUIWindow *pUIWindow, BOOL fFrameWindow) override;
    STDMETHOD(EnableModeless)(BOOL fEnable) override;

    STDMETHOD(Draw)(DWORD dwDrawAspect, LONG lindex, void *pvAspect, DVTARGETDEVICE *ptd, HDC hdcTargetDev, HDC hdcDraw,
                    LPCRECTL lprcBounds, LPCRECTL lprcWBounds, BOOL (STDMETHODCALLTYPE *pfnContinue)(ULONG_PTR dwContinue),
                    ULONG_PTR dwContinue) override;
    STDMETHOD(GetColorSet)(DWORD dwDrawAspect, LONG lindex, void *pvAspect, DVTARGETDEVICE *ptd, HDC hicTargetDev,
                           LOGPALETTE **ppColorSet) override;
    STDMETHOD(Freeze)(DWORD dwDrawAspect, LONG lindex, void *pvAspect, DWORD *pdwFreeze) override;
    STDMETHOD(Unfreeze)(DWORD dwFreeze) override;
    STDMETHOD(SetAdvise)(DWORD aspects, DWORD advf, IAdviseSink *pAdvSink) override;
    STDMETHOD(GetAdvise)(DWORD *pAspects, DWORD *pAdvf, IAdviseSink **ppAdvSink) override;
    STDMETHOD(GetExtent)(DWORD dwDrawAspect, LONG lindex, DVTARGETDEVICE *ptd, LPSIZEL lpsizel) override;
    STDMETHOD(GetRect)(DWORD dwAspect, LPRECTL pRect) override;
    STDMETHOD(GetViewStatus)(DWORD *pdwStatus) override;
    STDMETHOD(QueryHitPoint)(DWORD dwAspect, LPCRECT pRectBounds, POINT ptlLoc, LONG lCloseHint, DWORD *pHitResult) override;
    STDMETHOD(QueryHitRect)(DWORD dwAspect, LPCRECT pRectBounds, LPCRECT pRectLoc, LONG lCloseHint, DWORD *pHitResult) override;
    STDMETHOD(GetNaturalExtent)(DWORD dwAspect, LONG lindex, DVTARGETDEVICE *ptd, HDC hicTargetDev,
                                DVEXTENTINFO *pExtentInfo, LPSIZEL pSizel) override;

    STDMETHOD(InPlaceDeactivate)() override;
    STDMETHOD(UIDeactivate)() override;
    STDMETHOD(SetObjectRects)(LPCRECT lprcPosRect, LPCRECT lprcClipRect) override;
    STDMETHOD(ReactivateAndUndo)() override;
    STDMETHOD(OnWindowMessage)(UINT msg, WPARAM wParam, LPARAM lParam, LRESULT *plResult) override;
    STDMETHOD(GetDropTarget)(IDropTarget **ppDropTarget) override;

    STDMETHOD(DragEnter)(IDataObject *pDataObj, DWORD grfKeyState, POINTL pt, DWORD *pdwEffect) override;
    STDMETHOD(DragOver)(DWORD grfKeyState, POINTL pt, DWORD *pdwEffect) override;
    STDMETHOD(DragLeave)() override;
    STDMETHOD(Drop)(IDataObject *pDataObj, DWORD grfKeyState, POINTL pt, DWORD *pdwEffect) override;

    STDMETHOD(CreateTree)(HWND hwndParent, DWORD dwStyles, HWND *phwnd) override;
    STDMETHOD(Initialize)(PCIDLIST_ABSOLUTE pidlRoot, DWORD grfEnumFlags, DWORD dwFlags) override;
    STDMETHOD(ShowWindow)(BOOL fShow) override;
    STDMETHOD(Refresh)() override;
    STDMETHOD(GetSelectedItem)(PIDLIST_ABSOLUTE *ppidl, int nItem) override;
    STDMETHOD(SetSelectedItem)(PCIDLIST_ABSOLUTE pidl, BOOL fCreate, BOOL fReinsert, int nItem) override;
    STDMETHOD(GetNscMode)(UINT *pnMode) override;
    STDMETHOD(SetNscMode)(UINT nMode) override;
    STDMETHOD(GetSelectedItemName)(LPWSTR pszName, DWORD cchName) override;
    STDMETHOD(BindToSelectedItemParent)(REFIID riid, void **ppv, PITEMID_CHILD *ppidlLast) override;
    STDMETHOD_(BOOL, InLabelEdit)() override;
    STDMETHOD(RightPaneNavigationStarted)(PCIDLIST_ABSOLUTE pidl) override;
    STDMETHOD(RightPaneNavigationFinished)(PCIDLIST_ABSOLUTE pidl) override;

    STDMETHOD(InsertMenusSB)(HMENU hmenuShared, LPOLEMENUGROUPWIDTHS lpMenuWidths) override;
    STDMETHOD(SetMenuSB)(HMENU hmenuShared, HOLEMENU holemenuRes, HWND hwndActiveObject) override;
    STDMETHOD(RemoveMenusSB)(HMENU hmenuShared) override;
    STDMETHOD(SetStatusTextSB)(LPCWSTR pszStatusText) override;
    STDMETHOD(EnableModelessSB)(BOOL fEnable) override;
    STDMETHOD(TranslateAcceleratorSB)(MSG *pmsg, WORD wID) override;
    STDMETHOD(BrowseObject)(PCUIDLIST_RELATIVE pidl, UINT wFlags) override;
    STDMETHOD(GetViewStateStream)(DWORD grfMode, IStream **ppStrm) override;
    STDMETHOD(GetControlWindow)(UINT id, HWND *phwnd) override;
    STDMETHOD(SendControlMsg)(UINT id, UINT uMsg, WPARAM wParam, LPARAM lParam, LRESULT *pret) override;
    STDMETHOD(QueryActiveShellView)(IShellView **ppshv) override;
    STDMETHOD(OnViewWindowActive)(IShellView *pshv) override;
    STDMETHOD(SetToolbarItems)(LPTBBUTTONSB lpButtons, UINT nButtons, UINT uFlags) override;

    STDMETHOD(SetFilter)(IUnknown *punk) override;

    STDMETHOD(IncludeItems)(NMCII_FLAGS *pflags) override;
    STDMETHOD(SelectAndEditItem)(PCIDLIST_ABSOLUTE pidlItem, NMCSAEI_FLAGS flags) override;

    STDMETHOD(QueryService)(REFGUID guidService, REFIID riid, void **ppvObject) override;

    STDMETHOD(Initialize)(HWND hwndParent, RECT *prc, NSTCSTYLE nstcsFlags) override;
    STDMETHOD(TreeAdvise)(IUnknown *punk, DWORD *pdwCookie) override;
    STDMETHOD(TreeUnadvise)(DWORD dwCookie) override;
    STDMETHOD(AppendRoot)(IShellItem *psiRoot, SHCONTF grfEnumFlags, NSTCROOTSTYLE grfRootStyle, IShellItemFilter *pif) override;
    STDMETHOD(InsertRoot)(int iIndex, IShellItem *psiRoot, SHCONTF grfEnumFlags, NSTCROOTSTYLE grfRootStyle,
                          IShellItemFilter *pif) override;
    STDMETHOD(RemoveRoot)(IShellItem *psiRoot) override;
    STDMETHOD(RemoveAllRoots)() override;
    STDMETHOD(GetRootItems)(IShellItemArray **ppsiaRootItems) override;
    STDMETHOD(SetItemState)(IShellItem *psi, NSTCITEMSTATE nstcisMask, NSTCITEMSTATE nstcisFlags) override;
    STDMETHOD(GetItemState)(IShellItem *psi, NSTCITEMSTATE nstcismask, NSTCITEMSTATE *pnstcisFlags) override;
    STDMETHOD(GetSelectedItems)(IShellItemArray **psiaItems) override;
    STDMETHOD(GetItemCustomState)(IShellItem *psi, int *piStateNumber) override;
    STDMETHOD(SetItemCustomState)(IShellItem *psi, int iStateNumber) override;
    STDMETHOD(EnsureItemVisible)(IShellItem *psi) override;
    STDMETHOD(SetTheme)(LPCWSTR pszTheme) override;
    STDMETHOD(GetNextItem)(IShellItem *psi, NSTCGNI nstcgi, IShellItem **ppsiNext) override;
    STDMETHOD(HitTest)(POINT *ppt, IShellItem **ppsiOut) override;
    STDMETHOD(GetItemRect)(IShellItem *psi, RECT *prect) override;
    STDMETHOD(CollapseAll)() override;

    STDMETHOD(SetWatermark)(HBITMAP hbmp, VPWATERMARKFLAGS vpwf) override;
    STDMETHOD(SetColor)(VPCOLORFLAGS vpcf, COLORREF cr) override;
    STDMETHOD(GetColor)(VPCOLORFLAGS vpcf, COLORREF *pcr) override;
    STDMETHOD(SetItemHeight)(int cyItemInPixels) override;
    STDMETHOD(GetItemHeight)(int *cyItemInPixels) override;
    STDMETHOD(SetFont)(const LOGFONTW *plf, BOOL bRedraw) override;
    STDMETHOD(GetFont)(LOGFONTW *plf) override;
    STDMETHOD(SetTheme)(LPCWSTR pszSubAppName, LPCWSTR pszSubIdList) override;

    STDMETHOD(OnWinEvent)(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, LRESULT *theResult) override;
    STDMETHOD(IsWindowOwner)(HWND hWnd) override;

    DECLARE_NO_REGISTRY()
    DECLARE_NOT_AGGREGATABLE(CShellNameSpace)
    DECLARE_PROTECT_FINAL_CONSTRUCT()

    BEGIN_CONNECTION_POINT_MAP(CShellNameSpace)
        CONNECTION_POINT_ENTRY(DIID_DShellNameSpaceEvents)
    END_CONNECTION_POINT_MAP()

    BEGIN_COM_MAP(CShellNameSpace)
        COM_INTERFACE_ENTRY_IID(IID_IShellNameSpace, IShellNameSpace)
        COM_INTERFACE_ENTRY_IID(IID_IShellFavoritesNameSpace, IShellFavoritesNameSpace)
        COM_INTERFACE_ENTRY_IID(IID_IDispatch, IShellNameSpace)
        COM_INTERFACE_ENTRY_IID(IID_IProvideClassInfo2, IProvideClassInfo2)
        COM_INTERFACE_ENTRY_IID(IID_IProvideClassInfo, IProvideClassInfo2)
        COM_INTERFACE_ENTRY_IID(IID_IPersistStreamInit, IPersistStreamInit)
        COM_INTERFACE_ENTRY_IID(IID_IPersistPropertyBag, IPersistPropertyBag)
        COM_INTERFACE_ENTRY_IID(IID_IQuickActivate, IQuickActivate)
        COM_INTERFACE_ENTRY_IID(IID_IOleControl, IOleControl)
        COM_INTERFACE_ENTRY_IID(IID_IOleObject, IOleObject)
        COM_INTERFACE_ENTRY_IID(IID_IOleInPlaceActiveObject, IOleInPlaceActiveObject)
        COM_INTERFACE_ENTRY_IID(IID_IViewObjectEx, IViewObjectEx)
        COM_INTERFACE_ENTRY_IID(IID_IViewObject2, IViewObjectEx)
        COM_INTERFACE_ENTRY_IID(IID_IViewObject, IViewObjectEx)
        COM_INTERFACE_ENTRY_IID(IID_IOleInPlaceObjectWindowless, IOleInPlaceObjectWindowless)
        COM_INTERFACE_ENTRY_IID(IID_IOleInPlaceObject, IOleInPlaceObjectWindowless)
        COM_INTERFACE_ENTRY_IID(IID_IOleWindow, IOleInPlaceObjectWindowless)
        COM_INTERFACE_ENTRY_IID(IID_IConnectionPointContainer, IConnectionPointContainer)
        COM_INTERFACE_ENTRY_IID(IID_IDropTarget, IDropTarget)
        COM_INTERFACE_ENTRY_IID(IID_IObjectWithSite, IObjectWithSite)
        COM_INTERFACE_ENTRY_IID(IID_INSCTree, INscTree)
        COM_INTERFACE_ENTRY_IID(IID_IShellBrowser, IShellBrowser)
        COM_INTERFACE_ENTRY_IID(IID_IFolderFilterSite, IFolderFilterSite)
        COM_INTERFACE_ENTRY_IID(IID_INewMenuClient, INewMenuClient)
        COM_INTERFACE_ENTRY_IID(IID_IServiceProvider, IServiceProvider)
        COM_INTERFACE_ENTRY_IID(IID_INameSpaceTreeControl, INameSpaceTreeControl)
        COM_INTERFACE_ENTRY_IID(IID_IVisualProperties, IVisualProperties)
        COM_INTERFACE_ENTRY_IID(IID_IWinEventHandler, IWinEventHandler)
    END_COM_MAP()
};

LRESULT CShellNameSpaceWnd::OnSize(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
{
    if (m_pOwner)
        m_pOwner->ResizeTree();
    return 0;
}

LRESULT CShellNameSpaceWnd::OnSetFocus(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
{
    if (m_pOwner)
        m_pOwner->FocusTree();
    return 0;
}

STDMETHODIMP CShellNameSpaceEvents::OnItemClick(IShellItem *psi, NSTCEHITTEST nstceHitTest, NSTCECLICKTYPE nstceClickType)
{
    if (m_pOwner && (nstceClickType & NSTCECT_DBLCLICK))
        m_pOwner->FireEvent(SNS_DISPID_DOUBLECLICK);
    return E_NOTIMPL;
}

STDMETHODIMP CShellNameSpaceEvents::OnSelectionChanged(IShellItemArray *psiaSelection)
{
    if (m_pOwner)
        m_pOwner->FireEvent(SNS_DISPID_SELECTIONCHANGE);
    return S_OK;
}

CShellNameSpace::CShellNameSpace() :
    m_pEvents(NULL),
    m_dwTreeCookie(0),
    m_dwViewAdviseAspects(0),
    m_dwViewAdviseFlags(0),
    m_bUIActive(FALSE),
    m_lEnumOptions(0),
    m_iDepth(1),
    m_uMode(0),
    m_ulFlags(SNS_DEFAULT_FLAGS),
    m_ulTVFlags(0),
    m_uNscMode(SNS_DEFAULT_NSCMODE),
    m_crText(0),
    m_crBackground(0),
    m_cyItem(0),
    m_hFont(NULL)
{
    m_sizeExtent.cx = SNS_DEFAULT_EXTENT;
    m_sizeExtent.cy = SNS_DEFAULT_EXTENT;
    m_Wnd.m_pOwner = this;
}

HRESULT CShellNameSpace::FinalConstruct()
{
    HRESULT hr = CComObject<CShellNameSpaceEvents>::CreateInstance(&m_pEvents);
    if (FAILED(hr))
        return hr;
    m_pEvents->AddRef();
    m_pEvents->m_pOwner = this;
    return S_OK;
}

void CShellNameSpace::FinalRelease()
{
    DestroyTreeWindow();
    if (m_pEvents)
    {
        m_pEvents->m_pOwner = NULL;
        m_pEvents->Release();
        m_pEvents = NULL;
    }
    m_Wnd.m_pOwner = NULL;
    if (m_hFont)
        DeleteObject(m_hFont);
}

VOID CShellNameSpace::FireEvent(DISPID dispid)
{
    DISPPARAMS dp = { NULL, NULL, 0, 0 };
    CComDynamicUnkArray &vec = IConnectionPointImpl<CShellNameSpace, &DIID_DShellNameSpaceEvents>::m_vec;

    for (IUnknown **pp = vec.begin(); pp < vec.end(); pp++)
    {
        CComPtr<IDispatch> pDispatch;
        if (!*pp || FAILED((*pp)->QueryInterface(IID_PPV_ARG(IDispatch, &pDispatch))))
            continue;
        pDispatch->Invoke(dispid, IID_NULL, LOCALE_USER_DEFAULT, DISPATCH_METHOD, &dp, NULL, NULL, NULL);
    }
}

HRESULT CShellNameSpace::GetRootIDList(PIDLIST_ABSOLUTE *ppidl)
{
    CComVariant var;

    *ppidl = NULL;
    if (m_pidlRoot)
    {
        *ppidl = ILClone(m_pidlRoot);
        return *ppidl ? S_OK : E_OUTOFMEMORY;
    }

    switch (V_VT(&m_Root))
    {
        case VT_EMPTY:
            return SHGetSpecialFolderLocation(NULL, CSIDL_DESKTOP, ppidl);

        case VT_BSTR:
            if (!V_BSTR(&m_Root) || !*V_BSTR(&m_Root))
                return SHGetSpecialFolderLocation(NULL, CSIDL_DESKTOP, ppidl);
            return SHParseDisplayName(V_BSTR(&m_Root), NULL, ppidl, 0, NULL);
    }

    if (FAILED(var.ChangeType(VT_I4, &m_Root)))
        return E_INVALIDARG;
    return SHGetSpecialFolderLocation(NULL, V_I4(&var), ppidl);
}

HRESULT CShellNameSpace::ApplyRoot()
{
    CComHeapPtr<ITEMIDLIST_ABSOLUTE> pidl;
    CComPtr<IShellItem> psi;
    SHCONTF grfEnumFlags;
    HRESULT hr;

    if (!m_pTree)
        return S_OK;

    hr = GetRootIDList(&pidl);
    if (FAILED(hr))
        return hr;
    hr = SHCreateItemFromIDList(pidl, IID_PPV_ARG(IShellItem, &psi));
    if (FAILED(hr))
        return hr;

    grfEnumFlags = m_lEnumOptions ? (SHCONTF)m_lEnumOptions : (SHCONTF)(SHCONTF_FOLDERS | SHCONTF_NONFOLDERS);
    m_pTree->RemoveAllRoots();
    return m_pTree->AppendRoot(psi, grfEnumFlags, NSTCRS_EXPANDED, NULL);
}

HRESULT CShellNameSpace::CreateTreeWindow(HWND hwndParent, LPCRECT prc, DWORD dwStyle, DWORD dwExStyle)
{
    RECT rc;
    HRESULT hr;

    if (m_Wnd.m_hWnd)
    {
        m_Wnd.SetParent(hwndParent);
        if (prc)
            m_Wnd.SetWindowPos(NULL, prc->left, prc->top, prc->right - prc->left, prc->bottom - prc->top,
                               SWP_NOZORDER | SWP_NOACTIVATE);
        return S_OK;
    }

    if (prc)
        rc = *prc;
    else
        ::GetClientRect(hwndParent, &rc);

    if (!m_Wnd.Create(hwndParent, rc, NULL, WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS | dwStyle, dwExStyle))
        return HRESULT_FROM_WIN32(GetLastError());

    hr = CoCreateInstance(CLSID_NamespaceTreeControl, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARG(INameSpaceTreeControl, &m_pTree));
    if (SUCCEEDED(hr))
    {
        RECT rcClient;
        m_Wnd.GetClientRect(&rcClient);
        hr = m_pTree->Initialize(m_Wnd.m_hWnd, &rcClient, NSTCS_HASEXPANDOS | NSTCS_HASLINES | NSTCS_SINGLECLICKEXPAND |
                                 NSTCS_SHOWSELECTIONALWAYS | NSTCS_ROOTHASEXPANDO);
    }
    if (FAILED(hr))
    {
        m_pTree.Release();
        m_Wnd.DestroyWindow();
        return hr;
    }

    m_pTree->TreeAdvise(static_cast<INameSpaceTreeControlEvents *>(m_pEvents), &m_dwTreeCookie);
    ApplyRoot();
    if (m_pFilter)
    {
        CComPtr<IFolderFilterSite> pSite;
        if (SUCCEEDED(m_pTree->QueryInterface(IID_PPV_ARG(IFolderFilterSite, &pSite))))
            pSite->SetFilter(m_pFilter);
    }
    FireEvent(SNS_DISPID_INITIALIZED);
    return S_OK;
}

VOID CShellNameSpace::DestroyTreeWindow()
{
    if (m_pTree)
    {
        if (m_dwTreeCookie)
            m_pTree->TreeUnadvise(m_dwTreeCookie);
        m_dwTreeCookie = 0;
        m_pTree->RemoveAllRoots();
        m_pTree.Release();
    }
    if (m_Wnd.m_hWnd)
        m_Wnd.DestroyWindow();
}

VOID CShellNameSpace::ResizeTree()
{
    CComPtr<IOleWindow> pWindow;
    HWND hwndTree;
    RECT rc;

    if (!m_pTree || FAILED(m_pTree->QueryInterface(IID_PPV_ARG(IOleWindow, &pWindow))) ||
        FAILED(pWindow->GetWindow(&hwndTree)) || !hwndTree)
    {
        return;
    }
    m_Wnd.GetClientRect(&rc);
    ::MoveWindow(hwndTree, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, TRUE);
}

VOID CShellNameSpace::FocusTree()
{
    HWND hwndTree = GetTreeViewWindow();

    if (hwndTree)
        ::SetFocus(hwndTree);
}

HWND CShellNameSpace::GetTreeViewWindow()
{
    CComPtr<IOleWindow> pWindow;
    HWND hwndTree = NULL;

    if (m_pTree && SUCCEEDED(m_pTree->QueryInterface(IID_PPV_ARG(IOleWindow, &pWindow))))
        pWindow->GetWindow(&hwndTree);
    return hwndTree;
}

HRESULT CShellNameSpace::GetSelectedShellItem(int nItem, IShellItem **ppsi)
{
    CComPtr<IShellItemArray> pArray;
    DWORD cItems = 0;
    HRESULT hr;

    *ppsi = NULL;
    if (!m_pTree)
        return E_FAIL;
    hr = m_pTree->GetSelectedItems(&pArray);
    if (FAILED(hr) || !pArray)
        return S_FALSE;
    if (FAILED(pArray->GetCount(&cItems)) || nItem < 0 || (DWORD)nItem >= cItems)
        return S_FALSE;
    return pArray->GetItemAt(nItem, ppsi);
}

HRESULT CShellNameSpace::GetSelectedIDList(int nItem, PIDLIST_ABSOLUTE *ppidl)
{
    CComPtr<IShellItem> psi;
    HRESULT hr;

    *ppidl = NULL;
    hr = GetSelectedShellItem(nItem, &psi);
    if (hr != S_OK)
        return hr;
    return SHGetIDListFromObject(psi, ppidl);
}

HRESULT CShellNameSpace::SelectIDList(PCIDLIST_ABSOLUTE pidl)
{
    CComPtr<IShellItem> psi;
    HRESULT hr;

    if (!m_pTree)
        return E_FAIL;
    hr = SHCreateItemFromIDList(pidl, IID_PPV_ARG(IShellItem, &psi));
    if (FAILED(hr))
        return hr;
    hr = m_pTree->SetItemState(psi, NSTCIS_SELECTED, NSTCIS_SELECTED);
    if (SUCCEEDED(hr))
        m_pTree->EnsureItemVisible(psi);
    return hr;
}

STDMETHODIMP CShellNameSpace::MoveSelectionUp()
{
    return S_OK;
}

STDMETHODIMP CShellNameSpace::MoveSelectionDown()
{
    return S_OK;
}

STDMETHODIMP CShellNameSpace::ResetSort()
{
    return S_OK;
}

STDMETHODIMP CShellNameSpace::NewFolder()
{
    FIXME("(%p) favorites folder creation\n", this);
    return E_NOTIMPL;
}

STDMETHODIMP CShellNameSpace::Synchronize()
{
    return S_OK;
}

STDMETHODIMP CShellNameSpace::Import()
{
    FIXME("(%p) favorites import\n", this);
    return E_NOTIMPL;
}

STDMETHODIMP CShellNameSpace::Export()
{
    FIXME("(%p) favorites export\n", this);
    return E_NOTIMPL;
}

STDMETHODIMP CShellNameSpace::InvokeContextMenuCommand(BSTR strCommand)
{
    CComPtr<IShellItem> psi;
    CComPtr<IContextMenu> pcm;
    CMINVOKECOMMANDINFOEX ici = { sizeof(ici) };
    CHAR szVerb[MAX_PATH];
    HMENU hMenu;
    HRESULT hr;

    if (!strCommand || GetSelectedShellItem(0, &psi) != S_OK)
        return S_OK;

    hr = psi->BindToHandler(NULL, BHID_SFUIObject, IID_PPV_ARG(IContextMenu, &pcm));
    if (FAILED(hr))
        return hr;

    hMenu = CreatePopupMenu();
    if (!hMenu)
        return E_OUTOFMEMORY;
    hr = pcm->QueryContextMenu(hMenu, 0, 1, 0x7FFF, CMF_NORMAL);
    if (SUCCEEDED(hr))
    {
        WideCharToMultiByte(CP_ACP, 0, strCommand, -1, szVerb, _countof(szVerb), NULL, NULL);
        ici.fMask = CMIC_MASK_UNICODE;
        ici.hwnd = m_Wnd.m_hWnd;
        ici.lpVerb = szVerb;
        ici.lpVerbW = strCommand;
        ici.nShow = SW_SHOWNORMAL;
        hr = pcm->InvokeCommand((LPCMINVOKECOMMANDINFO)&ici);
    }
    DestroyMenu(hMenu);
    return hr;
}

STDMETHODIMP CShellNameSpace::MoveSelectionTo()
{
    return S_OK;
}

STDMETHODIMP CShellNameSpace::get_SubscriptionsEnabled(VARIANT_BOOL *pBool)
{
    if (!pBool)
        return E_POINTER;
    *pBool = VARIANT_FALSE;
    return S_OK;
}

STDMETHODIMP CShellNameSpace::CreateSubscriptionForSelection(VARIANT_BOOL *pBool)
{
    return E_NOTIMPL;
}

STDMETHODIMP CShellNameSpace::DeleteSubscriptionForSelection(VARIANT_BOOL *pBool)
{
    return E_NOTIMPL;
}

STDMETHODIMP CShellNameSpace::SetRoot(BSTR bstrFullPath)
{
    m_pidlRoot.Free();
    m_Root.Clear();
    if (bstrFullPath)
    {
        V_BSTR(&m_Root) = SysAllocString(bstrFullPath);
        if (!V_BSTR(&m_Root))
            return E_OUTOFMEMORY;
        V_VT(&m_Root) = VT_BSTR;
    }
    return ApplyRoot();
}

STDMETHODIMP CShellNameSpace::get_EnumOptions(LONG *pgrfEnumFlags)
{
    return E_UNEXPECTED;
}

STDMETHODIMP CShellNameSpace::put_EnumOptions(LONG lVal)
{
    m_lEnumOptions = lVal;
    return S_OK;
}

STDMETHODIMP CShellNameSpace::get_SelectedItem(IDispatch **pItem)
{
    CComPtr<FolderItem> pFolderItem;
    CComHeapPtr<ITEMIDLIST_ABSOLUTE> pidl;
    HRESULT hr;

    if (!pItem)
        return E_POINTER;
    *pItem = NULL;

    if (GetSelectedIDList(0, &pidl) != S_OK)
        return S_OK;
    hr = SNS_CreateFolderItem(pidl, &pFolderItem);
    if (hr != S_OK)
        return FAILED(hr) ? hr : S_OK;
    return pFolderItem->QueryInterface(IID_PPV_ARG(IDispatch, pItem));
}

STDMETHODIMP CShellNameSpace::put_SelectedItem(IDispatch *pItem)
{
    CComPtr<FolderItem> pFolderItem;
    CComHeapPtr<ITEMIDLIST_ABSOLUTE> pidl;
    CComBSTR path;
    HRESULT hr;

    if (!pItem)
        return E_INVALIDARG;
    hr = pItem->QueryInterface(IID_PPV_ARG(FolderItem, &pFolderItem));
    if (FAILED(hr))
        return hr;
    hr = pFolderItem->get_Path(&path);
    if (FAILED(hr))
        return hr;
    hr = SHParseDisplayName(path, NULL, &pidl, 0, NULL);
    if (FAILED(hr))
        return hr;
    return SelectIDList(pidl);
}

STDMETHODIMP CShellNameSpace::get_Root(VARIANT *pvar)
{
    if (!pvar)
        return E_POINTER;
    VariantInit(pvar);
    return S_OK;
}

STDMETHODIMP CShellNameSpace::put_Root(VARIANT var)
{
    HRESULT hr;

    m_pidlRoot.Free();
    hr = m_Root.Copy(&var);
    if (FAILED(hr))
        return hr;
    ApplyRoot();
    return S_OK;
}

STDMETHODIMP CShellNameSpace::get_Depth(int *piDepth)
{
    if (!piDepth)
        return E_POINTER;
    *piDepth = m_iDepth;
    return S_OK;
}

STDMETHODIMP CShellNameSpace::put_Depth(int iDepth)
{
    return S_OK;
}

STDMETHODIMP CShellNameSpace::get_Mode(UINT *puMode)
{
    if (!puMode)
        return E_POINTER;
    *puMode = m_uMode;
    return S_OK;
}

STDMETHODIMP CShellNameSpace::put_Mode(UINT uMode)
{
    m_uMode = uMode;
    return S_OK;
}

STDMETHODIMP CShellNameSpace::get_Flags(ULONG *pdwFlags)
{
    if (!pdwFlags)
        return E_POINTER;
    *pdwFlags = m_ulFlags;
    return S_OK;
}

STDMETHODIMP CShellNameSpace::put_Flags(ULONG dwFlags)
{
    m_ulFlags = dwFlags;
    return S_OK;
}

STDMETHODIMP CShellNameSpace::put_TVFlags(ULONG dwFlags)
{
    m_ulTVFlags = dwFlags;
    return S_OK;
}

STDMETHODIMP CShellNameSpace::get_TVFlags(ULONG *dwFlags)
{
    if (!dwFlags)
        return E_POINTER;
    *dwFlags = m_ulTVFlags;
    return S_OK;
}

STDMETHODIMP CShellNameSpace::get_Columns(BSTR *bstrColumns)
{
    if (!bstrColumns)
        return E_POINTER;
    *bstrColumns = SysAllocString(L"");
    return *bstrColumns ? S_OK : E_OUTOFMEMORY;
}

STDMETHODIMP CShellNameSpace::put_Columns(BSTR bstrColumns)
{
    return E_NOTIMPL;
}

STDMETHODIMP CShellNameSpace::get_CountViewTypes(int *piTypes)
{
    if (!piTypes)
        return E_POINTER;
    *piTypes = 0;
    return S_OK;
}

STDMETHODIMP CShellNameSpace::SetViewType(int iType)
{
    return S_OK;
}

STDMETHODIMP CShellNameSpace::SelectedItems(IDispatch **ppid)
{
    CComObject<CSelectedFolderItems> *pItems;
    CComPtr<IShellItemArray> pArray;
    DWORD cItems = 0;
    HRESULT hr;

    if (!ppid)
        return E_POINTER;
    *ppid = NULL;

    hr = CComObject<CSelectedFolderItems>::CreateInstance(&pItems);
    if (FAILED(hr))
        return hr;
    pItems->AddRef();

    if (m_pTree && SUCCEEDED(m_pTree->GetSelectedItems(&pArray)) && pArray && SUCCEEDED(pArray->GetCount(&cItems)))
    {
        for (DWORD i = 0; i < cItems; i++)
        {
            CComPtr<IShellItem> psi;
            CComHeapPtr<ITEMIDLIST_ABSOLUTE> pidl;
            if (SUCCEEDED(pArray->GetItemAt(i, &psi)) && SUCCEEDED(SHGetIDListFromObject(psi, &pidl)))
                pItems->AddItem(pidl);
        }
    }

    hr = pItems->QueryInterface(IID_PPV_ARG(IDispatch, ppid));
    pItems->Release();
    return hr;
}

STDMETHODIMP CShellNameSpace::Expand(VARIANT var, int iDepth)
{
    CComHeapPtr<ITEMIDLIST_ABSOLUTE> pidl;
    CComPtr<IShellItem> psi;
    CComVariant varI4;
    HRESULT hr;

    if (!m_pTree || V_VT(&var) == VT_EMPTY)
        return S_OK;

    if (V_VT(&var) == VT_BSTR)
        hr = SHParseDisplayName(V_BSTR(&var), NULL, &pidl, 0, NULL);
    else if (SUCCEEDED(varI4.ChangeType(VT_I4, &var)))
        hr = SHGetSpecialFolderLocation(NULL, V_I4(&varI4), &pidl);
    else
        hr = E_INVALIDARG;
    if (FAILED(hr))
        return hr;

    hr = SHCreateItemFromIDList(pidl, IID_PPV_ARG(IShellItem, &psi));
    if (FAILED(hr))
        return hr;
    return m_pTree->SetItemState(psi, NSTCIS_EXPANDED, iDepth ? NSTCIS_EXPANDED : NSTCIS_NONE);
}

STDMETHODIMP CShellNameSpace::UnselectAll()
{
    CComPtr<IShellItemArray> pArray;
    DWORD cItems = 0;

    if (!m_pTree || FAILED(m_pTree->GetSelectedItems(&pArray)) || !pArray || FAILED(pArray->GetCount(&cItems)))
        return S_OK;
    for (DWORD i = 0; i < cItems; i++)
    {
        CComPtr<IShellItem> psi;
        if (SUCCEEDED(pArray->GetItemAt(i, &psi)))
            m_pTree->SetItemState(psi, NSTCIS_SELECTED, NSTCIS_NONE);
    }
    return S_OK;
}

STDMETHODIMP CShellNameSpace::GetClassInfo(ITypeInfo **ppTI)
{
    CComPtr<ITypeLib> pTypeLib;
    HRESULT hr;

    if (!ppTI)
        return E_POINTER;
    *ppTI = NULL;
    hr = LoadRegTypeLib(LIBID_SHDocVw, 1, 1, LOCALE_SYSTEM_DEFAULT, &pTypeLib);
    if (FAILED(hr))
        return hr;
    return pTypeLib->GetTypeInfoOfGuid(CLSID_ShellNameSpace, ppTI);
}

STDMETHODIMP CShellNameSpace::GetGUID(DWORD dwGuidKind, GUID *pGUID)
{
    if (!pGUID)
        return E_POINTER;
    if (dwGuidKind != GUIDKIND_DEFAULT_SOURCE_DISP_IID)
    {
        *pGUID = GUID_NULL;
        return E_INVALIDARG;
    }
    *pGUID = DIID_DShellNameSpaceEvents;
    return S_OK;
}

STDMETHODIMP CShellNameSpace::GetClassID(CLSID *pClassID)
{
    if (!pClassID)
        return E_POINTER;
    *pClassID = CLSID_ShellNameSpace;
    return S_OK;
}

STDMETHODIMP CShellNameSpace::IsDirty()
{
    return S_FALSE;
}

STDMETHODIMP CShellNameSpace::Load(LPSTREAM pStm)
{
    DWORD dwVersion;
    SIZEL size;
    HRESULT hr;

    if (!pStm)
        return E_POINTER;
    hr = pStm->Read(&dwVersion, sizeof(dwVersion), NULL);
    if (FAILED(hr))
        return hr;
    hr = pStm->Read(&size, sizeof(size), NULL);
    if (FAILED(hr))
        return hr;
    m_sizeExtent = size;
    return S_OK;
}

STDMETHODIMP CShellNameSpace::Save(LPSTREAM pStm, BOOL fClearDirty)
{
    DWORD dwVersion = SNS_STREAM_VERSION;
    HRESULT hr;

    if (!pStm)
        return E_POINTER;
    hr = pStm->Write(&dwVersion, sizeof(dwVersion), NULL);
    if (FAILED(hr))
        return hr;
    hr = pStm->Write(&m_sizeExtent, sizeof(m_sizeExtent), NULL);
    if (FAILED(hr))
        return hr;
    return DISP_E_TYPEMISMATCH;
}

STDMETHODIMP CShellNameSpace::GetSizeMax(ULARGE_INTEGER *pCbSize)
{
    if (!pCbSize)
        return E_POINTER;
    pCbSize->QuadPart = sizeof(DWORD) + sizeof(SIZEL) + sizeof(DWORD) + sizeof(DWORD);
    return E_FAIL;
}

STDMETHODIMP CShellNameSpace::InitNew()
{
    return S_OK;
}

STDMETHODIMP CShellNameSpace::Load(IPropertyBag *pPropBag, IErrorLog *pErrorLog)
{
    CComVariant var;

    if (!pPropBag)
        return E_POINTER;

    var.Clear();
    V_VT(&var) = VT_UI4;
    if (SUCCEEDED(pPropBag->Read(L"_cx", &var, pErrorLog)) && SUCCEEDED(var.ChangeType(VT_I4)))
        m_sizeExtent.cx = V_I4(&var);
    var.Clear();
    V_VT(&var) = VT_UI4;
    if (SUCCEEDED(pPropBag->Read(L"_cy", &var, pErrorLog)) && SUCCEEDED(var.ChangeType(VT_I4)))
        m_sizeExtent.cy = V_I4(&var);
    var.Clear();
    V_VT(&var) = VT_VARIANT;
    if (SUCCEEDED(pPropBag->Read(L"Root", &var, pErrorLog)))
        put_Root(var);
    var.Clear();
    V_VT(&var) = VT_I4;
    if (SUCCEEDED(pPropBag->Read(L"EnumOptions", &var, pErrorLog)) && SUCCEEDED(var.ChangeType(VT_I4)))
        m_lEnumOptions = V_I4(&var);
    var.Clear();
    V_VT(&var) = VT_UI4;
    if (SUCCEEDED(pPropBag->Read(L"Flags", &var, pErrorLog)) && SUCCEEDED(var.ChangeType(VT_UI4)))
        m_ulFlags = V_UI4(&var);
    var.Clear();
    V_VT(&var) = VT_I4;
    pPropBag->Read(L"Depth", &var, pErrorLog);
    var.Clear();
    V_VT(&var) = VT_UI4;
    if (SUCCEEDED(pPropBag->Read(L"Mode", &var, pErrorLog)) && SUCCEEDED(var.ChangeType(VT_UI4)))
        m_uMode = V_UI4(&var);
    var.Clear();
    V_VT(&var) = VT_UI4;
    if (SUCCEEDED(pPropBag->Read(L"TVFlags", &var, pErrorLog)) && SUCCEEDED(var.ChangeType(VT_UI4)))
        m_ulTVFlags = V_UI4(&var);
    var.Clear();
    V_VT(&var) = VT_BSTR;
    pPropBag->Read(L"Columns", &var, pErrorLog);
    return S_OK;
}

STDMETHODIMP CShellNameSpace::Save(IPropertyBag *pPropBag, BOOL fClearDirty, BOOL fSaveAllProperties)
{
    CComVariant varCx((ULONG)m_sizeExtent.cx), varCy((ULONG)m_sizeExtent.cy);
    HRESULT hr;

    if (!pPropBag)
        return E_POINTER;

    hr = pPropBag->Write(L"_cx", &varCx);
    if (FAILED(hr))
        return hr;
    hr = pPropBag->Write(L"_cy", &varCy);
    if (FAILED(hr))
        return hr;
    return DISP_E_TYPEMISMATCH;
}

STDMETHODIMP CShellNameSpace::QuickActivate(QACONTAINER *pQaContainer, QACONTROL *pQaControl)
{
    if (!pQaContainer || !pQaControl || pQaContainer->cbSize < sizeof(QACONTAINER) ||
        pQaControl->cbSize < sizeof(QACONTROL))
    {
        return E_INVALIDARG;
    }

    SetClientSite(pQaContainer->pClientSite);
    if (pQaContainer->pAdviseSink)
        SetAdvise(DVASPECT_CONTENT, 0, pQaContainer->pAdviseSink);

    pQaControl->dwMiscStatus = 0;
    GetMiscStatus(DVASPECT_CONTENT, &pQaControl->dwMiscStatus);
    GetViewStatus(&pQaControl->dwViewStatus);
    pQaControl->dwEventCookie = 0;
    pQaControl->dwPropNotifyCookie = 0;
    pQaControl->dwPointerActivationPolicy = 0;
    if (pQaContainer->pUnkEventSink)
        IConnectionPointImpl<CShellNameSpace, &DIID_DShellNameSpaceEvents>::Advise(pQaContainer->pUnkEventSink,
                                                                                  &pQaControl->dwEventCookie);
    return S_OK;
}

STDMETHODIMP CShellNameSpace::SetContentExtent(LPSIZEL pSizel)
{
    if (!pSizel)
        return E_POINTER;
    m_sizeExtent = *pSizel;
    return S_OK;
}

STDMETHODIMP CShellNameSpace::GetContentExtent(LPSIZEL pSizel)
{
    if (!pSizel)
        return E_POINTER;
    *pSizel = m_sizeExtent;
    return S_OK;
}

STDMETHODIMP CShellNameSpace::GetControlInfo(CONTROLINFO *pCI)
{
    return E_NOTIMPL;
}

STDMETHODIMP CShellNameSpace::OnMnemonic(MSG *pMsg)
{
    return E_NOTIMPL;
}

STDMETHODIMP CShellNameSpace::OnAmbientPropertyChange(DISPID dispID)
{
    return S_OK;
}

STDMETHODIMP CShellNameSpace::FreezeEvents(BOOL bFreeze)
{
    return S_OK;
}

STDMETHODIMP CShellNameSpace::SetClientSite(IOleClientSite *pClientSite)
{
    m_pClientSite = pClientSite;
    return S_OK;
}

STDMETHODIMP CShellNameSpace::GetClientSite(IOleClientSite **ppClientSite)
{
    if (!ppClientSite)
        return E_POINTER;
    *ppClientSite = m_pClientSite;
    if (!*ppClientSite)
        return E_FAIL;
    (*ppClientSite)->AddRef();
    return S_OK;
}

STDMETHODIMP CShellNameSpace::SetHostNames(LPCOLESTR szContainerApp, LPCOLESTR szContainerObj)
{
    return S_OK;
}

STDMETHODIMP CShellNameSpace::Close(DWORD dwSaveOption)
{
    InPlaceDeactivate();
    if (m_pOleAdviseHolder)
        m_pOleAdviseHolder->SendOnClose();
    return S_OK;
}

STDMETHODIMP CShellNameSpace::SetMoniker(DWORD dwWhichMoniker, IMoniker *pmk)
{
    return E_NOTIMPL;
}

STDMETHODIMP CShellNameSpace::GetMoniker(DWORD dwAssign, DWORD dwWhichMoniker, IMoniker **ppmk)
{
    return E_NOTIMPL;
}

STDMETHODIMP CShellNameSpace::InitFromData(IDataObject *pDataObject, BOOL fCreation, DWORD dwReserved)
{
    return E_NOTIMPL;
}

STDMETHODIMP CShellNameSpace::GetClipboardData(DWORD dwReserved, IDataObject **ppDataObject)
{
    return E_NOTIMPL;
}

STDMETHODIMP CShellNameSpace::DoVerb(LONG iVerb, LPMSG lpmsg, IOleClientSite *pActiveSite, LONG lindex, HWND hwndParent,
                                     LPCRECT lprcPosRect)
{
    IOleClientSite *pSite = pActiveSite ? pActiveSite : m_pClientSite.p;
    CComPtr<IOleInPlaceFrame> pFrame;
    CComPtr<IOleInPlaceUIWindow> pDoc;
    OLEINPLACEFRAMEINFO frameInfo = { sizeof(frameInfo) };
    RECT rcPos, rcClip;
    HWND hwndSite;
    HRESULT hr;

    switch (iVerb)
    {
        case OLEIVERB_HIDE:
            if (m_Wnd.m_hWnd)
                m_Wnd.ShowWindow(SW_HIDE);
            return S_OK;

        case OLEIVERB_PRIMARY:
        case OLEIVERB_SHOW:
        case OLEIVERB_OPEN:
        case OLEIVERB_UIACTIVATE:
        case OLEIVERB_INPLACEACTIVATE:
            break;

        default:
            return OLEOBJ_S_INVALIDVERB;
    }

    if (!pSite)
        return S_OK;

    if (!m_pInPlaceSite)
    {
        hr = pSite->QueryInterface(IID_PPV_ARG(IOleInPlaceSite, &m_pInPlaceSite));
        if (FAILED(hr))
            return hr;
        if (m_pInPlaceSite->CanInPlaceActivate() != S_OK)
        {
            m_pInPlaceSite.Release();
            return E_FAIL;
        }
        m_pInPlaceSite->OnInPlaceActivate();
    }

    hr = m_pInPlaceSite->GetWindow(&hwndSite);
    if (FAILED(hr))
        hwndSite = hwndParent;
    hr = m_pInPlaceSite->GetWindowContext(&pFrame, &pDoc, &rcPos, &rcClip, &frameInfo);
    if (FAILED(hr) && lprcPosRect)
        rcPos = *lprcPosRect;

    hr = CreateTreeWindow(hwndSite, &rcPos, 0, 0);
    if (FAILED(hr))
        return hr;
    m_Wnd.ShowWindow(SW_SHOW);

    if (iVerb == OLEIVERB_UIACTIVATE || iVerb == OLEIVERB_PRIMARY)
    {
        if (!m_bUIActive)
        {
            m_bUIActive = TRUE;
            m_pInPlaceSite->OnUIActivate();
            if (pFrame)
                pFrame->SetActiveObject(static_cast<IOleInPlaceActiveObject *>(this), NULL);
            if (pDoc)
                pDoc->SetActiveObject(static_cast<IOleInPlaceActiveObject *>(this), NULL);
        }
        FocusTree();
    }
    return S_OK;
}

STDMETHODIMP CShellNameSpace::EnumVerbs(IEnumOLEVERB **ppEnumOleVerb)
{
    return OleRegEnumVerbs(CLSID_ShellNameSpace, ppEnumOleVerb);
}

STDMETHODIMP CShellNameSpace::Update()
{
    return S_OK;
}

STDMETHODIMP CShellNameSpace::IsUpToDate()
{
    return S_OK;
}

STDMETHODIMP CShellNameSpace::GetUserClassID(CLSID *pClsid)
{
    return GetClassID(pClsid);
}

STDMETHODIMP CShellNameSpace::GetUserType(DWORD dwFormOfType, LPOLESTR *pszUserType)
{
    return OleRegGetUserType(CLSID_ShellNameSpace, dwFormOfType, pszUserType);
}

STDMETHODIMP CShellNameSpace::SetExtent(DWORD dwDrawAspect, SIZEL *psizel)
{
    if (!psizel)
        return E_POINTER;
    if (dwDrawAspect != DVASPECT_CONTENT)
        return DV_E_DVASPECT;
    m_sizeExtent = *psizel;
    return S_OK;
}

STDMETHODIMP CShellNameSpace::GetExtent(DWORD dwDrawAspect, SIZEL *psizel)
{
    if (!psizel)
        return E_POINTER;
    if (dwDrawAspect != DVASPECT_CONTENT)
        return DV_E_DVASPECT;
    *psizel = m_sizeExtent;
    return S_OK;
}

STDMETHODIMP CShellNameSpace::Advise(IAdviseSink *pAdvSink, DWORD *pdwConnection)
{
    if (!m_pOleAdviseHolder)
    {
        HRESULT hr = CreateOleAdviseHolder(&m_pOleAdviseHolder);
        if (FAILED(hr))
            return hr;
    }
    return m_pOleAdviseHolder->Advise(pAdvSink, pdwConnection);
}

STDMETHODIMP CShellNameSpace::Unadvise(DWORD dwConnection)
{
    if (!m_pOleAdviseHolder)
        return OLE_E_NOCONNECTION;
    return m_pOleAdviseHolder->Unadvise(dwConnection);
}

STDMETHODIMP CShellNameSpace::EnumAdvise(IEnumSTATDATA **ppenumAdvise)
{
    if (!ppenumAdvise)
        return E_POINTER;
    *ppenumAdvise = NULL;
    if (!m_pOleAdviseHolder)
        return S_OK;
    return m_pOleAdviseHolder->EnumAdvise(ppenumAdvise);
}

STDMETHODIMP CShellNameSpace::GetMiscStatus(DWORD dwAspect, DWORD *pdwStatus)
{
    return OleRegGetMiscStatus(CLSID_ShellNameSpace, dwAspect, pdwStatus);
}

STDMETHODIMP CShellNameSpace::SetColorScheme(LOGPALETTE *pLogpal)
{
    return E_NOTIMPL;
}

STDMETHODIMP CShellNameSpace::GetWindow(HWND *phwnd)
{
    if (!phwnd)
        return E_POINTER;
    *phwnd = m_Wnd.m_hWnd;
    return S_OK;
}

STDMETHODIMP CShellNameSpace::ContextSensitiveHelp(BOOL fEnterMode)
{
    return E_NOTIMPL;
}

STDMETHODIMP CShellNameSpace::TranslateAccelerator(LPMSG lpmsg)
{
    HWND hwndTree = GetTreeViewWindow();

    if (lpmsg && hwndTree && (lpmsg->hwnd == hwndTree || ::IsChild(hwndTree, lpmsg->hwnd)) &&
        lpmsg->message == WM_KEYDOWN && (lpmsg->wParam == VK_RETURN || lpmsg->wParam == VK_F2))
    {
        ::TranslateMessage(lpmsg);
        ::DispatchMessageW(lpmsg);
        return S_OK;
    }
    return S_FALSE;
}

STDMETHODIMP CShellNameSpace::OnFrameWindowActivate(BOOL fActivate)
{
    return S_OK;
}

STDMETHODIMP CShellNameSpace::OnDocWindowActivate(BOOL fActivate)
{
    return S_OK;
}

STDMETHODIMP CShellNameSpace::ResizeBorder(LPCRECT prcBorder, IOleInPlaceUIWindow *pUIWindow, BOOL fFrameWindow)
{
    return S_OK;
}

STDMETHODIMP CShellNameSpace::EnableModeless(BOOL fEnable)
{
    return S_OK;
}

STDMETHODIMP CShellNameSpace::Draw(DWORD dwDrawAspect, LONG lindex, void *pvAspect, DVTARGETDEVICE *ptd, HDC hdcTargetDev,
                                   HDC hdcDraw, LPCRECTL lprcBounds, LPCRECTL lprcWBounds,
                                   BOOL (STDMETHODCALLTYPE *pfnContinue)(ULONG_PTR dwContinue), ULONG_PTR dwContinue)
{
    RECT rc;

    if (dwDrawAspect != DVASPECT_CONTENT)
        return DV_E_DVASPECT;
    if (!hdcDraw || !lprcBounds)
        return E_INVALIDARG;

    rc.left = lprcBounds->left;
    rc.top = lprcBounds->top;
    rc.right = lprcBounds->right;
    rc.bottom = lprcBounds->bottom;
    FillRect(hdcDraw, &rc, GetSysColorBrush(COLOR_WINDOW));
    return S_OK;
}

STDMETHODIMP CShellNameSpace::GetColorSet(DWORD dwDrawAspect, LONG lindex, void *pvAspect, DVTARGETDEVICE *ptd,
                                          HDC hicTargetDev, LOGPALETTE **ppColorSet)
{
    return E_NOTIMPL;
}

STDMETHODIMP CShellNameSpace::Freeze(DWORD dwDrawAspect, LONG lindex, void *pvAspect, DWORD *pdwFreeze)
{
    return E_NOTIMPL;
}

STDMETHODIMP CShellNameSpace::Unfreeze(DWORD dwFreeze)
{
    return E_NOTIMPL;
}

STDMETHODIMP CShellNameSpace::SetAdvise(DWORD aspects, DWORD advf, IAdviseSink *pAdvSink)
{
    m_pViewAdviseSink = pAdvSink;
    m_dwViewAdviseAspects = aspects;
    m_dwViewAdviseFlags = advf;
    return S_OK;
}

STDMETHODIMP CShellNameSpace::GetAdvise(DWORD *pAspects, DWORD *pAdvf, IAdviseSink **ppAdvSink)
{
    if (pAspects)
        *pAspects = m_dwViewAdviseAspects;
    if (pAdvf)
        *pAdvf = m_dwViewAdviseFlags;
    if (ppAdvSink)
    {
        *ppAdvSink = m_pViewAdviseSink;
        if (*ppAdvSink)
            (*ppAdvSink)->AddRef();
    }
    return S_OK;
}

STDMETHODIMP CShellNameSpace::GetExtent(DWORD dwDrawAspect, LONG lindex, DVTARGETDEVICE *ptd, LPSIZEL lpsizel)
{
    return GetExtent(dwDrawAspect, lpsizel);
}

STDMETHODIMP CShellNameSpace::GetRect(DWORD dwAspect, LPRECTL pRect)
{
    return E_NOTIMPL;
}

STDMETHODIMP CShellNameSpace::GetViewStatus(DWORD *pdwStatus)
{
    if (!pdwStatus)
        return E_POINTER;
    *pdwStatus = VIEWSTATUS_OPAQUE | VIEWSTATUS_SOLIDBKGND;
    return S_OK;
}

STDMETHODIMP CShellNameSpace::QueryHitPoint(DWORD dwAspect, LPCRECT pRectBounds, POINT ptlLoc, LONG lCloseHint,
                                            DWORD *pHitResult)
{
    if (!pRectBounds || !pHitResult)
        return E_POINTER;
    if (dwAspect != DVASPECT_CONTENT)
        return DV_E_DVASPECT;
    *pHitResult = PtInRect(pRectBounds, ptlLoc) ? HITRESULT_HIT : HITRESULT_OUTSIDE;
    return S_OK;
}

STDMETHODIMP CShellNameSpace::QueryHitRect(DWORD dwAspect, LPCRECT pRectBounds, LPCRECT pRectLoc, LONG lCloseHint,
                                           DWORD *pHitResult)
{
    RECT rc;

    if (!pRectBounds || !pRectLoc || !pHitResult)
        return E_POINTER;
    if (dwAspect != DVASPECT_CONTENT)
        return DV_E_DVASPECT;
    *pHitResult = IntersectRect(&rc, pRectBounds, pRectLoc) ? HITRESULT_HIT : HITRESULT_OUTSIDE;
    return S_OK;
}

STDMETHODIMP CShellNameSpace::GetNaturalExtent(DWORD dwAspect, LONG lindex, DVTARGETDEVICE *ptd, HDC hicTargetDev,
                                               DVEXTENTINFO *pExtentInfo, LPSIZEL pSizel)
{
    return E_NOTIMPL;
}

STDMETHODIMP CShellNameSpace::InPlaceDeactivate()
{
    UIDeactivate();
    DestroyTreeWindow();
    if (m_pInPlaceSite)
    {
        m_pInPlaceSite->OnInPlaceDeactivate();
        m_pInPlaceSite.Release();
    }
    return S_OK;
}

STDMETHODIMP CShellNameSpace::UIDeactivate()
{
    if (m_bUIActive && m_pInPlaceSite)
    {
        m_bUIActive = FALSE;
        m_pInPlaceSite->OnUIDeactivate(FALSE);
    }
    return S_OK;
}

STDMETHODIMP CShellNameSpace::SetObjectRects(LPCRECT lprcPosRect, LPCRECT lprcClipRect)
{
    if (!lprcPosRect)
        return E_POINTER;
    if (m_Wnd.m_hWnd)
    {
        m_Wnd.SetWindowPos(NULL, lprcPosRect->left, lprcPosRect->top, lprcPosRect->right - lprcPosRect->left,
                           lprcPosRect->bottom - lprcPosRect->top, SWP_NOZORDER | SWP_NOACTIVATE);
    }
    return S_OK;
}

STDMETHODIMP CShellNameSpace::ReactivateAndUndo()
{
    return E_NOTIMPL;
}

STDMETHODIMP CShellNameSpace::OnWindowMessage(UINT msg, WPARAM wParam, LPARAM lParam, LRESULT *plResult)
{
    return E_NOTIMPL;
}

STDMETHODIMP CShellNameSpace::GetDropTarget(IDropTarget **ppDropTarget)
{
    return E_NOTIMPL;
}

STDMETHODIMP CShellNameSpace::DragEnter(IDataObject *pDataObj, DWORD grfKeyState, POINTL pt, DWORD *pdwEffect)
{
    if (!pdwEffect)
        return E_POINTER;
    *pdwEffect = DROPEFFECT_NONE;
    return S_OK;
}

STDMETHODIMP CShellNameSpace::DragOver(DWORD grfKeyState, POINTL pt, DWORD *pdwEffect)
{
    if (!pdwEffect)
        return E_POINTER;
    *pdwEffect = DROPEFFECT_NONE;
    return S_OK;
}

STDMETHODIMP CShellNameSpace::DragLeave()
{
    return S_OK;
}

STDMETHODIMP CShellNameSpace::Drop(IDataObject *pDataObj, DWORD grfKeyState, POINTL pt, DWORD *pdwEffect)
{
    if (!pdwEffect)
        return E_POINTER;
    *pdwEffect = DROPEFFECT_NONE;
    return S_OK;
}

STDMETHODIMP CShellNameSpace::CreateTree(HWND hwndParent, DWORD dwStyles, HWND *phwnd)
{
    HRESULT hr = CreateTreeWindow(hwndParent, NULL, dwStyles, 0);

    if (phwnd)
        *phwnd = SUCCEEDED(hr) ? m_Wnd.m_hWnd : NULL;
    return hr;
}

STDMETHODIMP CShellNameSpace::Initialize(PCIDLIST_ABSOLUTE pidlRoot, DWORD grfEnumFlags, DWORD dwFlags)
{
    m_pidlRoot.Free();
    if (pidlRoot)
    {
        m_pidlRoot.Attach(ILClone(pidlRoot));
        if (!m_pidlRoot)
            return E_OUTOFMEMORY;
    }
    m_lEnumOptions = grfEnumFlags;
    return ApplyRoot();
}

STDMETHODIMP CShellNameSpace::ShowWindow(BOOL fShow)
{
    if (m_Wnd.m_hWnd)
        m_Wnd.ShowWindow(fShow ? SW_SHOW : SW_HIDE);
    return S_OK;
}

STDMETHODIMP CShellNameSpace::Refresh()
{
    return ApplyRoot();
}

STDMETHODIMP CShellNameSpace::GetSelectedItem(PIDLIST_ABSOLUTE *ppidl, int nItem)
{
    if (!ppidl)
        return E_POINTER;
    return GetSelectedIDList(nItem, ppidl) == S_OK ? S_OK : S_FALSE;
}

STDMETHODIMP CShellNameSpace::SetSelectedItem(PCIDLIST_ABSOLUTE pidl, BOOL fCreate, BOOL fReinsert, int nItem)
{
    if (!pidl)
        return E_INVALIDARG;
    return SelectIDList(pidl);
}

STDMETHODIMP CShellNameSpace::GetNscMode(UINT *pnMode)
{
    if (!pnMode)
        return E_POINTER;
    *pnMode = m_uNscMode;
    return S_OK;
}

STDMETHODIMP CShellNameSpace::SetNscMode(UINT nMode)
{
    m_uNscMode = nMode;
    return S_OK;
}

STDMETHODIMP CShellNameSpace::GetSelectedItemName(LPWSTR pszName, DWORD cchName)
{
    CComPtr<IShellItem> psi;
    CComHeapPtr<WCHAR> pszDisplay;

    if (!pszName || !cchName || GetSelectedShellItem(0, &psi) != S_OK)
        return E_FAIL;
    if (FAILED(psi->GetDisplayName(SIGDN_NORMALDISPLAY, &pszDisplay)))
        return E_FAIL;
    return StringCchCopyW(pszName, cchName, pszDisplay);
}

STDMETHODIMP CShellNameSpace::BindToSelectedItemParent(REFIID riid, void **ppv, PITEMID_CHILD *ppidlLast)
{
    CComHeapPtr<ITEMIDLIST_ABSOLUTE> pidl;
    PCUITEMID_CHILD pidlChild;
    HRESULT hr;

    if (!ppv)
        return E_POINTER;
    *ppv = NULL;
    if (ppidlLast)
        *ppidlLast = NULL;
    if (GetSelectedIDList(0, &pidl) != S_OK)
        return E_FAIL;
    hr = SHBindToParent(pidl, riid, ppv, &pidlChild);
    if (SUCCEEDED(hr) && ppidlLast)
    {
        *ppidlLast = ILClone(pidlChild);
        if (!*ppidlLast)
        {
            ((IUnknown *)*ppv)->Release();
            *ppv = NULL;
            return E_OUTOFMEMORY;
        }
    }
    return hr;
}

STDMETHODIMP_(BOOL) CShellNameSpace::InLabelEdit()
{
    HWND hwndTree = GetTreeViewWindow();

    return hwndTree && TreeView_GetEditControl(hwndTree) != NULL;
}

STDMETHODIMP CShellNameSpace::RightPaneNavigationStarted(PCIDLIST_ABSOLUTE pidl)
{
    return S_OK;
}

STDMETHODIMP CShellNameSpace::RightPaneNavigationFinished(PCIDLIST_ABSOLUTE pidl)
{
    if (pidl && m_pTree)
        SelectIDList(pidl);
    return S_OK;
}

STDMETHODIMP CShellNameSpace::InsertMenusSB(HMENU hmenuShared, LPOLEMENUGROUPWIDTHS lpMenuWidths)
{
    return E_NOTIMPL;
}

STDMETHODIMP CShellNameSpace::SetMenuSB(HMENU hmenuShared, HOLEMENU holemenuRes, HWND hwndActiveObject)
{
    return E_NOTIMPL;
}

STDMETHODIMP CShellNameSpace::RemoveMenusSB(HMENU hmenuShared)
{
    return E_NOTIMPL;
}

STDMETHODIMP CShellNameSpace::SetStatusTextSB(LPCWSTR pszStatusText)
{
    return E_NOTIMPL;
}

STDMETHODIMP CShellNameSpace::EnableModelessSB(BOOL fEnable)
{
    return E_NOTIMPL;
}

STDMETHODIMP CShellNameSpace::TranslateAcceleratorSB(MSG *pmsg, WORD wID)
{
    return E_NOTIMPL;
}

STDMETHODIMP CShellNameSpace::BrowseObject(PCUIDLIST_RELATIVE pidl, UINT wFlags)
{
    return E_NOTIMPL;
}

STDMETHODIMP CShellNameSpace::GetViewStateStream(DWORD grfMode, IStream **ppStrm)
{
    return E_NOTIMPL;
}

STDMETHODIMP CShellNameSpace::GetControlWindow(UINT id, HWND *phwnd)
{
    return E_NOTIMPL;
}

STDMETHODIMP CShellNameSpace::SendControlMsg(UINT id, UINT uMsg, WPARAM wParam, LPARAM lParam, LRESULT *pret)
{
    return E_NOTIMPL;
}

STDMETHODIMP CShellNameSpace::QueryActiveShellView(IShellView **ppshv)
{
    return E_NOTIMPL;
}

STDMETHODIMP CShellNameSpace::OnViewWindowActive(IShellView *pshv)
{
    return E_NOTIMPL;
}

STDMETHODIMP CShellNameSpace::SetToolbarItems(LPTBBUTTONSB lpButtons, UINT nButtons, UINT uFlags)
{
    return E_NOTIMPL;
}

STDMETHODIMP CShellNameSpace::SetFilter(IUnknown *punk)
{
    CComPtr<IFolderFilterSite> pSite;

    m_pFilter = punk;
    if (m_pTree && SUCCEEDED(m_pTree->QueryInterface(IID_PPV_ARG(IFolderFilterSite, &pSite))))
        pSite->SetFilter(punk);
    return S_OK;
}

STDMETHODIMP CShellNameSpace::IncludeItems(NMCII_FLAGS *pflags)
{
    if (!pflags)
        return E_POINTER;
    *pflags = NMCII_FOLDERS;
    return S_OK;
}

STDMETHODIMP CShellNameSpace::SelectAndEditItem(PCIDLIST_ABSOLUTE pidlItem, NMCSAEI_FLAGS flags)
{
    HWND hwndTree;
    HTREEITEM hItem;

    if (!pidlItem || !m_pTree)
        return S_OK;
    if (FAILED(SelectIDList(pidlItem)))
        return S_OK;
    if (flags & NMCSAEI_EDIT)
    {
        hwndTree = GetTreeViewWindow();
        if (hwndTree && (hItem = TreeView_GetSelection(hwndTree)) != NULL)
            TreeView_EditLabel(hwndTree, hItem);
    }
    return S_OK;
}

STDMETHODIMP CShellNameSpace::QueryService(REFGUID guidService, REFIID riid, void **ppvObject)
{
    if (!ppvObject)
        return E_POINTER;
    *ppvObject = NULL;
    if (m_pUnkSite)
        return IUnknown_QueryService(m_pUnkSite, guidService, riid, ppvObject);
    if (m_pClientSite)
        return IUnknown_QueryService(m_pClientSite, guidService, riid, ppvObject);
    return E_FAIL;
}

STDMETHODIMP CShellNameSpace::Initialize(HWND hwndParent, RECT *prc, NSTCSTYLE nstcsFlags)
{
    return CreateTreeWindow(hwndParent, prc, 0, 0);
}

STDMETHODIMP CShellNameSpace::TreeAdvise(IUnknown *punk, DWORD *pdwCookie)
{
    if (!m_pTree)
        return E_UNEXPECTED;
    return m_pTree->TreeAdvise(punk, pdwCookie);
}

STDMETHODIMP CShellNameSpace::TreeUnadvise(DWORD dwCookie)
{
    if (!m_pTree)
        return E_UNEXPECTED;
    return m_pTree->TreeUnadvise(dwCookie);
}

STDMETHODIMP CShellNameSpace::AppendRoot(IShellItem *psiRoot, SHCONTF grfEnumFlags, NSTCROOTSTYLE grfRootStyle,
                                         IShellItemFilter *pif)
{
    if (!m_pTree)
        return E_UNEXPECTED;
    return m_pTree->AppendRoot(psiRoot, grfEnumFlags, grfRootStyle, pif);
}

STDMETHODIMP CShellNameSpace::InsertRoot(int iIndex, IShellItem *psiRoot, SHCONTF grfEnumFlags, NSTCROOTSTYLE grfRootStyle,
                                         IShellItemFilter *pif)
{
    if (!m_pTree)
        return E_UNEXPECTED;
    return m_pTree->InsertRoot(iIndex, psiRoot, grfEnumFlags, grfRootStyle, pif);
}

STDMETHODIMP CShellNameSpace::RemoveRoot(IShellItem *psiRoot)
{
    if (!m_pTree)
        return E_INVALIDARG;
    return m_pTree->RemoveRoot(psiRoot);
}

STDMETHODIMP CShellNameSpace::RemoveAllRoots()
{
    if (!m_pTree)
        return E_INVALIDARG;
    return m_pTree->RemoveAllRoots();
}

STDMETHODIMP CShellNameSpace::GetRootItems(IShellItemArray **ppsiaRootItems)
{
    if (!ppsiaRootItems)
        return E_POINTER;
    if (!m_pTree)
    {
        *ppsiaRootItems = NULL;
        return E_INVALIDARG;
    }
    return m_pTree->GetRootItems(ppsiaRootItems);
}

STDMETHODIMP CShellNameSpace::SetItemState(IShellItem *psi, NSTCITEMSTATE nstcisMask, NSTCITEMSTATE nstcisFlags)
{
    if (!m_pTree)
        return E_UNEXPECTED;
    return m_pTree->SetItemState(psi, nstcisMask, nstcisFlags);
}

STDMETHODIMP CShellNameSpace::GetItemState(IShellItem *psi, NSTCITEMSTATE nstcismask, NSTCITEMSTATE *pnstcisFlags)
{
    if (!m_pTree)
        return E_UNEXPECTED;
    return m_pTree->GetItemState(psi, nstcismask, pnstcisFlags);
}

STDMETHODIMP CShellNameSpace::GetSelectedItems(IShellItemArray **psiaItems)
{
    if (!m_pTree)
        return E_FAIL;
    return m_pTree->GetSelectedItems(psiaItems);
}

STDMETHODIMP CShellNameSpace::GetItemCustomState(IShellItem *psi, int *piStateNumber)
{
    if (!m_pTree)
        return E_UNEXPECTED;
    return m_pTree->GetItemCustomState(psi, piStateNumber);
}

STDMETHODIMP CShellNameSpace::SetItemCustomState(IShellItem *psi, int iStateNumber)
{
    if (!m_pTree)
        return E_UNEXPECTED;
    return m_pTree->SetItemCustomState(psi, iStateNumber);
}

STDMETHODIMP CShellNameSpace::EnsureItemVisible(IShellItem *psi)
{
    if (!m_pTree)
        return E_UNEXPECTED;
    return m_pTree->EnsureItemVisible(psi);
}

STDMETHODIMP CShellNameSpace::SetTheme(LPCWSTR pszTheme)
{
    if (!m_pTree)
        return E_UNEXPECTED;
    return m_pTree->SetTheme(pszTheme);
}

STDMETHODIMP CShellNameSpace::GetNextItem(IShellItem *psi, NSTCGNI nstcgi, IShellItem **ppsiNext)
{
    if (!m_pTree)
        return E_UNEXPECTED;
    return m_pTree->GetNextItem(psi, nstcgi, ppsiNext);
}

STDMETHODIMP CShellNameSpace::HitTest(POINT *ppt, IShellItem **ppsiOut)
{
    if (!m_pTree)
        return E_UNEXPECTED;
    return m_pTree->HitTest(ppt, ppsiOut);
}

STDMETHODIMP CShellNameSpace::GetItemRect(IShellItem *psi, RECT *prect)
{
    if (!m_pTree)
        return E_UNEXPECTED;
    return m_pTree->GetItemRect(psi, prect);
}

STDMETHODIMP CShellNameSpace::CollapseAll()
{
    if (!m_pTree)
        return E_UNEXPECTED;
    return m_pTree->CollapseAll();
}

STDMETHODIMP CShellNameSpace::SetWatermark(HBITMAP hbmp, VPWATERMARKFLAGS vpwf)
{
    return E_NOTIMPL;
}

STDMETHODIMP CShellNameSpace::SetColor(VPCOLORFLAGS vpcf, COLORREF cr)
{
    HWND hwndTree = GetTreeViewWindow();

    switch (vpcf)
    {
        case VPCF_TEXT:
            m_crText = cr;
            if (hwndTree)
                TreeView_SetTextColor(hwndTree, cr);
            break;

        case VPCF_BACKGROUND:
            m_crBackground = cr;
            if (hwndTree)
                TreeView_SetBkColor(hwndTree, cr);
            break;

        default:
            break;
    }
    return S_OK;
}

STDMETHODIMP CShellNameSpace::GetColor(VPCOLORFLAGS vpcf, COLORREF *pcr)
{
    if (!pcr)
        return E_POINTER;
    switch (vpcf)
    {
        case VPCF_TEXT:
            *pcr = m_crText;
            break;

        case VPCF_BACKGROUND:
            *pcr = m_crBackground;
            break;

        default:
            *pcr = 0;
            break;
    }
    return S_OK;
}

STDMETHODIMP CShellNameSpace::SetItemHeight(int cyItemInPixels)
{
    HWND hwndTree = GetTreeViewWindow();

    m_cyItem = cyItemInPixels;
    if (hwndTree && cyItemInPixels > 0)
        TreeView_SetItemHeight(hwndTree, cyItemInPixels);
    return S_OK;
}

STDMETHODIMP CShellNameSpace::GetItemHeight(int *cyItemInPixels)
{
    if (!cyItemInPixels)
        return E_POINTER;
    *cyItemInPixels = m_cyItem;
    return S_OK;
}

STDMETHODIMP CShellNameSpace::SetFont(const LOGFONTW *plf, BOOL bRedraw)
{
    HWND hwndTree = GetTreeViewWindow();
    HFONT hFont;

    if (!plf)
        return E_INVALIDARG;
    if (!hwndTree)
        return S_OK;
    hFont = CreateFontIndirectW(plf);
    if (!hFont)
        return E_OUTOFMEMORY;
    ::SendMessageW(hwndTree, WM_SETFONT, (WPARAM)hFont, bRedraw);
    if (m_hFont)
        DeleteObject(m_hFont);
    m_hFont = hFont;
    return S_OK;
}

STDMETHODIMP CShellNameSpace::GetFont(LOGFONTW *plf)
{
    return E_NOTIMPL;
}

STDMETHODIMP CShellNameSpace::SetTheme(LPCWSTR pszSubAppName, LPCWSTR pszSubIdList)
{
    return E_NOTIMPL;
}

STDMETHODIMP CShellNameSpace::OnWinEvent(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, LRESULT *theResult)
{
    return E_FAIL;
}

STDMETHODIMP CShellNameSpace::IsWindowOwner(HWND hWnd)
{
    return E_NOTIMPL;
}

OBJECT_ENTRY_AUTO(CLSID_ShellNameSpace, CShellNameSpace)
