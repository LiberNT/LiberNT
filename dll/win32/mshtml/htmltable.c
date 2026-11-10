/*
 * Copyright 2007,2008,2012 Jacek Caban for CodeWeavers
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

#define COBJMACROS

#include "windef.h"
#include "winbase.h"
#include "winuser.h"
#include "ole2.h"
#include "mshtmdid.h"

#include "wine/debug.h"

#include "mshtml_private.h"
#include "htmlevent.h"

WINE_DEFAULT_DEBUG_CHANNEL(mshtml);

typedef struct {
    const WCHAR *name;
    const WCHAR *value;
} table_keyword_t;

static const table_keyword_t table_align_keywords[] = {
    {L"left", L"left"}, {L"center", L"center"}, {L"middle", L"center"}, {L"right", L"right"}, {NULL}
};

static const table_keyword_t table_valign_keywords[] = {
    {L"top", L"top"}, {L"middle", L"middle"}, {L"center", L"middle"}, {L"bottom", L"bottom"}, {L"baseline", L"baseline"}, {NULL}
};

static const WCHAR *find_table_keyword(const WCHAR *v, const table_keyword_t *keywords)
{
    for(; keywords->name; keywords++) {
        if(!wcsicmp(v, keywords->name))
            return keywords->value;
    }
    return NULL;
}

static HRESULT put_table_keyword(HTMLElement *elem, const WCHAR *name, BSTR v, const table_keyword_t *keywords)
{
    const WCHAR *keyword;

    if(!v || !*v)
        return element_remove_attribute(elem, name);
    if(!(keyword = find_table_keyword(v, keywords)))
        return E_INVALIDARG;
    return elem_put_reflected_string(elem, name, (BSTR)keyword);
}

static HRESULT get_table_keyword(HTMLElement *elem, const WCHAR *name, BSTR *p, const table_keyword_t *keywords,
        const WCHAR *ie9_invalid)
{
    const WCHAR *keyword = NULL;
    HRESULT hres;
    BSTR val;

    hres = elem_get_attr_string(elem, name, &val);
    if(FAILED(hres))
        return hres;
    if(val) {
        keyword = find_table_keyword(val, keywords);
        if(!keyword && dispex_compat_mode(&elem->node.event_target.dispex) >= COMPAT_MODE_IE9)
            keyword = ie9_invalid;
    }
    SysFreeString(val);
    if(!keyword) {
        *p = NULL;
        return S_OK;
    }
    return (*p = SysAllocString(keyword)) ? S_OK : E_OUTOFMEMORY;
}

static BOOL parse_table_length(const WCHAR *v, WCHAR *buf, size_t size)
{
    double val;
    WCHAR *end;

    while(iswspace(*v))
        v++;
    if(*v == '+')
        v++;
    if(!iswdigit(*v) && (*v != '.' || !iswdigit(v[1])))
        return FALSE;
    val = wcstod(v, &end);
    while(iswspace(*end))
        end++;
    if(val < 1.0 || val > INT_MAX)
        return FALSE;
    swprintf(buf, size, *end == '%' ? L"%d%%" : L"%d", (int)val);
    return TRUE;
}

static HRESULT put_table_length(HTMLElement *elem, const WCHAR *name, VARIANT *v)
{
    WCHAR buf[16];
    VARIANT str;
    HRESULT hres;
    BOOL valid;

    if(V_VT(v) == VT_NULL || (V_VT(v) == VT_BSTR && (!V_BSTR(v) || !*V_BSTR(v))))
        return element_remove_attribute(elem, name);

    V_VT(&str) = VT_EMPTY;
    hres = VariantChangeType(&str, v, 0, VT_BSTR);
    if(FAILED(hres))
        return hres;
    valid = parse_table_length(V_BSTR(&str), buf, ARRAY_SIZE(buf));
    VariantClear(&str);
    if(!valid)
        return E_INVALIDARG;
    return elem_put_reflected_string(elem, name, buf);
}

static HRESULT get_table_length(HTMLElement *elem, const WCHAR *name, VARIANT *p)
{
    WCHAR buf[16];
    HRESULT hres;
    BSTR val;

    V_VT(p) = VT_BSTR;
    V_BSTR(p) = NULL;
    hres = elem_get_attr_string(elem, name, &val);
    if(FAILED(hres) || !val)
        return hres;
    if(parse_table_length(val, buf, ARRAY_SIZE(buf)) && !(V_BSTR(p) = SysAllocString(buf)))
        hres = E_OUTOFMEMORY;
    SysFreeString(val);
    return hres;
}

struct HTMLTableCell {
    HTMLElement element;

    IHTMLTableCell IHTMLTableCell_iface;
    IHTMLTableCell2 IHTMLTableCell2_iface;
    IHTMLTableCell3 IHTMLTableCell3_iface;

    nsIDOMHTMLTableCellElement *nscell;
};

static inline HTMLTableCell *impl_from_IHTMLTableCell(IHTMLTableCell *iface)
{
    return CONTAINING_RECORD(iface, HTMLTableCell, IHTMLTableCell_iface);
}

DISPEX_IDISPATCH_IMPL(HTMLTableCell, IHTMLTableCell,
                      impl_from_IHTMLTableCell(iface)->element.node.event_target.dispex)

static HRESULT WINAPI HTMLTableCell_put_rowSpan(IHTMLTableCell *iface, LONG v)
{
    HTMLTableCell *This = impl_from_IHTMLTableCell(iface);
    nsresult nsres;

    TRACE("(%p)->(%ld)\n", This, v);

    if(v <= 0)
        return E_INVALIDARG;

    nsres = nsIDOMHTMLTableCellElement_SetRowSpan(This->nscell, v);
    if(NS_FAILED(nsres)) {
        ERR("SetRowSpan failed: %08lx\n", nsres);
        return E_FAIL;
    }

    return S_OK;
}

static HRESULT WINAPI HTMLTableCell_get_rowSpan(IHTMLTableCell *iface, LONG *p)
{
    HTMLTableCell *This = impl_from_IHTMLTableCell(iface);
    nsresult nsres;

    TRACE("(%p)->(%p)\n", This, p);

    nsres = nsIDOMHTMLTableCellElement_GetRowSpan(This->nscell, p);
    if(NS_FAILED(nsres)) {
        ERR("GetRowSpan failed: %08lx\n", nsres);
        return E_FAIL;
    }

    return S_OK;
}

static HRESULT WINAPI HTMLTableCell_put_colSpan(IHTMLTableCell *iface, LONG v)
{
    HTMLTableCell *This = impl_from_IHTMLTableCell(iface);
    nsresult nsres;

    TRACE("(%p)->(%ld)\n", This, v);

    if(v <= 0)
        return E_INVALIDARG;

    nsres = nsIDOMHTMLTableCellElement_SetColSpan(This->nscell, v);
    if(NS_FAILED(nsres)) {
        ERR("SetColSpan failed: %08lx\n", nsres);
        return E_FAIL;
    }

    return S_OK;
}

static HRESULT WINAPI HTMLTableCell_get_colSpan(IHTMLTableCell *iface, LONG *p)
{
    HTMLTableCell *This = impl_from_IHTMLTableCell(iface);
    nsresult nsres;

    TRACE("(%p)->(%p)\n", This, p);

    nsres = nsIDOMHTMLTableCellElement_GetColSpan(This->nscell, p);
    if(NS_FAILED(nsres)) {
        ERR("GetColSpan failed: %08lx\n", nsres);
        return E_FAIL;
    }

    return S_OK;
}

static HRESULT WINAPI HTMLTableCell_put_align(IHTMLTableCell *iface, BSTR v)
{
    HTMLTableCell *This = impl_from_IHTMLTableCell(iface);
    nsAString str;
    nsresult nsres;

    TRACE("(%p)->(%s)\n", This, debugstr_w(v));

    nsAString_InitDepend(&str, v);
    nsres = nsIDOMHTMLTableCellElement_SetAlign(This->nscell, &str);
    nsAString_Finish(&str);
    if (NS_FAILED(nsres)) {
        ERR("Set Align failed: %08lx\n", nsres);
        return E_FAIL;
    }
    return S_OK;
}

static HRESULT WINAPI HTMLTableCell_get_align(IHTMLTableCell *iface, BSTR *p)
{
    HTMLTableCell *This = impl_from_IHTMLTableCell(iface);
    nsAString str;
    nsresult nsres;

    TRACE("(%p)->(%p)\n", This, p);

    nsAString_Init(&str, NULL);
    nsres = nsIDOMHTMLTableCellElement_GetAlign(This->nscell, &str);

    return return_nsstr(nsres, &str, p);
}

static HRESULT WINAPI HTMLTableCell_put_vAlign(IHTMLTableCell *iface, BSTR v)
{
    HTMLTableCell *This = impl_from_IHTMLTableCell(iface);
    FIXME("(%p)->(%s)\n", This, debugstr_w(v));
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLTableCell_get_vAlign(IHTMLTableCell *iface, BSTR *p)
{
    HTMLTableCell *This = impl_from_IHTMLTableCell(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLTableCell_put_bgColor(IHTMLTableCell *iface, VARIANT v)
{
    HTMLTableCell *This = impl_from_IHTMLTableCell(iface);

    TRACE("(%p)->(%s)\n", This, debugstr_variant(&v));

    return elem_put_color_attr(&This->element, L"bgColor", &v);
}

static HRESULT WINAPI HTMLTableCell_get_bgColor(IHTMLTableCell *iface, VARIANT *p)
{
    HTMLTableCell *This = impl_from_IHTMLTableCell(iface);

    TRACE("(%p)->(%p)\n", This, p);

    return elem_get_color_attr(&This->element, L"bgColor", p);
}

static HRESULT WINAPI HTMLTableCell_put_noWrap(IHTMLTableCell *iface, VARIANT_BOOL v)
{
    HTMLTableCell *This = impl_from_IHTMLTableCell(iface);
    FIXME("(%p)->(%x)\n", This, v);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLTableCell_get_noWrap(IHTMLTableCell *iface, VARIANT_BOOL *p)
{
    HTMLTableCell *This = impl_from_IHTMLTableCell(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLTableCell_put_background(IHTMLTableCell *iface, BSTR v)
{
    HTMLTableCell *This = impl_from_IHTMLTableCell(iface);
    FIXME("(%p)->(%s)\n", This, debugstr_w(v));
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLTableCell_get_background(IHTMLTableCell *iface, BSTR *p)
{
    HTMLTableCell *This = impl_from_IHTMLTableCell(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLTableCell_put_borderColor(IHTMLTableCell *iface, VARIANT v)
{
    HTMLTableCell *This = impl_from_IHTMLTableCell(iface);

    TRACE("(%p)->(%s)\n", This, debugstr_variant(&v));

    return elem_put_color_attr(&This->element, L"borderColor", &v);
}

static HRESULT WINAPI HTMLTableCell_get_borderColor(IHTMLTableCell *iface, VARIANT *p)
{
    HTMLTableCell *This = impl_from_IHTMLTableCell(iface);

    TRACE("(%p)->(%p)\n", This, p);

    return elem_get_color_attr(&This->element, L"borderColor", p);
}

static HRESULT WINAPI HTMLTableCell_put_borderColorLight(IHTMLTableCell *iface, VARIANT v)
{
    HTMLTableCell *This = impl_from_IHTMLTableCell(iface);
    FIXME("(%p)->(%s)\n", This, debugstr_variant(&v));
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLTableCell_get_borderColorLight(IHTMLTableCell *iface, VARIANT *p)
{
    HTMLTableCell *This = impl_from_IHTMLTableCell(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLTableCell_put_borderColorDark(IHTMLTableCell *iface, VARIANT v)
{
    HTMLTableCell *This = impl_from_IHTMLTableCell(iface);
    FIXME("(%p)->(%s)\n", This, debugstr_variant(&v));
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLTableCell_get_borderColorDark(IHTMLTableCell *iface, VARIANT *p)
{
    HTMLTableCell *This = impl_from_IHTMLTableCell(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLTableCell_put_width(IHTMLTableCell *iface, VARIANT v)
{
    HTMLTableCell *This = impl_from_IHTMLTableCell(iface);
    nsAString nsstr;
    nsresult nsres;
    HRESULT hres;

    TRACE("(%p)->(%s)\n", This, debugstr_variant(&v));

    hres = variant_to_nsstr(&v, FALSE, &nsstr);
    if(FAILED(hres))
        return hres;

    nsres = nsIDOMHTMLTableCellElement_SetWidth(This->nscell, &nsstr);
    nsAString_Finish(&nsstr);
    return map_nsresult(nsres);
}

static HRESULT WINAPI HTMLTableCell_get_width(IHTMLTableCell *iface, VARIANT *p)
{
    HTMLTableCell *This = impl_from_IHTMLTableCell(iface);
    nsAString nsstr;
    nsresult nsres;

    TRACE("(%p)->(%p)\n", This, p);

    nsAString_Init(&nsstr, NULL);
    nsres = nsIDOMHTMLTableCellElement_GetWidth(This->nscell, &nsstr);
    return return_nsstr_variant(nsres, &nsstr, NSSTR_IMPLICIT_PX, p);
}

static HRESULT WINAPI HTMLTableCell_put_height(IHTMLTableCell *iface, VARIANT v)
{
    HTMLTableCell *This = impl_from_IHTMLTableCell(iface);
    nsAString nsstr;
    nsresult nsres;
    HRESULT hres;

    TRACE("(%p)->(%s)\n", This, debugstr_variant(&v));

    hres = variant_to_nsstr(&v, FALSE, &nsstr);
    if(FAILED(hres))
        return hres;

    nsres = nsIDOMHTMLTableCellElement_SetHeight(This->nscell, &nsstr);
    nsAString_Finish(&nsstr);
    return map_nsresult(nsres);
}

static HRESULT WINAPI HTMLTableCell_get_height(IHTMLTableCell *iface, VARIANT *p)
{
    HTMLTableCell *This = impl_from_IHTMLTableCell(iface);
    nsAString nsstr;
    nsresult nsres;

    TRACE("(%p)->(%p)\n", This, p);

    nsAString_Init(&nsstr, NULL);
    nsres = nsIDOMHTMLTableCellElement_GetHeight(This->nscell, &nsstr);
    return return_nsstr_variant(nsres, &nsstr, NSSTR_IMPLICIT_PX, p);
}

static HRESULT WINAPI HTMLTableCell_get_cellIndex(IHTMLTableCell *iface, LONG *p)
{
    HTMLTableCell *This = impl_from_IHTMLTableCell(iface);
    nsresult nsres;

    TRACE("(%p)->(%p)\n", This, p);
    nsres = nsIDOMHTMLTableCellElement_GetCellIndex(This->nscell, p);
    if (NS_FAILED(nsres)) {
        ERR("Get CellIndex failed: %08lx\n", nsres);
        return E_FAIL;
    }

    return S_OK;
}

static const IHTMLTableCellVtbl HTMLTableCellVtbl = {
    HTMLTableCell_QueryInterface,
    HTMLTableCell_AddRef,
    HTMLTableCell_Release,
    HTMLTableCell_GetTypeInfoCount,
    HTMLTableCell_GetTypeInfo,
    HTMLTableCell_GetIDsOfNames,
    HTMLTableCell_Invoke,
    HTMLTableCell_put_rowSpan,
    HTMLTableCell_get_rowSpan,
    HTMLTableCell_put_colSpan,
    HTMLTableCell_get_colSpan,
    HTMLTableCell_put_align,
    HTMLTableCell_get_align,
    HTMLTableCell_put_vAlign,
    HTMLTableCell_get_vAlign,
    HTMLTableCell_put_bgColor,
    HTMLTableCell_get_bgColor,
    HTMLTableCell_put_noWrap,
    HTMLTableCell_get_noWrap,
    HTMLTableCell_put_background,
    HTMLTableCell_get_background,
    HTMLTableCell_put_borderColor,
    HTMLTableCell_get_borderColor,
    HTMLTableCell_put_borderColorLight,
    HTMLTableCell_get_borderColorLight,
    HTMLTableCell_put_borderColorDark,
    HTMLTableCell_get_borderColorDark,
    HTMLTableCell_put_width,
    HTMLTableCell_get_width,
    HTMLTableCell_put_height,
    HTMLTableCell_get_height,
    HTMLTableCell_get_cellIndex
};

static inline HTMLTableCell *impl_from_IHTMLTableCell2(IHTMLTableCell2 *iface)
{
    return CONTAINING_RECORD(iface, HTMLTableCell, IHTMLTableCell2_iface);
}

DISPEX_IDISPATCH_IMPL(HTMLTableCell2, IHTMLTableCell2,
                      impl_from_IHTMLTableCell2(iface)->element.node.event_target.dispex)

static HRESULT WINAPI HTMLTableCell2_put_abbr(IHTMLTableCell2 *iface, BSTR v)
{
    HTMLTableCell *This = impl_from_IHTMLTableCell2(iface);
    TRACE("(%p)->(%s)\n", This, debugstr_w(v));
    return elem_put_reflected_string(&This->element, L"abbr", v);
}

static HRESULT WINAPI HTMLTableCell2_get_abbr(IHTMLTableCell2 *iface, BSTR *p)
{
    HTMLTableCell *This = impl_from_IHTMLTableCell2(iface);
    TRACE("(%p)->(%p)\n", This, p);
    return elem_get_attr_string(&This->element, L"abbr", p);
}

static HRESULT WINAPI HTMLTableCell2_put_axis(IHTMLTableCell2 *iface, BSTR v)
{
    HTMLTableCell *This = impl_from_IHTMLTableCell2(iface);
    TRACE("(%p)->(%s)\n", This, debugstr_w(v));
    return elem_put_reflected_string(&This->element, L"axis", v);
}

static HRESULT WINAPI HTMLTableCell2_get_axis(IHTMLTableCell2 *iface, BSTR *p)
{
    HTMLTableCell *This = impl_from_IHTMLTableCell2(iface);
    TRACE("(%p)->(%p)\n", This, p);
    return elem_get_attr_string(&This->element, L"axis", p);
}

static HRESULT WINAPI HTMLTableCell2_put_ch(IHTMLTableCell2 *iface, BSTR v)
{
    HTMLTableCell *This = impl_from_IHTMLTableCell2(iface);
    TRACE("(%p)->(%s)\n", This, debugstr_w(v));
    return elem_put_reflected_string(&This->element, L"ch", v);
}

static HRESULT WINAPI HTMLTableCell2_get_ch(IHTMLTableCell2 *iface, BSTR *p)
{
    HTMLTableCell *This = impl_from_IHTMLTableCell2(iface);
    TRACE("(%p)->(%p)\n", This, p);
    return elem_get_attr_string(&This->element, L"ch", p);
}

static HRESULT WINAPI HTMLTableCell2_put_chOff(IHTMLTableCell2 *iface, BSTR v)
{
    HTMLTableCell *This = impl_from_IHTMLTableCell2(iface);
    TRACE("(%p)->(%s)\n", This, debugstr_w(v));
    return elem_put_reflected_string(&This->element, L"choff", v);
}

static HRESULT WINAPI HTMLTableCell2_get_chOff(IHTMLTableCell2 *iface, BSTR *p)
{
    HTMLTableCell *This = impl_from_IHTMLTableCell2(iface);
    TRACE("(%p)->(%p)\n", This, p);
    return elem_get_attr_string(&This->element, L"choff", p);
}

static HRESULT WINAPI HTMLTableCell2_put_headers(IHTMLTableCell2 *iface, BSTR v)
{
    HTMLTableCell *This = impl_from_IHTMLTableCell2(iface);
    TRACE("(%p)->(%s)\n", This, debugstr_w(v));
    return elem_put_reflected_string(&This->element, L"headers", v);
}

static HRESULT WINAPI HTMLTableCell2_get_headers(IHTMLTableCell2 *iface, BSTR *p)
{
    HTMLTableCell *This = impl_from_IHTMLTableCell2(iface);
    TRACE("(%p)->(%p)\n", This, p);
    return elem_get_attr_string(&This->element, L"headers", p);
}

static HRESULT WINAPI HTMLTableCell2_put_scope(IHTMLTableCell2 *iface, BSTR v)
{
    HTMLTableCell *This = impl_from_IHTMLTableCell2(iface);
    TRACE("(%p)->(%s)\n", This, debugstr_w(v));
    return elem_put_reflected_string(&This->element, L"scope", v);
}

static HRESULT WINAPI HTMLTableCell2_get_scope(IHTMLTableCell2 *iface, BSTR *p)
{
    HTMLTableCell *This = impl_from_IHTMLTableCell2(iface);
    TRACE("(%p)->(%p)\n", This, p);
    return elem_get_attr_string(&This->element, L"scope", p);
}

static const IHTMLTableCell2Vtbl HTMLTableCell2Vtbl = {
    HTMLTableCell2_QueryInterface,
    HTMLTableCell2_AddRef,
    HTMLTableCell2_Release,
    HTMLTableCell2_GetTypeInfoCount,
    HTMLTableCell2_GetTypeInfo,
    HTMLTableCell2_GetIDsOfNames,
    HTMLTableCell2_Invoke,
    HTMLTableCell2_put_abbr,
    HTMLTableCell2_get_abbr,
    HTMLTableCell2_put_axis,
    HTMLTableCell2_get_axis,
    HTMLTableCell2_put_ch,
    HTMLTableCell2_get_ch,
    HTMLTableCell2_put_chOff,
    HTMLTableCell2_get_chOff,
    HTMLTableCell2_put_headers,
    HTMLTableCell2_get_headers,
    HTMLTableCell2_put_scope,
    HTMLTableCell2_get_scope,
};

static inline HTMLTableCell *impl_from_IHTMLTableCell3(IHTMLTableCell3 *iface)
{
    return CONTAINING_RECORD(iface, HTMLTableCell, IHTMLTableCell3_iface);
}

DISPEX_IDISPATCH_IMPL(HTMLTableCell3, IHTMLTableCell3,
                      impl_from_IHTMLTableCell3(iface)->element.node.event_target.dispex)

static HRESULT WINAPI HTMLTableCell3_put_ch(IHTMLTableCell3 *iface, BSTR v)
{
    HTMLTableCell *This = impl_from_IHTMLTableCell3(iface);
    TRACE("(%p)->(%s)\n", This, debugstr_w(v));
    return elem_put_reflected_string(&This->element, L"char", v);
}

static HRESULT WINAPI HTMLTableCell3_get_ch(IHTMLTableCell3 *iface, BSTR *p)
{
    HTMLTableCell *This = impl_from_IHTMLTableCell3(iface);
    TRACE("(%p)->(%p)\n", This, p);
    return elem_get_attr_string(&This->element, L"char", p);
}

static HRESULT WINAPI HTMLTableCell3_put_chOff(IHTMLTableCell3 *iface, BSTR v)
{
    HTMLTableCell *This = impl_from_IHTMLTableCell3(iface);
    TRACE("(%p)->(%s)\n", This, debugstr_w(v));
    return elem_put_reflected_string(&This->element, L"charoff", v);
}

static HRESULT WINAPI HTMLTableCell3_get_chOff(IHTMLTableCell3 *iface, BSTR *p)
{
    HTMLTableCell *This = impl_from_IHTMLTableCell3(iface);
    TRACE("(%p)->(%p)\n", This, p);
    return elem_get_attr_string(&This->element, L"charoff", p);
}

static const IHTMLTableCell3Vtbl HTMLTableCell3Vtbl = {
    HTMLTableCell3_QueryInterface,
    HTMLTableCell3_AddRef,
    HTMLTableCell3_Release,
    HTMLTableCell3_GetTypeInfoCount,
    HTMLTableCell3_GetTypeInfo,
    HTMLTableCell3_GetIDsOfNames,
    HTMLTableCell3_Invoke,
    HTMLTableCell3_put_ch,
    HTMLTableCell3_get_ch,
    HTMLTableCell3_put_chOff,
    HTMLTableCell3_get_chOff,
};

static inline HTMLTableCell *HTMLTableCell_from_DispatchEx(DispatchEx *iface)
{
    return CONTAINING_RECORD(iface, HTMLTableCell, element.node.event_target.dispex);
}

static void *HTMLTableCell_query_interface(DispatchEx *dispex, REFIID riid)
{
    HTMLTableCell *This = HTMLTableCell_from_DispatchEx(dispex);

    if(IsEqualGUID(&IID_IHTMLTableCell, riid))
        return &This->IHTMLTableCell_iface;
    if(IsEqualGUID(&IID_IHTMLTableCell2, riid))
        return &This->IHTMLTableCell2_iface;
    if(IsEqualGUID(&IID_IHTMLTableCell3, riid))
        return &This->IHTMLTableCell3_iface;

    return HTMLElement_query_interface(&This->element.node.event_target.dispex, riid);
}

static void HTMLTableCell_traverse(DispatchEx *dispex, nsCycleCollectionTraversalCallback *cb)
{
    HTMLTableCell *This = HTMLTableCell_from_DispatchEx(dispex);
    HTMLElement_traverse(dispex, cb);

    if(This->nscell)
        note_cc_edge((nsISupports*)This->nscell, "nstablecell", cb);
}

static void HTMLTableCell_unlink(DispatchEx *dispex)
{
    HTMLTableCell *This = HTMLTableCell_from_DispatchEx(dispex);
    HTMLElement_unlink(dispex);
    unlink_ref(&This->nscell);
}

static const NodeImplVtbl HTMLTableCellImplVtbl = {
    .clsid                 = &CLSID_HTMLTableCell,
    .cpc_entries           = HTMLElement_cpc,
    .clone                 = HTMLElement_clone,
    .get_attr_col          = HTMLElement_get_attr_col,
};

static void HTMLTableCellElement_init_dispex_info(dispex_data_t *info, compat_mode_t mode)
{
    static const dispex_hook_t hooks[] = {
        {DISPID_IHTMLTABLECELL_CELLINDEX, .noattr = TRUE},
        {DISPID_UNKNOWN}
    };
    dispex_info_add_interface(info, IHTMLTableCell_tid, hooks);
    if(mode >= COMPAT_MODE_IE9)
        dispex_info_add_interface(info, IHTMLTableCell3_tid, NULL);
    dispex_info_add_interface(info, IHTMLTableCell2_tid, NULL);

    HTMLElement_init_dispex_info(info, mode);
}

dispex_static_data_t HTMLTableCellElement_dispex = {
    .id           = OBJID_HTMLTableCellElement,
    .prototype_id = OBJID_HTMLElement,
    .disp_tid     = DispHTMLTableCell_tid,
    .init_info    = HTMLTableCellElement_init_dispex_info,
};

static const event_target_vtbl_t HTMLTableDataCellElement_event_target_vtbl = {
    {
        HTMLELEMENT_DISPEX_VTBL_ENTRIES,
        .query_interface= HTMLTableCell_query_interface,
        .destructor     = HTMLElement_destructor,
        .traverse       = HTMLTableCell_traverse,
        .unlink         = HTMLTableCell_unlink
    },
    HTMLELEMENT_EVENT_TARGET_VTBL_ENTRIES,
    .handle_event       = HTMLElement_handle_event
};

dispex_static_data_t HTMLTableDataCellElement_dispex = {
    .id           = OBJID_HTMLTableDataCellElement,
    .prototype_id = OBJID_HTMLTableCellElement,
    .vtbl         = &HTMLTableDataCellElement_event_target_vtbl.dispex_vtbl,
    .disp_tid     = DispHTMLTableCell_tid,
    .init_info    = HTMLTableCellElement_init_dispex_info,
};

HRESULT HTMLTableCell_Create(HTMLDocumentNode *doc, nsIDOMElement *nselem, HTMLElement **elem)
{
    HTMLTableCell *ret;
    nsresult nsres;

    ret = calloc(1, sizeof(*ret));
    if(!ret)
        return E_OUTOFMEMORY;

    ret->IHTMLTableCell_iface.lpVtbl = &HTMLTableCellVtbl;
    ret->IHTMLTableCell2_iface.lpVtbl = &HTMLTableCell2Vtbl;
    ret->IHTMLTableCell3_iface.lpVtbl = &HTMLTableCell3Vtbl;
    ret->element.node.vtbl = &HTMLTableCellImplVtbl;

    HTMLElement_Init(&ret->element, doc, nselem, &HTMLTableDataCellElement_dispex);

    nsres = nsIDOMElement_QueryInterface(nselem, &IID_nsIDOMHTMLTableCellElement, (void**)&ret->nscell);
    assert(nsres == NS_OK);

    *elem = &ret->element;
    return S_OK;
}

struct HTMLTableRow {
    HTMLElement element;

    IHTMLTableRow IHTMLTableRow_iface;
    IHTMLTableRow2 IHTMLTableRow2_iface;
    IHTMLTableRow3 IHTMLTableRow3_iface;
    IHTMLTableRow4 IHTMLTableRow4_iface;

    nsIDOMHTMLTableRowElement *nsrow;
};

static inline HTMLTableRow *impl_from_IHTMLTableRow(IHTMLTableRow *iface)
{
    return CONTAINING_RECORD(iface, HTMLTableRow, IHTMLTableRow_iface);
}

DISPEX_IDISPATCH_IMPL(HTMLTableRow, IHTMLTableRow,
                      impl_from_IHTMLTableRow(iface)->element.node.event_target.dispex)

static HRESULT WINAPI HTMLTableRow_put_align(IHTMLTableRow *iface, BSTR v)
{
    HTMLTableRow *This = impl_from_IHTMLTableRow(iface);
    nsAString val;
    nsresult nsres;

    TRACE("(%p)->(%s)\n", This, debugstr_w(v));

    nsAString_InitDepend(&val, v);

    nsres = nsIDOMHTMLTableRowElement_SetAlign(This->nsrow, &val);
    nsAString_Finish(&val);
    if (NS_FAILED(nsres)){
        ERR("Set Align(%s) failed!\n", debugstr_w(v));
        return E_FAIL;
    }
    return S_OK;
}

static HRESULT WINAPI HTMLTableRow_get_align(IHTMLTableRow *iface, BSTR *p)
{
    HTMLTableRow *This = impl_from_IHTMLTableRow(iface);
    nsAString val;
    nsresult nsres;

    TRACE("(%p)->(%p)\n", This, p);

    nsAString_Init(&val, NULL);
    nsres = nsIDOMHTMLTableRowElement_GetAlign(This->nsrow, &val);

    return return_nsstr(nsres, &val, p);
}

static HRESULT WINAPI HTMLTableRow_put_vAlign(IHTMLTableRow *iface, BSTR v)
{
    HTMLTableRow *This = impl_from_IHTMLTableRow(iface);
    nsAString val;
    nsresult nsres;

    TRACE("(%p)->(%s)\n", This, debugstr_w(v));

    nsAString_InitDepend(&val, v);

    nsres = nsIDOMHTMLTableRowElement_SetVAlign(This->nsrow, &val);
    nsAString_Finish(&val);

    if (NS_FAILED(nsres)){
        ERR("Set VAlign(%s) failed!\n", debugstr_w(v));
        return E_FAIL;
    }

    return S_OK;
}

static HRESULT WINAPI HTMLTableRow_get_vAlign(IHTMLTableRow *iface, BSTR *p)
{
    HTMLTableRow *This = impl_from_IHTMLTableRow(iface);
    nsAString val;
    nsresult nsres;

    TRACE("(%p)->(%p)\n", This, p);

    nsAString_Init(&val, NULL);
    nsres = nsIDOMHTMLTableRowElement_GetVAlign(This->nsrow, &val);

    return return_nsstr(nsres, &val, p);
}

static HRESULT WINAPI HTMLTableRow_put_bgColor(IHTMLTableRow *iface, VARIANT v)
{
    HTMLTableRow *This = impl_from_IHTMLTableRow(iface);

    TRACE("(%p)->(%s)\n", This, debugstr_variant(&v));

    return elem_put_color_attr(&This->element, L"bgColor", &v);
}

static HRESULT WINAPI HTMLTableRow_get_bgColor(IHTMLTableRow *iface, VARIANT *p)
{
    HTMLTableRow *This = impl_from_IHTMLTableRow(iface);

    TRACE("(%p)->(%p)\n", This, p);

    return elem_get_color_attr(&This->element, L"bgColor", p);
}

static HRESULT WINAPI HTMLTableRow_put_borderColor(IHTMLTableRow *iface, VARIANT v)
{
    HTMLTableRow *This = impl_from_IHTMLTableRow(iface);
    FIXME("(%p)->(%s)\n", This, debugstr_variant(&v));
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLTableRow_get_borderColor(IHTMLTableRow *iface, VARIANT *p)
{
    HTMLTableRow *This = impl_from_IHTMLTableRow(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLTableRow_put_borderColorLight(IHTMLTableRow *iface, VARIANT v)
{
    HTMLTableRow *This = impl_from_IHTMLTableRow(iface);
    FIXME("(%p)->(%s)\n", This, debugstr_variant(&v));
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLTableRow_get_borderColorLight(IHTMLTableRow *iface, VARIANT *p)
{
    HTMLTableRow *This = impl_from_IHTMLTableRow(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLTableRow_put_borderColorDark(IHTMLTableRow *iface, VARIANT v)
{
    HTMLTableRow *This = impl_from_IHTMLTableRow(iface);
    FIXME("(%p)->(%s)\n", This, debugstr_variant(&v));
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLTableRow_get_borderColorDark(IHTMLTableRow *iface, VARIANT *p)
{
    HTMLTableRow *This = impl_from_IHTMLTableRow(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLTableRow_get_rowIndex(IHTMLTableRow *iface, LONG *p)
{
    HTMLTableRow *This = impl_from_IHTMLTableRow(iface);
    nsresult nsres;

    TRACE("(%p)->(%p)\n", This, p);
    nsres = nsIDOMHTMLTableRowElement_GetRowIndex(This->nsrow, p);
    if(NS_FAILED(nsres)) {
        ERR("Get rowIndex failed: %08lx\n", nsres);
        return E_FAIL;
    }
    return S_OK;
}

static HRESULT WINAPI HTMLTableRow_get_sectionRowIndex(IHTMLTableRow *iface, LONG *p)
{
    HTMLTableRow *This = impl_from_IHTMLTableRow(iface);
    nsresult nsres;

    TRACE("(%p)->(%p)\n", This, p);
    nsres = nsIDOMHTMLTableRowElement_GetSectionRowIndex(This->nsrow, p);
    if(NS_FAILED(nsres)) {
        ERR("Get selectionRowIndex failed: %08lx\n", nsres);
        return E_FAIL;
    }
    return S_OK;
}

static HRESULT WINAPI HTMLTableRow_get_cells(IHTMLTableRow *iface, IHTMLElementCollection **p)
{
    HTMLTableRow *This = impl_from_IHTMLTableRow(iface);
    nsIDOMHTMLCollection *nscol;
    nsresult nsres;

    TRACE("(%p)->(%p)\n", This, p);

    nsres = nsIDOMHTMLTableRowElement_GetCells(This->nsrow, &nscol);
    if(NS_FAILED(nsres)) {
        ERR("GetCells failed: %08lx\n", nsres);
        return E_FAIL;
    }

    *p = create_collection_from_htmlcol(nscol, &This->element.node.event_target.dispex);

    nsIDOMHTMLCollection_Release(nscol);
    return S_OK;
}

static HRESULT WINAPI HTMLTableRow_insertCell(IHTMLTableRow *iface, LONG index, IDispatch **row)
{
    HTMLTableRow *This = impl_from_IHTMLTableRow(iface);
    nsIDOMHTMLElement *nselem;
    HTMLElement *elem;
    nsresult nsres;
    HRESULT hres;

    TRACE("(%p)->(%ld %p)\n", This, index, row);
    nsres = nsIDOMHTMLTableRowElement_InsertCell(This->nsrow, index, &nselem);
    if(NS_FAILED(nsres)) {
        ERR("Insert Cell at %ld failed: %08lx\n", index, nsres);
        return E_FAIL;
    }

    hres = HTMLTableCell_Create(This->element.node.doc, (nsIDOMElement*)nselem, &elem);
    nsIDOMHTMLElement_Release(nselem);
    if (FAILED(hres)) {
        ERR("Create TableCell failed: %08lx\n", hres);
        return hres;
    }

    *row = (IDispatch *)&elem->IHTMLElement_iface;
    return S_OK;
}

static HRESULT WINAPI HTMLTableRow_deleteCell(IHTMLTableRow *iface, LONG index)
{
    HTMLTableRow *This = impl_from_IHTMLTableRow(iface);
    nsresult nsres;

    TRACE("(%p)->(%ld)\n", This, index);
    nsres = nsIDOMHTMLTableRowElement_DeleteCell(This->nsrow, index);
    if(NS_FAILED(nsres)) {
        ERR("Delete Cell failed: %08lx\n", nsres);
        return E_FAIL;
    }
    return S_OK;
}

static const IHTMLTableRowVtbl HTMLTableRowVtbl = {
    HTMLTableRow_QueryInterface,
    HTMLTableRow_AddRef,
    HTMLTableRow_Release,
    HTMLTableRow_GetTypeInfoCount,
    HTMLTableRow_GetTypeInfo,
    HTMLTableRow_GetIDsOfNames,
    HTMLTableRow_Invoke,
    HTMLTableRow_put_align,
    HTMLTableRow_get_align,
    HTMLTableRow_put_vAlign,
    HTMLTableRow_get_vAlign,
    HTMLTableRow_put_bgColor,
    HTMLTableRow_get_bgColor,
    HTMLTableRow_put_borderColor,
    HTMLTableRow_get_borderColor,
    HTMLTableRow_put_borderColorLight,
    HTMLTableRow_get_borderColorLight,
    HTMLTableRow_put_borderColorDark,
    HTMLTableRow_get_borderColorDark,
    HTMLTableRow_get_rowIndex,
    HTMLTableRow_get_sectionRowIndex,
    HTMLTableRow_get_cells,
    HTMLTableRow_insertCell,
    HTMLTableRow_deleteCell
};

static inline HTMLTableRow *impl_from_IHTMLTableRow2(IHTMLTableRow2 *iface)
{
    return CONTAINING_RECORD(iface, HTMLTableRow, IHTMLTableRow2_iface);
}

DISPEX_IDISPATCH_IMPL(HTMLTableRow2, IHTMLTableRow2,
                      impl_from_IHTMLTableRow2(iface)->element.node.event_target.dispex)

static HRESULT WINAPI HTMLTableRow2_put_height(IHTMLTableRow2 *iface, VARIANT v)
{
    HTMLTableRow *This = impl_from_IHTMLTableRow2(iface);
    TRACE("(%p)->(%s)\n", This, debugstr_variant(&v));
    return put_table_length(&This->element, L"height", &v);
}

static HRESULT WINAPI HTMLTableRow2_get_height(IHTMLTableRow2 *iface, VARIANT *p)
{
    HTMLTableRow *This = impl_from_IHTMLTableRow2(iface);
    TRACE("(%p)->(%p)\n", This, p);
    return get_table_length(&This->element, L"height", p);
}

static const IHTMLTableRow2Vtbl HTMLTableRow2Vtbl = {
    HTMLTableRow2_QueryInterface,
    HTMLTableRow2_AddRef,
    HTMLTableRow2_Release,
    HTMLTableRow2_GetTypeInfoCount,
    HTMLTableRow2_GetTypeInfo,
    HTMLTableRow2_GetIDsOfNames,
    HTMLTableRow2_Invoke,
    HTMLTableRow2_put_height,
    HTMLTableRow2_get_height,
};

static inline HTMLTableRow *impl_from_IHTMLTableRow3(IHTMLTableRow3 *iface)
{
    return CONTAINING_RECORD(iface, HTMLTableRow, IHTMLTableRow3_iface);
}

DISPEX_IDISPATCH_IMPL(HTMLTableRow3, IHTMLTableRow3,
                      impl_from_IHTMLTableRow3(iface)->element.node.event_target.dispex)

static HRESULT WINAPI HTMLTableRow3_put_ch(IHTMLTableRow3 *iface, BSTR v)
{
    HTMLTableRow *This = impl_from_IHTMLTableRow3(iface);
    TRACE("(%p)->(%s)\n", This, debugstr_w(v));
    return elem_put_reflected_string(&This->element, L"ch", v);
}

static HRESULT WINAPI HTMLTableRow3_get_ch(IHTMLTableRow3 *iface, BSTR *p)
{
    HTMLTableRow *This = impl_from_IHTMLTableRow3(iface);
    TRACE("(%p)->(%p)\n", This, p);
    return elem_get_attr_string(&This->element, L"ch", p);
}

static HRESULT WINAPI HTMLTableRow3_put_chOff(IHTMLTableRow3 *iface, BSTR v)
{
    HTMLTableRow *This = impl_from_IHTMLTableRow3(iface);
    TRACE("(%p)->(%s)\n", This, debugstr_w(v));
    return elem_put_reflected_string(&This->element, L"choff", v);
}

static HRESULT WINAPI HTMLTableRow3_get_chOff(IHTMLTableRow3 *iface, BSTR *p)
{
    HTMLTableRow *This = impl_from_IHTMLTableRow3(iface);
    TRACE("(%p)->(%p)\n", This, p);
    return elem_get_attr_string(&This->element, L"choff", p);
}

static const IHTMLTableRow3Vtbl HTMLTableRow3Vtbl = {
    HTMLTableRow3_QueryInterface,
    HTMLTableRow3_AddRef,
    HTMLTableRow3_Release,
    HTMLTableRow3_GetTypeInfoCount,
    HTMLTableRow3_GetTypeInfo,
    HTMLTableRow3_GetIDsOfNames,
    HTMLTableRow3_Invoke,
    HTMLTableRow3_put_ch,
    HTMLTableRow3_get_ch,
    HTMLTableRow3_put_chOff,
    HTMLTableRow3_get_chOff,
};

static inline HTMLTableRow *impl_from_IHTMLTableRow4(IHTMLTableRow4 *iface)
{
    return CONTAINING_RECORD(iface, HTMLTableRow, IHTMLTableRow4_iface);
}

DISPEX_IDISPATCH_IMPL(HTMLTableRow4, IHTMLTableRow4,
                      impl_from_IHTMLTableRow4(iface)->element.node.event_target.dispex)

static HRESULT WINAPI HTMLTableRow4_put_ch(IHTMLTableRow4 *iface, BSTR v)
{
    HTMLTableRow *This = impl_from_IHTMLTableRow4(iface);
    TRACE("(%p)->(%s)\n", This, debugstr_w(v));
    return elem_put_reflected_string(&This->element, L"char", v);
}

static HRESULT WINAPI HTMLTableRow4_get_ch(IHTMLTableRow4 *iface, BSTR *p)
{
    HTMLTableRow *This = impl_from_IHTMLTableRow4(iface);
    TRACE("(%p)->(%p)\n", This, p);
    return elem_get_attr_string(&This->element, L"char", p);
}

static HRESULT WINAPI HTMLTableRow4_put_chOff(IHTMLTableRow4 *iface, BSTR v)
{
    HTMLTableRow *This = impl_from_IHTMLTableRow4(iface);
    TRACE("(%p)->(%s)\n", This, debugstr_w(v));
    return elem_put_reflected_string(&This->element, L"charoff", v);
}

static HRESULT WINAPI HTMLTableRow4_get_chOff(IHTMLTableRow4 *iface, BSTR *p)
{
    HTMLTableRow *This = impl_from_IHTMLTableRow4(iface);
    TRACE("(%p)->(%p)\n", This, p);
    return elem_get_attr_string(&This->element, L"charoff", p);
}

static HRESULT get_row_cell_count(HTMLTableRow *This, LONG *ret)
{
    nsIDOMHTMLCollection *nscol;
    nsresult nsres;
    UINT32 len;

    nsres = nsIDOMHTMLTableRowElement_GetCells(This->nsrow, &nscol);
    if(NS_FAILED(nsres))
        return map_nsresult(nsres);
    nsres = nsIDOMHTMLCollection_GetLength(nscol, &len);
    nsIDOMHTMLCollection_Release(nscol);
    if(NS_FAILED(nsres))
        return map_nsresult(nsres);
    *ret = len;
    return S_OK;
}

static HRESULT WINAPI HTMLTableRow4_insertCell(IHTMLTableRow4 *iface, LONG index, IDispatch **row)
{
    HTMLTableRow *This = impl_from_IHTMLTableRow4(iface);
    HRESULT hres;
    LONG count;

    TRACE("(%p)->(%ld %p)\n", This, index, row);

    hres = get_row_cell_count(This, &count);
    if(FAILED(hres))
        return hres;
    if(index < -1 || index > count)
        return MSHTML_E_INDEX_SIZE;
    if(dispex_compat_mode(&This->element.node.event_target.dispex) < COMPAT_MODE_IE9)
        return E_UNEXPECTED;
    return IHTMLTableRow_insertCell(&This->IHTMLTableRow_iface, index, row);
}

static HRESULT WINAPI HTMLTableRow4_deleteCell(IHTMLTableRow4 *iface, LONG index)
{
    HTMLTableRow *This = impl_from_IHTMLTableRow4(iface);
    HRESULT hres;
    LONG count;

    TRACE("(%p)->(%ld)\n", This, index);

    hres = get_row_cell_count(This, &count);
    if(FAILED(hres))
        return hres;
    if(index == -1 && !count)
        return S_OK;
    if(index < -1 || index >= count)
        return MSHTML_E_INDEX_SIZE;
    if(dispex_compat_mode(&This->element.node.event_target.dispex) < COMPAT_MODE_IE9)
        return E_UNEXPECTED;
    return IHTMLTableRow_deleteCell(&This->IHTMLTableRow_iface, index);
}

static const IHTMLTableRow4Vtbl HTMLTableRow4Vtbl = {
    HTMLTableRow4_QueryInterface,
    HTMLTableRow4_AddRef,
    HTMLTableRow4_Release,
    HTMLTableRow4_GetTypeInfoCount,
    HTMLTableRow4_GetTypeInfo,
    HTMLTableRow4_GetIDsOfNames,
    HTMLTableRow4_Invoke,
    HTMLTableRow4_put_ch,
    HTMLTableRow4_get_ch,
    HTMLTableRow4_put_chOff,
    HTMLTableRow4_get_chOff,
    HTMLTableRow4_insertCell,
    HTMLTableRow4_deleteCell,
};

static inline HTMLTableRow *HTMLTableRow_from_DispatchEx(DispatchEx *iface)
{
    return CONTAINING_RECORD(iface, HTMLTableRow, element.node.event_target.dispex);
}

static void *HTMLTableRow_query_interface(DispatchEx *dispex, REFIID riid)
{
    HTMLTableRow *This = HTMLTableRow_from_DispatchEx(dispex);

    if(IsEqualGUID(&IID_IHTMLTableRow, riid))
        return &This->IHTMLTableRow_iface;
    if(IsEqualGUID(&IID_IHTMLTableRow2, riid))
        return &This->IHTMLTableRow2_iface;
    if(IsEqualGUID(&IID_IHTMLTableRow3, riid))
        return &This->IHTMLTableRow3_iface;
    if(IsEqualGUID(&IID_IHTMLTableRow4, riid))
        return &This->IHTMLTableRow4_iface;

    return HTMLElement_query_interface(&This->element.node.event_target.dispex, riid);
}

static void HTMLTableRow_traverse(DispatchEx *dispex, nsCycleCollectionTraversalCallback *cb)
{
    HTMLTableRow *This = HTMLTableRow_from_DispatchEx(dispex);
    HTMLElement_traverse(dispex, cb);

    if(This->nsrow)
        note_cc_edge((nsISupports*)This->nsrow, "nstablerow", cb);
}

static void HTMLTableRow_unlink(DispatchEx *dispex)
{
    HTMLTableRow *This = HTMLTableRow_from_DispatchEx(dispex);
    HTMLElement_unlink(dispex);
    unlink_ref(&This->nsrow);
}

static const NodeImplVtbl HTMLTableRowImplVtbl = {
    .clsid                 = &CLSID_HTMLTableRow,
    .cpc_entries           = HTMLElement_cpc,
    .clone                 = HTMLElement_clone,
    .get_attr_col          = HTMLElement_get_attr_col,
};

static void HTMLTableRowElement_init_dispex_info(dispex_data_t *info, compat_mode_t mode)
{
    static const dispex_hook_t hooks[] = {
        {DISPID_IHTMLTABLEROW_ROWINDEX,        .noattr = TRUE},
        {DISPID_IHTMLTABLEROW_SECTIONROWINDEX, .noattr = TRUE},
        {DISPID_IHTMLTABLEROW_CELLS,           .noattr = TRUE},
        {DISPID_UNKNOWN}
    };
    dispex_info_add_interface(info, IHTMLTableRow_tid, hooks);
    dispex_info_add_interface(info, IHTMLTableRow2_tid, NULL);
    dispex_info_add_interface(info, mode >= COMPAT_MODE_IE9 ? IHTMLTableRow4_tid : IHTMLTableRow3_tid, NULL);

    HTMLElement_init_dispex_info(info, mode);
}

static const event_target_vtbl_t HTMLTableRowElement_event_target_vtbl = {
    {
        HTMLELEMENT_DISPEX_VTBL_ENTRIES,
        .query_interface= HTMLTableRow_query_interface,
        .destructor     = HTMLElement_destructor,
        .traverse       = HTMLTableRow_traverse,
        .unlink         = HTMLTableRow_unlink
    },
    HTMLELEMENT_EVENT_TARGET_VTBL_ENTRIES,
    .handle_event       = HTMLElement_handle_event
};

dispex_static_data_t HTMLTableRowElement_dispex = {
    .id           = OBJID_HTMLTableRowElement,
    .prototype_id = OBJID_HTMLElement,
    .vtbl         = &HTMLTableRowElement_event_target_vtbl.dispex_vtbl,
    .disp_tid     =  DispHTMLTableRow_tid,
    .init_info    = HTMLTableRowElement_init_dispex_info,
};

HRESULT HTMLTableRow_Create(HTMLDocumentNode *doc, nsIDOMElement *nselem, HTMLElement **elem)
{
    HTMLTableRow *ret;
    nsresult nsres;

    ret = calloc(1, sizeof(HTMLTableRow));
    if(!ret)
        return E_OUTOFMEMORY;

    ret->IHTMLTableRow_iface.lpVtbl = &HTMLTableRowVtbl;
    ret->IHTMLTableRow2_iface.lpVtbl = &HTMLTableRow2Vtbl;
    ret->IHTMLTableRow3_iface.lpVtbl = &HTMLTableRow3Vtbl;
    ret->IHTMLTableRow4_iface.lpVtbl = &HTMLTableRow4Vtbl;
    ret->element.node.vtbl = &HTMLTableRowImplVtbl;

    HTMLElement_Init(&ret->element, doc, nselem, &HTMLTableRowElement_dispex);

    nsres = nsIDOMElement_QueryInterface(nselem, &IID_nsIDOMHTMLTableRowElement, (void**)&ret->nsrow);
    assert(nsres == NS_OK);

    *elem = &ret->element;
    return S_OK;
}

struct HTMLTable {
    HTMLElement element;

    IHTMLTable  IHTMLTable_iface;
    IHTMLTable2 IHTMLTable2_iface;
    IHTMLTable3 IHTMLTable3_iface;

    nsIDOMHTMLTableElement *nstable;
};

static inline HTMLTable *impl_from_IHTMLTable(IHTMLTable *iface)
{
    return CONTAINING_RECORD(iface, HTMLTable, IHTMLTable_iface);
}

static inline HTMLTable *impl_from_IHTMLTable2(IHTMLTable2 *iface)
{
    return CONTAINING_RECORD(iface, HTMLTable, IHTMLTable2_iface);
}

static inline HTMLTable *impl_from_IHTMLTable3(IHTMLTable3 *iface)
{
    return CONTAINING_RECORD(iface, HTMLTable, IHTMLTable3_iface);
}

static HRESULT var2str(const VARIANT *p, nsAString *nsstr)
{
    BSTR str;
    BOOL ret;
    HRESULT hres;

    switch(V_VT(p)) {
    case VT_BSTR:
        return nsAString_Init(nsstr, V_BSTR(p))?
            S_OK : E_OUTOFMEMORY;
    case VT_R8:
        hres = VarBstrFromR8(V_R8(p), MAKELCID(MAKELANGID(LANG_ENGLISH,SUBLANG_ENGLISH_US),SORT_DEFAULT), 0, &str);
        break;
    case VT_R4:
        hres = VarBstrFromR4(V_R4(p), MAKELCID(MAKELANGID(LANG_ENGLISH,SUBLANG_ENGLISH_US),SORT_DEFAULT), 0, &str);
        break;
    case VT_I4:
        hres = VarBstrFromI4(V_I4(p), MAKELCID(MAKELANGID(LANG_ENGLISH,SUBLANG_ENGLISH_US),SORT_DEFAULT), 0, &str);
        break;
    default:
        FIXME("unsupported arg %s\n", debugstr_variant(p));
        return E_NOTIMPL;
    }
    if (FAILED(hres))
        return hres;

    ret = nsAString_Init(nsstr, str);
    SysFreeString(str);
    return ret ? S_OK : E_OUTOFMEMORY;
}

DISPEX_IDISPATCH_IMPL(HTMLTable, IHTMLTable, impl_from_IHTMLTable(iface)->element.node.event_target.dispex)

static HRESULT WINAPI HTMLTable_put_cols(IHTMLTable *iface, LONG v)
{
    HTMLTable *This = impl_from_IHTMLTable(iface);
    FIXME("(%p)->(%ld)\n", This, v);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLTable_get_cols(IHTMLTable *iface, LONG *p)
{
    HTMLTable *This = impl_from_IHTMLTable(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLTable_put_border(IHTMLTable *iface, VARIANT v)
{
    HTMLTable *This = impl_from_IHTMLTable(iface);
    FIXME("(%p)->(%s)\n", This, debugstr_variant(&v));
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLTable_get_border(IHTMLTable *iface, VARIANT *p)
{
    HTMLTable *This = impl_from_IHTMLTable(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLTable_put_frame(IHTMLTable *iface, BSTR v)
{
    HTMLTable *This = impl_from_IHTMLTable(iface);
    nsAString str;
    nsresult nsres;

    TRACE("(%p)->(%s)\n", This, debugstr_w(v));

    nsAString_InitDepend(&str, v);
    nsres = nsIDOMHTMLTableElement_SetFrame(This->nstable, &str);
    nsAString_Finish(&str);

    if (NS_FAILED(nsres)) {
        ERR("SetFrame(%s) failed: %08lx\n", debugstr_w(v), nsres);
        return E_FAIL;
    }
    return S_OK;
}

static HRESULT WINAPI HTMLTable_get_frame(IHTMLTable *iface, BSTR *p)
{
    HTMLTable *This = impl_from_IHTMLTable(iface);
    nsAString str;
    nsresult nsres;

    TRACE("(%p)->(%p)\n", This, p);

    nsAString_Init(&str, NULL);
    nsres = nsIDOMHTMLTableElement_GetFrame(This->nstable, &str);

    return return_nsstr(nsres, &str, p);
}

static HRESULT WINAPI HTMLTable_put_rules(IHTMLTable *iface, BSTR v)
{
    HTMLTable *This = impl_from_IHTMLTable(iface);
    FIXME("(%p)->(%s)\n", This, debugstr_w(v));
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLTable_get_rules(IHTMLTable *iface, BSTR *p)
{
    HTMLTable *This = impl_from_IHTMLTable(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLTable_put_cellSpacing(IHTMLTable *iface, VARIANT v)
{
    HTMLTable *This = impl_from_IHTMLTable(iface);
    nsAString nsstr;
    WCHAR buf[64];
    nsresult nsres;

    TRACE("(%p)->(%s)\n", This, debugstr_variant(&v));

    switch(V_VT(&v)) {
    case VT_BSTR:
        nsAString_InitDepend(&nsstr, V_BSTR(&v));
        break;
    case VT_I4: {
        swprintf(buf, ARRAY_SIZE(buf), L"%d", V_I4(&v));
        nsAString_InitDepend(&nsstr, buf);
        break;
    }
    default:
        FIXME("unsupported arg %s\n", debugstr_variant(&v));
        return E_NOTIMPL;
    }

    nsres = nsIDOMHTMLTableElement_SetCellSpacing(This->nstable, &nsstr);
    nsAString_Finish(&nsstr);
    if(NS_FAILED(nsres)) {
        ERR("SetCellSpacing failed: %08lx\n", nsres);
        return E_FAIL;
    }

    return S_OK;
}

static HRESULT WINAPI HTMLTable_get_cellSpacing(IHTMLTable *iface, VARIANT *p)
{
    HTMLTable *This = impl_from_IHTMLTable(iface);
    nsAString nsstr;
    nsresult nsres;

    TRACE("(%p)->(%p)\n", This, p);

    nsAString_Init(&nsstr, NULL);
    nsres = nsIDOMHTMLTableElement_GetCellSpacing(This->nstable, &nsstr);
    V_VT(p) = VT_BSTR;
    return return_nsstr(nsres, &nsstr, &V_BSTR(p));
}

static HRESULT WINAPI HTMLTable_put_cellPadding(IHTMLTable *iface, VARIANT v)
{
    HTMLTable *This = impl_from_IHTMLTable(iface);
    nsAString val;
    HRESULT hres;
    nsresult nsres;

    TRACE("(%p)->(%s)\n", This, debugstr_variant(&v));

    hres = var2str(&v, &val);
    if(FAILED(hres))
        return hres;

    nsres = nsIDOMHTMLTableElement_SetCellPadding(This->nstable, &val);
    nsAString_Finish(&val);
    if(NS_FAILED(nsres)) {
        ERR("Set Width(%s) failed, err = %08lx\n", debugstr_variant(&v), nsres);
        return E_FAIL;
    }

    return S_OK;
}

static HRESULT WINAPI HTMLTable_get_cellPadding(IHTMLTable *iface, VARIANT *p)
{
    HTMLTable *This = impl_from_IHTMLTable(iface);
    nsAString val;
    nsresult nsres;

    TRACE("(%p)->(%p)\n", This, p);

    nsAString_Init(&val, NULL);
    nsres = nsIDOMHTMLTableElement_GetCellPadding(This->nstable, &val);
    return return_nsstr_variant(nsres, &val, 0, p);
}

static HRESULT WINAPI HTMLTable_put_background(IHTMLTable *iface, BSTR v)
{
    HTMLTable *This = impl_from_IHTMLTable(iface);
    FIXME("(%p)->(%s)\n", This, debugstr_w(v));
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLTable_get_background(IHTMLTable *iface, BSTR *p)
{
    HTMLTable *This = impl_from_IHTMLTable(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLTable_put_bgColor(IHTMLTable *iface, VARIANT v)
{
    HTMLTable *This = impl_from_IHTMLTable(iface);

    TRACE("(%p)->(%s)\n", This, debugstr_variant(&v));

    return elem_put_color_attr(&This->element, L"bgColor", &v);
}

static HRESULT WINAPI HTMLTable_get_bgColor(IHTMLTable *iface, VARIANT *p)
{
    HTMLTable *This = impl_from_IHTMLTable(iface);

    TRACE("(%p)->(%p)\n", This, p);

    return elem_get_color_attr(&This->element, L"bgColor", p);
}

static HRESULT WINAPI HTMLTable_put_borderColor(IHTMLTable *iface, VARIANT v)
{
    HTMLTable *This = impl_from_IHTMLTable(iface);

    TRACE("(%p)->(%s)\n", This, debugstr_variant(&v));

    return elem_put_color_attr(&This->element, L"borderColor", &v);
}

static HRESULT WINAPI HTMLTable_get_borderColor(IHTMLTable *iface, VARIANT *p)
{
    HTMLTable *This = impl_from_IHTMLTable(iface);

    TRACE("(%p)->(%p)\n", This, p);

    return elem_get_color_attr(&This->element, L"borderColor", p);
}

static HRESULT WINAPI HTMLTable_put_borderColorLight(IHTMLTable *iface, VARIANT v)
{
    HTMLTable *This = impl_from_IHTMLTable(iface);
    FIXME("(%p)->(%s)\n", This, debugstr_variant(&v));
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLTable_get_borderColorLight(IHTMLTable *iface, VARIANT *p)
{
    HTMLTable *This = impl_from_IHTMLTable(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLTable_put_borderColorDark(IHTMLTable *iface, VARIANT v)
{
    HTMLTable *This = impl_from_IHTMLTable(iface);
    FIXME("(%p)->(%s)\n", This, debugstr_variant(&v));
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLTable_get_borderColorDark(IHTMLTable *iface, VARIANT *p)
{
    HTMLTable *This = impl_from_IHTMLTable(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLTable_put_align(IHTMLTable *iface, BSTR v)
{
    HTMLTable *This = impl_from_IHTMLTable(iface);
    nsAString val;
    nsresult nsres;

    TRACE("(%p)->(%s)\n", This, debugstr_w(v));

    nsAString_InitDepend(&val, v);

    nsres = nsIDOMHTMLTableElement_SetAlign(This->nstable, &val);
    nsAString_Finish(&val);
    if (NS_FAILED(nsres)){
        ERR("Set Align(%s) failed!\n", debugstr_w(v));
        return E_FAIL;
    }
    return S_OK;
}

static HRESULT WINAPI HTMLTable_get_align(IHTMLTable *iface, BSTR *p)
{
    HTMLTable *This = impl_from_IHTMLTable(iface);
    nsAString val;
    nsresult nsres;

    TRACE("(%p)->(%p)\n", This, p);

    nsAString_Init(&val, NULL);
    nsres = nsIDOMHTMLTableElement_GetAlign(This->nstable, &val);

    return return_nsstr(nsres, &val, p);
}

static HRESULT WINAPI HTMLTable_refresh(IHTMLTable *iface)
{
    HTMLTable *This = impl_from_IHTMLTable(iface);
    FIXME("(%p)\n", This);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLTable_get_rows(IHTMLTable *iface, IHTMLElementCollection **p)
{
    HTMLTable *This = impl_from_IHTMLTable(iface);
    nsIDOMHTMLCollection *nscol;
    nsresult nsres;

    TRACE("(%p)->(%p)\n", This, p);

    nsres = nsIDOMHTMLTableElement_GetRows(This->nstable, &nscol);
    if(NS_FAILED(nsres)) {
        ERR("GetRows failed: %08lx\n", nsres);
        return E_FAIL;
    }

    *p = create_collection_from_htmlcol(nscol, &This->element.node.event_target.dispex);

    nsIDOMHTMLCollection_Release(nscol);
    return S_OK;
}

static HRESULT WINAPI HTMLTable_put_width(IHTMLTable *iface, VARIANT v)
{
    HTMLTable *This = impl_from_IHTMLTable(iface);
    nsAString val;
    HRESULT hres;
    nsresult nsres;

    TRACE("(%p)->(%s)\n", This, debugstr_variant(&v));
    hres = var2str(&v, &val);

    if (FAILED(hres)){
        ERR("Set Width(%s) failed when initializing a nsAString, err = %08lx\n",
            debugstr_variant(&v), hres);
        return hres;
    }

    nsres = nsIDOMHTMLTableElement_SetWidth(This->nstable, &val);
    nsAString_Finish(&val);

    if (NS_FAILED(nsres)){
        ERR("Set Width(%s) failed, err = %08lx\n", debugstr_variant(&v), nsres);
        return E_FAIL;
    }
    return S_OK;
}

static HRESULT WINAPI HTMLTable_get_width(IHTMLTable *iface, VARIANT *p)
{
    HTMLTable *This = impl_from_IHTMLTable(iface);
    nsAString val;
    nsresult nsres;

    TRACE("(%p)->(%p)\n", This, p);

    nsAString_Init(&val, NULL);
    nsres = nsIDOMHTMLTableElement_GetWidth(This->nstable, &val);
    return return_nsstr_variant(nsres, &val, NSSTR_IMPLICIT_PX, p);
}

static HRESULT WINAPI HTMLTable_put_height(IHTMLTable *iface, VARIANT v)
{
    HTMLTable *This = impl_from_IHTMLTable(iface);
    FIXME("(%p)->(%s)\n", This, debugstr_variant(&v));
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLTable_get_height(IHTMLTable *iface, VARIANT *p)
{
    HTMLTable *This = impl_from_IHTMLTable(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLTable_put_dataPageSize(IHTMLTable *iface, LONG v)
{
    HTMLTable *This = impl_from_IHTMLTable(iface);
    FIXME("(%p)->(%ld)\n", This, v);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLTable_get_dataPageSize(IHTMLTable *iface, LONG *p)
{
    HTMLTable *This = impl_from_IHTMLTable(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLTable_nextPage(IHTMLTable *iface)
{
    HTMLTable *This = impl_from_IHTMLTable(iface);
    FIXME("(%p)\n", This);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLTable_previousPage(IHTMLTable *iface)
{
    HTMLTable *This = impl_from_IHTMLTable(iface);
    FIXME("(%p)\n", This);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLTable_get_tHead(IHTMLTable *iface, IHTMLTableSection **p)
{
    HTMLTable *This = impl_from_IHTMLTable(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLTable_get_tFoot(IHTMLTable *iface, IHTMLTableSection **p)
{
    HTMLTable *This = impl_from_IHTMLTable(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLTable_get_tBodies(IHTMLTable *iface, IHTMLElementCollection **p)
{
    HTMLTable *This = impl_from_IHTMLTable(iface);
    nsIDOMHTMLCollection *nscol = NULL;
    nsresult nsres;

    TRACE("(%p)->(%p)\n", This, p);

    nsres = nsIDOMHTMLTableElement_GetTBodies(This->nstable, &nscol);
    if(NS_FAILED(nsres)) {
        ERR("GetTBodies failed: %08lx\n", nsres);
        return E_FAIL;
    }

    *p = create_collection_from_htmlcol(nscol, &This->element.node.event_target.dispex);

    nsIDOMHTMLCollection_Release(nscol);
    return S_OK;
}

static HRESULT WINAPI HTMLTable_get_caption(IHTMLTable *iface, IHTMLTableCaption **p)
{
    HTMLTable *This = impl_from_IHTMLTable(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLTable_createTHead(IHTMLTable *iface, IDispatch **head)
{
    HTMLTable *This = impl_from_IHTMLTable(iface);
    FIXME("(%p)->(%p)\n", This, head);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLTable_deleteTHead(IHTMLTable *iface)
{
    HTMLTable *This = impl_from_IHTMLTable(iface);
    FIXME("(%p)\n", This);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLTable_createTFoot(IHTMLTable *iface, IDispatch **foot)
{
    HTMLTable *This = impl_from_IHTMLTable(iface);
    FIXME("(%p)->(%p)\n", This, foot);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLTable_deleteTFoot(IHTMLTable *iface)
{
    HTMLTable *This = impl_from_IHTMLTable(iface);
    FIXME("(%p)\n", This);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLTable_createCaption(IHTMLTable *iface, IHTMLTableCaption **caption)
{
    HTMLTable *This = impl_from_IHTMLTable(iface);
    FIXME("(%p)->(%p)\n", This, caption);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLTable_deleteCaption(IHTMLTable *iface)
{
    HTMLTable *This = impl_from_IHTMLTable(iface);
    FIXME("(%p)\n", This);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLTable_insertRow(IHTMLTable *iface, LONG index, IDispatch **row)
{
    HTMLTable *This = impl_from_IHTMLTable(iface);
    nsIDOMHTMLElement *nselem;
    HTMLElement *elem;
    nsresult nsres;
    HRESULT hres;

    TRACE("(%p)->(%ld %p)\n", This, index, row);
    nsres = nsIDOMHTMLTableElement_InsertRow(This->nstable, index, &nselem);
    if(NS_FAILED(nsres)) {
        ERR("Insert Row at %ld failed: %08lx\n", index, nsres);
        return E_FAIL;
    }

    hres = HTMLTableRow_Create(This->element.node.doc, (nsIDOMElement*)nselem, &elem);
    nsIDOMHTMLElement_Release(nselem);
    if (FAILED(hres)) {
        ERR("Create TableRow failed: %08lx\n", hres);
        return hres;
    }

    *row = (IDispatch *)&elem->IHTMLElement_iface;
    return S_OK;
}

static HRESULT WINAPI HTMLTable_deleteRow(IHTMLTable *iface, LONG index)
{
    HTMLTable *This = impl_from_IHTMLTable(iface);
    nsresult nsres;

    TRACE("(%p)->(%ld)\n", This, index);
    nsres = nsIDOMHTMLTableElement_DeleteRow(This->nstable, index);
    if(NS_FAILED(nsres)) {
        ERR("Delete Row failed: %08lx\n", nsres);
        return E_FAIL;
    }
    return S_OK;
}

static HRESULT WINAPI HTMLTable_get_readyState(IHTMLTable *iface, BSTR *p)
{
    HTMLTable *This = impl_from_IHTMLTable(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLTable_put_onreadystatechange(IHTMLTable *iface, VARIANT v)
{
    HTMLTable *This = impl_from_IHTMLTable(iface);
    FIXME("(%p)->(%s)\n", This, debugstr_variant(&v));
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLTable_get_onreadystatechange(IHTMLTable *iface, VARIANT *p)
{
    HTMLTable *This = impl_from_IHTMLTable(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static const IHTMLTableVtbl HTMLTableVtbl = {
    HTMLTable_QueryInterface,
    HTMLTable_AddRef,
    HTMLTable_Release,
    HTMLTable_GetTypeInfoCount,
    HTMLTable_GetTypeInfo,
    HTMLTable_GetIDsOfNames,
    HTMLTable_Invoke,
    HTMLTable_put_cols,
    HTMLTable_get_cols,
    HTMLTable_put_border,
    HTMLTable_get_border,
    HTMLTable_put_frame,
    HTMLTable_get_frame,
    HTMLTable_put_rules,
    HTMLTable_get_rules,
    HTMLTable_put_cellSpacing,
    HTMLTable_get_cellSpacing,
    HTMLTable_put_cellPadding,
    HTMLTable_get_cellPadding,
    HTMLTable_put_background,
    HTMLTable_get_background,
    HTMLTable_put_bgColor,
    HTMLTable_get_bgColor,
    HTMLTable_put_borderColor,
    HTMLTable_get_borderColor,
    HTMLTable_put_borderColorLight,
    HTMLTable_get_borderColorLight,
    HTMLTable_put_borderColorDark,
    HTMLTable_get_borderColorDark,
    HTMLTable_put_align,
    HTMLTable_get_align,
    HTMLTable_refresh,
    HTMLTable_get_rows,
    HTMLTable_put_width,
    HTMLTable_get_width,
    HTMLTable_put_height,
    HTMLTable_get_height,
    HTMLTable_put_dataPageSize,
    HTMLTable_get_dataPageSize,
    HTMLTable_nextPage,
    HTMLTable_previousPage,
    HTMLTable_get_tHead,
    HTMLTable_get_tFoot,
    HTMLTable_get_tBodies,
    HTMLTable_get_caption,
    HTMLTable_createTHead,
    HTMLTable_deleteTHead,
    HTMLTable_createTFoot,
    HTMLTable_deleteTFoot,
    HTMLTable_createCaption,
    HTMLTable_deleteCaption,
    HTMLTable_insertRow,
    HTMLTable_deleteRow,
    HTMLTable_get_readyState,
    HTMLTable_put_onreadystatechange,
    HTMLTable_get_onreadystatechange
};

DISPEX_IDISPATCH_IMPL(HTMLTable2, IHTMLTable2, impl_from_IHTMLTable2(iface)->element.node.event_target.dispex)

static HRESULT WINAPI HTMLTable2_firstPage(IHTMLTable2 *iface)
{
    HTMLTable *This = impl_from_IHTMLTable2(iface);
    FIXME("(%p)->()\n", This);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLTable2_lastPage(IHTMLTable2 *iface)
{
    HTMLTable *This = impl_from_IHTMLTable2(iface);
    FIXME("(%p)->()\n", This);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLTable2_cells(IHTMLTable2 *iface, IHTMLElementCollection **p)
{
    HTMLTable *This = impl_from_IHTMLTable2(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLTable2_moveRow(IHTMLTable2 *iface, LONG indexFrom, LONG indexTo, IDispatch **row)
{
    HTMLTable *This = impl_from_IHTMLTable2(iface);
    FIXME("(%p)->(%ld %ld %p)\n", This, indexFrom, indexTo, row);
    return E_NOTIMPL;
}


static const IHTMLTable2Vtbl HTMLTable2Vtbl = {
    HTMLTable2_QueryInterface,
    HTMLTable2_AddRef,
    HTMLTable2_Release,
    HTMLTable2_GetTypeInfoCount,
    HTMLTable2_GetTypeInfo,
    HTMLTable2_GetIDsOfNames,
    HTMLTable2_Invoke,
    HTMLTable2_firstPage,
    HTMLTable2_lastPage,
    HTMLTable2_cells,
    HTMLTable2_moveRow
};

DISPEX_IDISPATCH_IMPL(HTMLTable3, IHTMLTable3, impl_from_IHTMLTable3(iface)->element.node.event_target.dispex)

static HRESULT WINAPI HTMLTable3_put_summary(IHTMLTable3 *iface, BSTR v)
{
    HTMLTable *This = impl_from_IHTMLTable3(iface);
    nsAString str;
    nsresult nsres;

    TRACE("(%p)->(%s)\n", This, debugstr_w(v));

    nsAString_InitDepend(&str, v);

    nsres = nsIDOMHTMLTableElement_SetSummary(This->nstable, &str);

    nsAString_Finish(&str);
    if (NS_FAILED(nsres)) {
        ERR("Set summary(%s) failed: %08lx\n", debugstr_w(v), nsres);
        return E_FAIL;
    }
    return S_OK;
}

static HRESULT WINAPI HTMLTable3_get_summary(IHTMLTable3 *iface, BSTR * p)
{
    HTMLTable *This = impl_from_IHTMLTable3(iface);
    nsAString str;
    nsresult nsres;

    TRACE("(%p)->(%p)\n", This, p);

    nsAString_Init(&str, NULL);
    nsres = nsIDOMHTMLTableElement_GetSummary(This->nstable, &str);

    return return_nsstr(nsres, &str, p);
}

static const IHTMLTable3Vtbl HTMLTable3Vtbl = {
    HTMLTable3_QueryInterface,
    HTMLTable3_AddRef,
    HTMLTable3_Release,
    HTMLTable3_GetTypeInfoCount,
    HTMLTable3_GetTypeInfo,
    HTMLTable3_GetIDsOfNames,
    HTMLTable3_Invoke,
    HTMLTable3_put_summary,
    HTMLTable3_get_summary
};

static inline HTMLTable *impl_from_DispatchEx(DispatchEx *iface)
{
    return CONTAINING_RECORD(iface, HTMLTable, element.node.event_target.dispex);
}

static void *HTMLTable_query_interface(DispatchEx *dispex, REFIID riid)
{
    HTMLTable *This = impl_from_DispatchEx(dispex);

    if(IsEqualGUID(&IID_IHTMLTable, riid))
        return &This->IHTMLTable_iface;
    if(IsEqualGUID(&IID_IHTMLTable2, riid))
        return &This->IHTMLTable2_iface;
    if(IsEqualGUID(&IID_IHTMLTable3, riid))
        return &This->IHTMLTable3_iface;
    if(IsEqualGUID(&IID_IHTMLDatabinding, riid))
        return &This->element.IHTMLDatabinding_iface;

    return HTMLElement_query_interface(&This->element.node.event_target.dispex, riid);
}

static void HTMLTable_traverse(DispatchEx *dispex, nsCycleCollectionTraversalCallback *cb)
{
    HTMLTable *This = impl_from_DispatchEx(dispex);
    HTMLElement_traverse(dispex, cb);

    if(This->nstable)
        note_cc_edge((nsISupports*)This->nstable, "nstable", cb);
}

static void HTMLTable_unlink(DispatchEx *dispex)
{
    HTMLTable *This = impl_from_DispatchEx(dispex);
    HTMLElement_unlink(dispex);
    unlink_ref(&This->nstable);
}

static const cpc_entry_t HTMLTable_cpc[] = {
    {&DIID_HTMLTableEvents},
    HTMLELEMENT_CPC,
    {NULL}
};

static const NodeImplVtbl HTMLTableImplVtbl = {
    .clsid                 = &CLSID_HTMLTable,
    .cpc_entries           = HTMLTable_cpc,
    .clone                 = HTMLElement_clone,
    .get_attr_col          = HTMLElement_get_attr_col,
};

static void HTMLTableElement_init_dispex_info(dispex_data_t *info, compat_mode_t mode)
{
    static const dispex_hook_t table_hooks[] = {
        {DISPID_IHTMLTABLE_ROWS,       .noattr = TRUE},
        {DISPID_IHTMLTABLE_THEAD,      .noattr = TRUE},
        {DISPID_IHTMLTABLE_TFOOT,      .noattr = TRUE},
        {DISPID_IHTMLTABLE_TBODIES,    .noattr = TRUE},
        {DISPID_IHTMLTABLE_CAPTION,    .noattr = TRUE},
        {DISPID_IHTMLTABLE_READYSTATE, .noattr = TRUE},
        {DISPID_UNKNOWN}
    };
    static const dispex_hook_t table2_hooks[] = {
        {DISPID_IHTMLTABLE2_CELLS,     .noattr = TRUE},
        {DISPID_UNKNOWN}
    };
    dispex_info_add_interface(info, IHTMLTable_tid, table_hooks);
    dispex_info_add_interface(info, IHTMLTable2_tid, table2_hooks);
    if(mode < COMPAT_MODE_IE11)
        dispex_info_add_interface(info, IHTMLDatabinding_tid, NULL);

    HTMLElement_init_dispex_info(info, mode);
}

static const event_target_vtbl_t HTMLTableElement_event_target_vtbl = {
    {
        HTMLELEMENT_DISPEX_VTBL_ENTRIES,
        .query_interface= HTMLTable_query_interface,
        .destructor     = HTMLElement_destructor,
        .traverse       = HTMLTable_traverse,
        .unlink         = HTMLTable_unlink
    },
    HTMLELEMENT_EVENT_TARGET_VTBL_ENTRIES,
    .handle_event       = HTMLElement_handle_event
};

static const tid_t HTMLTableElement_iface_tids[] = {
    IHTMLTable3_tid,
    0
};

dispex_static_data_t HTMLTableElement_dispex = {
    .id           = OBJID_HTMLTableElement,
    .prototype_id = OBJID_HTMLElement,
    .vtbl         = &HTMLTableElement_event_target_vtbl.dispex_vtbl,
    .disp_tid     = DispHTMLTable_tid,
    .iface_tids   = HTMLTableElement_iface_tids,
    .init_info    = HTMLTableElement_init_dispex_info,
};

HRESULT HTMLTable_Create(HTMLDocumentNode *doc, nsIDOMElement *nselem, HTMLElement **elem)
{
    HTMLTable *ret;
    nsresult nsres;

    ret = calloc(1, sizeof(HTMLTable));
    if(!ret)
        return E_OUTOFMEMORY;

    ret->element.node.vtbl = &HTMLTableImplVtbl;
    ret->IHTMLTable_iface.lpVtbl = &HTMLTableVtbl;
    ret->IHTMLTable2_iface.lpVtbl = &HTMLTable2Vtbl;
    ret->IHTMLTable3_iface.lpVtbl = &HTMLTable3Vtbl;

    HTMLElement_Init(&ret->element, doc, nselem, &HTMLTableElement_dispex);

    nsres = nsIDOMElement_QueryInterface(nselem, &IID_nsIDOMHTMLTableElement, (void**)&ret->nstable);
    assert(nsres == NS_OK);

    *elem = &ret->element;
    return S_OK;
}

typedef struct {
    HTMLElement element;

    IHTMLTableCol IHTMLTableCol_iface;
    IHTMLTableCol2 IHTMLTableCol2_iface;
    IHTMLTableCol3 IHTMLTableCol3_iface;
} HTMLTableColElement;

static inline HTMLTableColElement *impl_from_IHTMLTableCol(IHTMLTableCol *iface)
{
    return CONTAINING_RECORD(iface, HTMLTableColElement, IHTMLTableCol_iface);
}

DISPEX_IDISPATCH_IMPL(HTMLTableCol, IHTMLTableCol,
                      impl_from_IHTMLTableCol(iface)->element.node.event_target.dispex)

static HRESULT WINAPI HTMLTableCol_put_span(IHTMLTableCol *iface, LONG v)
{
    HTMLTableColElement *This = impl_from_IHTMLTableCol(iface);
    WCHAR buf[12];

    TRACE("(%p)->(%ld)\n", This, v);

    if(v < 1 || v > 1000)
        return CTL_E_INVALIDPROPERTYVALUE;
    swprintf(buf, ARRAY_SIZE(buf), L"%ld", v);
    return elem_put_reflected_string(&This->element, L"span", buf);
}

static HRESULT WINAPI HTMLTableCol_get_span(IHTMLTableCol *iface, LONG *p)
{
    HTMLTableColElement *This = impl_from_IHTMLTableCol(iface);
    HRESULT hres;
    LONG val;
    BSTR str;

    TRACE("(%p)->(%p)\n", This, p);

    hres = elem_get_attr_string(&This->element, L"span", &str);
    if(FAILED(hres))
        return hres;
    val = str ? wcstol(str, NULL, 10) : 1;
    SysFreeString(str);
    *p = val < 1 || val > 1000 ? 1 : val;
    return S_OK;
}

static HRESULT WINAPI HTMLTableCol_put_width(IHTMLTableCol *iface, VARIANT v)
{
    HTMLTableColElement *This = impl_from_IHTMLTableCol(iface);
    TRACE("(%p)->(%s)\n", This, debugstr_variant(&v));
    return put_table_length(&This->element, L"width", &v);
}

static HRESULT WINAPI HTMLTableCol_get_width(IHTMLTableCol *iface, VARIANT *p)
{
    HTMLTableColElement *This = impl_from_IHTMLTableCol(iface);
    TRACE("(%p)->(%p)\n", This, p);
    return get_table_length(&This->element, L"width", p);
}

static HRESULT WINAPI HTMLTableCol_put_align(IHTMLTableCol *iface, BSTR v)
{
    HTMLTableColElement *This = impl_from_IHTMLTableCol(iface);
    TRACE("(%p)->(%s)\n", This, debugstr_w(v));
    return put_table_keyword(&This->element, L"align", v, table_align_keywords);
}

static HRESULT WINAPI HTMLTableCol_get_align(IHTMLTableCol *iface, BSTR *p)
{
    HTMLTableColElement *This = impl_from_IHTMLTableCol(iface);
    TRACE("(%p)->(%p)\n", This, p);
    return get_table_keyword(&This->element, L"align", p, table_align_keywords, NULL);
}

static HRESULT WINAPI HTMLTableCol_put_vAlign(IHTMLTableCol *iface, BSTR v)
{
    HTMLTableColElement *This = impl_from_IHTMLTableCol(iface);
    TRACE("(%p)->(%s)\n", This, debugstr_w(v));
    return put_table_keyword(&This->element, L"valign", v, table_valign_keywords);
}

static HRESULT WINAPI HTMLTableCol_get_vAlign(IHTMLTableCol *iface, BSTR *p)
{
    HTMLTableColElement *This = impl_from_IHTMLTableCol(iface);
    TRACE("(%p)->(%p)\n", This, p);
    return get_table_keyword(&This->element, L"valign", p, table_valign_keywords, L"top");
}

static const IHTMLTableColVtbl HTMLTableColVtbl = {
    HTMLTableCol_QueryInterface,
    HTMLTableCol_AddRef,
    HTMLTableCol_Release,
    HTMLTableCol_GetTypeInfoCount,
    HTMLTableCol_GetTypeInfo,
    HTMLTableCol_GetIDsOfNames,
    HTMLTableCol_Invoke,
    HTMLTableCol_put_span,
    HTMLTableCol_get_span,
    HTMLTableCol_put_width,
    HTMLTableCol_get_width,
    HTMLTableCol_put_align,
    HTMLTableCol_get_align,
    HTMLTableCol_put_vAlign,
    HTMLTableCol_get_vAlign,
};

static inline HTMLTableColElement *impl_from_IHTMLTableCol2(IHTMLTableCol2 *iface)
{
    return CONTAINING_RECORD(iface, HTMLTableColElement, IHTMLTableCol2_iface);
}

DISPEX_IDISPATCH_IMPL(HTMLTableCol2, IHTMLTableCol2,
                      impl_from_IHTMLTableCol2(iface)->element.node.event_target.dispex)

static HRESULT WINAPI HTMLTableCol2_put_ch(IHTMLTableCol2 *iface, BSTR v)
{
    HTMLTableColElement *This = impl_from_IHTMLTableCol2(iface);
    TRACE("(%p)->(%s)\n", This, debugstr_w(v));
    return elem_put_reflected_string(&This->element, L"ch", v);
}

static HRESULT WINAPI HTMLTableCol2_get_ch(IHTMLTableCol2 *iface, BSTR *p)
{
    HTMLTableColElement *This = impl_from_IHTMLTableCol2(iface);
    TRACE("(%p)->(%p)\n", This, p);
    return elem_get_attr_string(&This->element, L"ch", p);
}

static HRESULT WINAPI HTMLTableCol2_put_chOff(IHTMLTableCol2 *iface, BSTR v)
{
    HTMLTableColElement *This = impl_from_IHTMLTableCol2(iface);
    TRACE("(%p)->(%s)\n", This, debugstr_w(v));
    return elem_put_reflected_string(&This->element, L"choff", v);
}

static HRESULT WINAPI HTMLTableCol2_get_chOff(IHTMLTableCol2 *iface, BSTR *p)
{
    HTMLTableColElement *This = impl_from_IHTMLTableCol2(iface);
    TRACE("(%p)->(%p)\n", This, p);
    return elem_get_attr_string(&This->element, L"choff", p);
}

static const IHTMLTableCol2Vtbl HTMLTableCol2Vtbl = {
    HTMLTableCol2_QueryInterface,
    HTMLTableCol2_AddRef,
    HTMLTableCol2_Release,
    HTMLTableCol2_GetTypeInfoCount,
    HTMLTableCol2_GetTypeInfo,
    HTMLTableCol2_GetIDsOfNames,
    HTMLTableCol2_Invoke,
    HTMLTableCol2_put_ch,
    HTMLTableCol2_get_ch,
    HTMLTableCol2_put_chOff,
    HTMLTableCol2_get_chOff,
};

static inline HTMLTableColElement *impl_from_IHTMLTableCol3(IHTMLTableCol3 *iface)
{
    return CONTAINING_RECORD(iface, HTMLTableColElement, IHTMLTableCol3_iface);
}

DISPEX_IDISPATCH_IMPL(HTMLTableCol3, IHTMLTableCol3,
                      impl_from_IHTMLTableCol3(iface)->element.node.event_target.dispex)

static HRESULT WINAPI HTMLTableCol3_put_ch(IHTMLTableCol3 *iface, BSTR v)
{
    HTMLTableColElement *This = impl_from_IHTMLTableCol3(iface);
    TRACE("(%p)->(%s)\n", This, debugstr_w(v));
    return elem_put_reflected_string(&This->element, L"char", v);
}

static HRESULT WINAPI HTMLTableCol3_get_ch(IHTMLTableCol3 *iface, BSTR *p)
{
    HTMLTableColElement *This = impl_from_IHTMLTableCol3(iface);
    TRACE("(%p)->(%p)\n", This, p);
    return elem_get_attr_string(&This->element, L"char", p);
}

static HRESULT WINAPI HTMLTableCol3_put_chOff(IHTMLTableCol3 *iface, BSTR v)
{
    HTMLTableColElement *This = impl_from_IHTMLTableCol3(iface);
    TRACE("(%p)->(%s)\n", This, debugstr_w(v));
    return elem_put_reflected_string(&This->element, L"charoff", v);
}

static HRESULT WINAPI HTMLTableCol3_get_chOff(IHTMLTableCol3 *iface, BSTR *p)
{
    HTMLTableColElement *This = impl_from_IHTMLTableCol3(iface);
    TRACE("(%p)->(%p)\n", This, p);
    return elem_get_attr_string(&This->element, L"charoff", p);
}

static const IHTMLTableCol3Vtbl HTMLTableCol3Vtbl = {
    HTMLTableCol3_QueryInterface,
    HTMLTableCol3_AddRef,
    HTMLTableCol3_Release,
    HTMLTableCol3_GetTypeInfoCount,
    HTMLTableCol3_GetTypeInfo,
    HTMLTableCol3_GetIDsOfNames,
    HTMLTableCol3_Invoke,
    HTMLTableCol3_put_ch,
    HTMLTableCol3_get_ch,
    HTMLTableCol3_put_chOff,
    HTMLTableCol3_get_chOff,
};

static inline HTMLTableColElement *HTMLTableCol_from_DispatchEx(DispatchEx *iface)
{
    return CONTAINING_RECORD(iface, HTMLTableColElement, element.node.event_target.dispex);
}

static void *HTMLTableCol_query_interface(DispatchEx *dispex, REFIID riid)
{
    HTMLTableColElement *This = HTMLTableCol_from_DispatchEx(dispex);

    if(IsEqualGUID(&IID_IHTMLTableCol, riid))
        return &This->IHTMLTableCol_iface;
    if(IsEqualGUID(&IID_IHTMLTableCol2, riid))
        return &This->IHTMLTableCol2_iface;
    if(IsEqualGUID(&IID_IHTMLTableCol3, riid))
        return &This->IHTMLTableCol3_iface;

    return HTMLElement_query_interface(&This->element.node.event_target.dispex, riid);
}

static const NodeImplVtbl HTMLTableColImplVtbl = {
    .clsid                 = &CLSID_HTMLTableCol,
    .cpc_entries           = HTMLElement_cpc,
    .clone                 = HTMLElement_clone,
    .get_attr_col          = HTMLElement_get_attr_col,
};

static const event_target_vtbl_t HTMLTableColElement_event_target_vtbl = {
    {
        HTMLELEMENT_DISPEX_VTBL_ENTRIES,
        .query_interface= HTMLTableCol_query_interface,
        .destructor     = HTMLElement_destructor,
        .traverse       = HTMLElement_traverse,
        .unlink         = HTMLElement_unlink
    },
    HTMLELEMENT_EVENT_TARGET_VTBL_ENTRIES,
    .handle_event       = HTMLElement_handle_event
};

static void HTMLTableColElement_init_dispex_info(dispex_data_t *info, compat_mode_t mode)
{
    dispex_info_add_interface(info, IHTMLTableCol_tid, NULL);
    dispex_info_add_interface(info, mode >= COMPAT_MODE_IE9 ? IHTMLTableCol3_tid : IHTMLTableCol2_tid, NULL);

    HTMLElement_init_dispex_info(info, mode);
}

dispex_static_data_t HTMLTableColElement_dispex = {
    .id           = OBJID_HTMLTableColElement,
    .prototype_id = OBJID_HTMLElement,
    .vtbl         = &HTMLTableColElement_event_target_vtbl.dispex_vtbl,
    .disp_tid     = DispHTMLTableCol_tid,
    .init_info    = HTMLTableColElement_init_dispex_info,
};

HRESULT HTMLTableCol_Create(HTMLDocumentNode *doc, nsIDOMElement *nselem, HTMLElement **elem)
{
    HTMLTableColElement *ret;

    ret = calloc(1, sizeof(*ret));
    if(!ret)
        return E_OUTOFMEMORY;

    ret->IHTMLTableCol_iface.lpVtbl = &HTMLTableColVtbl;
    ret->IHTMLTableCol2_iface.lpVtbl = &HTMLTableCol2Vtbl;
    ret->IHTMLTableCol3_iface.lpVtbl = &HTMLTableCol3Vtbl;
    ret->element.node.vtbl = &HTMLTableColImplVtbl;

    HTMLElement_Init(&ret->element, doc, nselem, &HTMLTableColElement_dispex);

    *elem = &ret->element;
    return S_OK;
}

typedef struct {
    HTMLElement element;

    IHTMLTableSection IHTMLTableSection_iface;
    IHTMLTableSection2 IHTMLTableSection2_iface;
    IHTMLTableSection3 IHTMLTableSection3_iface;
    IHTMLTableSection4 IHTMLTableSection4_iface;
} HTMLTableSectionElement;

static HRESULT get_section_row(HTMLTableSectionElement *This, LONG index, nsIDOMNode **ret, LONG *count)
{
    nsIDOMHTMLTableRowElement *nsrow;
    nsIDOMNode *iter, *next;
    nsresult nsres;
    LONG n = 0;

    *ret = NULL;
    nsres = nsIDOMNode_GetFirstChild(This->element.node.nsnode, &iter);
    if(NS_FAILED(nsres))
        return map_nsresult(nsres);

    while(iter) {
        nsres = nsIDOMNode_QueryInterface(iter, &IID_nsIDOMHTMLTableRowElement, (void**)&nsrow);
        if(NS_SUCCEEDED(nsres)) {
            nsIDOMHTMLTableRowElement_Release(nsrow);
            if(n++ == index) {
                nsIDOMNode_AddRef(iter);
                *ret = iter;
            }
        }
        nsres = nsIDOMNode_GetNextSibling(iter, &next);
        nsIDOMNode_Release(iter);
        iter = NS_SUCCEEDED(nsres) ? next : NULL;
    }

    *count = n;
    return S_OK;
}

static BOOL section_is_detached_quirks(HTMLTableSectionElement *This)
{
    nsIDOMHTMLTableElement *nstable;
    nsIDOMNode *parent;
    nsresult nsres;

    if(dispex_compat_mode(&This->element.node.event_target.dispex) >= COMPAT_MODE_IE9)
        return FALSE;

    nsres = nsIDOMNode_GetParentNode(This->element.node.nsnode, &parent);
    if(NS_FAILED(nsres) || !parent)
        return TRUE;
    nsres = nsIDOMNode_QueryInterface(parent, &IID_nsIDOMHTMLTableElement, (void**)&nstable);
    nsIDOMNode_Release(parent);
    if(NS_FAILED(nsres))
        return TRUE;
    nsIDOMHTMLTableElement_Release(nstable);
    return FALSE;
}

static HRESULT section_insert_row(HTMLTableSectionElement *This, LONG index, BOOL ie9, IDispatch **row)
{
    nsIDOMNode *ref, *nsnode;
    HTMLElement *elem;
    nsresult nsres;
    HRESULT hres;
    LONG count;

    if(!ie9 && section_is_detached_quirks(This)) {
        *row = NULL;
        return S_OK;
    }

    hres = get_section_row(This, index, &ref, &count);
    if(FAILED(hres))
        return hres;
    if(index < -1 || index > count || (ie9 && dispex_compat_mode(&This->element.node.event_target.dispex) < COMPAT_MODE_IE9)) {
        if(ref)
            nsIDOMNode_Release(ref);
        if(index < -1 || index > count)
            return ie9 ? MSHTML_E_INDEX_SIZE : E_INVALIDARG;
        return E_UNEXPECTED;
    }

    hres = create_element(This->element.node.doc, L"tr", &elem);
    if(FAILED(hres)) {
        if(ref)
            nsIDOMNode_Release(ref);
        return hres;
    }

    if(ref) {
        nsres = nsIDOMNode_InsertBefore(This->element.node.nsnode, elem->node.nsnode, ref, &nsnode);
        nsIDOMNode_Release(ref);
    }else {
        nsres = nsIDOMNode_AppendChild(This->element.node.nsnode, elem->node.nsnode, &nsnode);
    }
    if(NS_FAILED(nsres)) {
        IHTMLElement_Release(&elem->IHTMLElement_iface);
        return map_nsresult(nsres);
    }
    nsIDOMNode_Release(nsnode);

    *row = (IDispatch*)&elem->IHTMLElement_iface;
    return S_OK;
}

static HRESULT section_delete_row(HTMLTableSectionElement *This, LONG index, BOOL ie9)
{
    nsIDOMNode *ref, *nsnode;
    nsresult nsres;
    HRESULT hres;
    LONG count;

    if(!ie9 && section_is_detached_quirks(This))
        return S_OK;

    hres = get_section_row(This, index, &ref, &count);
    if(FAILED(hres))
        return hres;
    if(!count && index == -1)
        return S_OK;
    if(index == -1)
        hres = get_section_row(This, count - 1, &ref, &count);
    if(FAILED(hres))
        return hres;
    if(!ref)
        return ie9 ? MSHTML_E_INDEX_SIZE : E_INVALIDARG;
    if(ie9 && dispex_compat_mode(&This->element.node.event_target.dispex) < COMPAT_MODE_IE9) {
        nsIDOMNode_Release(ref);
        return E_UNEXPECTED;
    }

    nsres = nsIDOMNode_RemoveChild(This->element.node.nsnode, ref, &nsnode);
    nsIDOMNode_Release(ref);
    if(NS_FAILED(nsres))
        return map_nsresult(nsres);
    nsIDOMNode_Release(nsnode);
    return S_OK;
}

static inline HTMLTableSectionElement *impl_from_IHTMLTableSection(IHTMLTableSection *iface)
{
    return CONTAINING_RECORD(iface, HTMLTableSectionElement, IHTMLTableSection_iface);
}

DISPEX_IDISPATCH_IMPL(HTMLTableSection, IHTMLTableSection,
                      impl_from_IHTMLTableSection(iface)->element.node.event_target.dispex)

static HRESULT WINAPI HTMLTableSection_put_align(IHTMLTableSection *iface, BSTR v)
{
    HTMLTableSectionElement *This = impl_from_IHTMLTableSection(iface);
    TRACE("(%p)->(%s)\n", This, debugstr_w(v));
    return put_table_keyword(&This->element, L"align", v, table_align_keywords);
}

static HRESULT WINAPI HTMLTableSection_get_align(IHTMLTableSection *iface, BSTR *p)
{
    HTMLTableSectionElement *This = impl_from_IHTMLTableSection(iface);
    TRACE("(%p)->(%p)\n", This, p);
    return get_table_keyword(&This->element, L"align", p, table_align_keywords, NULL);
}

static HRESULT WINAPI HTMLTableSection_put_vAlign(IHTMLTableSection *iface, BSTR v)
{
    HTMLTableSectionElement *This = impl_from_IHTMLTableSection(iface);
    TRACE("(%p)->(%s)\n", This, debugstr_w(v));
    return put_table_keyword(&This->element, L"valign", v, table_valign_keywords);
}

static HRESULT WINAPI HTMLTableSection_get_vAlign(IHTMLTableSection *iface, BSTR *p)
{
    HTMLTableSectionElement *This = impl_from_IHTMLTableSection(iface);
    TRACE("(%p)->(%p)\n", This, p);
    return get_table_keyword(&This->element, L"valign", p, table_valign_keywords, L"top");
}

static HRESULT WINAPI HTMLTableSection_put_bgColor(IHTMLTableSection *iface, VARIANT v)
{
    HTMLTableSectionElement *This = impl_from_IHTMLTableSection(iface);
    TRACE("(%p)->(%s)\n", This, debugstr_variant(&v));
    return elem_put_color_attr(&This->element, L"bgcolor", &v);
}

static HRESULT WINAPI HTMLTableSection_get_bgColor(IHTMLTableSection *iface, VARIANT *p)
{
    HTMLTableSectionElement *This = impl_from_IHTMLTableSection(iface);
    TRACE("(%p)->(%p)\n", This, p);
    return elem_get_color_attr(&This->element, L"bgcolor", p);
}

static HRESULT WINAPI HTMLTableSection_get_rows(IHTMLTableSection *iface, IHTMLElementCollection **p)
{
    HTMLTableSectionElement *This = impl_from_IHTMLTableSection(iface);
    IHTMLElementCollection *col;
    nsIDOMNodeList *nslist;
    IDispatch *disp;
    nsresult nsres;
    HRESULT hres;
    VARIANT tag;

    TRACE("(%p)->(%p)\n", This, p);

    /* FIXME: in quirks mode, the length of the rows of a section outside a table returns S_FALSE */
    nsres = nsIDOMNode_GetChildNodes(This->element.node.nsnode, &nslist);
    if(NS_FAILED(nsres))
        return map_nsresult(nsres);
    col = create_collection_from_nodelist(nslist, &This->element.node.event_target.dispex);
    nsIDOMNodeList_Release(nslist);
    if(!col)
        return E_OUTOFMEMORY;

    V_VT(&tag) = VT_BSTR;
    V_BSTR(&tag) = SysAllocString(L"tr");
    if(!V_BSTR(&tag)) {
        IHTMLElementCollection_Release(col);
        return E_OUTOFMEMORY;
    }
    hres = IHTMLElementCollection_tags(col, tag, &disp);
    SysFreeString(V_BSTR(&tag));
    IHTMLElementCollection_Release(col);
    if(FAILED(hres))
        return hres;

    hres = IDispatch_QueryInterface(disp, &IID_IHTMLElementCollection, (void**)p);
    IDispatch_Release(disp);
    return hres;
}

static HRESULT WINAPI HTMLTableSection_insertRow(IHTMLTableSection *iface, LONG index, IDispatch **row)
{
    HTMLTableSectionElement *This = impl_from_IHTMLTableSection(iface);
    TRACE("(%p)->(%ld %p)\n", This, index, row);
    return section_insert_row(This, index, FALSE, row);
}

static HRESULT WINAPI HTMLTableSection_deleteRow(IHTMLTableSection *iface, LONG index)
{
    HTMLTableSectionElement *This = impl_from_IHTMLTableSection(iface);
    TRACE("(%p)->(%ld)\n", This, index);
    return section_delete_row(This, index, FALSE);
}

static const IHTMLTableSectionVtbl HTMLTableSectionVtbl = {
    HTMLTableSection_QueryInterface,
    HTMLTableSection_AddRef,
    HTMLTableSection_Release,
    HTMLTableSection_GetTypeInfoCount,
    HTMLTableSection_GetTypeInfo,
    HTMLTableSection_GetIDsOfNames,
    HTMLTableSection_Invoke,
    HTMLTableSection_put_align,
    HTMLTableSection_get_align,
    HTMLTableSection_put_vAlign,
    HTMLTableSection_get_vAlign,
    HTMLTableSection_put_bgColor,
    HTMLTableSection_get_bgColor,
    HTMLTableSection_get_rows,
    HTMLTableSection_insertRow,
    HTMLTableSection_deleteRow,
};

static inline HTMLTableSectionElement *impl_from_IHTMLTableSection2(IHTMLTableSection2 *iface)
{
    return CONTAINING_RECORD(iface, HTMLTableSectionElement, IHTMLTableSection2_iface);
}

DISPEX_IDISPATCH_IMPL(HTMLTableSection2, IHTMLTableSection2,
                      impl_from_IHTMLTableSection2(iface)->element.node.event_target.dispex)

static HRESULT WINAPI HTMLTableSection2_moveRow(IHTMLTableSection2 *iface, LONG indexFrom, LONG indexTo, IDispatch **row)
{
    HTMLTableSectionElement *This = impl_from_IHTMLTableSection2(iface);
    /* TODO: move the row */
    FIXME("(%p)->(%ld %ld %p)\n", This, indexFrom, indexTo, row);
    return E_NOTIMPL;
}

static const IHTMLTableSection2Vtbl HTMLTableSection2Vtbl = {
    HTMLTableSection2_QueryInterface,
    HTMLTableSection2_AddRef,
    HTMLTableSection2_Release,
    HTMLTableSection2_GetTypeInfoCount,
    HTMLTableSection2_GetTypeInfo,
    HTMLTableSection2_GetIDsOfNames,
    HTMLTableSection2_Invoke,
    HTMLTableSection2_moveRow,
};

static inline HTMLTableSectionElement *impl_from_IHTMLTableSection3(IHTMLTableSection3 *iface)
{
    return CONTAINING_RECORD(iface, HTMLTableSectionElement, IHTMLTableSection3_iface);
}

DISPEX_IDISPATCH_IMPL(HTMLTableSection3, IHTMLTableSection3,
                      impl_from_IHTMLTableSection3(iface)->element.node.event_target.dispex)

static HRESULT WINAPI HTMLTableSection3_put_ch(IHTMLTableSection3 *iface, BSTR v)
{
    HTMLTableSectionElement *This = impl_from_IHTMLTableSection3(iface);
    TRACE("(%p)->(%s)\n", This, debugstr_w(v));
    return elem_put_reflected_string(&This->element, L"ch", v);
}

static HRESULT WINAPI HTMLTableSection3_get_ch(IHTMLTableSection3 *iface, BSTR *p)
{
    HTMLTableSectionElement *This = impl_from_IHTMLTableSection3(iface);
    TRACE("(%p)->(%p)\n", This, p);
    return elem_get_attr_string(&This->element, L"ch", p);
}

static HRESULT WINAPI HTMLTableSection3_put_chOff(IHTMLTableSection3 *iface, BSTR v)
{
    HTMLTableSectionElement *This = impl_from_IHTMLTableSection3(iface);
    TRACE("(%p)->(%s)\n", This, debugstr_w(v));
    return elem_put_reflected_string(&This->element, L"choff", v);
}

static HRESULT WINAPI HTMLTableSection3_get_chOff(IHTMLTableSection3 *iface, BSTR *p)
{
    HTMLTableSectionElement *This = impl_from_IHTMLTableSection3(iface);
    TRACE("(%p)->(%p)\n", This, p);
    return elem_get_attr_string(&This->element, L"choff", p);
}

static const IHTMLTableSection3Vtbl HTMLTableSection3Vtbl = {
    HTMLTableSection3_QueryInterface,
    HTMLTableSection3_AddRef,
    HTMLTableSection3_Release,
    HTMLTableSection3_GetTypeInfoCount,
    HTMLTableSection3_GetTypeInfo,
    HTMLTableSection3_GetIDsOfNames,
    HTMLTableSection3_Invoke,
    HTMLTableSection3_put_ch,
    HTMLTableSection3_get_ch,
    HTMLTableSection3_put_chOff,
    HTMLTableSection3_get_chOff,
};

static inline HTMLTableSectionElement *impl_from_IHTMLTableSection4(IHTMLTableSection4 *iface)
{
    return CONTAINING_RECORD(iface, HTMLTableSectionElement, IHTMLTableSection4_iface);
}

DISPEX_IDISPATCH_IMPL(HTMLTableSection4, IHTMLTableSection4,
                      impl_from_IHTMLTableSection4(iface)->element.node.event_target.dispex)

static HRESULT WINAPI HTMLTableSection4_put_ch(IHTMLTableSection4 *iface, BSTR v)
{
    HTMLTableSectionElement *This = impl_from_IHTMLTableSection4(iface);
    TRACE("(%p)->(%s)\n", This, debugstr_w(v));
    return elem_put_reflected_string(&This->element, L"char", v);
}

static HRESULT WINAPI HTMLTableSection4_get_ch(IHTMLTableSection4 *iface, BSTR *p)
{
    HTMLTableSectionElement *This = impl_from_IHTMLTableSection4(iface);
    TRACE("(%p)->(%p)\n", This, p);
    return elem_get_attr_string(&This->element, L"char", p);
}

static HRESULT WINAPI HTMLTableSection4_put_chOff(IHTMLTableSection4 *iface, BSTR v)
{
    HTMLTableSectionElement *This = impl_from_IHTMLTableSection4(iface);
    TRACE("(%p)->(%s)\n", This, debugstr_w(v));
    return elem_put_reflected_string(&This->element, L"charoff", v);
}

static HRESULT WINAPI HTMLTableSection4_get_chOff(IHTMLTableSection4 *iface, BSTR *p)
{
    HTMLTableSectionElement *This = impl_from_IHTMLTableSection4(iface);
    TRACE("(%p)->(%p)\n", This, p);
    return elem_get_attr_string(&This->element, L"charoff", p);
}

static HRESULT WINAPI HTMLTableSection4_insertRow(IHTMLTableSection4 *iface, LONG index, IDispatch **row)
{
    HTMLTableSectionElement *This = impl_from_IHTMLTableSection4(iface);
    TRACE("(%p)->(%ld %p)\n", This, index, row);
    return section_insert_row(This, index, TRUE, row);
}

static HRESULT WINAPI HTMLTableSection4_deleteRow(IHTMLTableSection4 *iface, LONG index)
{
    HTMLTableSectionElement *This = impl_from_IHTMLTableSection4(iface);
    TRACE("(%p)->(%ld)\n", This, index);
    return section_delete_row(This, index, TRUE);
}

static const IHTMLTableSection4Vtbl HTMLTableSection4Vtbl = {
    HTMLTableSection4_QueryInterface,
    HTMLTableSection4_AddRef,
    HTMLTableSection4_Release,
    HTMLTableSection4_GetTypeInfoCount,
    HTMLTableSection4_GetTypeInfo,
    HTMLTableSection4_GetIDsOfNames,
    HTMLTableSection4_Invoke,
    HTMLTableSection4_put_ch,
    HTMLTableSection4_get_ch,
    HTMLTableSection4_put_chOff,
    HTMLTableSection4_get_chOff,
    HTMLTableSection4_insertRow,
    HTMLTableSection4_deleteRow,
};

static inline HTMLTableSectionElement *HTMLTableSection_from_DispatchEx(DispatchEx *iface)
{
    return CONTAINING_RECORD(iface, HTMLTableSectionElement, element.node.event_target.dispex);
}

static void *HTMLTableSection_query_interface(DispatchEx *dispex, REFIID riid)
{
    HTMLTableSectionElement *This = HTMLTableSection_from_DispatchEx(dispex);

    if(IsEqualGUID(&IID_IHTMLTableSection, riid))
        return &This->IHTMLTableSection_iface;
    if(IsEqualGUID(&IID_IHTMLTableSection2, riid))
        return &This->IHTMLTableSection2_iface;
    if(IsEqualGUID(&IID_IHTMLTableSection3, riid))
        return &This->IHTMLTableSection3_iface;
    if(IsEqualGUID(&IID_IHTMLTableSection4, riid))
        return &This->IHTMLTableSection4_iface;

    return HTMLElement_query_interface(&This->element.node.event_target.dispex, riid);
}

static const NodeImplVtbl HTMLTableSectionImplVtbl = {
    .clsid                 = &CLSID_HTMLTableSection,
    .cpc_entries           = HTMLElement_cpc,
    .clone                 = HTMLElement_clone,
    .get_attr_col          = HTMLElement_get_attr_col,
};

static const event_target_vtbl_t HTMLTableSectionElement_event_target_vtbl = {
    {
        HTMLELEMENT_DISPEX_VTBL_ENTRIES,
        .query_interface= HTMLTableSection_query_interface,
        .destructor     = HTMLElement_destructor,
        .traverse       = HTMLElement_traverse,
        .unlink         = HTMLElement_unlink
    },
    HTMLELEMENT_EVENT_TARGET_VTBL_ENTRIES,
    .handle_event       = HTMLElement_handle_event
};

static void HTMLTableSectionElement_init_dispex_info(dispex_data_t *info, compat_mode_t mode)
{
    static const dispex_hook_t hooks[] = {
        {DISPID_IHTMLTABLESECTION_ROWS, .noattr = TRUE},
        {DISPID_UNKNOWN}
    };
    if(mode >= COMPAT_MODE_IE9)
        dispex_info_add_interface(info, IHTMLTableSection4_tid, NULL);
    dispex_info_add_interface(info, IHTMLTableSection_tid, hooks);
    dispex_info_add_interface(info, IHTMLTableSection2_tid, NULL);
    if(mode < COMPAT_MODE_IE9)
        dispex_info_add_interface(info, IHTMLTableSection3_tid, NULL);

    HTMLElement_init_dispex_info(info, mode);
}

dispex_static_data_t HTMLTableSectionElement_dispex = {
    .id           = OBJID_HTMLTableSectionElement,
    .prototype_id = OBJID_HTMLElement,
    .vtbl         = &HTMLTableSectionElement_event_target_vtbl.dispex_vtbl,
    .disp_tid     = DispHTMLTableSection_tid,
    .init_info    = HTMLTableSectionElement_init_dispex_info,
};

HRESULT HTMLTableSection_Create(HTMLDocumentNode *doc, nsIDOMElement *nselem, HTMLElement **elem)
{
    HTMLTableSectionElement *ret;

    ret = calloc(1, sizeof(*ret));
    if(!ret)
        return E_OUTOFMEMORY;

    ret->IHTMLTableSection_iface.lpVtbl = &HTMLTableSectionVtbl;
    ret->IHTMLTableSection2_iface.lpVtbl = &HTMLTableSection2Vtbl;
    ret->IHTMLTableSection3_iface.lpVtbl = &HTMLTableSection3Vtbl;
    ret->IHTMLTableSection4_iface.lpVtbl = &HTMLTableSection4Vtbl;
    ret->element.node.vtbl = &HTMLTableSectionImplVtbl;

    HTMLElement_Init(&ret->element, doc, nselem, &HTMLTableSectionElement_dispex);

    *elem = &ret->element;
    return S_OK;
}
