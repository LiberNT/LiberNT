/*
 * PROJECT:     LiberNT MSHTML
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Frameset element
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#include <stdarg.h>
#include <stdio.h>

#define COBJMACROS

#include "windef.h"
#include "winbase.h"
#include "winuser.h"
#include "ole2.h"
#include "mshtmdid.h"

#include "mshtml_private.h"
#include "htmlevent.h"

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(mshtml);

typedef struct HTMLFrameSetElement HTMLFrameSetElement;

struct HTMLFrameSetElement {
    HTMLElement element;

    IHTMLFrameSetElement IHTMLFrameSetElement_iface;
    IHTMLFrameSetElement2 IHTMLFrameSetElement2_iface;
    IHTMLFrameSetElement3 IHTMLFrameSetElement3_iface;
};

static HRESULT put_int_attr(HTMLElement *elem, const WCHAR *name, VARIANT *v)
{
    const WCHAR *ptr;
    WCHAR buf[16];
    VARIANT str;
    HRESULT hres;
    size_t len;

    switch(V_VT(v)) {
    case VT_NULL:
        return element_remove_attribute(elem, name);
    case VT_I4:
        swprintf(buf, ARRAY_SIZE(buf), L"%ld", V_I4(v));
        return elem_put_reflected_string(elem, name, buf);
    case VT_BSTR:
        break;
    default:
        V_VT(&str) = VT_EMPTY;
        hres = VariantChangeType(&str, v, 0, VT_BSTR);
        if(FAILED(hres))
            return E_INVALIDARG;
        hres = put_int_attr(elem, name, &str);
        VariantClear(&str);
        return hres;
    }

    if(!V_BSTR(v) || !*V_BSTR(v))
        return element_remove_attribute(elem, name);

    ptr = V_BSTR(v);
    if(*ptr == '-')
        ptr++;
    if(*ptr < '0' || *ptr > '9')
        return E_INVALIDARG;
    while(*ptr >= '0' && *ptr <= '9')
        ptr++;
    len = ptr - V_BSTR(v);
    if(len >= ARRAY_SIZE(buf))
        return E_INVALIDARG;
    memcpy(buf, V_BSTR(v), len * sizeof(WCHAR));
    buf[len] = 0;
    return elem_put_reflected_string(elem, name, buf);
}

static HRESULT get_int_attr(HTMLElement *elem, const WCHAR *name, VARIANT *p)
{
    V_VT(p) = VT_BSTR;
    return elem_get_attr_string(elem, name, &V_BSTR(p));
}

static inline HTMLFrameSetElement *impl_from_IHTMLFrameSetElement(IHTMLFrameSetElement *iface)
{
    return CONTAINING_RECORD(iface, HTMLFrameSetElement, IHTMLFrameSetElement_iface);
}

DISPEX_IDISPATCH_IMPL(HTMLFrameSetElement, IHTMLFrameSetElement,
                      impl_from_IHTMLFrameSetElement(iface)->element.node.event_target.dispex)

static HRESULT WINAPI HTMLFrameSetElement_put_rows(IHTMLFrameSetElement *iface, BSTR v)
{
    return elem_put_reflected_string(&impl_from_IHTMLFrameSetElement(iface)->element, L"rows", v);
}

static HRESULT WINAPI HTMLFrameSetElement_get_rows(IHTMLFrameSetElement *iface, BSTR *p)
{
    return elem_get_attr_string(&impl_from_IHTMLFrameSetElement(iface)->element, L"rows", p);
}

static HRESULT WINAPI HTMLFrameSetElement_put_cols(IHTMLFrameSetElement *iface, BSTR v)
{
    return elem_put_reflected_string(&impl_from_IHTMLFrameSetElement(iface)->element, L"cols", v);
}

static HRESULT WINAPI HTMLFrameSetElement_get_cols(IHTMLFrameSetElement *iface, BSTR *p)
{
    return elem_get_attr_string(&impl_from_IHTMLFrameSetElement(iface)->element, L"cols", p);
}

static HRESULT WINAPI HTMLFrameSetElement_put_border(IHTMLFrameSetElement *iface, VARIANT v)
{
    return put_int_attr(&impl_from_IHTMLFrameSetElement(iface)->element, L"border", &v);
}

static HRESULT WINAPI HTMLFrameSetElement_get_border(IHTMLFrameSetElement *iface, VARIANT *p)
{
    return get_int_attr(&impl_from_IHTMLFrameSetElement(iface)->element, L"border", p);
}

static HRESULT WINAPI HTMLFrameSetElement_put_borderColor(IHTMLFrameSetElement *iface, VARIANT v)
{
    return elem_put_color_attr(&impl_from_IHTMLFrameSetElement(iface)->element, L"borderColor", &v);
}

static HRESULT WINAPI HTMLFrameSetElement_get_borderColor(IHTMLFrameSetElement *iface, VARIANT *p)
{
    return elem_get_color_attr(&impl_from_IHTMLFrameSetElement(iface)->element, L"borderColor", p);
}

static HRESULT WINAPI HTMLFrameSetElement_put_frameBorder(IHTMLFrameSetElement *iface, BSTR v)
{
    return elem_put_reflected_string(&impl_from_IHTMLFrameSetElement(iface)->element, L"frameBorder", v);
}

static HRESULT WINAPI HTMLFrameSetElement_get_frameBorder(IHTMLFrameSetElement *iface, BSTR *p)
{
    return elem_get_attr_string(&impl_from_IHTMLFrameSetElement(iface)->element, L"frameBorder", p);
}

static HRESULT WINAPI HTMLFrameSetElement_put_frameSpacing(IHTMLFrameSetElement *iface, VARIANT v)
{
    return put_int_attr(&impl_from_IHTMLFrameSetElement(iface)->element, L"frameSpacing", &v);
}

static HRESULT WINAPI HTMLFrameSetElement_get_frameSpacing(IHTMLFrameSetElement *iface, VARIANT *p)
{
    return get_int_attr(&impl_from_IHTMLFrameSetElement(iface)->element, L"frameSpacing", p);
}

static HRESULT WINAPI HTMLFrameSetElement_put_name(IHTMLFrameSetElement *iface, BSTR v)
{
    return elem_put_reflected_string(&impl_from_IHTMLFrameSetElement(iface)->element, L"name", v);
}

static HRESULT WINAPI HTMLFrameSetElement_get_name(IHTMLFrameSetElement *iface, BSTR *p)
{
    return elem_get_reflected_string(&impl_from_IHTMLFrameSetElement(iface)->element, L"name", p);
}

static HRESULT WINAPI HTMLFrameSetElement_put_onload(IHTMLFrameSetElement *iface, VARIANT v)
{
    return set_node_event(&impl_from_IHTMLFrameSetElement(iface)->element.node, EVENTID_LOAD, &v);
}

static HRESULT WINAPI HTMLFrameSetElement_get_onload(IHTMLFrameSetElement *iface, VARIANT *p)
{
    return get_node_event(&impl_from_IHTMLFrameSetElement(iface)->element.node, EVENTID_LOAD, p);
}

static HRESULT WINAPI HTMLFrameSetElement_put_onunload(IHTMLFrameSetElement *iface, VARIANT v)
{
    return set_node_event(&impl_from_IHTMLFrameSetElement(iface)->element.node, EVENTID_UNLOAD, &v);
}

static HRESULT WINAPI HTMLFrameSetElement_get_onunload(IHTMLFrameSetElement *iface, VARIANT *p)
{
    return get_node_event(&impl_from_IHTMLFrameSetElement(iface)->element.node, EVENTID_UNLOAD, p);
}

static HRESULT WINAPI HTMLFrameSetElement_put_onbeforeunload(IHTMLFrameSetElement *iface, VARIANT v)
{
    return set_node_event(&impl_from_IHTMLFrameSetElement(iface)->element.node, EVENTID_BEFOREUNLOAD, &v);
}

static HRESULT WINAPI HTMLFrameSetElement_get_onbeforeunload(IHTMLFrameSetElement *iface, VARIANT *p)
{
    return get_node_event(&impl_from_IHTMLFrameSetElement(iface)->element.node, EVENTID_BEFOREUNLOAD, p);
}

static const IHTMLFrameSetElementVtbl HTMLFrameSetElementVtbl = {
    HTMLFrameSetElement_QueryInterface,
    HTMLFrameSetElement_AddRef,
    HTMLFrameSetElement_Release,
    HTMLFrameSetElement_GetTypeInfoCount,
    HTMLFrameSetElement_GetTypeInfo,
    HTMLFrameSetElement_GetIDsOfNames,
    HTMLFrameSetElement_Invoke,
    HTMLFrameSetElement_put_rows,
    HTMLFrameSetElement_get_rows,
    HTMLFrameSetElement_put_cols,
    HTMLFrameSetElement_get_cols,
    HTMLFrameSetElement_put_border,
    HTMLFrameSetElement_get_border,
    HTMLFrameSetElement_put_borderColor,
    HTMLFrameSetElement_get_borderColor,
    HTMLFrameSetElement_put_frameBorder,
    HTMLFrameSetElement_get_frameBorder,
    HTMLFrameSetElement_put_frameSpacing,
    HTMLFrameSetElement_get_frameSpacing,
    HTMLFrameSetElement_put_name,
    HTMLFrameSetElement_get_name,
    HTMLFrameSetElement_put_onload,
    HTMLFrameSetElement_get_onload,
    HTMLFrameSetElement_put_onunload,
    HTMLFrameSetElement_get_onunload,
    HTMLFrameSetElement_put_onbeforeunload,
    HTMLFrameSetElement_get_onbeforeunload
};

static inline HTMLFrameSetElement *impl_from_IHTMLFrameSetElement2(IHTMLFrameSetElement2 *iface)
{
    return CONTAINING_RECORD(iface, HTMLFrameSetElement, IHTMLFrameSetElement2_iface);
}

DISPEX_IDISPATCH_IMPL(HTMLFrameSetElement2, IHTMLFrameSetElement2,
                      impl_from_IHTMLFrameSetElement2(iface)->element.node.event_target.dispex)

static HRESULT WINAPI HTMLFrameSetElement2_put_onbeforeprint(IHTMLFrameSetElement2 *iface, VARIANT v)
{
    return set_node_event(&impl_from_IHTMLFrameSetElement2(iface)->element.node, EVENTID_BEFOREPRINT, &v);
}

static HRESULT WINAPI HTMLFrameSetElement2_get_onbeforeprint(IHTMLFrameSetElement2 *iface, VARIANT *p)
{
    return get_node_event(&impl_from_IHTMLFrameSetElement2(iface)->element.node, EVENTID_BEFOREPRINT, p);
}

static HRESULT WINAPI HTMLFrameSetElement2_put_onafterprint(IHTMLFrameSetElement2 *iface, VARIANT v)
{
    return set_node_event(&impl_from_IHTMLFrameSetElement2(iface)->element.node, EVENTID_AFTERPRINT, &v);
}

static HRESULT WINAPI HTMLFrameSetElement2_get_onafterprint(IHTMLFrameSetElement2 *iface, VARIANT *p)
{
    return get_node_event(&impl_from_IHTMLFrameSetElement2(iface)->element.node, EVENTID_AFTERPRINT, p);
}

static const IHTMLFrameSetElement2Vtbl HTMLFrameSetElement2Vtbl = {
    HTMLFrameSetElement2_QueryInterface,
    HTMLFrameSetElement2_AddRef,
    HTMLFrameSetElement2_Release,
    HTMLFrameSetElement2_GetTypeInfoCount,
    HTMLFrameSetElement2_GetTypeInfo,
    HTMLFrameSetElement2_GetIDsOfNames,
    HTMLFrameSetElement2_Invoke,
    HTMLFrameSetElement2_put_onbeforeprint,
    HTMLFrameSetElement2_get_onbeforeprint,
    HTMLFrameSetElement2_put_onafterprint,
    HTMLFrameSetElement2_get_onafterprint
};

static inline HTMLFrameSetElement *impl_from_IHTMLFrameSetElement3(IHTMLFrameSetElement3 *iface)
{
    return CONTAINING_RECORD(iface, HTMLFrameSetElement, IHTMLFrameSetElement3_iface);
}

DISPEX_IDISPATCH_IMPL(HTMLFrameSetElement3, IHTMLFrameSetElement3,
                      impl_from_IHTMLFrameSetElement3(iface)->element.node.event_target.dispex)

static HRESULT WINAPI HTMLFrameSetElement3_put_onhashchange(IHTMLFrameSetElement3 *iface, VARIANT v)
{
    /* TODO: hashchange, online and offline events */
    FIXME("(%p)->(%s)\n", iface, debugstr_variant(&v));
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLFrameSetElement3_get_onhashchange(IHTMLFrameSetElement3 *iface, VARIANT *p)
{
    FIXME("(%p)->(%p)\n", iface, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLFrameSetElement3_put_onmessage(IHTMLFrameSetElement3 *iface, VARIANT v)
{
    return set_node_event(&impl_from_IHTMLFrameSetElement3(iface)->element.node, EVENTID_MESSAGE, &v);
}

static HRESULT WINAPI HTMLFrameSetElement3_get_onmessage(IHTMLFrameSetElement3 *iface, VARIANT *p)
{
    return get_node_event(&impl_from_IHTMLFrameSetElement3(iface)->element.node, EVENTID_MESSAGE, p);
}

static HRESULT WINAPI HTMLFrameSetElement3_put_onoffline(IHTMLFrameSetElement3 *iface, VARIANT v)
{
    FIXME("(%p)->(%s)\n", iface, debugstr_variant(&v));
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLFrameSetElement3_get_onoffline(IHTMLFrameSetElement3 *iface, VARIANT *p)
{
    FIXME("(%p)->(%p)\n", iface, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLFrameSetElement3_put_ononline(IHTMLFrameSetElement3 *iface, VARIANT v)
{
    FIXME("(%p)->(%s)\n", iface, debugstr_variant(&v));
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLFrameSetElement3_get_ononline(IHTMLFrameSetElement3 *iface, VARIANT *p)
{
    FIXME("(%p)->(%p)\n", iface, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLFrameSetElement3_put_onstorage(IHTMLFrameSetElement3 *iface, VARIANT v)
{
    return set_node_event(&impl_from_IHTMLFrameSetElement3(iface)->element.node, EVENTID_STORAGE, &v);
}

static HRESULT WINAPI HTMLFrameSetElement3_get_onstorage(IHTMLFrameSetElement3 *iface, VARIANT *p)
{
    return get_node_event(&impl_from_IHTMLFrameSetElement3(iface)->element.node, EVENTID_STORAGE, p);
}

static const IHTMLFrameSetElement3Vtbl HTMLFrameSetElement3Vtbl = {
    HTMLFrameSetElement3_QueryInterface,
    HTMLFrameSetElement3_AddRef,
    HTMLFrameSetElement3_Release,
    HTMLFrameSetElement3_GetTypeInfoCount,
    HTMLFrameSetElement3_GetTypeInfo,
    HTMLFrameSetElement3_GetIDsOfNames,
    HTMLFrameSetElement3_Invoke,
    HTMLFrameSetElement3_put_onhashchange,
    HTMLFrameSetElement3_get_onhashchange,
    HTMLFrameSetElement3_put_onmessage,
    HTMLFrameSetElement3_get_onmessage,
    HTMLFrameSetElement3_put_onoffline,
    HTMLFrameSetElement3_get_onoffline,
    HTMLFrameSetElement3_put_ononline,
    HTMLFrameSetElement3_get_ononline,
    HTMLFrameSetElement3_put_onstorage,
    HTMLFrameSetElement3_get_onstorage
};

static inline HTMLFrameSetElement *HTMLFrameSetElement_from_DispatchEx(DispatchEx *iface)
{
    return CONTAINING_RECORD(iface, HTMLFrameSetElement, element.node.event_target.dispex);
}

static void *HTMLFrameSetElement_query_interface(DispatchEx *dispex, REFIID riid)
{
    HTMLFrameSetElement *This = HTMLFrameSetElement_from_DispatchEx(dispex);

    /* TODO: IHTMLControlElement */
    if(IsEqualGUID(&IID_IHTMLFrameSetElement, riid))
        return &This->IHTMLFrameSetElement_iface;
    if(IsEqualGUID(&IID_IHTMLFrameSetElement2, riid))
        return &This->IHTMLFrameSetElement2_iface;
    if(IsEqualGUID(&IID_IHTMLFrameSetElement3, riid))
        return &This->IHTMLFrameSetElement3_iface;

    return HTMLElement_query_interface(&This->element.node.event_target.dispex, riid);
}

static const NodeImplVtbl HTMLFrameSetElementImplVtbl = {
    .clsid                 = &CLSID_HTMLFrameSetSite,
    .cpc_entries           = HTMLElement_cpc,
    .clone                 = HTMLElement_clone,
    .get_attr_col          = HTMLElement_get_attr_col,
    .get_event_prop_target = get_window_event_prop_target
};

static const event_target_vtbl_t HTMLFrameSetElement_event_target_vtbl = {
    {
        HTMLELEMENT_DISPEX_VTBL_ENTRIES,
        .query_interface= HTMLFrameSetElement_query_interface,
        .destructor     = HTMLElement_destructor,
        .traverse       = HTMLElement_traverse,
        .unlink         = HTMLElement_unlink
    },
    HTMLELEMENT_EVENT_TARGET_VTBL_ENTRIES,
    .handle_event       = HTMLElement_handle_event
};

static HRESULT ie9_string_hook(DispatchEx *dispex, WORD flags, DISPPARAMS *dp, VARIANT *res,
        HRESULT (*getter)(HTMLElement*,const WCHAR*,VARIANT*), const WCHAR *name)
{
    HTMLElement *elem = CONTAINING_RECORD(dispex, HTMLElement, node.event_target.dispex);
    HRESULT hres;

    if(!(flags & DISPATCH_PROPERTYGET) || (dp && dp->cArgs))
        return S_FALSE;

    hres = getter(elem, name, res);
    if(SUCCEEDED(hres) && !V_BSTR(res) && !(V_BSTR(res) = SysAllocString(L"")))
        hres = E_OUTOFMEMORY;
    return hres;
}

static HRESULT get_string_attr(HTMLElement *elem, const WCHAR *name, VARIANT *p)
{
    V_VT(p) = VT_BSTR;
    return elem_get_attr_string(elem, name, &V_BSTR(p));
}

#define IE9_STRING_HOOK(hook, getter, name) \
    static HRESULT hook(DispatchEx *dispex, WORD flags, DISPPARAMS *dp, VARIANT *res, \
            EXCEPINFO *ei, IServiceProvider *caller) \
    { \
        return ie9_string_hook(dispex, flags, dp, res, getter, name); \
    }

IE9_STRING_HOOK(ie9_rows_hook,         get_string_attr,     L"rows")
IE9_STRING_HOOK(ie9_cols_hook,         get_string_attr,     L"cols")
IE9_STRING_HOOK(ie9_border_hook,       get_int_attr,        L"border")
IE9_STRING_HOOK(ie9_bordercolor_hook,  elem_get_color_attr, L"borderColor")
IE9_STRING_HOOK(ie9_frameborder_hook,  get_string_attr,     L"frameBorder")
IE9_STRING_HOOK(ie9_framespacing_hook, get_int_attr,        L"frameSpacing")
IE9_STRING_HOOK(ie9_name_hook,         get_string_attr,     L"name")

static void HTMLFrameSetElement_init_dispex_info(dispex_data_t *info, compat_mode_t mode)
{
    static const dispex_hook_t ie9_hooks[] = {
        {DISPID_IHTMLFRAMESETELEMENT_ROWS,         ie9_rows_hook},
        {DISPID_IHTMLFRAMESETELEMENT_COLS,         ie9_cols_hook},
        {DISPID_IHTMLFRAMESETELEMENT_BORDER,       ie9_border_hook},
        {DISPID_IHTMLFRAMESETELEMENT_BORDERCOLOR,  ie9_bordercolor_hook},
        {DISPID_IHTMLFRAMESETELEMENT_FRAMEBORDER,  ie9_frameborder_hook},
        {DISPID_IHTMLFRAMESETELEMENT_FRAMESPACING, ie9_framespacing_hook},
        {DISPID_IHTMLFRAMESETELEMENT_NAME,         ie9_name_hook},
        {DISPID_UNKNOWN}
    };

    HTMLElement_init_dispex_info(info, mode);
    dispex_info_add_interface(info, IHTMLFrameSetElement_tid, mode >= COMPAT_MODE_IE9 ? ie9_hooks : NULL);
    dispex_info_add_interface(info, IHTMLFrameSetElement2_tid, NULL);
    if(mode >= COMPAT_MODE_IE9)
        dispex_info_add_interface(info, IHTMLFrameSetElement3_tid, NULL);
}

dispex_static_data_t HTMLFrameSetElement_dispex = {
    .id           = OBJID_HTMLFrameSetElement,
    .prototype_id = OBJID_HTMLElement,
    .vtbl         = &HTMLFrameSetElement_event_target_vtbl.dispex_vtbl,
    .disp_tid     = DispHTMLFrameSetSite_tid,
    .init_info    = HTMLFrameSetElement_init_dispex_info,
};

HRESULT HTMLFrameSetElement_Create(HTMLDocumentNode *doc, nsIDOMElement *nselem, HTMLElement **elem)
{
    HTMLFrameSetElement *ret;

    ret = calloc(1, sizeof(*ret));
    if(!ret)
        return E_OUTOFMEMORY;

    ret->IHTMLFrameSetElement_iface.lpVtbl = &HTMLFrameSetElementVtbl;
    ret->IHTMLFrameSetElement2_iface.lpVtbl = &HTMLFrameSetElement2Vtbl;
    ret->IHTMLFrameSetElement3_iface.lpVtbl = &HTMLFrameSetElement3Vtbl;
    ret->element.node.vtbl = &HTMLFrameSetElementImplVtbl;

    HTMLElement_Init(&ret->element, doc, nselem, &HTMLFrameSetElement_dispex);

    *elem = &ret->element;
    return S_OK;
}
