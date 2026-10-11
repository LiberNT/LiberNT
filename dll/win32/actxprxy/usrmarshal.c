/*
 * Miscellaneous Marshaling Routines
 *
 * Copyright 2006 Robert Shearman (for CodeWeavers)
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
#include <string.h>

#define COBJMACROS

#include "windef.h"
#include "winbase.h"
#include "wingdi.h"
#include "winuser.h"
#include "winerror.h"
#include "objbase.h"
#include "servprov.h"
#include "comcat.h"
#include "docobj.h"
#include "shobjidl.h"

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(actxprxy);

HRESULT CALLBACK IServiceProvider_QueryService_Proxy(
    IServiceProvider* This,
    REFGUID guidService,
    REFIID riid,
    void** ppvObject)
{
    TRACE("(%p, %s, %s, %p)\n", This, debugstr_guid(guidService),
        debugstr_guid(riid), ppvObject);

    return IServiceProvider_RemoteQueryService_Proxy(This, guidService, riid,
        (IUnknown **)ppvObject);
}

HRESULT __RPC_STUB IServiceProvider_QueryService_Stub(
    IServiceProvider* This,
    REFGUID guidService,
    REFIID riid,
    IUnknown** ppvObject)
{
    TRACE("(%p, %s, %s, %p)\n", This, debugstr_guid(guidService),
        debugstr_guid(riid), ppvObject);

    return IServiceProvider_QueryService(This, guidService, riid,
        (void **)ppvObject);
}

HRESULT CALLBACK ICatInformation_EnumClassesOfCategories_Proxy(
    ICatInformation *This,
    ULONG cImplemented,
    CATID rgcatidImpl[],
    ULONG cRequired,
    CATID rgcatidReq[],
    IEnumCLSID** ppenumClsid )
{
    TRACE("(%p)\n", This);
    return ICatInformation_RemoteEnumClassesOfCategories_Proxy( This, cImplemented, rgcatidImpl,
                                                                cRequired, rgcatidReq, ppenumClsid );
}

HRESULT __RPC_STUB ICatInformation_EnumClassesOfCategories_Stub(
    ICatInformation *This,
    ULONG cImplemented,
    CATID rgcatidImpl[],
    ULONG cRequired,
    CATID rgcatidReq[],
    IEnumCLSID** ppenumClsid )
{
    TRACE("(%p)\n", This);
    return ICatInformation_EnumClassesOfCategories( This, cImplemented, rgcatidImpl,
                                                    cRequired, rgcatidReq, ppenumClsid );
}

HRESULT CALLBACK ICatInformation_IsClassOfCategories_Proxy(
    ICatInformation *This,
    REFCLSID rclsid,
    ULONG cImplemented,
    CATID rgcatidImpl[],
    ULONG cRequired,
    CATID rgcatidReq[] )
{
    TRACE("(%p)\n", This);
    return ICatInformation_RemoteIsClassOfCategories_Proxy( This, rclsid, cImplemented, rgcatidImpl,
                                                            cRequired, rgcatidReq );
}

HRESULT __RPC_STUB ICatInformation_IsClassOfCategories_Stub(
    ICatInformation *This,
    REFCLSID rclsid,
    ULONG cImplemented,
    CATID rgcatidImpl[],
    ULONG cRequired,
    CATID rgcatidReq[] )
{
    TRACE("(%p)\n", This);
    return ICatInformation_IsClassOfCategories( This, rclsid, cImplemented, rgcatidImpl,
                                                cRequired, rgcatidReq );
}

HRESULT CALLBACK IPrint_Print_Proxy(
    IPrint *This,
    DWORD grfFlags,
    DVTARGETDEVICE **pptd,
    PAGESET **ppPageSet,
    STGMEDIUM *pstgmOptions,
    IContinueCallback *pcallback,
    LONG nFirstPage,
    LONG *pcPagesPrinted,
    LONG *pnLastPage )
{
    TRACE("(%p)\n", This);
    return IPrint_RemotePrint_Proxy( This, grfFlags, pptd, ppPageSet, (RemSTGMEDIUM *)pstgmOptions,
                                     pcallback, nFirstPage, pcPagesPrinted, pnLastPage );
}

HRESULT __RPC_STUB IPrint_Print_Stub(
    IPrint *This,
    DWORD grfFlags,
    DVTARGETDEVICE **pptd,
    PAGESET **ppPageSet,
    RemSTGMEDIUM *pstgmOptions,
    IContinueCallback *pcallback,
    LONG nFirstPage,
    LONG *pcPagesPrinted,
    LONG *pnLastPage )
{
    TRACE("(%p)\n", This);
    return IPrint_Print( This, grfFlags, pptd, ppPageSet, (STGMEDIUM *)pstgmOptions,
                         pcallback, nFirstPage, pcPagesPrinted, pnLastPage );
}

HRESULT CALLBACK IEnumOleDocumentViews_Next_Proxy(
    IEnumOleDocumentViews *This,
    ULONG cViews,
    IOleDocumentView **rgpView,
    ULONG *pcFetched )
{
    TRACE("(%p)\n", This);
    return IEnumOleDocumentViews_RemoteNext_Proxy( This, cViews, rgpView, pcFetched );
}

HRESULT __RPC_STUB IEnumOleDocumentViews_Next_Stub(
    IEnumOleDocumentViews *This,
    ULONG cViews,
    IOleDocumentView **rgpView,
    ULONG *pcFetched )
{
    TRACE("(%p)\n", This);
    return IEnumOleDocumentViews_Next( This, cViews, rgpView, pcFetched );
}

HRESULT CALLBACK IEnumShellItems_Next_Proxy(
    IEnumShellItems *This,
    ULONG celt,
    IShellItem **rgelt,
    ULONG *pceltFetched)
{
    ULONG fetched;
    TRACE("(%p)->(%ld, %p, %p)\n", This, celt, rgelt, pceltFetched);
    if (!pceltFetched) pceltFetched = &fetched;
    return IEnumShellItems_RemoteNext_Proxy(This, celt, rgelt, pceltFetched);
}

HRESULT __RPC_STUB IEnumShellItems_Next_Stub(
    IEnumShellItems *This,
    ULONG celt,
    IShellItem **rgelt,
    ULONG *pceltFetched)
{
    HRESULT hr;
    TRACE("(%p)->(%ld, %p, %p)\n", This, celt, rgelt, pceltFetched);
    *pceltFetched = 0;
    hr = IEnumShellItems_Next(This, celt, rgelt, pceltFetched);
    if (hr == S_OK) *pceltFetched = celt;
    return hr;
}

#ifdef __REACTOS__

HRESULT CALLBACK IEnumExplorerCommand_Next_Proxy(
    IEnumExplorerCommand *This,
    ULONG celt,
    IExplorerCommand **pUICommand,
    ULONG *pceltFetched)
{
    ULONG fetched;
    TRACE("(%p)->(%ld, %p, %p)\n", This, celt, pUICommand, pceltFetched);
    if (!pceltFetched) pceltFetched = &fetched;
    return IEnumExplorerCommand_RemoteNext_Proxy(This, celt, pUICommand, pceltFetched);
}

HRESULT __RPC_STUB IEnumExplorerCommand_Next_Stub(
    IEnumExplorerCommand *This,
    ULONG celt,
    IExplorerCommand **pUICommand,
    ULONG *pceltFetched)
{
    HRESULT hr;
    TRACE("(%p)->(%ld, %p, %p)\n", This, celt, pUICommand, pceltFetched);
    *pceltFetched = 0;
    hr = IEnumExplorerCommand_Next(This, celt, pUICommand, pceltFetched);
    if (hr == S_OK) *pceltFetched = celt;
    return hr;
}

#endif /* __REACTOS__ */

HRESULT CALLBACK IModalWindow_Show_Proxy(
    IModalWindow *This,
    HWND hwndOwner)
{
    TRACE("(%p)->(%p)\n", This, hwndOwner);
    return IModalWindow_RemoteShow_Proxy(This, hwndOwner);
}

HRESULT __RPC_STUB IModalWindow_Show_Stub(
    IModalWindow *This,
    HWND hwndOwner)
{
    TRACE("(%p)->(%p)\n", This, hwndOwner);
    return IModalWindow_Show(This, hwndOwner);
}

HRESULT __RPC_STUB IFolderView2_GetGroupBy_Stub(
    IFolderView2 *This,
    PROPERTYKEY *pkey,
    BOOL *ascending)
{
    TRACE("(%p)->(%p %p)\n", This, pkey, ascending);
    return IFolderView2_GetGroupBy(This, pkey, ascending);
}

HRESULT __RPC_STUB IFolderView2_GetGroupBy_Proxy(
    IFolderView2 *This,
    PROPERTYKEY *pkey,
    BOOL *ascending)
{
    TRACE("(%p)->(%p %p)\n", This, pkey, ascending);
    return IFolderView2_RemoteGetGroupBy_Proxy(This, pkey, ascending);
}

HRESULT __RPC_STUB IParentAndItem_GetParentAndItem_Stub(
    IParentAndItem *This,
    PIDLIST_ABSOLUTE *parent,
    IShellFolder **folder,
    PITEMID_CHILD *child)
{
    TRACE("(%p)->(%p %p %p)\n", This, parent, folder, child);
    return IParentAndItem_GetParentAndItem(This, parent, folder, child);
}

HRESULT __RPC_STUB IParentAndItem_GetParentAndItem_Proxy(
    IParentAndItem *This,
    PIDLIST_ABSOLUTE *parent,
    IShellFolder **folder,
    PITEMID_CHILD *child)
{
    TRACE("(%p)->(%p %p %p)\n", This, parent, folder, child);
    return IParentAndItem_RemoteGetParentAndItem_Proxy(This, parent, folder, child);
}

HRESULT CALLBACK IEnumObjects_Next_Proxy(IEnumObjects *This, ULONG celt, REFIID riid, void **rgelt, ULONG *pceltFetched)
{
    ULONG fetched;
    TRACE("(%p)->(%ld, %p, %p, %p)\n", This, celt, debugstr_guid(riid), rgelt, pceltFetched);
    if (!pceltFetched) pceltFetched = &fetched;
    return IEnumObjects_RemoteNext_Proxy(This, celt, riid, rgelt, pceltFetched);
}

HRESULT __RPC_STUB IEnumObjects_Next_Stub(IEnumObjects *This, ULONG celt, REFIID riid, void **rgelt, ULONG *pceltFetched)
{
    HRESULT hr;
    TRACE("(%p)->(%ld, %p, %p, %p)\n", This, celt, debugstr_guid(riid), rgelt, pceltFetched);
    *pceltFetched = 0;
    hr = IEnumObjects_Next(This, celt, riid, rgelt, pceltFetched);
    return hr;
}

#ifdef __REACTOS__
#define PIDL_ALIGN_LENGTH(len, align) (((len) + (align)) & ~(align))
#define PIDL_ALIGN_POINTER(ptr, align) ((unsigned char *)PIDL_ALIGN_LENGTH((ULONG_PTR)(ptr), (align)))

static ULONG pidl_get_size(const ITEMIDLIST *pidl)
{
    const BYTE *ptr = (const BYTE *)pidl;
    ULONG size = sizeof(USHORT);
    USHORT cb;

    for (;;)
    {
        memcpy(&cb, ptr, sizeof(cb));
        if (!cb)
            break;
        size += cb;
        ptr += cb;
    }
    return size;
}

static ULONG pidl_user_size(ULONG start, const ITEMIDLIST *pidl)
{
    ULONG size = PIDL_ALIGN_LENGTH(start, 3) + sizeof(ULONG);

    if (pidl)
        size += 2 * sizeof(ULONG) + pidl_get_size(pidl);
    return size;
}

static unsigned char *pidl_user_marshal(unsigned char *buffer, const ITEMIDLIST *pidl)
{
    ULONG cb;

    buffer = PIDL_ALIGN_POINTER(buffer, 3);
    *(ULONG *)buffer = pidl ? 0x20000 : 0;
    buffer += sizeof(ULONG);
    if (!pidl)
        return buffer;

    cb = pidl_get_size(pidl);
    *(ULONG *)buffer = cb;
    buffer += sizeof(ULONG);
    *(ULONG *)buffer = cb;
    buffer += sizeof(ULONG);
    memcpy(buffer, pidl, cb);
    return buffer + cb;
}

static unsigned char *pidl_user_unmarshal(ULONG *flags, unsigned char *buffer, ITEMIDLIST **pidl)
{
    MIDL_STUB_MESSAGE *stub = ((USER_MARSHAL_CB *)flags)->pStubMsg;
    ULONG referent, count, cb;
    ITEMIDLIST *copy;

    buffer = PIDL_ALIGN_POINTER(buffer, 3);
    if (buffer + sizeof(ULONG) > stub->BufferEnd)
        RpcRaiseException(RPC_X_BAD_STUB_DATA);
    referent = *(ULONG *)buffer;
    buffer += sizeof(ULONG);
    if (!referent)
    {
        *pidl = NULL;
        return buffer;
    }

    if (buffer + 2 * sizeof(ULONG) > stub->BufferEnd)
        RpcRaiseException(RPC_X_BAD_STUB_DATA);
    count = *(ULONG *)buffer;
    buffer += sizeof(ULONG);
    cb = *(ULONG *)buffer;
    buffer += sizeof(ULONG);
    if (cb != count || cb < sizeof(USHORT) || cb > (ULONG)(stub->BufferEnd - buffer))
        RpcRaiseException(RPC_X_BAD_STUB_DATA);

    copy = CoTaskMemAlloc(cb);
    if (!copy)
        RpcRaiseException(E_OUTOFMEMORY);
    memcpy(copy, buffer, cb);
    *pidl = copy;
    return buffer + cb;
}

#define DEFINE_PIDL_USER_MARSHAL(type) \
ULONG __RPC_USER type##_UserSize(ULONG *flags, ULONG start, type *pidl) \
{ \
    return pidl_user_size(start, (const ITEMIDLIST *)*pidl); \
} \
unsigned char * __RPC_USER type##_UserMarshal(ULONG *flags, unsigned char *buffer, type *pidl) \
{ \
    return pidl_user_marshal(buffer, (const ITEMIDLIST *)*pidl); \
} \
unsigned char * __RPC_USER type##_UserUnmarshal(ULONG *flags, unsigned char *buffer, type *pidl) \
{ \
    return pidl_user_unmarshal(flags, buffer, (ITEMIDLIST **)pidl); \
} \
void __RPC_USER type##_UserFree(ULONG *flags, type *pidl) \
{ \
    CoTaskMemFree((void *)*pidl); \
    *pidl = NULL; \
}

DEFINE_PIDL_USER_MARSHAL(LPITEMIDLIST)
DEFINE_PIDL_USER_MARSHAL(LPCITEMIDLIST)
DEFINE_PIDL_USER_MARSHAL(PIDLIST_ABSOLUTE)
DEFINE_PIDL_USER_MARSHAL(PCIDLIST_ABSOLUTE)
DEFINE_PIDL_USER_MARSHAL(PCUIDLIST_ABSOLUTE)
DEFINE_PIDL_USER_MARSHAL(PIDLIST_RELATIVE)
DEFINE_PIDL_USER_MARSHAL(PCIDLIST_RELATIVE)
DEFINE_PIDL_USER_MARSHAL(PUIDLIST_RELATIVE)
DEFINE_PIDL_USER_MARSHAL(PCUIDLIST_RELATIVE)
DEFINE_PIDL_USER_MARSHAL(PITEMID_CHILD)
DEFINE_PIDL_USER_MARSHAL(PCITEMID_CHILD)
DEFINE_PIDL_USER_MARSHAL(PUITEMID_CHILD)
DEFINE_PIDL_USER_MARSHAL(PCUITEMID_CHILD)
#endif
