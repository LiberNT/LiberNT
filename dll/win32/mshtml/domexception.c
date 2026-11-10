/*
 * PROJECT:     LiberNT MSHTML
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     DOMException object thrown to scripts for DOM errors
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#include <stdarg.h>

#define COBJMACROS

#include "windef.h"
#include "winbase.h"
#include "winuser.h"
#include "ole2.h"
#include "mshtmdid.h"

#include "mshtml_private.h"

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(mshtml);

#define DOM_EXCEPTION_FACILITY_MASK 0xffff0000
#define DOM_EXCEPTION_HRESULT_BASE  0x80700000
#define DOM_EXCEPTION_MAX_CODE      25

static const WCHAR *const dom_exception_names[] = {
    L"UnknownError",
    L"IndexSizeError",
    L"DOMStringSizeError",
    L"HierarchyRequestError",
    L"WrongDocumentError",
    L"InvalidCharacterError",
    L"NoDataAllowedError",
    L"NoModificationAllowedError",
    L"NotFoundError",
    L"NotSupportedError",
    L"InuseAttributeError",
    L"InvalidStateError",
    L"SyntaxError",
    L"InvalidModificationError",
    L"NamespaceError",
    L"InvalidAccessError",
    L"ValidationError",
    L"TypeMismatchError",
    L"SecurityError",
    L"NetworkError",
    L"AbortError",
    L"UrlMismatchError",
    L"QuotaExceededError",
    L"TimeoutError",
    L"InvalidNodeTypeError",
    L"DataCloneError",
    L"ConstraintError",
    L"DataError",
};

static const dispex_constant_t dom_exception_constants[] = {
    {L"INDEX_SIZE_ERR",              1},
    {L"DOMSTRING_SIZE_ERR",          2},
    {L"HIERARCHY_REQUEST_ERR",       3},
    {L"WRONG_DOCUMENT_ERR",          4},
    {L"INVALID_CHARACTER_ERR",       5},
    {L"NO_DATA_ALLOWED_ERR",         6},
    {L"NO_MODIFICATION_ALLOWED_ERR", 7},
    {L"NOT_FOUND_ERR",               8},
    {L"NOT_SUPPORTED_ERR",           9},
    {L"INUSE_ATTRIBUTE_ERR",         10},
    {L"INVALID_STATE_ERR",           11},
    {L"SYNTAX_ERR",                  12},
    {L"INVALID_MODIFICATION_ERR",    13},
    {L"NAMESPACE_ERR",               14},
    {L"INVALID_ACCESS_ERR",          15},
    {L"VALIDATION_ERR",              16},
    {L"TYPE_MISMATCH_ERR",           17},
    {L"SECURITY_ERR",                18},
    {L"NETWORK_ERR",                 19},
    {L"ABORT_ERR",                   20},
    {L"URL_MISMATCH_ERR",            21},
    {L"QUOTA_EXCEEDED_ERR",          22},
    {L"TIMEOUT_ERR",                 23, COMPAT_MODE_IE10},
    {L"INVALID_NODE_TYPE_ERR",       24, COMPAT_MODE_IE10},
    {L"DATA_CLONE_ERR",              25, COMPAT_MODE_IE10},
    {L"PARSE_ERR",                   81},
    {L"SERIALIZE_ERR",               82},
    {NULL}
};

typedef struct {
    DispatchEx dispex;
    IDOMException IDOMException_iface;
    IWineDOMException IWineDOMException_iface;

    ULONG type;
} DOMExceptionObj;

static void set_exception_type(DOMExceptionObj *This, LONG code)
{
    This->type = min((ULONG)code, ARRAY_SIZE(dom_exception_names) - 1);
}

static inline DOMExceptionObj *impl_from_IDOMException(IDOMException *iface)
{
    return CONTAINING_RECORD(iface, DOMExceptionObj, IDOMException_iface);
}

DISPEX_IDISPATCH_IMPL(DOMException, IDOMException, impl_from_IDOMException(iface)->dispex)

static HRESULT WINAPI DOMException_put_code(IDOMException *iface, LONG v)
{
    DOMExceptionObj *This = impl_from_IDOMException(iface);

    TRACE("(%p)->(%ld)\n", This, v);

    set_exception_type(This, v);
    return S_OK;
}

static HRESULT WINAPI DOMException_get_code(IDOMException *iface, LONG *p)
{
    DOMExceptionObj *This = impl_from_IDOMException(iface);

    TRACE("(%p)->(%p)\n", This, p);

    *p = This->type <= DOM_EXCEPTION_MAX_CODE ? This->type : 0;
    return S_OK;
}

static HRESULT WINAPI DOMException_get_message(IDOMException *iface, BSTR *p)
{
    DOMExceptionObj *This = impl_from_IDOMException(iface);

    TRACE("(%p)->(%p)\n", This, p);

    *p = SysAllocString(dom_exception_names[This->type]);
    return *p ? S_OK : E_OUTOFMEMORY;
}

static const IDOMExceptionVtbl DOMExceptionVtbl = {
    DOMException_QueryInterface,
    DOMException_AddRef,
    DOMException_Release,
    DOMException_GetTypeInfoCount,
    DOMException_GetTypeInfo,
    DOMException_GetIDsOfNames,
    DOMException_Invoke,
    DOMException_put_code,
    DOMException_get_code,
    DOMException_get_message
};

static inline DOMExceptionObj *impl_from_IWineDOMException(IWineDOMException *iface)
{
    return CONTAINING_RECORD(iface, DOMExceptionObj, IWineDOMException_iface);
}

DISPEX_IDISPATCH_IMPL(WineDOMException, IWineDOMException, impl_from_IWineDOMException(iface)->dispex)

static HRESULT WINAPI WineDOMException_get_code(IWineDOMException *iface, LONG *p)
{
    DOMExceptionObj *This = impl_from_IWineDOMException(iface);

    return DOMException_get_code(&This->IDOMException_iface, p);
}

static HRESULT WINAPI WineDOMException_get_message(IWineDOMException *iface, BSTR *p)
{
    DOMExceptionObj *This = impl_from_IWineDOMException(iface);

    return DOMException_get_message(&This->IDOMException_iface, p);
}

static HRESULT WINAPI WineDOMException_get_name(IWineDOMException *iface, BSTR *p)
{
    DOMExceptionObj *This = impl_from_IWineDOMException(iface);

    TRACE("(%p)->(%p)\n", This, p);

    return DOMException_get_message(&This->IDOMException_iface, p);
}

static HRESULT WINAPI WineDOMException_toString(IWineDOMException *iface, BSTR *String)
{
    DOMExceptionObj *This = impl_from_IWineDOMException(iface);

    TRACE("(%p)->(%p)\n", This, String);

    return DOMException_get_message(&This->IDOMException_iface, String);
}

static const IWineDOMExceptionVtbl WineDOMExceptionVtbl = {
    WineDOMException_QueryInterface,
    WineDOMException_AddRef,
    WineDOMException_Release,
    WineDOMException_GetTypeInfoCount,
    WineDOMException_GetTypeInfo,
    WineDOMException_GetIDsOfNames,
    WineDOMException_Invoke,
    WineDOMException_get_code,
    WineDOMException_get_message,
    WineDOMException_get_name,
    WineDOMException_toString
};

static inline DOMExceptionObj *DOMException_from_DispatchEx(DispatchEx *iface)
{
    return CONTAINING_RECORD(iface, DOMExceptionObj, dispex);
}

static void *DOMException_query_interface(DispatchEx *dispex, REFIID riid)
{
    DOMExceptionObj *This = DOMException_from_DispatchEx(dispex);

    if(IsEqualGUID(&IID_IDOMException, riid))
        return &This->IDOMException_iface;
    if(IsEqualGUID(&IID_IWineDOMException, riid))
        return &This->IWineDOMException_iface;

    return NULL;
}

static void DOMException_destructor(DispatchEx *dispex)
{
    DOMExceptionObj *This = DOMException_from_DispatchEx(dispex);
    free(This);
}

static void DOMException_init_dispex_info(dispex_data_t *info, compat_mode_t mode)
{
    static const dispex_hook_t ie9_hooks[] = {
        {DISPID_IWINEDOMEXCEPTION_NAME},
        {DISPID_UNKNOWN}
    };

    dispex_info_add_interface(info, IWineDOMException_tid, mode < COMPAT_MODE_IE10 ? ie9_hooks : NULL);
}

static const dispex_static_data_vtbl_t DOMException_dispex_vtbl = {
    .query_interface = DOMException_query_interface,
    .destructor      = DOMException_destructor,
};

dispex_static_data_t DOMException_dispex = {
    .id              = OBJID_DOMException,
    .vtbl            = &DOMException_dispex_vtbl,
    .disp_tid        = DispDOMException_tid,
    .init_info       = DOMException_init_dispex_info,
    .min_compat_mode = COMPAT_MODE_IE9,
    .constants       = dom_exception_constants,
};

HRESULT create_dom_exception(DispatchEx *owner, HRESULT error, IDispatch **ret)
{
    DOMExceptionObj *exception;

    if((error & DOM_EXCEPTION_FACILITY_MASK) != DOM_EXCEPTION_HRESULT_BASE || dispex_compat_mode(owner) < COMPAT_MODE_IE9)
        return S_FALSE;

    if(!(exception = calloc(1, sizeof(*exception))))
        return E_OUTOFMEMORY;

    exception->IDOMException_iface.lpVtbl = &DOMExceptionVtbl;
    exception->IWineDOMException_iface.lpVtbl = &WineDOMExceptionVtbl;
    set_exception_type(exception, error & ~DOM_EXCEPTION_FACILITY_MASK);

    init_dispatch_with_owner(&exception->dispex, &DOMException_dispex, owner);

    *ret = (IDispatch*)&exception->IDOMException_iface;
    return S_OK;
}
