/*
 *    ShellFolderViewDual
 *
 *    Copyright 2016         Mark Jansen
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

#include "precomp.h"

class CDefViewDual :
    public CComCoClass<CDefViewDual, &CLSID_ShellFolderView>,
    public CComObjectRootEx<CComMultiThreadModelNoCS>,
    public IDispatchImpl<IShellFolderViewDual3, &IID_IShellFolderViewDual3>,
    public IShellService,
    public IServiceProvider,
    public IObjectSafety,
    public IObjectWithSite,
    public IConnectionPointContainerImpl<CDefViewDual>,
    public IConnectionPointImpl<CDefViewDual, &DIID_DShellFolderViewEvents>
{
    private:
        CComPtr<IShellView> m_pView;
        CComPtr<IUnknown> m_pSite;
        DWORD m_SafetyOptions;

        HRESULT GetFolderView(REFIID riid, void **ppv)
        {
            *ppv = NULL;
            if (!m_pView)
                return E_FAIL;
            return m_pView->QueryInterface(riid, ppv);
        }

        HRESULT GetFolderPidl(PIDLIST_ABSOLUTE *ppidl)
        {
            CComPtr<IFolderView> pfv;
            CComPtr<IPersistFolder2> ppf2;
            HRESULT hr = GetFolderView(IID_PPV_ARG(IFolderView, &pfv));
            if (FAILED(hr))
                return hr;
            hr = pfv->GetFolder(IID_PPV_ARG(IPersistFolder2, &ppf2));
            if (FAILED(hr))
                return hr;
            return ppf2->GetCurFolder(ppidl);
        }

        HRESULT GetFolderObject(Folder **ppFolder)
        {
            CComHeapPtr<ITEMIDLIST> pidl;
            CComPtr<IDispatch> pApplication;
            HRESULT hr = GetFolderPidl(&pidl);
            if (FAILED(hr))
                return hr;
            hr = CShellDispatch_Constructor(IID_PPV_ARG(IDispatch, &pApplication));
            if (FAILED(hr))
                return hr;
            return CFolder::CreateInstance(pidl, pApplication, IID_PPV_ARG(Folder, ppFolder));
        }

        HRESULT GetItemFromVariant(const VARIANT *pv, IShellFolder **ppsf, PITEMID_CHILD *ppidl)
        {
            CComPtr<IParentAndItem> pai;
            HRESULT hr = CFolderItem::GetParentAndItem(pv, &pai);
            if (FAILED(hr))
                return hr;
            return pai->GetParentAndItem(NULL, ppsf, ppidl);
        }

    public:
        CDefViewDual() : m_SafetyOptions(0)
        {
        }

        // *** IShellFolderViewDual methods ***
        STDMETHOD(get_Application)(IDispatch **app) override
        {
            if (!app)
                return E_POINTER;
            *app = NULL;
            if (!m_pView)
                return E_FAIL;
            return CShellDispatch_Constructor(IID_PPV_ARG(IDispatch, app));
        }

        STDMETHOD(get_Parent)(IDispatch **parent) override
        {
            if (!parent)
                return E_POINTER;
            *parent = NULL;
            return E_FAIL;
        }

        STDMETHOD(get_Folder)(Folder **folder) override
        {
            if (!folder)
                return E_POINTER;
            *folder = NULL;
            return GetFolderObject(folder);
        }

        STDMETHOD(SelectedItems)(FolderItems **items) override
        {
            CComPtr<IFolderView> pfv;
            CComPtr<IEnumIDList> pEnum;
            CComHeapPtr<ITEMIDLIST> pidlFolder;
            CSimpleArray<PITEMID_CHILD> pidls;
            CComObject<CFolderItemsFDF> *pObj;
            CComPtr<IUnknown> pItems;
            PITEMID_CHILD pidl;
            HRESULT hr;

            if (!items)
                return E_POINTER;
            *items = NULL;

            hr = GetFolderView(IID_PPV_ARG(IFolderView, &pfv));
            if (FAILED(hr))
                return hr;
            hr = GetFolderPidl(&pidlFolder);
            if (FAILED(hr))
                return hr;
            hr = pfv->Items(SVGIO_SELECTION, IID_PPV_ARG(IEnumIDList, &pEnum));
            if (FAILED(hr))
                return hr;
            while (pEnum->Next(1, &pidl, NULL) == S_OK)
            {
                if (!pidls.Add(pidl))
                {
                    ILFree(pidl);
                    hr = E_OUTOFMEMORY;
                    break;
                }
            }

            if (SUCCEEDED(hr))
                hr = CComObject<CFolderItemsFDF>::CreateInstance(&pObj);
            if (SUCCEEDED(hr))
            {
                pItems = static_cast<IPersistFolder *>(pObj);
                hr = pObj->SetItems(pidlFolder, pidls.GetSize(), (PCUITEMID_CHILD_ARRAY)pidls.GetData());
            }
            if (SUCCEEDED(hr))
                hr = pItems->QueryInterface(IID_PPV_ARG(FolderItems, items));

            for (int i = 0; i < pidls.GetSize(); i++)
                ILFree(pidls[i]);
            return hr;
        }

        STDMETHOD(get_FocusedItem)(FolderItem **item) override
        {
            CComPtr<IFolderView> pfv;
            CComPtr<Folder> pFolder;
            CComHeapPtr<ITEMID_CHILD> pidlChild;
            CComHeapPtr<ITEMIDLIST> pidlFolder, pidlFull;
            int index;
            HRESULT hr;

            if (!item)
                return E_POINTER;
            *item = NULL;

            hr = GetFolderView(IID_PPV_ARG(IFolderView, &pfv));
            if (FAILED(hr))
                return hr;
            if (FAILED(pfv->GetFocusedItem(&index)) || index < 0)
                return S_FALSE;
            hr = pfv->Item(index, &pidlChild);
            if (FAILED(hr))
                return hr;
            hr = GetFolderPidl(&pidlFolder);
            if (FAILED(hr))
                return hr;
            hr = SHILCombine(pidlFolder, pidlChild, &pidlFull);
            if (FAILED(hr))
                return hr;
            hr = GetFolderObject(&pFolder);
            if (FAILED(hr))
                return hr;
            return ShellObjectCreatorInit<CFolderItem>(static_cast<Folder*>(pFolder), static_cast<LPCITEMIDLIST>(pidlFull), IID_PPV_ARG(FolderItem, item));
        }

        STDMETHOD(SelectItem)(VARIANT *item, int flags) override
        {
            CComPtr<IFolderView> pfv;
            CComPtr<IShellFolder> psf;
            CComHeapPtr<ITEMID_CHILD> pidl;
            CComVariant var;
            HRESULT hr;

            if (!m_pView)
                return E_FAIL;
            if (!item)
                return E_INVALIDARG;

            hr = VariantCopyInd(&var, item);
            if (FAILED(hr))
                return hr;

            switch (V_VT(&var))
            {
                case VT_EMPTY:
                case VT_NULL:
                    return m_pView->SelectItem(NULL, flags);
                case VT_I2:
                case VT_I4:
                    hr = VariantChangeType(&var, &var, 0, VT_I4);
                    if (FAILED(hr))
                        return hr;
                    hr = GetFolderView(IID_PPV_ARG(IFolderView, &pfv));
                    if (FAILED(hr))
                        return hr;
                    return pfv->SelectItem(V_I4(&var), flags);
                case VT_BSTR:
                    hr = GetFolderView(IID_PPV_ARG(IFolderView, &pfv));
                    if (FAILED(hr))
                        return hr;
                    hr = pfv->GetFolder(IID_PPV_ARG(IShellFolder, &psf));
                    if (FAILED(hr))
                        return hr;
                    hr = psf->ParseDisplayName(NULL, NULL, V_BSTR(&var), NULL, (PIDLIST_RELATIVE *)&pidl, NULL);
                    if (FAILED(hr))
                        return hr;
                    break;
                default:
                    hr = GetItemFromVariant(&var, NULL, &pidl);
                    if (FAILED(hr))
                        return hr;
                    break;
            }
            return m_pView->SelectItem(pidl, flags);
        }

        STDMETHOD(PopupItemMenu)(FolderItem *item, VARIANT vx, VARIANT vy, BSTR *command) override
        {
            CComPtr<IShellFolder> psf;
            CComPtr<IContextMenu> pcm;
            CComHeapPtr<ITEMID_CHILD> pidl;
            CComVariant var(item), x, y;
            WCHAR verb[MAX_PATH];
            HWND hwnd = NULL;
            POINT pt;
            HRESULT hr;

            if (!command)
                return E_POINTER;
            *command = NULL;
            if (!m_pView)
                return E_FAIL;

            hr = GetItemFromVariant(&var, &psf, &pidl);
            if (FAILED(hr))
                return hr;
            IUnknown_GetWindow(m_pView, &hwnd);
            PCUITEMID_CHILD child = pidl;
            hr = psf->GetUIObjectOf(hwnd, 1, &child, IID_IContextMenu, NULL, (void **)&pcm);
            if (FAILED(hr))
                return hr;

            GetCursorPos(&pt);
            if (SUCCEEDED(VariantChangeType(&x, &vx, 0, VT_I4)) && SUCCEEDED(VariantChangeType(&y, &vy, 0, VT_I4)))
            {
                pt.x = V_I4(&x);
                pt.y = V_I4(&y);
            }

            HMENU hMenu = CreatePopupMenu();
            if (!hMenu)
                return E_OUTOFMEMORY;
            hr = pcm->QueryContextMenu(hMenu, 0, 1, 0x7fff, CMF_NORMAL);
            if (SUCCEEDED(hr))
            {
                UINT id = TrackPopupMenuEx(hMenu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, hwnd, NULL);
                if (!id)
                    hr = S_FALSE;
                else if (SUCCEEDED(pcm->GetCommandString(id - 1, GCS_VERBW, NULL, (LPSTR)verb, _countof(verb))))
                    *command = SysAllocString(verb);
                else
                    hr = S_FALSE;
            }
            DestroyMenu(hMenu);
            if (hr == S_OK && !*command)
                return E_OUTOFMEMORY;
            return hr;
        }

        STDMETHOD(get_Script)(IDispatch **script) override
        {
            if (!script)
                return E_POINTER;
            *script = NULL;
            return S_FALSE;
        }

        STDMETHOD(get_ViewOptions)(long *options) override
        {
            SHELLSTATE ss = {};

            if (!options)
                return E_POINTER;
            SHGetSetSettings(&ss, SSF_SHOWALLOBJECTS | SSF_SHOWEXTENSIONS | SSF_SHOWCOMPCOLOR | SSF_SHOWSYSFILES |
                             SSF_WIN95CLASSIC | SSF_DOUBLECLICKINWEBVIEW | SSF_DESKTOPHTML, FALSE);
            *options = 0;
            if (ss.fShowAllObjects)
                *options |= SFVVO_SHOWALLOBJECTS;
            if (ss.fShowExtensions)
                *options |= SFVVO_SHOWEXTENSIONS;
            if (ss.fShowCompColor)
                *options |= SFVVO_SHOWCOMPCOLOR;
            if (ss.fShowSysFiles)
                *options |= SFVVO_SHOWSYSFILES;
            if (ss.fWin95Classic)
                *options |= SFVVO_WIN95CLASSIC;
            if (ss.fDoubleClickInWebView)
                *options |= SFVVO_DOUBLECLICKINWEBVIEW;
            if (ss.fDesktopHTML)
                *options |= SFVVO_DESKTOPHTML;
            return S_OK;
        }

        // *** IShellFolderViewDual2 methods ***
        STDMETHOD(get_CurrentViewMode)(UINT *mode) override
        {
            CComPtr<IFolderView> pfv;

            if (!mode)
                return E_POINTER;
            if (FAILED(GetFolderView(IID_PPV_ARG(IFolderView, &pfv))))
                return S_FALSE;
            return pfv->GetCurrentViewMode(mode);
        }

        STDMETHOD(put_CurrentViewMode)(UINT mode) override
        {
            CComPtr<IFolderView> pfv;

            if (FAILED(GetFolderView(IID_PPV_ARG(IFolderView, &pfv))))
                return S_FALSE;
            return pfv->SetCurrentViewMode(mode);
        }

        STDMETHOD(SelectItemRelative)(int relative) override
        {
            CComPtr<IFolderView> pfv;
            int index, count;

            if (FAILED(GetFolderView(IID_PPV_ARG(IFolderView, &pfv))))
                return S_FALSE;
            if (FAILED(pfv->ItemCount(SVGIO_ALLVIEW, &count)) || !count)
                return S_FALSE;
            if (FAILED(pfv->GetFocusedItem(&index)) || index < 0)
                index = 0;
            index += relative;
            if (index < 0)
                index = 0;
            if (index >= count)
                index = count - 1;
            return pfv->SelectItem(index, SVSI_SELECT | SVSI_DESELECTOTHERS | SVSI_FOCUSED | SVSI_ENSUREVISIBLE);
        }

        // *** IShellFolderViewDual3 methods ***
        STDMETHOD(get_GroupBy)(BSTR *groupby) override
        {
            CComPtr<IFolderView2> pfv2;
            CComHeapPtr<WCHAR> name;
            PROPERTYKEY key;
            BOOL ascending = TRUE;
            HRESULT hr;

            if (!groupby)
                return E_POINTER;
            *groupby = NULL;
            if (FAILED(GetFolderView(IID_PPV_ARG(IFolderView2, &pfv2))))
                return E_UNEXPECTED;
            hr = pfv2->GetGroupBy(&key, &ascending);
            if (FAILED(hr))
                return hr;
            if (IsEqualGUID(key.fmtid, GUID_NULL) && key.pid == 0)
            {
                *groupby = SysAllocString(L"System.Null");
            }
            else
            {
                hr = PSGetNameFromPropertyKey(key, &name);
                if (FAILED(hr))
                    return hr;
                CStringW str = ascending ? L"" : L"-";
                str += name;
                *groupby = SysAllocString(str);
            }
            return *groupby ? S_OK : E_OUTOFMEMORY;
        }

        STDMETHOD(put_GroupBy)(BSTR groupby) override
        {
            CComPtr<IFolderView2> pfv2;
            PROPERTYKEY key = {};
            BOOL ascending = TRUE;
            HRESULT hr;

            if (FAILED(GetFolderView(IID_PPV_ARG(IFolderView2, &pfv2))))
                return E_UNEXPECTED;
            if (groupby && *groupby == L'-')
            {
                ascending = FALSE;
                groupby++;
            }
            if (groupby && *groupby && lstrcmpiW(groupby, L"System.Null"))
            {
                hr = PSGetPropertyKeyFromName(groupby, &key);
                if (FAILED(hr))
                    return hr;
            }
            return pfv2->SetGroupBy(key, ascending);
        }

        STDMETHOD(get_FolderFlags)(DWORD *flags) override
        {
            CComPtr<IFolderView2> pfv2;

            if (!flags)
                return E_POINTER;
            if (FAILED(GetFolderView(IID_PPV_ARG(IFolderView2, &pfv2))))
                return E_UNEXPECTED;
            return pfv2->GetCurrentFolderFlags(flags);
        }

        STDMETHOD(put_FolderFlags)(DWORD flags) override
        {
            CComPtr<IFolderView2> pfv2;

            if (FAILED(GetFolderView(IID_PPV_ARG(IFolderView2, &pfv2))))
                return E_UNEXPECTED;
            return pfv2->SetCurrentFolderFlags(~0u, flags);
        }

        STDMETHOD(get_SortColumns)(BSTR *sortcolumns) override
        {
            CComPtr<IFolderView2> pfv2;
            CStringW str(L"prop:");
            SORTCOLUMN *cols;
            int count = 0;
            HRESULT hr;

            if (!sortcolumns)
                return E_POINTER;
            *sortcolumns = NULL;
            if (FAILED(GetFolderView(IID_PPV_ARG(IFolderView2, &pfv2))))
                return E_UNEXPECTED;
            hr = pfv2->GetSortColumnCount(&count);
            if (FAILED(hr))
                return hr;
            if (count > 0)
            {
                cols = (SORTCOLUMN *)LocalAlloc(LMEM_FIXED, count * sizeof(*cols));
                if (!cols)
                    return E_OUTOFMEMORY;
                hr = pfv2->GetSortColumns(cols, count);
                for (int i = 0; SUCCEEDED(hr) && i < count; i++)
                {
                    CComHeapPtr<WCHAR> name;
                    hr = PSGetNameFromPropertyKey(cols[i].propkey, &name);
                    if (SUCCEEDED(hr))
                    {
                        if (cols[i].direction == SORT_DESCENDING)
                            str += L"-";
                        str += name;
                        str += L";";
                    }
                }
                LocalFree(cols);
                if (FAILED(hr))
                    return hr;
            }
            *sortcolumns = SysAllocString(str);
            return *sortcolumns ? S_OK : E_OUTOFMEMORY;
        }

        STDMETHOD(put_SortColumns)(BSTR sortcolumns) override
        {
            CComPtr<IFolderView2> pfv2;
            CSimpleArray<SORTCOLUMN> cols;
            HRESULT hr;

            if (!sortcolumns || StrCmpNIW(sortcolumns, L"prop:", 5))
                return E_INVALIDARG;
            if (FAILED(GetFolderView(IID_PPV_ARG(IFolderView2, &pfv2))))
                return E_UNEXPECTED;

            CStringW list(sortcolumns + 5);
            int pos = 0;
            CStringW token = list.Tokenize(L";", pos);
            while (!token.IsEmpty())
            {
                SORTCOLUMN col;
                LPCWSTR name = token;
                col.direction = SORT_ASCENDING;
                if (*name == L'-' || *name == L'+')
                {
                    if (*name == L'-')
                        col.direction = SORT_DESCENDING;
                    name++;
                }
                hr = PSGetPropertyKeyFromName(name, &col.propkey);
                if (FAILED(hr))
                    return hr;
                if (!cols.Add(col))
                    return E_OUTOFMEMORY;
                token = list.Tokenize(L";", pos);
            }
            if (!cols.GetSize())
                return E_INVALIDARG;
            return pfv2->SetSortColumns(cols.GetData(), cols.GetSize());
        }

        STDMETHOD(put_IconSize)(int icon_size) override
        {
            CComPtr<IFolderView2> pfv2;
            FOLDERVIEWMODE mode;
            HRESULT hr;

            if (FAILED(GetFolderView(IID_PPV_ARG(IFolderView2, &pfv2))))
                return E_UNEXPECTED;
            hr = pfv2->GetViewModeAndIconSize(&mode, NULL);
            if (FAILED(hr))
                return hr;
            return pfv2->SetViewModeAndIconSize(mode, icon_size);
        }

        STDMETHOD(get_IconSize)(int *icon_size) override
        {
            CComPtr<IFolderView2> pfv2;

            if (!icon_size)
                return E_POINTER;
            if (FAILED(GetFolderView(IID_PPV_ARG(IFolderView2, &pfv2))))
                return E_UNEXPECTED;
            return pfv2->GetViewModeAndIconSize(NULL, icon_size);
        }

        STDMETHOD(FilterView)(BSTR filter_text) override
        {
            if (!filter_text)
                return E_INVALIDARG;
            if (!m_pView)
                return E_UNEXPECTED;
            return E_NOTIMPL;
        }

        // *** IShellService methods ***
        STDMETHOD(SetOwner)(IUnknown *punkOwner) override
        {
            m_pView.Release();
            if (punkOwner)
                return punkOwner->QueryInterface(IID_PPV_ARG(IShellView, &m_pView));
            return S_OK;
        }

        // *** IServiceProvider methods ***
        STDMETHOD(QueryService)(REFGUID guidService, REFIID riid, void **ppvObject) override
        {
            if (!ppvObject)
                return E_POINTER;
            *ppvObject = NULL;
            if (m_pView)
                return IUnknown_QueryService(m_pView, guidService, riid, ppvObject);
            if (m_pSite)
                return IUnknown_QueryService(m_pSite, guidService, riid, ppvObject);
            return E_FAIL;
        }

        // *** IObjectSafety methods ***
        STDMETHOD(GetInterfaceSafetyOptions)(REFIID riid, DWORD *pdwSupportedOptions, DWORD *pdwEnabledOptions) override
        {
            CComPtr<IUnknown> pUnk;

            if (!pdwSupportedOptions || !pdwEnabledOptions)
                return E_POINTER;
            if (FAILED(QueryInterface(riid, (void **)&pUnk)))
                return E_NOINTERFACE;
            *pdwSupportedOptions = INTERFACESAFE_FOR_UNTRUSTED_CALLER | INTERFACESAFE_FOR_UNTRUSTED_DATA;
            *pdwEnabledOptions = m_SafetyOptions;
            return S_OK;
        }

        STDMETHOD(SetInterfaceSafetyOptions)(REFIID riid, DWORD dwOptionSetMask, DWORD dwEnabledOptions) override
        {
            CComPtr<IUnknown> pUnk;

            if (FAILED(QueryInterface(riid, (void **)&pUnk)))
                return E_NOINTERFACE;
            if (dwOptionSetMask & ~(INTERFACESAFE_FOR_UNTRUSTED_CALLER | INTERFACESAFE_FOR_UNTRUSTED_DATA))
                return E_FAIL;
            m_SafetyOptions = (m_SafetyOptions & ~dwOptionSetMask) | (dwEnabledOptions & dwOptionSetMask);
            return S_OK;
        }

        // *** IObjectWithSite methods ***
        STDMETHOD(SetSite)(IUnknown *pUnkSite) override
        {
            m_pSite = pUnkSite;
            return S_OK;
        }

        STDMETHOD(GetSite)(REFIID riid, void **ppvSite) override
        {
            if (!ppvSite)
                return E_POINTER;
            *ppvSite = NULL;
            if (!m_pSite)
                return E_FAIL;
            return m_pSite->QueryInterface(riid, ppvSite);
        }

    DECLARE_REGISTRY_RESOURCEID(IDR_SHELLFOLDERVIEW)
    DECLARE_NOT_AGGREGATABLE(CDefViewDual)
    DECLARE_PROTECT_FINAL_CONSTRUCT()

    BEGIN_COM_MAP(CDefViewDual)
        COM_INTERFACE_ENTRY_IID(IID_IDispatch, IDispatch)
        COM_INTERFACE_ENTRY_IID(IID_IShellFolderViewDual, IShellFolderViewDual)
        COM_INTERFACE_ENTRY_IID(IID_IShellFolderViewDual2, IShellFolderViewDual2)
        COM_INTERFACE_ENTRY_IID(IID_IShellFolderViewDual3, IShellFolderViewDual3)
        COM_INTERFACE_ENTRY_IID(IID_IShellService, IShellService)
        COM_INTERFACE_ENTRY_IID(IID_IServiceProvider, IServiceProvider)
        COM_INTERFACE_ENTRY_IID(IID_IObjectSafety, IObjectSafety)
        COM_INTERFACE_ENTRY_IID(IID_IObjectWithSite, IObjectWithSite)
        COM_INTERFACE_ENTRY_IID(IID_IConnectionPointContainer, IConnectionPointContainer)
    END_COM_MAP()

    BEGIN_CONNECTION_POINT_MAP(CDefViewDual)
        CONNECTION_POINT_ENTRY(DIID_DShellFolderViewEvents)
    END_CONNECTION_POINT_MAP()
};

OBJECT_ENTRY_AUTO(CLSID_ShellFolderView, CDefViewDual)

/**********************************************************
 *    CDefViewDual_Constructor
 */

HRESULT WINAPI CDefViewDual_Constructor(REFIID riid, LPVOID * ppvOut)
{
    return ShellObjectCreator<CDefViewDual>(riid, ppvOut);
}
