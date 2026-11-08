/*
 * PROJECT:     LiberNT MSHTML
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     List, list item and definition list elements
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

typedef enum {
    LIST_OL,
    LIST_UL,
    LIST_DL
} list_kind_t;

typedef enum {
    LIST_NUM_EMPTY,
    LIST_NUM_INVALID,
    LIST_NUM_VALID
} list_num_t;

static const WCHAR *const list_types[] = { L"1", L"a", L"A", L"i", L"I", L"disc", L"circle", L"square" };

static BOOL is_list_space(WCHAR c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

static list_num_t parse_list_number(const WCHAR *str, LONG *ret, BOOL *negative)
{
    LONGLONG val = 0;

    *negative = FALSE;
    if(!*str)
        return LIST_NUM_EMPTY;

    while(is_list_space(*str))
        str++;
    if(*str == '-' || *str == '+')
        *negative = *str++ == '-';
    if(!iswdigit(*str))
        return LIST_NUM_INVALID;
    while(iswdigit(*str)) {
        if(val <= 0x80000000ll)
            val = val * 10 + (*str - '0');
        str++;
    }
    if(*str == '.') {
        str++;
        while(iswdigit(*str))
            str++;
    }
    while(is_list_space(*str))
        str++;
    if(*str)
        return LIST_NUM_INVALID;

    if(*negative)
        val = -val;
    *ret = val > 0x7fffffffll ? 0x7fffffff : val < -0x80000000ll ? (LONG)0x80000000 : (LONG)val;
    return LIST_NUM_VALID;
}

static HRESULT get_list_number(HTMLElement *elem, const WCHAR *name, BOOL li_value, LONG *p)
{
    BOOL negative;
    HRESULT hres;
    LONG val;
    BSTR str;

    hres = elem_get_attr_string(elem, name, &str);
    if(FAILED(hres))
        return hres;
    if(!str) {
        *p = li_value ? 0 : 1;
        return S_OK;
    }

    if(dispex_compat_mode(&elem->node.event_target.dispex) >= COMPAT_MODE_IE8) {
        if(parse_list_number(str, &val, &negative) != LIST_NUM_VALID)
            *p = 1;
        else
            *p = li_value && negative ? 0 : val;
    }else if(FAILED(VarI4FromStr(str, MAKELCID(MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US), SORT_DEFAULT), 0, &val))) {
        *p = li_value ? 0 : 1;
    }else {
        *p = li_value && val < 0 ? 0 : val;
    }
    SysFreeString(str);
    return S_OK;
}

static HRESULT put_list_number(HTMLElement *elem, const WCHAR *name, BOOL li_value, LONG v)
{
    WCHAR buf[12];
    HRESULT hres;
    LONG cur;

    hres = get_list_number(elem, name, li_value, &cur);
    /* FIXME: quirks keeps an invalid li value attribute string after a put of another value */
    if(FAILED(hres) || cur == v)
        return hres;
    if(li_value && v < 1)
        return CTL_E_INVALIDPROPERTYVALUE;

    swprintf(buf, ARRAY_SIZE(buf), L"%ld", v);
    return elem_put_reflected_string(elem, name, buf);
}

static const WCHAR *find_list_type(const WCHAR *str)
{
    unsigned i;

    for(i = 0; i < ARRAY_SIZE(list_types); i++) {
        if(!wcscmp(list_types[i], str))
            return list_types[i];
    }
    return NULL;
}

static HRESULT get_list_type(HTMLElement *elem, BSTR *p)
{
    const WCHAR *type;
    HRESULT hres;
    BSTR str;

    *p = NULL;
    hres = elem_get_attr_string(elem, L"type", &str);
    if(FAILED(hres) || !str)
        return hres;
    type = find_list_type(str);
    SysFreeString(str);
    if(type && !(*p = SysAllocString(type)))
        return E_OUTOFMEMORY;
    return S_OK;
}

static HRESULT put_list_type(HTMLElement *elem, BSTR v)
{
    const WCHAR *type = NULL;
    HRESULT hres;
    BSTR cur;

    if(v && *v && !(type = find_list_type(v)))
        return E_INVALIDARG;

    hres = get_list_type(elem, &cur);
    if(FAILED(hres))
        return hres;
    if(!type && !cur)
        return S_OK;
    if(type && cur && !wcscmp(type, cur)) {
        SysFreeString(cur);
        return S_OK;
    }
    SysFreeString(cur);

    if(!type)
        return element_remove_attribute(elem, L"type");
    return elem_put_reflected_string(elem, L"type", (BSTR)type);
}

struct HTMLListElement {
    HTMLElement element;

    IHTMLListElement2 IHTMLListElement2_iface;
    IHTMLUListElement IHTMLUListElement_iface;
    IHTMLOListElement IHTMLOListElement_iface;
    IHTMLDListElement IHTMLDListElement_iface;

    list_kind_t kind;
};

typedef struct HTMLListElement HTMLListElement;

static inline HTMLListElement *impl_from_IHTMLListElement2(IHTMLListElement2 *iface)
{
    return CONTAINING_RECORD(iface, HTMLListElement, IHTMLListElement2_iface);
}

DISPEX_IDISPATCH_IMPL(HTMLListElement2, IHTMLListElement2,
                      impl_from_IHTMLListElement2(iface)->element.node.event_target.dispex)

static HRESULT WINAPI HTMLListElement2_put_compact(IHTMLListElement2 *iface, VARIANT_BOOL v)
{
    return elem_put_bool_attr(&impl_from_IHTMLListElement2(iface)->element, L"compact", v);
}

static HRESULT WINAPI HTMLListElement2_get_compact(IHTMLListElement2 *iface, VARIANT_BOOL *p)
{
    return elem_get_bool_attr(&impl_from_IHTMLListElement2(iface)->element, L"compact", p);
}

static const IHTMLListElement2Vtbl HTMLListElement2Vtbl = {
    HTMLListElement2_QueryInterface,
    HTMLListElement2_AddRef,
    HTMLListElement2_Release,
    HTMLListElement2_GetTypeInfoCount,
    HTMLListElement2_GetTypeInfo,
    HTMLListElement2_GetIDsOfNames,
    HTMLListElement2_Invoke,
    HTMLListElement2_put_compact,
    HTMLListElement2_get_compact
};

static inline HTMLListElement *impl_from_IHTMLUListElement(IHTMLUListElement *iface)
{
    return CONTAINING_RECORD(iface, HTMLListElement, IHTMLUListElement_iface);
}

DISPEX_IDISPATCH_IMPL(HTMLUListElement, IHTMLUListElement,
                      impl_from_IHTMLUListElement(iface)->element.node.event_target.dispex)

static HRESULT WINAPI HTMLUListElement_put_compact(IHTMLUListElement *iface, VARIANT_BOOL v)
{
    return elem_put_bool_attr(&impl_from_IHTMLUListElement(iface)->element, L"compact", v);
}

static HRESULT WINAPI HTMLUListElement_get_compact(IHTMLUListElement *iface, VARIANT_BOOL *p)
{
    return elem_get_bool_attr(&impl_from_IHTMLUListElement(iface)->element, L"compact", p);
}

static HRESULT WINAPI HTMLUListElement_put_type(IHTMLUListElement *iface, BSTR v)
{
    return put_list_type(&impl_from_IHTMLUListElement(iface)->element, v);
}

static HRESULT WINAPI HTMLUListElement_get_type(IHTMLUListElement *iface, BSTR *p)
{
    return get_list_type(&impl_from_IHTMLUListElement(iface)->element, p);
}

static const IHTMLUListElementVtbl HTMLUListElementVtbl = {
    HTMLUListElement_QueryInterface,
    HTMLUListElement_AddRef,
    HTMLUListElement_Release,
    HTMLUListElement_GetTypeInfoCount,
    HTMLUListElement_GetTypeInfo,
    HTMLUListElement_GetIDsOfNames,
    HTMLUListElement_Invoke,
    HTMLUListElement_put_compact,
    HTMLUListElement_get_compact,
    HTMLUListElement_put_type,
    HTMLUListElement_get_type
};

static inline HTMLListElement *impl_from_IHTMLOListElement(IHTMLOListElement *iface)
{
    return CONTAINING_RECORD(iface, HTMLListElement, IHTMLOListElement_iface);
}

DISPEX_IDISPATCH_IMPL(HTMLOListElement, IHTMLOListElement,
                      impl_from_IHTMLOListElement(iface)->element.node.event_target.dispex)

static HRESULT WINAPI HTMLOListElement_put_compact(IHTMLOListElement *iface, VARIANT_BOOL v)
{
    return elem_put_bool_attr(&impl_from_IHTMLOListElement(iface)->element, L"compact", v);
}

static HRESULT WINAPI HTMLOListElement_get_compact(IHTMLOListElement *iface, VARIANT_BOOL *p)
{
    return elem_get_bool_attr(&impl_from_IHTMLOListElement(iface)->element, L"compact", p);
}

static HRESULT WINAPI HTMLOListElement_put_start(IHTMLOListElement *iface, LONG v)
{
    return put_list_number(&impl_from_IHTMLOListElement(iface)->element, L"start", FALSE, v);
}

static HRESULT WINAPI HTMLOListElement_get_start(IHTMLOListElement *iface, LONG *p)
{
    return get_list_number(&impl_from_IHTMLOListElement(iface)->element, L"start", FALSE, p);
}

static HRESULT WINAPI HTMLOListElement_put_type(IHTMLOListElement *iface, BSTR v)
{
    return put_list_type(&impl_from_IHTMLOListElement(iface)->element, v);
}

static HRESULT WINAPI HTMLOListElement_get_type(IHTMLOListElement *iface, BSTR *p)
{
    return get_list_type(&impl_from_IHTMLOListElement(iface)->element, p);
}

static const IHTMLOListElementVtbl HTMLOListElementVtbl = {
    HTMLOListElement_QueryInterface,
    HTMLOListElement_AddRef,
    HTMLOListElement_Release,
    HTMLOListElement_GetTypeInfoCount,
    HTMLOListElement_GetTypeInfo,
    HTMLOListElement_GetIDsOfNames,
    HTMLOListElement_Invoke,
    HTMLOListElement_put_compact,
    HTMLOListElement_get_compact,
    HTMLOListElement_put_start,
    HTMLOListElement_get_start,
    HTMLOListElement_put_type,
    HTMLOListElement_get_type
};

static inline HTMLListElement *impl_from_IHTMLDListElement(IHTMLDListElement *iface)
{
    return CONTAINING_RECORD(iface, HTMLListElement, IHTMLDListElement_iface);
}

DISPEX_IDISPATCH_IMPL(HTMLDListElement, IHTMLDListElement,
                      impl_from_IHTMLDListElement(iface)->element.node.event_target.dispex)

static HRESULT WINAPI HTMLDListElement_put_compact(IHTMLDListElement *iface, VARIANT_BOOL v)
{
    return elem_put_bool_attr(&impl_from_IHTMLDListElement(iface)->element, L"compact", v);
}

static HRESULT WINAPI HTMLDListElement_get_compact(IHTMLDListElement *iface, VARIANT_BOOL *p)
{
    return elem_get_bool_attr(&impl_from_IHTMLDListElement(iface)->element, L"compact", p);
}

static const IHTMLDListElementVtbl HTMLDListElementVtbl = {
    HTMLDListElement_QueryInterface,
    HTMLDListElement_AddRef,
    HTMLDListElement_Release,
    HTMLDListElement_GetTypeInfoCount,
    HTMLDListElement_GetTypeInfo,
    HTMLDListElement_GetIDsOfNames,
    HTMLDListElement_Invoke,
    HTMLDListElement_put_compact,
    HTMLDListElement_get_compact
};

static inline HTMLListElement *HTMLListElement_from_DispatchEx(DispatchEx *iface)
{
    return CONTAINING_RECORD(iface, HTMLListElement, element.node.event_target.dispex);
}

static void *HTMLListElement_query_interface(DispatchEx *dispex, REFIID riid)
{
    HTMLListElement *This = HTMLListElement_from_DispatchEx(dispex);

    if(IsEqualGUID(&IID_IHTMLListElement2, riid))
        return &This->IHTMLListElement2_iface;
    if(This->kind == LIST_UL && IsEqualGUID(&IID_IHTMLUListElement, riid))
        return &This->IHTMLUListElement_iface;
    if(This->kind == LIST_OL && IsEqualGUID(&IID_IHTMLOListElement, riid))
        return &This->IHTMLOListElement_iface;
    if(This->kind == LIST_DL && IsEqualGUID(&IID_IHTMLDListElement, riid))
        return &This->IHTMLDListElement_iface;

    return HTMLElement_query_interface(&This->element.node.event_target.dispex, riid);
}

static const NodeImplVtbl HTMLUListElementImplVtbl = {
    .clsid                 = &CLSID_HTMLUListElement,
    .cpc_entries           = HTMLElement_cpc,
    .clone                 = HTMLElement_clone,
    .get_attr_col          = HTMLElement_get_attr_col
};

static const NodeImplVtbl HTMLOListElementImplVtbl = {
    .clsid                 = &CLSID_HTMLOListElement,
    .cpc_entries           = HTMLElement_cpc,
    .clone                 = HTMLElement_clone,
    .get_attr_col          = HTMLElement_get_attr_col
};

static const NodeImplVtbl HTMLDListElementImplVtbl = {
    .clsid                 = &CLSID_HTMLDListElement,
    .cpc_entries           = HTMLElement_cpc,
    .clone                 = HTMLElement_clone,
    .get_attr_col          = HTMLElement_get_attr_col
};

static const event_target_vtbl_t HTMLListElement_event_target_vtbl = {
    {
        HTMLELEMENT_DISPEX_VTBL_ENTRIES,
        .query_interface= HTMLListElement_query_interface,
        .destructor     = HTMLElement_destructor,
        .traverse       = HTMLElement_traverse,
        .unlink         = HTMLElement_unlink
    },
    HTMLELEMENT_EVENT_TARGET_VTBL_ENTRIES,
    .handle_event       = HTMLElement_handle_event
};

static void HTMLUListElement_init_dispex_info(dispex_data_t *info, compat_mode_t mode)
{
    HTMLElement_init_dispex_info(info, mode);
    dispex_info_add_interface(info, IHTMLUListElement_tid, NULL);
}

static void HTMLOListElement_init_dispex_info(dispex_data_t *info, compat_mode_t mode)
{
    HTMLElement_init_dispex_info(info, mode);
    dispex_info_add_interface(info, IHTMLOListElement_tid, NULL);
}

static void HTMLDListElement_init_dispex_info(dispex_data_t *info, compat_mode_t mode)
{
    HTMLElement_init_dispex_info(info, mode);
    dispex_info_add_interface(info, IHTMLDListElement_tid, NULL);
}

dispex_static_data_t HTMLUListElement_dispex = {
    .id           = OBJID_HTMLUListElement,
    .prototype_id = OBJID_HTMLElement,
    .vtbl         = &HTMLListElement_event_target_vtbl.dispex_vtbl,
    .disp_tid     = DispHTMLUListElement_tid,
    .init_info    = HTMLUListElement_init_dispex_info,
};

dispex_static_data_t HTMLDirectoryElement_dispex = {
    .id           = OBJID_HTMLDirectoryElement,
    .prototype_id = OBJID_HTMLElement,
    .vtbl         = &HTMLListElement_event_target_vtbl.dispex_vtbl,
    .disp_tid     = DispHTMLUListElement_tid,
    .init_info    = HTMLUListElement_init_dispex_info,
};

dispex_static_data_t HTMLMenuElement_dispex = {
    .id           = OBJID_HTMLMenuElement,
    .prototype_id = OBJID_HTMLElement,
    .vtbl         = &HTMLListElement_event_target_vtbl.dispex_vtbl,
    .disp_tid     = DispHTMLUListElement_tid,
    .init_info    = HTMLUListElement_init_dispex_info,
};

dispex_static_data_t HTMLOListElement_dispex = {
    .id           = OBJID_HTMLOListElement,
    .prototype_id = OBJID_HTMLElement,
    .vtbl         = &HTMLListElement_event_target_vtbl.dispex_vtbl,
    .disp_tid     = DispHTMLOListElement_tid,
    .init_info    = HTMLOListElement_init_dispex_info,
};

dispex_static_data_t HTMLDListElement_dispex = {
    .id           = OBJID_HTMLDListElement,
    .prototype_id = OBJID_HTMLElement,
    .vtbl         = &HTMLListElement_event_target_vtbl.dispex_vtbl,
    .disp_tid     = DispHTMLDListElement_tid,
    .init_info    = HTMLDListElement_init_dispex_info,
};

static HRESULT create_list_element(HTMLDocumentNode *doc, nsIDOMElement *nselem, list_kind_t kind,
        const NodeImplVtbl *vtbl, dispex_static_data_t *dispex, HTMLElement **elem)
{
    HTMLListElement *ret;

    ret = calloc(1, sizeof(*ret));
    if(!ret)
        return E_OUTOFMEMORY;

    ret->IHTMLListElement2_iface.lpVtbl = &HTMLListElement2Vtbl;
    ret->IHTMLUListElement_iface.lpVtbl = &HTMLUListElementVtbl;
    ret->IHTMLOListElement_iface.lpVtbl = &HTMLOListElementVtbl;
    ret->IHTMLDListElement_iface.lpVtbl = &HTMLDListElementVtbl;
    ret->kind = kind;
    ret->element.node.vtbl = vtbl;

    HTMLElement_Init(&ret->element, doc, nselem, dispex);

    *elem = &ret->element;
    return S_OK;
}

HRESULT HTMLUListElement_Create(HTMLDocumentNode *doc, nsIDOMElement *nselem, HTMLElement **elem)
{
    return create_list_element(doc, nselem, LIST_UL, &HTMLUListElementImplVtbl, &HTMLUListElement_dispex, elem);
}

HRESULT HTMLDirectoryElement_Create(HTMLDocumentNode *doc, nsIDOMElement *nselem, HTMLElement **elem)
{
    return create_list_element(doc, nselem, LIST_UL, &HTMLUListElementImplVtbl, &HTMLDirectoryElement_dispex, elem);
}

HRESULT HTMLMenuElement_Create(HTMLDocumentNode *doc, nsIDOMElement *nselem, HTMLElement **elem)
{
    return create_list_element(doc, nselem, LIST_UL, &HTMLUListElementImplVtbl, &HTMLMenuElement_dispex, elem);
}

HRESULT HTMLOListElement_Create(HTMLDocumentNode *doc, nsIDOMElement *nselem, HTMLElement **elem)
{
    return create_list_element(doc, nselem, LIST_OL, &HTMLOListElementImplVtbl, &HTMLOListElement_dispex, elem);
}

HRESULT HTMLDListElement_Create(HTMLDocumentNode *doc, nsIDOMElement *nselem, HTMLElement **elem)
{
    return create_list_element(doc, nselem, LIST_DL, &HTMLDListElementImplVtbl, &HTMLDListElement_dispex, elem);
}

struct HTMLLIElement {
    HTMLElement element;

    IHTMLLIElement IHTMLLIElement_iface;
};

typedef struct HTMLLIElement HTMLLIElement;

static inline HTMLLIElement *impl_from_IHTMLLIElement(IHTMLLIElement *iface)
{
    return CONTAINING_RECORD(iface, HTMLLIElement, IHTMLLIElement_iface);
}

DISPEX_IDISPATCH_IMPL(HTMLLIElement, IHTMLLIElement,
                      impl_from_IHTMLLIElement(iface)->element.node.event_target.dispex)

static HRESULT WINAPI HTMLLIElement_put_type(IHTMLLIElement *iface, BSTR v)
{
    return put_list_type(&impl_from_IHTMLLIElement(iface)->element, v);
}

static HRESULT WINAPI HTMLLIElement_get_type(IHTMLLIElement *iface, BSTR *p)
{
    return get_list_type(&impl_from_IHTMLLIElement(iface)->element, p);
}

static HRESULT WINAPI HTMLLIElement_put_value(IHTMLLIElement *iface, LONG v)
{
    return put_list_number(&impl_from_IHTMLLIElement(iface)->element, L"value", TRUE, v);
}

static HRESULT WINAPI HTMLLIElement_get_value(IHTMLLIElement *iface, LONG *p)
{
    return get_list_number(&impl_from_IHTMLLIElement(iface)->element, L"value", TRUE, p);
}

static const IHTMLLIElementVtbl HTMLLIElementVtbl = {
    HTMLLIElement_QueryInterface,
    HTMLLIElement_AddRef,
    HTMLLIElement_Release,
    HTMLLIElement_GetTypeInfoCount,
    HTMLLIElement_GetTypeInfo,
    HTMLLIElement_GetIDsOfNames,
    HTMLLIElement_Invoke,
    HTMLLIElement_put_type,
    HTMLLIElement_get_type,
    HTMLLIElement_put_value,
    HTMLLIElement_get_value
};

static inline HTMLLIElement *HTMLLIElement_from_DispatchEx(DispatchEx *iface)
{
    return CONTAINING_RECORD(iface, HTMLLIElement, element.node.event_target.dispex);
}

static void *HTMLLIElement_query_interface(DispatchEx *dispex, REFIID riid)
{
    HTMLLIElement *This = HTMLLIElement_from_DispatchEx(dispex);

    if(IsEqualGUID(&IID_IHTMLLIElement, riid))
        return &This->IHTMLLIElement_iface;

    return HTMLElement_query_interface(&This->element.node.event_target.dispex, riid);
}

static const NodeImplVtbl HTMLLIElementImplVtbl = {
    .clsid                 = &CLSID_HTMLLIElement,
    .cpc_entries           = HTMLElement_cpc,
    .clone                 = HTMLElement_clone,
    .get_attr_col          = HTMLElement_get_attr_col
};

static const event_target_vtbl_t HTMLLIElement_event_target_vtbl = {
    {
        HTMLELEMENT_DISPEX_VTBL_ENTRIES,
        .query_interface= HTMLLIElement_query_interface,
        .destructor     = HTMLElement_destructor,
        .traverse       = HTMLElement_traverse,
        .unlink         = HTMLElement_unlink
    },
    HTMLELEMENT_EVENT_TARGET_VTBL_ENTRIES,
    .handle_event       = HTMLElement_handle_event
};

static void HTMLLIElement_init_dispex_info(dispex_data_t *info, compat_mode_t mode)
{
    HTMLElement_init_dispex_info(info, mode);
    dispex_info_add_interface(info, IHTMLLIElement_tid, NULL);
}

dispex_static_data_t HTMLLIElement_dispex = {
    .id           = OBJID_HTMLLIElement,
    .prototype_id = OBJID_HTMLElement,
    .vtbl         = &HTMLLIElement_event_target_vtbl.dispex_vtbl,
    .disp_tid     = DispHTMLLIElement_tid,
    .init_info    = HTMLLIElement_init_dispex_info,
};

HRESULT HTMLLIElement_Create(HTMLDocumentNode *doc, nsIDOMElement *nselem, HTMLElement **elem)
{
    HTMLLIElement *ret;

    ret = calloc(1, sizeof(*ret));
    if(!ret)
        return E_OUTOFMEMORY;

    ret->IHTMLLIElement_iface.lpVtbl = &HTMLLIElementVtbl;
    ret->element.node.vtbl = &HTMLLIElementImplVtbl;

    HTMLElement_Init(&ret->element, doc, nselem, &HTMLLIElement_dispex);

    *elem = &ret->element;
    return S_OK;
}

struct HTMLDefinitionElement {
    HTMLElement element;

    IHTMLDDElement IHTMLDDElement_iface;
    IHTMLDTElement IHTMLDTElement_iface;

    BOOL is_dt;
};

typedef struct HTMLDefinitionElement HTMLDefinitionElement;

static inline HTMLDefinitionElement *impl_from_IHTMLDDElement(IHTMLDDElement *iface)
{
    return CONTAINING_RECORD(iface, HTMLDefinitionElement, IHTMLDDElement_iface);
}

DISPEX_IDISPATCH_IMPL(HTMLDDElement, IHTMLDDElement,
                      impl_from_IHTMLDDElement(iface)->element.node.event_target.dispex)

static HRESULT WINAPI HTMLDDElement_put_noWrap(IHTMLDDElement *iface, VARIANT_BOOL v)
{
    return elem_put_bool_attr(&impl_from_IHTMLDDElement(iface)->element, L"nowrap", v);
}

static HRESULT WINAPI HTMLDDElement_get_noWrap(IHTMLDDElement *iface, VARIANT_BOOL *p)
{
    return elem_get_bool_attr(&impl_from_IHTMLDDElement(iface)->element, L"nowrap", p);
}

static const IHTMLDDElementVtbl HTMLDDElementVtbl = {
    HTMLDDElement_QueryInterface,
    HTMLDDElement_AddRef,
    HTMLDDElement_Release,
    HTMLDDElement_GetTypeInfoCount,
    HTMLDDElement_GetTypeInfo,
    HTMLDDElement_GetIDsOfNames,
    HTMLDDElement_Invoke,
    HTMLDDElement_put_noWrap,
    HTMLDDElement_get_noWrap
};

static inline HTMLDefinitionElement *impl_from_IHTMLDTElement(IHTMLDTElement *iface)
{
    return CONTAINING_RECORD(iface, HTMLDefinitionElement, IHTMLDTElement_iface);
}

DISPEX_IDISPATCH_IMPL(HTMLDTElement, IHTMLDTElement,
                      impl_from_IHTMLDTElement(iface)->element.node.event_target.dispex)

static HRESULT WINAPI HTMLDTElement_put_noWrap(IHTMLDTElement *iface, VARIANT_BOOL v)
{
    return elem_put_bool_attr(&impl_from_IHTMLDTElement(iface)->element, L"nowrap", v);
}

static HRESULT WINAPI HTMLDTElement_get_noWrap(IHTMLDTElement *iface, VARIANT_BOOL *p)
{
    return elem_get_bool_attr(&impl_from_IHTMLDTElement(iface)->element, L"nowrap", p);
}

static const IHTMLDTElementVtbl HTMLDTElementVtbl = {
    HTMLDTElement_QueryInterface,
    HTMLDTElement_AddRef,
    HTMLDTElement_Release,
    HTMLDTElement_GetTypeInfoCount,
    HTMLDTElement_GetTypeInfo,
    HTMLDTElement_GetIDsOfNames,
    HTMLDTElement_Invoke,
    HTMLDTElement_put_noWrap,
    HTMLDTElement_get_noWrap
};

static inline HTMLDefinitionElement *HTMLDefinitionElement_from_DispatchEx(DispatchEx *iface)
{
    return CONTAINING_RECORD(iface, HTMLDefinitionElement, element.node.event_target.dispex);
}

static void *HTMLDefinitionElement_query_interface(DispatchEx *dispex, REFIID riid)
{
    HTMLDefinitionElement *This = HTMLDefinitionElement_from_DispatchEx(dispex);

    if(!This->is_dt && IsEqualGUID(&IID_IHTMLDDElement, riid))
        return &This->IHTMLDDElement_iface;
    if(This->is_dt && IsEqualGUID(&IID_IHTMLDTElement, riid))
        return &This->IHTMLDTElement_iface;

    return HTMLElement_query_interface(&This->element.node.event_target.dispex, riid);
}

static const NodeImplVtbl HTMLDDElementImplVtbl = {
    .clsid                 = &CLSID_HTMLDDElement,
    .cpc_entries           = HTMLElement_cpc,
    .clone                 = HTMLElement_clone,
    .get_attr_col          = HTMLElement_get_attr_col
};

static const NodeImplVtbl HTMLDTElementImplVtbl = {
    .clsid                 = &CLSID_HTMLDTElement,
    .cpc_entries           = HTMLElement_cpc,
    .clone                 = HTMLElement_clone,
    .get_attr_col          = HTMLElement_get_attr_col
};

static const event_target_vtbl_t HTMLDefinitionElement_event_target_vtbl = {
    {
        HTMLELEMENT_DISPEX_VTBL_ENTRIES,
        .query_interface= HTMLDefinitionElement_query_interface,
        .destructor     = HTMLElement_destructor,
        .traverse       = HTMLElement_traverse,
        .unlink         = HTMLElement_unlink
    },
    HTMLELEMENT_EVENT_TARGET_VTBL_ENTRIES,
    .handle_event       = HTMLElement_handle_event
};

static void HTMLDDElement_init_dispex_info(dispex_data_t *info, compat_mode_t mode)
{
    HTMLElement_init_dispex_info(info, mode);
    dispex_info_add_interface(info, IHTMLDDElement_tid, NULL);
}

static void HTMLDTElement_init_dispex_info(dispex_data_t *info, compat_mode_t mode)
{
    HTMLElement_init_dispex_info(info, mode);
    dispex_info_add_interface(info, IHTMLDTElement_tid, NULL);
}

dispex_static_data_t HTMLDDElement_dispex = {
    .id           = OBJID_HTMLDDElement,
    .prototype_id = OBJID_HTMLElement,
    .vtbl         = &HTMLDefinitionElement_event_target_vtbl.dispex_vtbl,
    .disp_tid     = DispHTMLDDElement_tid,
    .init_info    = HTMLDDElement_init_dispex_info,
};

dispex_static_data_t HTMLDTElement_dispex = {
    .id           = OBJID_HTMLDTElement,
    .prototype_id = OBJID_HTMLElement,
    .vtbl         = &HTMLDefinitionElement_event_target_vtbl.dispex_vtbl,
    .disp_tid     = DispHTMLDTElement_tid,
    .init_info    = HTMLDTElement_init_dispex_info,
};

static HRESULT create_definition_element(HTMLDocumentNode *doc, nsIDOMElement *nselem, BOOL is_dt, HTMLElement **elem)
{
    HTMLDefinitionElement *ret;

    ret = calloc(1, sizeof(*ret));
    if(!ret)
        return E_OUTOFMEMORY;

    ret->IHTMLDDElement_iface.lpVtbl = &HTMLDDElementVtbl;
    ret->IHTMLDTElement_iface.lpVtbl = &HTMLDTElementVtbl;
    ret->is_dt = is_dt;
    ret->element.node.vtbl = is_dt ? &HTMLDTElementImplVtbl : &HTMLDDElementImplVtbl;

    HTMLElement_Init(&ret->element, doc, nselem, is_dt ? &HTMLDTElement_dispex : &HTMLDDElement_dispex);

    *elem = &ret->element;
    return S_OK;
}

HRESULT HTMLDDElement_Create(HTMLDocumentNode *doc, nsIDOMElement *nselem, HTMLElement **elem)
{
    return create_definition_element(doc, nselem, FALSE, elem);
}

HRESULT HTMLDTElement_Create(HTMLDocumentNode *doc, nsIDOMElement *nselem, HTMLElement **elem)
{
    return create_definition_element(doc, nselem, TRUE, elem);
}

const WCHAR *list_canonical_attr(HTMLElement *elem, const WCHAR *name, const WCHAR *value, WCHAR *buf, unsigned size)
{
    BOOL li_value, negative;
    list_num_t r;
    LONG val;

    if(elem->node.vtbl == &HTMLOListElementImplVtbl && !wcsicmp(name, L"start"))
        li_value = FALSE;
    else if(elem->node.vtbl == &HTMLLIElementImplVtbl && !wcsicmp(name, L"value"))
        li_value = TRUE;
    else
        return NULL;

    r = parse_list_number(value, &val, &negative);
    if(r == LIST_NUM_EMPTY)
        return L"1";
    if(r == LIST_NUM_INVALID || (li_value && negative))
        return NULL;
    swprintf(buf, size, L"%ld", val);
    return buf;
}

BOOL is_list_attr(HTMLElement *elem, const WCHAR *name)
{
    if(!wcsicmp(name, L"type"))
        return elem->node.vtbl == &HTMLOListElementImplVtbl || elem->node.vtbl == &HTMLUListElementImplVtbl ||
               elem->node.vtbl == &HTMLLIElementImplVtbl;
    if(!wcsicmp(name, L"start"))
        return elem->node.vtbl == &HTMLOListElementImplVtbl;
    if(!wcsicmp(name, L"value"))
        return elem->node.vtbl == &HTMLLIElementImplVtbl;
    return FALSE;
}

BOOL list_attr_is_invalid(HTMLElement *elem, const WCHAR *name)
{
    BOOL ret;
    LONG val;
    BSTR str;

    if(!is_list_attr(elem, name) || FAILED(elem_get_attr_string(elem, name, &str)) || !str)
        return FALSE;

    if(!wcsicmp(name, L"type"))
        ret = *str && !find_list_type(str);
    else
        ret = FAILED(VarI4FromStr(str, MAKELCID(MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US), SORT_DEFAULT), 0, &val))
              || (elem->node.vtbl == &HTMLLIElementImplVtbl && val < 0);

    SysFreeString(str);
    return ret;
}
