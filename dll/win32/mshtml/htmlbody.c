/*
 * Copyright 2006 Jacek Caban for CodeWeavers
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

#include <stdarg.h>
#include <stdio.h>

#define COBJMACROS

#include "windef.h"
#include "winbase.h"
#include "winuser.h"
#include "ole2.h"
#include "mshtmdid.h"

#include "wine/debug.h"

#include "mshtml_private.h"
#include "htmlevent.h"
#include "htmlstyle.h"

WINE_DEFAULT_DEBUG_CHANNEL(mshtml);

typedef struct {
    HTMLElement element;

    IHTMLBodyElement IHTMLBodyElement_iface;
    IHTMLBodyElement2 IHTMLBodyElement2_iface;
    IHTMLTextContainer IHTMLTextContainer_iface;

    nsIDOMHTMLBodyElement *nsbody;
} HTMLBodyElement;

static inline HTMLBodyElement *impl_from_IHTMLBodyElement(IHTMLBodyElement *iface)
{
    return CONTAINING_RECORD(iface, HTMLBodyElement, IHTMLBodyElement_iface);
}

DISPEX_IDISPATCH_IMPL(HTMLBodyElement, IHTMLBodyElement,
                      impl_from_IHTMLBodyElement(iface)->element.node.event_target.dispex)

static HRESULT WINAPI HTMLBodyElement_put_background(IHTMLBodyElement *iface, BSTR v)
{
    HTMLBodyElement *This = impl_from_IHTMLBodyElement(iface);
    nsAString nsstr;
    nsresult nsres;

    TRACE("(%p)->(%s)\n", This, debugstr_w(v));

    nsAString_InitDepend(&nsstr, v);
    nsres = nsIDOMHTMLBodyElement_SetBackground(This->nsbody, &nsstr);
    nsAString_Finish(&nsstr);
    if(NS_FAILED(nsres))
        return E_FAIL;

    return S_OK;
}

static HRESULT WINAPI HTMLBodyElement_get_background(IHTMLBodyElement *iface, BSTR *p)
{
    HTMLBodyElement *This = impl_from_IHTMLBodyElement(iface);
    nsAString background_str;
    nsresult nsres;

    TRACE("(%p)->(%p)\n", This, p);

    nsAString_Init(&background_str, NULL);
    nsres = nsIDOMHTMLBodyElement_GetBackground(This->nsbody, &background_str);
    return return_nsstr(nsres, &background_str, p);
}

static HRESULT WINAPI HTMLBodyElement_put_bgProperties(IHTMLBodyElement *iface, BSTR v)
{
    HTMLBodyElement *This = impl_from_IHTMLBodyElement(iface);
    FIXME("(%p)->(%s)\n", This, debugstr_w(v));
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLBodyElement_get_bgProperties(IHTMLBodyElement *iface, BSTR *p)
{
    HTMLBodyElement *This = impl_from_IHTMLBodyElement(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLBodyElement_put_leftMargin(IHTMLBodyElement *iface, VARIANT v)
{
    HTMLBodyElement *This = impl_from_IHTMLBodyElement(iface);
    FIXME("(%p)->(%s)\n", This, debugstr_variant(&v));
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLBodyElement_get_leftMargin(IHTMLBodyElement *iface, VARIANT *p)
{
    HTMLBodyElement *This = impl_from_IHTMLBodyElement(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLBodyElement_put_topMargin(IHTMLBodyElement *iface, VARIANT v)
{
    HTMLBodyElement *This = impl_from_IHTMLBodyElement(iface);
    FIXME("(%p)->(%s)\n", This, debugstr_variant(&v));
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLBodyElement_get_topMargin(IHTMLBodyElement *iface, VARIANT *p)
{
    HTMLBodyElement *This = impl_from_IHTMLBodyElement(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLBodyElement_put_rightMargin(IHTMLBodyElement *iface, VARIANT v)
{
    HTMLBodyElement *This = impl_from_IHTMLBodyElement(iface);
    FIXME("(%p)->(%s)\n", This, debugstr_variant(&v));
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLBodyElement_get_rightMargin(IHTMLBodyElement *iface, VARIANT *p)
{
    HTMLBodyElement *This = impl_from_IHTMLBodyElement(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLBodyElement_put_bottomMargin(IHTMLBodyElement *iface, VARIANT v)
{
    HTMLBodyElement *This = impl_from_IHTMLBodyElement(iface);
    FIXME("(%p)->(%s)\n", This, debugstr_variant(&v));
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLBodyElement_get_bottomMargin(IHTMLBodyElement *iface, VARIANT *p)
{
    HTMLBodyElement *This = impl_from_IHTMLBodyElement(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLBodyElement_put_noWrap(IHTMLBodyElement *iface, VARIANT_BOOL v)
{
    HTMLBodyElement *This = impl_from_IHTMLBodyElement(iface);
    FIXME("(%p)->(%x)\n", This, v);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLBodyElement_get_noWrap(IHTMLBodyElement *iface, VARIANT_BOOL *p)
{
    HTMLBodyElement *This = impl_from_IHTMLBodyElement(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLBodyElement_put_bgColor(IHTMLBodyElement *iface, VARIANT v)
{
    HTMLBodyElement *This = impl_from_IHTMLBodyElement(iface);

    TRACE("(%p)->(%s)\n", This, debugstr_variant(&v));

    return elem_put_color_attr(&This->element, L"bgColor", &v);
}

static HRESULT WINAPI HTMLBodyElement_get_bgColor(IHTMLBodyElement *iface, VARIANT *p)
{
    HTMLBodyElement *This = impl_from_IHTMLBodyElement(iface);

    TRACE("(%p)->(%p)\n", This, p);

    return elem_get_color_attr(&This->element, L"bgColor", p);
}

static HRESULT WINAPI HTMLBodyElement_put_text(IHTMLBodyElement *iface, VARIANT v)
{
    HTMLBodyElement *This = impl_from_IHTMLBodyElement(iface);

    TRACE("(%p)->(%s)\n", This, debugstr_variant(&v));

    return elem_put_color_attr(&This->element, L"text", &v);
}

static HRESULT WINAPI HTMLBodyElement_get_text(IHTMLBodyElement *iface, VARIANT *p)
{
    HTMLBodyElement *This = impl_from_IHTMLBodyElement(iface);

    TRACE("(%p)->(%p)\n", This, p);

    return elem_get_color_attr(&This->element, L"text", p);
}

static HRESULT WINAPI HTMLBodyElement_put_link(IHTMLBodyElement *iface, VARIANT v)
{
    HTMLBodyElement *This = impl_from_IHTMLBodyElement(iface);

    TRACE("(%p)->(%s)\n", This, debugstr_variant(&v));

    return elem_put_color_attr(&This->element, L"link", &v);
}

static HRESULT WINAPI HTMLBodyElement_get_link(IHTMLBodyElement *iface, VARIANT *p)
{
    HTMLBodyElement *This = impl_from_IHTMLBodyElement(iface);

    TRACE("(%p)->(%p)\n", This, p);

    return elem_get_color_attr(&This->element, L"link", p);
}

static HRESULT WINAPI HTMLBodyElement_put_vLink(IHTMLBodyElement *iface, VARIANT v)
{
    HTMLBodyElement *This = impl_from_IHTMLBodyElement(iface);

    TRACE("(%p)->(%s)\n", This, debugstr_variant(&v));

    return elem_put_color_attr(&This->element, L"vLink", &v);
}

static HRESULT WINAPI HTMLBodyElement_get_vLink(IHTMLBodyElement *iface, VARIANT *p)
{
    HTMLBodyElement *This = impl_from_IHTMLBodyElement(iface);

    TRACE("(%p)->(%p)\n", This, p);

    return elem_get_color_attr(&This->element, L"vLink", p);
}

static HRESULT WINAPI HTMLBodyElement_put_aLink(IHTMLBodyElement *iface, VARIANT v)
{
    HTMLBodyElement *This = impl_from_IHTMLBodyElement(iface);

    TRACE("(%p)->(%s)\n", This, debugstr_variant(&v));

    return elem_put_color_attr(&This->element, L"aLink", &v);
}

static HRESULT WINAPI HTMLBodyElement_get_aLink(IHTMLBodyElement *iface, VARIANT *p)
{
    HTMLBodyElement *This = impl_from_IHTMLBodyElement(iface);

    TRACE("(%p)->(%p)\n", This, p);

    return elem_get_color_attr(&This->element, L"aLink", p);
}

static HRESULT WINAPI HTMLBodyElement_put_onload(IHTMLBodyElement *iface, VARIANT v)
{
    HTMLBodyElement *This = impl_from_IHTMLBodyElement(iface);

    TRACE("(%p)->(%s)\n", This, debugstr_variant(&v));

    return set_node_event(&This->element.node, EVENTID_LOAD, &v);
}

static HRESULT WINAPI HTMLBodyElement_get_onload(IHTMLBodyElement *iface, VARIANT *p)
{
    HTMLBodyElement *This = impl_from_IHTMLBodyElement(iface);

    TRACE("(%p)->(%p)\n", This, p);

    return get_node_event(&This->element.node, EVENTID_LOAD, p);
}

static HRESULT WINAPI HTMLBodyElement_put_onunload(IHTMLBodyElement *iface, VARIANT v)
{
    HTMLBodyElement *This = impl_from_IHTMLBodyElement(iface);

    TRACE("(%p)->(%s)\n", This, debugstr_variant(&v));

    return set_node_event(&This->element.node, EVENTID_UNLOAD, &v);
}

static HRESULT WINAPI HTMLBodyElement_get_onunload(IHTMLBodyElement *iface, VARIANT *p)
{
    HTMLBodyElement *This = impl_from_IHTMLBodyElement(iface);

    TRACE("(%p)->(%p)\n", This, p);

    return get_node_event(&This->element.node, EVENTID_UNLOAD, p);
}

static HRESULT WINAPI HTMLBodyElement_put_scroll(IHTMLBodyElement *iface, BSTR v)
{
    HTMLBodyElement *This = impl_from_IHTMLBodyElement(iface);
    static const WCHAR *val;

    TRACE("(%p)->(%s)\n", This, debugstr_w(v));

    /* Emulate with CSS visibility attribute */
    if(!wcscmp(v, L"yes")) {
        val = L"scroll";
    }else if(!wcscmp(v, L"auto")) {
        val = L"visible";
    }else if(!wcscmp(v, L"no")) {
        val = L"hidden";
    }else {
        WARN("Invalid argument %s\n", debugstr_w(v));
        return E_INVALIDARG;
    }

    return set_elem_style(&This->element, STYLEID_OVERFLOW, val);
}

static HRESULT WINAPI HTMLBodyElement_get_scroll(IHTMLBodyElement *iface, BSTR *p)
{
    HTMLBodyElement *This = impl_from_IHTMLBodyElement(iface);
    const WCHAR *ret = NULL;
    BSTR overflow;
    HRESULT hres;

    TRACE("(%p)->(%p)\n", This, p);

    /* Emulate with CSS visibility attribute */
    hres = get_elem_style(&This->element, STYLEID_OVERFLOW, &overflow);
    if(FAILED(hres))
        return hres;

    if(!overflow || !*overflow) {
        *p = NULL;
        hres = S_OK;
    }else if(!wcscmp(overflow, L"visible") || !wcscmp(overflow, L"auto")) {
        ret = L"auto";
    }else if(!wcscmp(overflow, L"scroll")) {
        ret = L"yes";
    }else if(!wcscmp(overflow, L"hidden")) {
        ret = L"no";
    }else {
        TRACE("Defaulting %s to NULL\n", debugstr_w(overflow));
        *p = NULL;
        hres = S_OK;
    }

    SysFreeString(overflow);
    if(ret) {
        *p = SysAllocString(ret);
        hres = *p ? S_OK : E_OUTOFMEMORY;
    }

    return hres;
}

static HRESULT WINAPI HTMLBodyElement_put_onselect(IHTMLBodyElement *iface, VARIANT v)
{
    HTMLBodyElement *This = impl_from_IHTMLBodyElement(iface);
    FIXME("(%p)->(%s)\n", This, debugstr_variant(&v));
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLBodyElement_get_onselect(IHTMLBodyElement *iface, VARIANT *p)
{
    HTMLBodyElement *This = impl_from_IHTMLBodyElement(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLBodyElement_put_onbeforeunload(IHTMLBodyElement *iface, VARIANT v)
{
    HTMLBodyElement *This = impl_from_IHTMLBodyElement(iface);

    TRACE("(%p)->(%s)\n", This, debugstr_variant(&v));

    return set_node_event(&This->element.node, EVENTID_BEFOREUNLOAD, &v);
}

static HRESULT WINAPI HTMLBodyElement_get_onbeforeunload(IHTMLBodyElement *iface, VARIANT *p)
{
    HTMLBodyElement *This = impl_from_IHTMLBodyElement(iface);

    TRACE("(%p)->(%p)\n", This, p);

    return get_node_event(&This->element.node, EVENTID_BEFOREUNLOAD, p);
}

static HRESULT WINAPI HTMLBodyElement_createTextRange(IHTMLBodyElement *iface, IHTMLTxtRange **range)
{
    HTMLBodyElement *This = impl_from_IHTMLBodyElement(iface);
    nsIDOMRange *nsrange = NULL;
    nsresult nsres;
    HRESULT hres;

    TRACE("(%p)->(%p)\n", This, range);

    if(!This->element.node.doc->dom_document) {
        WARN("No dom_document\n");
        return E_UNEXPECTED;
    }

    nsres = nsIDOMDocument_CreateRange(This->element.node.doc->dom_document, &nsrange);
    if(NS_SUCCEEDED(nsres)) {
        nsres = nsIDOMRange_SelectNodeContents(nsrange, This->element.node.nsnode);
        if(NS_FAILED(nsres))
            ERR("SelectNodeContents failed: %08lx\n", nsres);
    }else {
        ERR("CreateRange failed: %08lx\n", nsres);
    }

    hres = HTMLTxtRange_Create(This->element.node.doc, nsrange, range);

    nsIDOMRange_Release(nsrange);
    return hres;
}

static const IHTMLBodyElementVtbl HTMLBodyElementVtbl = {
    HTMLBodyElement_QueryInterface,
    HTMLBodyElement_AddRef,
    HTMLBodyElement_Release,
    HTMLBodyElement_GetTypeInfoCount,
    HTMLBodyElement_GetTypeInfo,
    HTMLBodyElement_GetIDsOfNames,
    HTMLBodyElement_Invoke,
    HTMLBodyElement_put_background,
    HTMLBodyElement_get_background,
    HTMLBodyElement_put_bgProperties,
    HTMLBodyElement_get_bgProperties,
    HTMLBodyElement_put_leftMargin,
    HTMLBodyElement_get_leftMargin,
    HTMLBodyElement_put_topMargin,
    HTMLBodyElement_get_topMargin,
    HTMLBodyElement_put_rightMargin,
    HTMLBodyElement_get_rightMargin,
    HTMLBodyElement_put_bottomMargin,
    HTMLBodyElement_get_bottomMargin,
    HTMLBodyElement_put_noWrap,
    HTMLBodyElement_get_noWrap,
    HTMLBodyElement_put_bgColor,
    HTMLBodyElement_get_bgColor,
    HTMLBodyElement_put_text,
    HTMLBodyElement_get_text,
    HTMLBodyElement_put_link,
    HTMLBodyElement_get_link,
    HTMLBodyElement_put_vLink,
    HTMLBodyElement_get_vLink,
    HTMLBodyElement_put_aLink,
    HTMLBodyElement_get_aLink,
    HTMLBodyElement_put_onload,
    HTMLBodyElement_get_onload,
    HTMLBodyElement_put_onunload,
    HTMLBodyElement_get_onunload,
    HTMLBodyElement_put_scroll,
    HTMLBodyElement_get_scroll,
    HTMLBodyElement_put_onselect,
    HTMLBodyElement_get_onselect,
    HTMLBodyElement_put_onbeforeunload,
    HTMLBodyElement_get_onbeforeunload,
    HTMLBodyElement_createTextRange
};

static inline HTMLBodyElement *impl_from_IHTMLBodyElement2(IHTMLBodyElement2 *iface)
{
    return CONTAINING_RECORD(iface, HTMLBodyElement, IHTMLBodyElement2_iface);
}

DISPEX_IDISPATCH_IMPL(HTMLBodyElement2, IHTMLBodyElement2,
                      impl_from_IHTMLBodyElement2(iface)->element.node.event_target.dispex)

static HRESULT WINAPI HTMLBodyElement2_put_onbeforeprint(IHTMLBodyElement2 *iface, VARIANT v)
{
    HTMLBodyElement *This = impl_from_IHTMLBodyElement2(iface);

    TRACE("(%p)->(%s)\n", This, debugstr_variant(&v));

    return set_node_event(&This->element.node, EVENTID_BEFOREPRINT, &v);
}

static HRESULT WINAPI HTMLBodyElement2_get_onbeforeprint(IHTMLBodyElement2 *iface, VARIANT *p)
{
    HTMLBodyElement *This = impl_from_IHTMLBodyElement2(iface);

    TRACE("(%p)->(%p)\n", This, p);

    return get_node_event(&This->element.node, EVENTID_BEFOREPRINT, p);
}

static HRESULT WINAPI HTMLBodyElement2_put_onafterprint(IHTMLBodyElement2 *iface, VARIANT v)
{
    HTMLBodyElement *This = impl_from_IHTMLBodyElement2(iface);

    TRACE("(%p)->(%s)\n", This, debugstr_variant(&v));

    return set_node_event(&This->element.node, EVENTID_AFTERPRINT, &v);
}

static HRESULT WINAPI HTMLBodyElement2_get_onafterprint(IHTMLBodyElement2 *iface, VARIANT *p)
{
    HTMLBodyElement *This = impl_from_IHTMLBodyElement2(iface);

    TRACE("(%p)->(%p)\n", This, p);

    return get_node_event(&This->element.node, EVENTID_AFTERPRINT, p);
}

static const IHTMLBodyElement2Vtbl HTMLBodyElement2Vtbl = {
    HTMLBodyElement2_QueryInterface,
    HTMLBodyElement2_AddRef,
    HTMLBodyElement2_Release,
    HTMLBodyElement2_GetTypeInfoCount,
    HTMLBodyElement2_GetTypeInfo,
    HTMLBodyElement2_GetIDsOfNames,
    HTMLBodyElement2_Invoke,
    HTMLBodyElement2_put_onbeforeprint,
    HTMLBodyElement2_get_onbeforeprint,
    HTMLBodyElement2_put_onafterprint,
    HTMLBodyElement2_get_onafterprint
};

static inline HTMLBodyElement *impl_from_IHTMLTextContainer(IHTMLTextContainer *iface)
{
    return CONTAINING_RECORD(iface, HTMLBodyElement, IHTMLTextContainer_iface);
}

DISPEX_IDISPATCH_IMPL(HTMLTextContainer, IHTMLTextContainer,
                      impl_from_IHTMLTextContainer(iface)->element.node.event_target.dispex)

static HRESULT WINAPI HTMLTextContainer_createControlRange(IHTMLTextContainer *iface,
                                                           IDispatch **range)
{
    HTMLBodyElement *This = impl_from_IHTMLTextContainer(iface);
    FIXME("(%p)->(%p)\n", This, range);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLTextContainer_get_scrollHeight(IHTMLTextContainer *iface, LONG *p)
{
    HTMLBodyElement *This = impl_from_IHTMLTextContainer(iface);

    TRACE("(%p)->(%p)\n", This, p);

    return IHTMLElement2_get_scrollHeight(&This->element.IHTMLElement2_iface, p);
}

static HRESULT WINAPI HTMLTextContainer_get_scrollWidth(IHTMLTextContainer *iface, LONG *p)
{
    HTMLBodyElement *This = impl_from_IHTMLTextContainer(iface);

    TRACE("(%p)->(%p)\n", This, p);

    return IHTMLElement2_get_scrollWidth(&This->element.IHTMLElement2_iface, p);
}

static HRESULT WINAPI HTMLTextContainer_put_scrollTop(IHTMLTextContainer *iface, LONG v)
{
    HTMLBodyElement *This = impl_from_IHTMLTextContainer(iface);

    TRACE("(%p)->(%ld)\n", This, v);

    return IHTMLElement2_put_scrollTop(&This->element.IHTMLElement2_iface, v);
}

static HRESULT WINAPI HTMLTextContainer_get_scrollTop(IHTMLTextContainer *iface, LONG *p)
{
    HTMLBodyElement *This = impl_from_IHTMLTextContainer(iface);

    TRACE("(%p)->(%p)\n", This, p);

    return IHTMLElement2_get_scrollTop(&This->element.IHTMLElement2_iface, p);
}

static HRESULT WINAPI HTMLTextContainer_put_scrollLeft(IHTMLTextContainer *iface, LONG v)
{
    HTMLBodyElement *This = impl_from_IHTMLTextContainer(iface);

    TRACE("(%p)->(%ld)\n", This, v);

    return IHTMLElement2_put_scrollLeft(&This->element.IHTMLElement2_iface, v);
}

static HRESULT WINAPI HTMLTextContainer_get_scrollLeft(IHTMLTextContainer *iface, LONG *p)
{
    HTMLBodyElement *This = impl_from_IHTMLTextContainer(iface);

    TRACE("(%p)->(%p)\n", This, p);

    return IHTMLElement2_get_scrollLeft(&This->element.IHTMLElement2_iface, p);
}

static HRESULT WINAPI HTMLTextContainer_put_onscroll(IHTMLTextContainer *iface, VARIANT v)
{
    HTMLBodyElement *This = impl_from_IHTMLTextContainer(iface);
    FIXME("(%p)->()\n", This);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLTextContainer_get_onscroll(IHTMLTextContainer *iface, VARIANT *p)
{
    HTMLBodyElement *This = impl_from_IHTMLTextContainer(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static const IHTMLTextContainerVtbl HTMLTextContainerVtbl = {
    HTMLTextContainer_QueryInterface,
    HTMLTextContainer_AddRef,
    HTMLTextContainer_Release,
    HTMLTextContainer_GetTypeInfoCount,
    HTMLTextContainer_GetTypeInfo,
    HTMLTextContainer_GetIDsOfNames,
    HTMLTextContainer_Invoke,
    HTMLTextContainer_createControlRange,
    HTMLTextContainer_get_scrollHeight,
    HTMLTextContainer_get_scrollWidth,
    HTMLTextContainer_put_scrollTop,
    HTMLTextContainer_get_scrollTop,
    HTMLTextContainer_put_scrollLeft,
    HTMLTextContainer_get_scrollLeft,
    HTMLTextContainer_put_onscroll,
    HTMLTextContainer_get_onscroll
};

EventTarget *get_window_event_prop_target(HTMLDOMNode *node, int eid)
{
    HTMLDocumentNode *doc = node->doc;
    cpp_bool connected = FALSE;
    nsresult nsres;

    /* FIXME: in IE9+ a detached element's onscroll reads VT_EMPTY while its put is kept. */
    if(eid == EVENTID_SCROLL && dispex_compat_mode(&node->event_target.dispex) >= COMPAT_MODE_IE9)
        return &node->event_target;

    switch(eid) {
    case EVENTID_AFTERPRINT:
    case EVENTID_BEFOREPRINT:
    case EVENTID_BEFOREUNLOAD:
    case EVENTID_BLUR:
    case EVENTID_FOCUS:
    case EVENTID_LOAD:
    case EVENTID_PAGEHIDE:
    case EVENTID_PAGESHOW:
    case EVENTID_RESIZE:
    case EVENTID_SCROLL:
    case EVENTID_UNLOAD:
        break;
    default:
        return &node->event_target;
    }

    if(!doc || !doc->window)
        return &node->event_target;

    nsres = nsIDOMNode_Contains(doc->node.nsnode, node->nsnode, &connected);
    return NS_SUCCEEDED(nsres) && connected ? &doc->window->event_target : NULL;
}

static BOOL HTMLBodyElement_is_text_edit(HTMLDOMNode *iface)
{
    return TRUE;
}

static BOOL HTMLBodyElement_is_settable(HTMLDOMNode *iface, DISPID dispid)
{
    switch(dispid) {
    case DISPID_IHTMLELEMENT_OUTERTEXT:
        return FALSE;
    default:
        return TRUE;
    }
}

static inline HTMLBodyElement *impl_from_DispatchEx(DispatchEx *iface)
{
    return CONTAINING_RECORD(iface, HTMLBodyElement, element.node.event_target.dispex);
}

static void *HTMLBodyElement_query_interface(DispatchEx *dispex, REFIID riid)
{
    HTMLBodyElement *This = impl_from_DispatchEx(dispex);

    if(IsEqualGUID(&IID_IHTMLBodyElement, riid))
        return &This->IHTMLBodyElement_iface;
    if(IsEqualGUID(&IID_IHTMLBodyElement2, riid))
        return &This->IHTMLBodyElement2_iface;
    if(IsEqualGUID(&IID_IHTMLTextContainer, riid))
        return &This->IHTMLTextContainer_iface;

    return HTMLElement_query_interface(&This->element.node.event_target.dispex, riid);
}

static void HTMLBodyElement_traverse(DispatchEx *dispex, nsCycleCollectionTraversalCallback *cb)
{
    HTMLBodyElement *This = impl_from_DispatchEx(dispex);
    HTMLElement_traverse(dispex, cb);

    if(This->nsbody)
        note_cc_edge((nsISupports*)This->nsbody, "nsbody", cb);
}

static void HTMLBodyElement_unlink(DispatchEx *dispex)
{
    HTMLBodyElement *This = impl_from_DispatchEx(dispex);
    HTMLElement_unlink(dispex);
    unlink_ref(&This->nsbody);
}

static const cpc_entry_t HTMLBodyElement_cpc[] = {
    {&DIID_HTMLTextContainerEvents},
    {&IID_IPropertyNotifySink},
    HTMLELEMENT_CPC,
    {NULL}
};

static const NodeImplVtbl HTMLBodyElementImplVtbl = {
    .clsid                 = &CLSID_HTMLBody,
    .cpc_entries           = HTMLBodyElement_cpc,
    .clone                 = HTMLElement_clone,
    .get_attr_col          = HTMLElement_get_attr_col,
    .get_event_prop_target = get_window_event_prop_target,
    .is_text_edit          = HTMLBodyElement_is_text_edit,
    .is_settable           = HTMLBodyElement_is_settable
};

static const event_target_vtbl_t HTMLBodyElement_event_target_vtbl = {
    {
        HTMLELEMENT_DISPEX_VTBL_ENTRIES,
        .query_interface= HTMLBodyElement_query_interface,
        .destructor     = HTMLElement_destructor,
        .traverse       = HTMLBodyElement_traverse,
        .unlink         = HTMLBodyElement_unlink
    },
    HTMLELEMENT_EVENT_TARGET_VTBL_ENTRIES,
    .handle_event       = HTMLElement_handle_event
};

static const tid_t HTMLBodyElement_iface_tids[] = {
    IHTMLBodyElement_tid,
    IHTMLBodyElement2_tid,
    IHTMLTextContainer_tid,
    0
};

dispex_static_data_t HTMLBodyElement_dispex = {
    .id           = OBJID_HTMLBodyElement,
    .prototype_id = OBJID_HTMLElement,
    .vtbl         = &HTMLBodyElement_event_target_vtbl.dispex_vtbl,
    .disp_tid     = DispHTMLBody_tid,
    .iface_tids   = HTMLBodyElement_iface_tids,
    .init_info    = HTMLElement_init_dispex_info,
};

HRESULT HTMLBodyElement_Create(HTMLDocumentNode *doc, nsIDOMElement *nselem, HTMLElement **elem)
{
    HTMLBodyElement *ret;
    nsresult nsres;

    ret = calloc(1, sizeof(HTMLBodyElement));
    if(!ret)
        return E_OUTOFMEMORY;

    ret->IHTMLBodyElement_iface.lpVtbl = &HTMLBodyElementVtbl;
    ret->IHTMLBodyElement2_iface.lpVtbl = &HTMLBodyElement2Vtbl;
    ret->IHTMLTextContainer_iface.lpVtbl = &HTMLTextContainerVtbl;
    ret->element.node.vtbl = &HTMLBodyElementImplVtbl;

    HTMLElement_Init(&ret->element, doc, nselem, &HTMLBodyElement_dispex);

    nsres = nsIDOMElement_QueryInterface(nselem, &IID_nsIDOMHTMLBodyElement, (void**)&ret->nsbody);
    assert(nsres == NS_OK);

    *elem = &ret->element;
    return S_OK;
}
