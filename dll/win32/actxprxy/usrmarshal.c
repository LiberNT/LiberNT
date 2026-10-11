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

HRESULT STDMETHODCALLTYPE IContextMenu_RemoteQueryContextMenu_Proxy(IContextMenu *This, HMENU hmenu, UINT indexMenu,
    UINT idCmdFirst, UINT idCmdLast, UINT uFlags, UINT *fileMappingHandlesCount, HANDLE **fileMappingHandles,
    ULONG **bitmapIndices);
HRESULT STDMETHODCALLTYPE IContextMenu_RemoteInvokeCommand_Proxy(IContextMenu *This, const CMINVOKECOMMANDINFOEX_REMOTE *pici);
HRESULT STDMETHODCALLTYPE IContextMenu_RemoteGetCommandString_Proxy(IContextMenu *This, UINT_PTR idCmd, UINT uType,
    UINT *pReserved, CHAR *pszName, UINT cchMax);
HRESULT STDMETHODCALLTYPE IContextMenu2_RemoteHandleMenuMsg_Proxy(IContextMenu2 *This, UINT uMsg, WPARAM wParam, LPARAM lParam);
HRESULT STDMETHODCALLTYPE IContextMenu3_RemoteHandleMenuMsg2_Proxy(IContextMenu3 *This, UINT uMsg, WPARAM wParam,
    LPARAM lParam, LRESULT *plResult);
HRESULT STDMETHODCALLTYPE IShellExtInit_RemoteInitialize_Proxy(IShellExtInit *This, PCIDLIST_ABSOLUTE pidlFolder,
    IDataObject *pdtobj, HANDLE hkeyProgID);

#define MENU_BITMAP_MAX_DIMENSION 4096

static BOOL menu_item_has_bitmap(HBITMAP bitmap)
{
    return bitmap && bitmap != HBMMENU_CALLBACK && (ULONG_PTR)bitmap > (ULONG_PTR)HBMMENU_POPUP_MINIMIZE;
}

static HANDLE menu_bitmap_to_section(HBITMAP bitmap)
{
    BITMAPINFOHEADER header;
    HANDLE section;
    BITMAP bm;
    BYTE *view;
    HDC hdc;
    DWORD size;
    int lines = 0;

    if (GetObjectW(bitmap, sizeof(bm), &bm) != sizeof(bm))
        return NULL;
    if (bm.bmWidth <= 0 || bm.bmHeight <= 0 ||
        bm.bmWidth > MENU_BITMAP_MAX_DIMENSION || bm.bmHeight > MENU_BITMAP_MAX_DIMENSION)
        return NULL;

    memset(&header, 0, sizeof(header));
    header.biSize = sizeof(header);
    header.biWidth = bm.bmWidth;
    header.biHeight = bm.bmHeight;
    header.biPlanes = 1;
    header.biBitCount = 32;
    header.biCompression = BI_RGB;
    size = bm.bmWidth * bm.bmHeight * 4;

    section = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, sizeof(header) + size, NULL);
    if (!section)
        return NULL;
    view = MapViewOfFile(section, FILE_MAP_WRITE, 0, 0, 0);
    if (view)
    {
        memcpy(view, &header, sizeof(header));
        hdc = CreateCompatibleDC(NULL);
        if (hdc)
        {
            lines = GetDIBits(hdc, bitmap, 0, bm.bmHeight, view + sizeof(header), (BITMAPINFO *)view, DIB_RGB_COLORS);
            DeleteDC(hdc);
        }
        memcpy(view, &header, sizeof(header));
        UnmapViewOfFile(view);
    }
    if (lines != bm.bmHeight)
    {
        CloseHandle(section);
        return NULL;
    }
    return section;
}

static HBITMAP menu_section_to_bitmap(HANDLE section)
{
    BITMAPINFOHEADER header;
    HBITMAP bitmap;
    void *bits, *view;

    view = MapViewOfFile(section, FILE_MAP_READ, 0, 0, sizeof(header));
    if (!view)
        return NULL;
    memcpy(&header, view, sizeof(header));
    UnmapViewOfFile(view);

    if (header.biSize != sizeof(header) || header.biPlanes != 1 || header.biBitCount != 32 ||
        header.biCompression != BI_RGB || header.biWidth <= 0 || header.biHeight <= 0 ||
        header.biWidth > MENU_BITMAP_MAX_DIMENSION || header.biHeight > MENU_BITMAP_MAX_DIMENSION)
        return NULL;

    bitmap = CreateDIBSection(NULL, (BITMAPINFO *)&header, DIB_RGB_COLORS, &bits, section, sizeof(header));
    return bitmap;
}

static BOOL menu_collect_bitmaps(HMENU menu, ULONG *ordinal, UINT *count, UINT *capacity,
                                 HANDLE **sections, ULONG **indices)
{
    int i, items = GetMenuItemCount(menu);

    for (i = 0; i < items; i++)
    {
        MENUITEMINFOW info;
        HANDLE section;

        memset(&info, 0, sizeof(info));
        info.cbSize = sizeof(info);
        info.fMask = MIIM_BITMAP | MIIM_SUBMENU;
        if (GetMenuItemInfoW(menu, i, TRUE, &info) && menu_item_has_bitmap(info.hbmpItem) &&
            (section = menu_bitmap_to_section(info.hbmpItem)))
        {
            if (*count == *capacity)
            {
                UINT grow = *capacity ? *capacity * 2 : 8;
                HANDLE *new_sections = CoTaskMemRealloc(*sections, grow * sizeof(**sections));
                ULONG *new_indices;

                if (new_sections)
                    *sections = new_sections;
                new_indices = new_sections ? CoTaskMemRealloc(*indices, grow * sizeof(**indices)) : NULL;
                if (new_indices)
                    *indices = new_indices;
                if (!new_sections || !new_indices)
                {
                    CloseHandle(section);
                    return FALSE;
                }
                *capacity = grow;
            }
            (*sections)[*count] = section;
            (*indices)[*count] = *ordinal;
            (*count)++;
        }
        (*ordinal)++;
        if (info.hSubMenu && !menu_collect_bitmaps(info.hSubMenu, ordinal, count, capacity, sections, indices))
            return FALSE;
    }
    return TRUE;
}

static void menu_apply_bitmaps(HMENU menu, ULONG *ordinal, UINT count, const HANDLE *sections, const ULONG *indices)
{
    int i, items = GetMenuItemCount(menu);
    UINT j;

    for (i = 0; i < items; i++)
    {
        MENUITEMINFOW info;

        for (j = 0; j < count; j++)
        {
            if (indices[j] == *ordinal && sections[j])
            {
                memset(&info, 0, sizeof(info));
                info.cbSize = sizeof(info);
                info.fMask = MIIM_BITMAP;
                info.hbmpItem = menu_section_to_bitmap(sections[j]);
                if (info.hbmpItem)
                    SetMenuItemInfoW(menu, i, TRUE, &info);
                break;
            }
        }
        (*ordinal)++;

        memset(&info, 0, sizeof(info));
        info.cbSize = sizeof(info);
        info.fMask = MIIM_SUBMENU;
        if (GetMenuItemInfoW(menu, i, TRUE, &info) && info.hSubMenu)
            menu_apply_bitmaps(info.hSubMenu, ordinal, count, sections, indices);
    }
}

HRESULT CALLBACK IContextMenu_QueryContextMenu_Proxy(IContextMenu *This, HMENU hmenu, UINT indexMenu,
                                                     UINT idCmdFirst, UINT idCmdLast, UINT uFlags)
{
    HANDLE *sections = NULL;
    ULONG *indices = NULL, ordinal = 0;
    UINT count = 0, i;
    HRESULT hr;

    TRACE("(%p)->(%p, %u, %u, %u, %#x)\n", This, hmenu, indexMenu, idCmdFirst, idCmdLast, uFlags);

    hr = IContextMenu_RemoteQueryContextMenu_Proxy(This, hmenu, indexMenu, idCmdFirst, idCmdLast, uFlags,
                                                   &count, &sections, &indices);
    if (SUCCEEDED(hr) && count && sections && indices)
        menu_apply_bitmaps(hmenu, &ordinal, count, sections, indices);
    for (i = 0; sections && i < count; i++)
        if (sections[i])
            CloseHandle(sections[i]);
    CoTaskMemFree(sections);
    CoTaskMemFree(indices);
    return hr;
}

HRESULT __RPC_STUB IContextMenu_QueryContextMenu_Stub(IContextMenu *This, HMENU hmenu, UINT indexMenu,
                                                      UINT idCmdFirst, UINT idCmdLast, UINT uFlags,
                                                      UINT *fileMappingHandlesCount, HANDLE **fileMappingHandles,
                                                      ULONG **bitmapIndices)
{
    ULONG ordinal = 0;
    UINT capacity = 0, i;
    HRESULT hr;

    TRACE("(%p)->(%p, %u, %u, %u, %#x)\n", This, hmenu, indexMenu, idCmdFirst, idCmdLast, uFlags);

    *fileMappingHandlesCount = 0;
    *fileMappingHandles = NULL;
    *bitmapIndices = NULL;
    hr = IContextMenu_QueryContextMenu(This, hmenu, indexMenu, idCmdFirst, idCmdLast, uFlags);
    if (SUCCEEDED(hr) &&
        !menu_collect_bitmaps(hmenu, &ordinal, fileMappingHandlesCount, &capacity, fileMappingHandles, bitmapIndices))
    {
        for (i = 0; i < *fileMappingHandlesCount; i++)
            CloseHandle((*fileMappingHandles)[i]);
        CoTaskMemFree(*fileMappingHandles);
        CoTaskMemFree(*bitmapIndices);
        *fileMappingHandlesCount = 0;
        *fileMappingHandles = NULL;
        *bitmapIndices = NULL;
    }
    return hr;
}

HRESULT CALLBACK IContextMenu_InvokeCommand_Proxy(IContextMenu *This, LPCMINVOKECOMMANDINFO lpici)
{
    const CMINVOKECOMMANDINFOEX *ex = (const CMINVOKECOMMANDINFOEX *)lpici;
    CMINVOKECOMMANDINFOEX_REMOTE remote;

    TRACE("(%p)->(%p)\n", This, lpici);

    if (!lpici)
        return IContextMenu_RemoteInvokeCommand_Proxy(This, NULL);

    memset(&remote, 0, sizeof(remote));
    remote.cbSize = lpici->cbSize;
    remote.fMask = lpici->fMask;
    remote.hwnd = lpici->hwnd;
    if (IS_INTRESOURCE(lpici->lpVerb))
        remote.lpVerbInt = LOWORD(lpici->lpVerb);
    else
        remote.lpVerbString = lpici->lpVerb;
    remote.lpParameters = lpici->lpParameters;
    remote.lpDirectory = lpici->lpDirectory;
    remote.nShow = lpici->nShow;
    remote.dwHotKey = lpici->dwHotKey;
    if (lpici->cbSize >= sizeof(*ex))
    {
        remote.lpTitle = ex->lpTitle;
        if (IS_INTRESOURCE(ex->lpVerbW))
            remote.lpVerbWInt = LOWORD(ex->lpVerbW);
        else
            remote.lpVerbWString = ex->lpVerbW;
        remote.lpParametersW = ex->lpParametersW;
        remote.lpDirectoryW = ex->lpDirectoryW;
        remote.lpTitleW = ex->lpTitleW;
        remote.ptInvoke = ex->ptInvoke;
    }
    return IContextMenu_RemoteInvokeCommand_Proxy(This, &remote);
}

HRESULT __RPC_STUB IContextMenu_InvokeCommand_Stub(IContextMenu *This, const CMINVOKECOMMANDINFOEX_REMOTE *pici)
{
    CMINVOKECOMMANDINFOEX ex;

    TRACE("(%p)->(%p)\n", This, pici);

    memset(&ex, 0, sizeof(ex));
    ex.cbSize = min(pici->cbSize, sizeof(ex));
    ex.fMask = pici->fMask;
    ex.hwnd = pici->hwnd;
    ex.lpVerb = pici->lpVerbString ? pici->lpVerbString : MAKEINTRESOURCEA(pici->lpVerbInt);
    ex.lpParameters = pici->lpParameters;
    ex.lpDirectory = pici->lpDirectory;
    ex.nShow = pici->nShow;
    ex.dwHotKey = pici->dwHotKey;
    ex.lpTitle = pici->lpTitle;
    ex.lpVerbW = pici->lpVerbWString ? pici->lpVerbWString : MAKEINTRESOURCEW(pici->lpVerbWInt);
    ex.lpParametersW = pici->lpParametersW;
    ex.lpDirectoryW = pici->lpDirectoryW;
    ex.lpTitleW = pici->lpTitleW;
    ex.ptInvoke = pici->ptInvoke;
    return IContextMenu_InvokeCommand(This, (CMINVOKECOMMANDINFO *)&ex);
}

HRESULT CALLBACK IContextMenu_GetCommandString_Proxy(IContextMenu *This, UINT_PTR idCmd, UINT uType,
                                                     UINT *pwReserved, LPSTR pszName, UINT cchMax)
{
    TRACE("(%p)->(%Iu, %#x, %p, %p, %u)\n", This, idCmd, uType, pwReserved, pszName, cchMax);

    return IContextMenu_RemoteGetCommandString_Proxy(This, idCmd, uType, pwReserved, pszName, cchMax);
}

HRESULT __RPC_STUB IContextMenu_GetCommandString_Stub(IContextMenu *This, UINT_PTR idCmd, UINT uType,
                                                      UINT *pReserved, CHAR *pszName, UINT cchMax)
{
    TRACE("(%p)->(%Iu, %#x, %p, %p, %u)\n", This, idCmd, uType, pReserved, pszName, cchMax);

    return IContextMenu_GetCommandString(This, idCmd, uType, pReserved, pszName, cchMax);
}

HRESULT CALLBACK IContextMenu2_HandleMenuMsg_Proxy(IContextMenu2 *This, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    TRACE("(%p)->(%#x, %#Ix, %#Ix)\n", This, uMsg, wParam, lParam);

    return IContextMenu2_RemoteHandleMenuMsg_Proxy(This, uMsg, wParam, lParam);
}

HRESULT __RPC_STUB IContextMenu2_HandleMenuMsg_Stub(IContextMenu2 *This, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    TRACE("(%p)->(%#x, %#Ix, %#Ix)\n", This, uMsg, wParam, lParam);

    return IContextMenu2_HandleMenuMsg(This, uMsg, wParam, lParam);
}

HRESULT CALLBACK IContextMenu3_HandleMenuMsg2_Proxy(IContextMenu3 *This, UINT uMsg, WPARAM wParam, LPARAM lParam,
                                                    LRESULT *plResult)
{
    LRESULT result = 0;

    TRACE("(%p)->(%#x, %#Ix, %#Ix, %p)\n", This, uMsg, wParam, lParam, plResult);

    return IContextMenu3_RemoteHandleMenuMsg2_Proxy(This, uMsg, wParam, lParam, plResult ? plResult : &result);
}

HRESULT __RPC_STUB IContextMenu3_HandleMenuMsg2_Stub(IContextMenu3 *This, UINT uMsg, WPARAM wParam, LPARAM lParam,
                                                     LRESULT *plResult)
{
    TRACE("(%p)->(%#x, %#Ix, %#Ix, %p)\n", This, uMsg, wParam, lParam, plResult);

    return IContextMenu3_HandleMenuMsg2(This, uMsg, wParam, lParam, plResult);
}

HRESULT CALLBACK IShellExtInit_Initialize_Proxy(IShellExtInit *This, PCIDLIST_ABSOLUTE pidlFolder,
                                                IDataObject *pdtobj, HKEY hkeyProgID)
{
    TRACE("(%p)->(%p, %p, %p)\n", This, pidlFolder, pdtobj, hkeyProgID);

    return IShellExtInit_RemoteInitialize_Proxy(This, pidlFolder, pdtobj, (HANDLE)hkeyProgID);
}

HRESULT __RPC_STUB IShellExtInit_Initialize_Stub(IShellExtInit *This, PCIDLIST_ABSOLUTE pidlFolder,
                                                 IDataObject *pdtobj, HANDLE hkeyProgID)
{
    TRACE("(%p)->(%p, %p, %p)\n", This, pidlFolder, pdtobj, hkeyProgID);

    return IShellExtInit_Initialize(This, pidlFolder, pdtobj, (HKEY)hkeyProgID);
}
#endif
