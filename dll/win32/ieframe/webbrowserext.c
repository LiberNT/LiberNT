/*
 * PROJECT:     LiberNT IE Frame
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     WebBrowser control persistence, safety and shell service interfaces
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "ieframe.h"

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(ieframe);

#define WB_STREAM_SIZE_MAX   0x2020
#define WB_STREAM_MARKER     0x4c

#define WB_FLAG_OFFLINE      0x1
#define WB_FLAG_SILENT       0x2
#define WB_FLAG_REGISTER     0x4

#define WB_DEFAULT_WIDTH     300
#define WB_DEFAULT_HEIGHT    150

#include <pshpack1.h>
typedef struct
{
    DWORD cbSize;
    SIZEL extent;
    DWORD view_mode;
    DWORD folder_flags;
    BYTE reserved1[16];
    DWORD marker;
    DWORD reserved2;
    DWORD reserved3;
    BYTE reserved4[20];
    DWORD browser_flags;
    DWORD reserved5;
} WB_PERSIST_HEADER;
#include <poppack.h>

C_ASSERT(sizeof(WB_PERSIST_HEADER) == 76);

static const struct
{
    const WCHAR *name;
    DWORD flag;
} folder_flag_props[] =
{
    { L"AutoArrange",     FWF_AUTOARRANGE },
    { L"NoClientEdge",    FWF_NOCLIENTEDGE },
    { L"AlignLeft",       FWF_ALIGNLEFT },
    { L"NoWebView",       FWF_NOWEBVIEW },
    { L"HideFileNames",   FWF_HIDEFILENAMES },
    { L"SingleClick",     FWF_SINGLECLICKACTIVATE },
    { L"SingleSelection", FWF_SINGLESEL },
    { L"NoFolders",       FWF_NOSUBFOLDERS },
    { L"Transparent",     FWF_TRANSPARENT },
};

static void get_screen_dpi(int *dpi_x, int *dpi_y)
{
    HDC hdc = GetDC(NULL);
    *dpi_x = GetDeviceCaps(hdc, LOGPIXELSX);
    *dpi_y = GetDeviceCaps(hdc, LOGPIXELSY);
    ReleaseDC(NULL, hdc);
}

void WebBrowser_Ext_InitNew(WebBrowser *This)
{
    int dpi_x, dpi_y;

    get_screen_dpi(&dpi_x, &dpi_y);
    This->extent.cx = MulDiv(WB_DEFAULT_WIDTH, 2540, dpi_x);
    This->extent.cy = MulDiv(WB_DEFAULT_HEIGHT, 2540, dpi_y);
    This->view_mode = FVM_ICON;
    This->folder_flags = FWF_AUTOARRANGE | FWF_NOCLIENTEDGE;
    This->doc_host.offline = VARIANT_FALSE;
    This->doc_host.silent = VARIANT_FALSE;
    This->register_browser = This->version == 1 ? VARIANT_TRUE : VARIANT_FALSE;
    This->register_drop_target = VARIANT_FALSE;
}

static DWORD get_browser_flags(WebBrowser *This)
{
    DWORD flags = 0;

    if (This->doc_host.offline)
        flags |= WB_FLAG_OFFLINE;
    if (This->doc_host.silent)
        flags |= WB_FLAG_SILENT;
    if (This->register_browser)
        flags |= WB_FLAG_REGISTER;
    return flags;
}

HRESULT WebBrowser_Ext_SaveStream(WebBrowser *This, IStream *stream)
{
    WB_PERSIST_HEADER header;
    IPersistStream *link;
    HRESULT hres;

    memset(&header, 0, sizeof(header));
    header.cbSize = sizeof(header);
    header.extent = This->extent;
    header.view_mode = This->view_mode;
    header.folder_flags = This->folder_flags;
    header.marker = WB_STREAM_MARKER;
    header.browser_flags = get_browser_flags(This);

    hres = IStream_Write(stream, &header, sizeof(header), NULL);
    if (FAILED(hres))
        return hres;

    hres = CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, &IID_IPersistStream, (void **)&link);
    if (FAILED(hres))
        return hres;

    hres = IPersistStream_Save(link, stream, TRUE);
    IPersistStream_Release(link);
    return hres;
}

HRESULT WebBrowser_Ext_LoadStream(WebBrowser *This, IStream *stream)
{
    WB_PERSIST_HEADER header;
    IPersistStream *link;
    ULONG read = 0;
    HRESULT hres;

    hres = IStream_Read(stream, &header, sizeof(header), &read);
    if (FAILED(hres) || read != sizeof(header))
        return E_FAIL;
    if (header.marker != WB_STREAM_MARKER)
        return E_FAIL;
    if (header.reserved3)
        return E_UNEXPECTED;

    This->extent = header.extent;
    This->view_mode = header.view_mode;
    This->folder_flags = header.folder_flags;
    This->doc_host.offline = (header.browser_flags & WB_FLAG_OFFLINE) ? VARIANT_TRUE : VARIANT_FALSE;
    This->doc_host.silent = (header.browser_flags & WB_FLAG_SILENT) ? VARIANT_TRUE : VARIANT_FALSE;
    This->register_browser = (header.browser_flags & WB_FLAG_REGISTER) ? VARIANT_TRUE : VARIANT_FALSE;

    if (SUCCEEDED(CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, &IID_IPersistStream, (void **)&link)))
    {
        IPersistStream_Load(link, stream);
        IPersistStream_Release(link);
    }

    return S_OK;
}

static inline WebBrowser *impl_from_IPersistPropertyBag(IPersistPropertyBag *iface)
{
    return CONTAINING_RECORD(iface, WebBrowser, IPersistPropertyBag_iface);
}

static HRESULT WINAPI PersistPropertyBag_QueryInterface(IPersistPropertyBag *iface, REFIID riid, void **ppv)
{
    WebBrowser *This = impl_from_IPersistPropertyBag(iface);
    return IUnknown_QueryInterface(This->hlink_frame.outer, riid, ppv);
}

static ULONG WINAPI PersistPropertyBag_AddRef(IPersistPropertyBag *iface)
{
    WebBrowser *This = impl_from_IPersistPropertyBag(iface);
    return IUnknown_AddRef(This->hlink_frame.outer);
}

static ULONG WINAPI PersistPropertyBag_Release(IPersistPropertyBag *iface)
{
    WebBrowser *This = impl_from_IPersistPropertyBag(iface);
    return IUnknown_Release(This->hlink_frame.outer);
}

static HRESULT WINAPI PersistPropertyBag_GetClassID(IPersistPropertyBag *iface, CLSID *pClassID)
{
    WebBrowser *This = impl_from_IPersistPropertyBag(iface);
    return IPersistStorage_GetClassID(&This->IPersistStorage_iface, pClassID);
}

static HRESULT WINAPI PersistPropertyBag_InitNew(IPersistPropertyBag *iface)
{
    WebBrowser *This = impl_from_IPersistPropertyBag(iface);

    TRACE("(%p)\n", This);

    WebBrowser_Ext_InitNew(This);
    return S_OK;
}

static BOOL read_dword(IPropertyBag *bag, const WCHAR *name, IErrorLog *log, DWORD *value)
{
    VARIANT v;

    V_VT(&v) = VT_UI4;
    if (FAILED(IPropertyBag_Read(bag, name, &v, log)))
        return FALSE;
    if (V_VT(&v) != VT_UI4 && FAILED(VariantChangeType(&v, &v, 0, VT_UI4)))
    {
        VariantClear(&v);
        return FALSE;
    }
    *value = V_UI4(&v);
    return TRUE;
}

static BOOL read_bool(IPropertyBag *bag, const WCHAR *name, IErrorLog *log)
{
    VARIANT v;

    V_VT(&v) = VT_BOOL;
    if (FAILED(IPropertyBag_Read(bag, name, &v, log)))
        return FALSE;
    if (V_VT(&v) != VT_BOOL && FAILED(VariantChangeType(&v, &v, 0, VT_BOOL)))
    {
        VariantClear(&v);
        return FALSE;
    }
    return V_BOOL(&v) != VARIANT_FALSE;
}

static HRESULT WINAPI PersistPropertyBag_Load(IPersistPropertyBag *iface, IPropertyBag *pPropBag, IErrorLog *pErrorLog)
{
    WebBrowser *This = impl_from_IPersistPropertyBag(iface);
    DWORD value, extent_x, extent_y;
    int dpi_x, dpi_y;
    unsigned int i;

    TRACE("(%p)->(%p %p)\n", This, pPropBag, pErrorLog);

    if (!pPropBag)
        return E_POINTER;

    get_screen_dpi(&dpi_x, &dpi_y);

    if (read_dword(pPropBag, L"Height", pErrorLog, &value))
        This->extent.cy = MulDiv(value, 2540, dpi_y);
    if (read_dword(pPropBag, L"Width", pErrorLog, &value))
        This->extent.cx = MulDiv(value, 2540, dpi_x);
    if (read_dword(pPropBag, L"ViewMode", pErrorLog, &value))
        This->view_mode = value;
    if (read_dword(pPropBag, L"Offline", pErrorLog, &value))
        This->doc_host.offline = value ? VARIANT_TRUE : VARIANT_FALSE;
    if (read_dword(pPropBag, L"Silent", pErrorLog, &value))
        This->doc_host.silent = value ? VARIANT_TRUE : VARIANT_FALSE;
    if (read_dword(pPropBag, L"RegisterAsBrowser", pErrorLog, &value))
        This->register_browser = value ? VARIANT_TRUE : VARIANT_FALSE;
    if (read_dword(pPropBag, L"RegisterAsDropTarget", pErrorLog, &value))
        This->register_drop_target = value ? VARIANT_TRUE : VARIANT_FALSE;
    if (read_dword(pPropBag, L"ExtentX", pErrorLog, &extent_x) &&
        read_dword(pPropBag, L"ExtentY", pErrorLog, &extent_y))
    {
        This->extent.cx = extent_x;
        This->extent.cy = extent_y;
    }

    This->folder_flags = 0;
    for (i = 0; i < ARRAY_SIZE(folder_flag_props); i++)
    {
        if (read_bool(pPropBag, folder_flag_props[i].name, pErrorLog))
            This->folder_flags |= folder_flag_props[i].flag;
    }

    return S_OK;
}

static HRESULT write_i4(IPropertyBag *bag, const WCHAR *name, LONG value)
{
    VARIANT v;

    V_VT(&v) = VT_I4;
    V_I4(&v) = value;
    return IPropertyBag_Write(bag, name, &v);
}

static HRESULT WINAPI PersistPropertyBag_Save(IPersistPropertyBag *iface, IPropertyBag *pPropBag,
                                              BOOL fClearDirty, BOOL fSaveAllProperties)
{
    WebBrowser *This = impl_from_IPersistPropertyBag(iface);
    int dpi_x, dpi_y;
    unsigned int i;
    VARIANT v;
    HRESULT hres;

    TRACE("(%p)->(%p %x %x)\n", This, pPropBag, fClearDirty, fSaveAllProperties);

    if (!pPropBag)
        return E_POINTER;

    write_i4(pPropBag, L"ExtentX", This->extent.cx);
    write_i4(pPropBag, L"ExtentY", This->extent.cy);
    write_i4(pPropBag, L"ViewMode", This->view_mode);
    write_i4(pPropBag, L"Offline", This->doc_host.offline ? 1 : 0);
    write_i4(pPropBag, L"Silent", This->doc_host.silent ? 1 : 0);
    write_i4(pPropBag, L"RegisterAsBrowser", This->register_browser ? 1 : 0);
    write_i4(pPropBag, L"RegisterAsDropTarget", This->register_drop_target ? 1 : 0);

    if (This->version == 1)
    {
        get_screen_dpi(&dpi_x, &dpi_y);
        write_i4(pPropBag, L"Height", MulDiv(This->extent.cy, dpi_y, 2540));
        write_i4(pPropBag, L"Width", MulDiv(This->extent.cx, dpi_x, 2540));
    }

    for (i = 0; i < ARRAY_SIZE(folder_flag_props); i++)
    {
        V_VT(&v) = VT_BOOL;
        V_BOOL(&v) = (This->folder_flags & folder_flag_props[i].flag) ? VARIANT_TRUE : VARIANT_FALSE;
        IPropertyBag_Write(pPropBag, folder_flag_props[i].name, &v);
    }

    V_VT(&v) = VT_BSTR;
    hres = get_location_url(&This->doc_host, &V_BSTR(&v));
    if (FAILED(hres) || !V_BSTR(&v))
        V_BSTR(&v) = SysAllocString(L"");
    IPropertyBag_Write(pPropBag, L"Location", &v);
    VariantClear(&v);

    return S_OK;
}

static const IPersistPropertyBagVtbl PersistPropertyBagVtbl =
{
    PersistPropertyBag_QueryInterface,
    PersistPropertyBag_AddRef,
    PersistPropertyBag_Release,
    PersistPropertyBag_GetClassID,
    PersistPropertyBag_InitNew,
    PersistPropertyBag_Load,
    PersistPropertyBag_Save
};

static inline WebBrowser *impl_from_IObjectSafety(IObjectSafety *iface)
{
    return CONTAINING_RECORD(iface, WebBrowser, IObjectSafety_iface);
}

static HRESULT WINAPI ObjectSafety_QueryInterface(IObjectSafety *iface, REFIID riid, void **ppv)
{
    WebBrowser *This = impl_from_IObjectSafety(iface);
    return IUnknown_QueryInterface(This->hlink_frame.outer, riid, ppv);
}

static ULONG WINAPI ObjectSafety_AddRef(IObjectSafety *iface)
{
    WebBrowser *This = impl_from_IObjectSafety(iface);
    return IUnknown_AddRef(This->hlink_frame.outer);
}

static ULONG WINAPI ObjectSafety_Release(IObjectSafety *iface)
{
    WebBrowser *This = impl_from_IObjectSafety(iface);
    return IUnknown_Release(This->hlink_frame.outer);
}

#define WB_SAFETY_SUPPORTED (INTERFACESAFE_FOR_UNTRUSTED_CALLER | INTERFACESAFE_FOR_UNTRUSTED_DATA)

static BOOL is_persist_iid(REFIID riid)
{
    return IsEqualGUID(riid, &IID_IPersistPropertyBag) ||
           IsEqualGUID(riid, &IID_IPersistStreamInit) ||
           IsEqualGUID(riid, &IID_IPersistStream);
}

static HRESULT WINAPI ObjectSafety_GetInterfaceSafetyOptions(IObjectSafety *iface, REFIID riid,
                                                             DWORD *pdwSupportedOptions, DWORD *pdwEnabledOptions)
{
    WebBrowser *This = impl_from_IObjectSafety(iface);
    DWORD supported = 0, enabled = 0;
    HRESULT hres = S_OK;

    TRACE("(%p)->(%s %p %p)\n", This, debugstr_guid(riid), pdwSupportedOptions, pdwEnabledOptions);

    if (IsEqualGUID(riid, &IID_IDispatch))
    {
        supported = WB_SAFETY_SUPPORTED;
        enabled = This->dispatch_safety;
    }
    else if (is_persist_iid(riid))
    {
        supported = WB_SAFETY_SUPPORTED;
        enabled = WB_SAFETY_SUPPORTED;
    }
    else
    {
        hres = E_NOINTERFACE;
    }

    if (pdwSupportedOptions)
        *pdwSupportedOptions = supported;
    if (pdwEnabledOptions)
        *pdwEnabledOptions = enabled;
    return hres;
}

static HRESULT WINAPI ObjectSafety_SetInterfaceSafetyOptions(IObjectSafety *iface, REFIID riid,
                                                             DWORD dwOptionSetMask, DWORD dwEnabledOptions)
{
    WebBrowser *This = impl_from_IObjectSafety(iface);

    TRACE("(%p)->(%s %lx %lx)\n", This, debugstr_guid(riid), dwOptionSetMask, dwEnabledOptions);

    if (IsEqualGUID(riid, &IID_IDispatch))
    {
        This->dispatch_safety = ((This->dispatch_safety & ~dwOptionSetMask) |
                                 (dwEnabledOptions & dwOptionSetMask)) & WB_SAFETY_SUPPORTED;
    }

    return E_ACCESSDENIED;
}

static const IObjectSafetyVtbl ObjectSafetyVtbl =
{
    ObjectSafety_QueryInterface,
    ObjectSafety_AddRef,
    ObjectSafety_Release,
    ObjectSafety_GetInterfaceSafetyOptions,
    ObjectSafety_SetInterfaceSafetyOptions
};

static inline WebBrowser *impl_from_ITargetEmbedding(ITargetEmbedding *iface)
{
    return CONTAINING_RECORD(iface, WebBrowser, ITargetEmbedding_iface);
}

static HRESULT WINAPI TargetEmbedding_QueryInterface(ITargetEmbedding *iface, REFIID riid, void **ppv)
{
    WebBrowser *This = impl_from_ITargetEmbedding(iface);
    return IUnknown_QueryInterface(This->hlink_frame.outer, riid, ppv);
}

static ULONG WINAPI TargetEmbedding_AddRef(ITargetEmbedding *iface)
{
    WebBrowser *This = impl_from_ITargetEmbedding(iface);
    return IUnknown_AddRef(This->hlink_frame.outer);
}

static ULONG WINAPI TargetEmbedding_Release(ITargetEmbedding *iface)
{
    WebBrowser *This = impl_from_ITargetEmbedding(iface);
    return IUnknown_Release(This->hlink_frame.outer);
}

static HRESULT WINAPI TargetEmbedding_GetTargetFrame(ITargetEmbedding *iface, ITargetFrame **ppTargetFrame)
{
    WebBrowser *This = impl_from_ITargetEmbedding(iface);

    TRACE("(%p)->(%p)\n", This, ppTargetFrame);

    if (!ppTargetFrame)
        return E_POINTER;

    *ppTargetFrame = NULL;
    return E_FAIL;
}

static const ITargetEmbeddingVtbl TargetEmbeddingVtbl =
{
    TargetEmbedding_QueryInterface,
    TargetEmbedding_AddRef,
    TargetEmbedding_Release,
    TargetEmbedding_GetTargetFrame
};

static inline WebBrowser *impl_from_IPersistHistory(IPersistHistory *iface)
{
    return CONTAINING_RECORD(iface, WebBrowser, IPersistHistory_iface);
}

static HRESULT WINAPI PersistHistory_QueryInterface(IPersistHistory *iface, REFIID riid, void **ppv)
{
    WebBrowser *This = impl_from_IPersistHistory(iface);
    return IUnknown_QueryInterface(This->hlink_frame.outer, riid, ppv);
}

static ULONG WINAPI PersistHistory_AddRef(IPersistHistory *iface)
{
    WebBrowser *This = impl_from_IPersistHistory(iface);
    return IUnknown_AddRef(This->hlink_frame.outer);
}

static ULONG WINAPI PersistHistory_Release(IPersistHistory *iface)
{
    WebBrowser *This = impl_from_IPersistHistory(iface);
    return IUnknown_Release(This->hlink_frame.outer);
}

static HRESULT WINAPI PersistHistory_GetClassID(IPersistHistory *iface, CLSID *pClassID)
{
    WebBrowser *This = impl_from_IPersistHistory(iface);
    return IPersistStorage_GetClassID(&This->IPersistStorage_iface, pClassID);
}

static HRESULT get_document_history(WebBrowser *This, IPersistHistory **history)
{
    *history = NULL;
    if (!This->doc_host.document)
        return E_FAIL;
    return IUnknown_QueryInterface(This->doc_host.document, &IID_IPersistHistory, (void **)history);
}

static HRESULT WINAPI PersistHistory_LoadHistory(IPersistHistory *iface, IStream *pStream, IBindCtx *pbc)
{
    WebBrowser *This = impl_from_IPersistHistory(iface);
    IPersistHistory *history;
    HRESULT hres;

    TRACE("(%p)->(%p %p)\n", This, pStream, pbc);

    hres = get_document_history(This, &history);
    if (FAILED(hres))
        return hres;
    hres = IPersistHistory_LoadHistory(history, pStream, pbc);
    IPersistHistory_Release(history);
    return hres;
}

static HRESULT WINAPI PersistHistory_SaveHistory(IPersistHistory *iface, IStream *pStream)
{
    WebBrowser *This = impl_from_IPersistHistory(iface);
    IPersistHistory *history;
    HRESULT hres;

    TRACE("(%p)->(%p)\n", This, pStream);

    hres = get_document_history(This, &history);
    if (FAILED(hres))
        return hres;
    hres = IPersistHistory_SaveHistory(history, pStream);
    IPersistHistory_Release(history);
    return hres;
}

static HRESULT WINAPI PersistHistory_SetPositionCookie(IPersistHistory *iface, DWORD dwPositioncookie)
{
    WebBrowser *This = impl_from_IPersistHistory(iface);
    IPersistHistory *history;
    HRESULT hres;

    TRACE("(%p)->(%lx)\n", This, dwPositioncookie);

    hres = get_document_history(This, &history);
    if (FAILED(hres))
        return hres;
    hres = IPersistHistory_SetPositionCookie(history, dwPositioncookie);
    IPersistHistory_Release(history);
    return hres;
}

static HRESULT WINAPI PersistHistory_GetPositionCookie(IPersistHistory *iface, DWORD *pdwPositioncookie)
{
    WebBrowser *This = impl_from_IPersistHistory(iface);
    IPersistHistory *history;
    HRESULT hres;

    TRACE("(%p)->(%p)\n", This, pdwPositioncookie);

    hres = get_document_history(This, &history);
    if (FAILED(hres))
        return hres;
    hres = IPersistHistory_GetPositionCookie(history, pdwPositioncookie);
    IPersistHistory_Release(history);
    return hres;
}

static const IPersistHistoryVtbl PersistHistoryVtbl =
{
    PersistHistory_QueryInterface,
    PersistHistory_AddRef,
    PersistHistory_Release,
    PersistHistory_GetClassID,
    PersistHistory_LoadHistory,
    PersistHistory_SaveHistory,
    PersistHistory_SetPositionCookie,
    PersistHistory_GetPositionCookie
};

static inline WebBrowser *impl_from_IShellService(IShellService *iface)
{
    return CONTAINING_RECORD(iface, WebBrowser, IShellService_iface);
}

static HRESULT WINAPI ShellService_QueryInterface(IShellService *iface, REFIID riid, void **ppv)
{
    WebBrowser *This = impl_from_IShellService(iface);
    return IUnknown_QueryInterface(This->hlink_frame.outer, riid, ppv);
}

static ULONG WINAPI ShellService_AddRef(IShellService *iface)
{
    WebBrowser *This = impl_from_IShellService(iface);
    return IUnknown_AddRef(This->hlink_frame.outer);
}

static ULONG WINAPI ShellService_Release(IShellService *iface)
{
    WebBrowser *This = impl_from_IShellService(iface);
    return IUnknown_Release(This->hlink_frame.outer);
}

static HRESULT WINAPI ShellService_SetOwner(IShellService *iface, IUnknown *pUnk)
{
    WebBrowser *This = impl_from_IShellService(iface);

    TRACE("(%p)->(%p)\n", This, pUnk);

    return S_OK;
}

static const IShellServiceVtbl ShellServiceVtbl =
{
    ShellService_QueryInterface,
    ShellService_AddRef,
    ShellService_Release,
    ShellService_SetOwner
};

static inline WebBrowser *impl_from_IUrlHistoryNotify(IUrlHistoryNotify *iface)
{
    return CONTAINING_RECORD(iface, WebBrowser, IUrlHistoryNotify_iface);
}

static HRESULT WINAPI UrlHistoryNotify_QueryInterface(IUrlHistoryNotify *iface, REFIID riid, void **ppv)
{
    WebBrowser *This = impl_from_IUrlHistoryNotify(iface);
    return IUnknown_QueryInterface(This->hlink_frame.outer, riid, ppv);
}

static ULONG WINAPI UrlHistoryNotify_AddRef(IUrlHistoryNotify *iface)
{
    WebBrowser *This = impl_from_IUrlHistoryNotify(iface);
    return IUnknown_AddRef(This->hlink_frame.outer);
}

static ULONG WINAPI UrlHistoryNotify_Release(IUrlHistoryNotify *iface)
{
    WebBrowser *This = impl_from_IUrlHistoryNotify(iface);
    return IUnknown_Release(This->hlink_frame.outer);
}

static HRESULT WINAPI UrlHistoryNotify_QueryStatus(IUrlHistoryNotify *iface, const GUID *pguidCmdGroup,
                                                   ULONG cCmds, OLECMD prgCmds[], OLECMDTEXT *pCmdText)
{
    WebBrowser *This = impl_from_IUrlHistoryNotify(iface);
    return IOleCommandTarget_QueryStatus(&This->IOleCommandTarget_iface, pguidCmdGroup, cCmds, prgCmds, pCmdText);
}

static HRESULT WINAPI UrlHistoryNotify_Exec(IUrlHistoryNotify *iface, const GUID *pguidCmdGroup, DWORD nCmdID,
                                            DWORD nCmdexecopt, VARIANT *pvaIn, VARIANT *pvaOut)
{
    WebBrowser *This = impl_from_IUrlHistoryNotify(iface);
    return IOleCommandTarget_Exec(&This->IOleCommandTarget_iface, pguidCmdGroup, nCmdID, nCmdexecopt, pvaIn, pvaOut);
}

static const IUrlHistoryNotifyVtbl UrlHistoryNotifyVtbl =
{
    UrlHistoryNotify_QueryInterface,
    UrlHistoryNotify_AddRef,
    UrlHistoryNotify_Release,
    UrlHistoryNotify_QueryStatus,
    UrlHistoryNotify_Exec
};

static inline WebBrowser *impl_from_ITargetNotify(ITargetNotify *iface)
{
    return CONTAINING_RECORD(iface, WebBrowser, ITargetNotify_iface);
}

static HRESULT WINAPI TargetNotify_QueryInterface(ITargetNotify *iface, REFIID riid, void **ppv)
{
    WebBrowser *This = impl_from_ITargetNotify(iface);
    return IUnknown_QueryInterface(This->hlink_frame.outer, riid, ppv);
}

static ULONG WINAPI TargetNotify_AddRef(ITargetNotify *iface)
{
    WebBrowser *This = impl_from_ITargetNotify(iface);
    return IUnknown_AddRef(This->hlink_frame.outer);
}

static ULONG WINAPI TargetNotify_Release(ITargetNotify *iface)
{
    WebBrowser *This = impl_from_ITargetNotify(iface);
    return IUnknown_Release(This->hlink_frame.outer);
}

static HRESULT WINAPI TargetNotify_OnCreate(ITargetNotify *iface, IUnknown *pUnkDestination, ULONG cbCookie)
{
    WebBrowser *This = impl_from_ITargetNotify(iface);

    TRACE("(%p)->(%p %lu)\n", This, pUnkDestination, cbCookie);

    return S_OK;
}

static HRESULT WINAPI TargetNotify_OnReuse(ITargetNotify *iface, IUnknown *pUnkDestination)
{
    WebBrowser *This = impl_from_ITargetNotify(iface);

    TRACE("(%p)->(%p)\n", This, pUnkDestination);

    return S_OK;
}

static const ITargetNotifyVtbl TargetNotifyVtbl =
{
    TargetNotify_QueryInterface,
    TargetNotify_AddRef,
    TargetNotify_Release,
    TargetNotify_OnCreate,
    TargetNotify_OnReuse
};

void WebBrowser_Ext_Init(WebBrowser *This)
{
    This->IPersistPropertyBag_iface.lpVtbl = &PersistPropertyBagVtbl;
    This->IObjectSafety_iface.lpVtbl = &ObjectSafetyVtbl;
    This->ITargetEmbedding_iface.lpVtbl = &TargetEmbeddingVtbl;
    This->IPersistHistory_iface.lpVtbl = &PersistHistoryVtbl;
    This->IShellService_iface.lpVtbl = &ShellServiceVtbl;
    This->IUrlHistoryNotify_iface.lpVtbl = &UrlHistoryNotifyVtbl;
    This->ITargetNotify_iface.lpVtbl = &TargetNotifyVtbl;

    WebBrowser_Ext_InitNew(This);
}
