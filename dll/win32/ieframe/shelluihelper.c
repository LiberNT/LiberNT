/*
 * Copyright 2012 Jacek Caban for CodeWeavers
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

#include "ieframe.h"
#ifdef __REACTOS__
#include "objsafe.h"
#endif

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(ieframe);

struct ShellUIHelper {
    IShellUIHelper2 IShellUIHelper2_iface;
#ifdef __REACTOS__
    IDispatchEx IDispatchEx_iface;
    IObjectWithSite IObjectWithSite_iface;
    IObjectSafety IObjectSafety_iface;
    IUnknown *site;
    DWORD safety_options;
#endif
    LONG ref;
};

static inline ShellUIHelper *impl_from_IShellUIHelper2(IShellUIHelper2 *iface)
{
    return CONTAINING_RECORD(iface, ShellUIHelper, IShellUIHelper2_iface);
}

static HRESULT WINAPI ShellUIHelper2_QueryInterface(IShellUIHelper2 *iface, REFIID riid, void **ppv)
{
    ShellUIHelper *This = impl_from_IShellUIHelper2(iface);

    if(IsEqualGUID(&IID_IUnknown, riid)) {
        TRACE("(%p)->(IID_IUnknown %p)\n", This, ppv);
        *ppv = &This->IShellUIHelper2_iface;
    }else if(IsEqualGUID(&IID_IDispatch, riid)) {
        TRACE("(%p)->(IID_IDispatch %p)\n", This, ppv);
        *ppv = &This->IShellUIHelper2_iface;
    }else if(IsEqualGUID(&IID_IShellUIHelper, riid)) {
        TRACE("(%p)->(IID_IShellUIHelper %p)\n", This, ppv);
        *ppv = &This->IShellUIHelper2_iface;
    }else if(IsEqualGUID(&IID_IShellUIHelper2, riid)) {
        TRACE("(%p)->(IID_IShellUIHelper2 %p)\n", This, ppv);
        *ppv = &This->IShellUIHelper2_iface;
#ifdef __REACTOS__
    }else if(IsEqualGUID(&IID_IDispatchEx, riid)) {
        *ppv = &This->IDispatchEx_iface;
    }else if(IsEqualGUID(&IID_IObjectWithSite, riid)) {
        *ppv = &This->IObjectWithSite_iface;
    }else if(IsEqualGUID(&IID_IObjectSafety, riid)) {
        *ppv = &This->IObjectSafety_iface;
#endif
    }else {
        WARN("(%p)->(%s %p)\n", This, debugstr_guid(riid), ppv);
        *ppv = NULL;
        return E_NOINTERFACE;
    }

    IUnknown_AddRef((IUnknown*)*ppv);
    return S_OK;
}

static ULONG WINAPI ShellUIHelper2_AddRef(IShellUIHelper2 *iface)
{
    ShellUIHelper *This = impl_from_IShellUIHelper2(iface);
    LONG ref = InterlockedIncrement(&This->ref);

    TRACE("(%p) ref=%ld\n", This, ref);

    return ref;
}

static ULONG WINAPI ShellUIHelper2_Release(IShellUIHelper2 *iface)
{
    ShellUIHelper *This = impl_from_IShellUIHelper2(iface);
    LONG ref = InterlockedDecrement(&This->ref);

    TRACE("(%p) ref=%ld\n", This, ref);

    if(!ref) {
#ifdef __REACTOS__
        if(This->site)
            IUnknown_Release(This->site);
#endif
        free(This);
    }

    return ref;
}

static HRESULT WINAPI ShellUIHelper2_GetTypeInfoCount(IShellUIHelper2 *iface, UINT *pctinfo)
{
    ShellUIHelper *This = impl_from_IShellUIHelper2(iface);

    TRACE("(%p)->(%p)\n", This, pctinfo);

    *pctinfo = 1;
    return S_OK;
}

static HRESULT WINAPI ShellUIHelper2_GetTypeInfo(IShellUIHelper2 *iface, UINT iTInfo, LCID lcid, LPTYPEINFO *ppTInfo)
{
#ifdef __REACTOS__
    HRESULT hres;

    TRACE("(%p)->(%d %ld %p)\n", iface, iTInfo, lcid, ppTInfo);

    if(iTInfo)
        return DISP_E_BADINDEX;
    hres = get_typeinfo(IShellUIHelper2_tid, ppTInfo);
    if(SUCCEEDED(hres))
        ITypeInfo_AddRef(*ppTInfo);
    return hres;
#else
    ShellUIHelper *This = impl_from_IShellUIHelper2(iface);
    FIXME("(%p)->(%d %ld %p)\n", This, iTInfo, lcid, ppTInfo);
    return E_NOTIMPL;
#endif
}

static HRESULT WINAPI ShellUIHelper2_GetIDsOfNames(IShellUIHelper2 *iface, REFIID riid, LPOLESTR *rgszNames, UINT cNames,
        LCID lcid, DISPID *rgDispId)
{
#ifdef __REACTOS__
    ITypeInfo *typeinfo;
    HRESULT hres;

    TRACE("(%p)->(%s %p %d %ld %p)\n", iface, debugstr_guid(riid), rgszNames, cNames, lcid, rgDispId);

    hres = get_typeinfo(IShellUIHelper2_tid, &typeinfo);
    if(FAILED(hres))
        return hres;
    return ITypeInfo_GetIDsOfNames(typeinfo, rgszNames, cNames, rgDispId);
#else
    ShellUIHelper *This = impl_from_IShellUIHelper2(iface);
    unsigned i;

    FIXME("(%p)->(%s %p %d %ld %p)\n", This, debugstr_guid(riid), rgszNames, cNames, lcid, rgDispId);
    for(i = 0; i < cNames; i++)
        FIXME("%s\n", debugstr_w(rgszNames[i]));

    return DISP_E_UNKNOWNNAME;
#endif
}

static HRESULT WINAPI ShellUIHelper2_Invoke(IShellUIHelper2 *iface, DISPID dispIdMember,
        REFIID riid, LCID lcid, WORD wFlags, DISPPARAMS *pDispParams, VARIANT *pVarResult,
        EXCEPINFO *pExepInfo, UINT *puArgErr)
{
#ifdef __REACTOS__
    ITypeInfo *typeinfo;
    HRESULT hres;

    TRACE("(%p)->(%ld %s %ld %08x %p %p %p %p)\n", iface, dispIdMember, debugstr_guid(riid),
          lcid, wFlags, pDispParams, pVarResult, pExepInfo, puArgErr);

    hres = get_typeinfo(IShellUIHelper2_tid, &typeinfo);
    if(FAILED(hres))
        return hres;
    return ITypeInfo_Invoke(typeinfo, iface, dispIdMember, wFlags, pDispParams, pVarResult, pExepInfo, puArgErr);
#else
    ShellUIHelper *This = impl_from_IShellUIHelper2(iface);
    FIXME("(%p)->(%ld %s %ld %08x %p %p %p %p)\n", This, dispIdMember, debugstr_guid(riid),
          lcid, wFlags, pDispParams, pVarResult, pExepInfo, puArgErr);
    return E_NOTIMPL;
#endif
}

static HRESULT WINAPI ShellUIHelper2_ResetFirstBootMode(IShellUIHelper2 *iface)
{
    ShellUIHelper *This = impl_from_IShellUIHelper2(iface);
    FIXME("(%p)->()\n", This);
    return E_NOTIMPL;
}

static HRESULT WINAPI ShellUIHelper2_ResetSafeMode(IShellUIHelper2 *iface)
{
    ShellUIHelper *This = impl_from_IShellUIHelper2(iface);
    FIXME("(%p)->()\n", This);
    return E_NOTIMPL;
}

static HRESULT WINAPI ShellUIHelper2_RefreshOfflineDesktop(IShellUIHelper2 *iface)
{
    ShellUIHelper *This = impl_from_IShellUIHelper2(iface);
    FIXME("(%p)->()\n", This);
    return E_NOTIMPL;
}

static HRESULT WINAPI ShellUIHelper2_AddFavourite(IShellUIHelper2 *iface, BSTR URL, VARIANT *Title)
{
    ShellUIHelper *This = impl_from_IShellUIHelper2(iface);
    FIXME("(%p)->(%s %s)\n", This, debugstr_w(URL), debugstr_variant(Title));
    return E_NOTIMPL;
}

static HRESULT WINAPI ShellUIHelper2_AddChannel(IShellUIHelper2 *iface, BSTR URL)
{
    ShellUIHelper *This = impl_from_IShellUIHelper2(iface);
    FIXME("(%p)->(%s)\n", This, debugstr_w(URL));
    return E_NOTIMPL;
}

static HRESULT WINAPI ShellUIHelper2_AddDesktopComponent(IShellUIHelper2 *iface, BSTR URL, BSTR Type,
        VARIANT *Left, VARIANT *Top, VARIANT *Width, VARIANT *Height)
{
    ShellUIHelper *This = impl_from_IShellUIHelper2(iface);
    FIXME("(%p)->(%s %s %s %s %s %s)\n", This, debugstr_w(URL), debugstr_w(Type), debugstr_variant(Left),
          debugstr_variant(Top), debugstr_variant(Width), debugstr_variant(Height));
    return E_NOTIMPL;
}

static HRESULT WINAPI ShellUIHelper2_IsSubscribed(IShellUIHelper2 *iface, BSTR URL, VARIANT_BOOL *pBool)
{
    ShellUIHelper *This = impl_from_IShellUIHelper2(iface);
    FIXME("(%p)->(%s %p)\n", This, debugstr_w(URL), pBool);
    return E_NOTIMPL;
}

static HRESULT WINAPI ShellUIHelper2_NavigateAndFind(IShellUIHelper2 *iface, BSTR URL, BSTR strQuery, VARIANT *varTargetFrame)
{
    ShellUIHelper *This = impl_from_IShellUIHelper2(iface);
    FIXME("(%p)->(%s %s %s)\n", This, debugstr_w(URL), debugstr_w(strQuery), debugstr_variant(varTargetFrame));
    return E_NOTIMPL;
}

static HRESULT WINAPI ShellUIHelper2_ImportExportFavourites(IShellUIHelper2 *iface, VARIANT_BOOL fImport, BSTR strImpExpPath)
{
    ShellUIHelper *This = impl_from_IShellUIHelper2(iface);
    FIXME("(%p)->(%x %s)\n", This, fImport, debugstr_w(strImpExpPath));
    return E_NOTIMPL;
}

static HRESULT WINAPI ShellUIHelper2_AutoCompleteSaveForm(IShellUIHelper2 *iface, VARIANT *Form)
{
    ShellUIHelper *This = impl_from_IShellUIHelper2(iface);
    FIXME("(%p)->(%s)\n", This, debugstr_variant(Form));
    return E_NOTIMPL;
}

static HRESULT WINAPI ShellUIHelper2_AutoScan(IShellUIHelper2 *iface, BSTR strSearch, BSTR strFailureUrl, VARIANT *pvarTargetFrame)
{
    ShellUIHelper *This = impl_from_IShellUIHelper2(iface);
    FIXME("(%p)->(%s %s %s)\n", This, debugstr_w(strSearch), debugstr_w(strFailureUrl), debugstr_variant(pvarTargetFrame));
    return E_NOTIMPL;
}

static HRESULT WINAPI ShellUIHelper2_AutoCompleteAttach(IShellUIHelper2 *iface, VARIANT *Reserved)
{
    ShellUIHelper *This = impl_from_IShellUIHelper2(iface);
    FIXME("(%p)->(%s)\n", This, debugstr_variant(Reserved));
    return E_NOTIMPL;
}

static HRESULT WINAPI ShellUIHelper2_ShowBrowserUI(IShellUIHelper2 *iface, BSTR bstrName, VARIANT *pvarIn, VARIANT *pvarOut)
{
    ShellUIHelper *This = impl_from_IShellUIHelper2(iface);
    FIXME("(%p)->(%s %s %p)\n", This, debugstr_w(bstrName), debugstr_variant(pvarIn), pvarOut);
    return E_NOTIMPL;
}

static HRESULT WINAPI ShellUIHelper2_AddSearchProvider(IShellUIHelper2 *iface, BSTR URL)
{
    ShellUIHelper *This = impl_from_IShellUIHelper2(iface);
    FIXME("(%p)->(%s)\n", This, debugstr_w(URL));
    return E_NOTIMPL;
}

static HRESULT WINAPI ShellUIHelper2_RunOnceShown(IShellUIHelper2 *iface)
{
    ShellUIHelper *This = impl_from_IShellUIHelper2(iface);
    FIXME("(%p)->()\n", This);
    return E_NOTIMPL;
}

static HRESULT WINAPI ShellUIHelper2_SkipRunOnce(IShellUIHelper2 *iface)
{
    ShellUIHelper *This = impl_from_IShellUIHelper2(iface);
    FIXME("(%p)->()\n", This);
    return E_NOTIMPL;
}

static HRESULT WINAPI ShellUIHelper2_CustomizeSettings(IShellUIHelper2 *iface, VARIANT_BOOL fSQM,
        VARIANT_BOOL fPhishing, BSTR bstrLocale)
{
    ShellUIHelper *This = impl_from_IShellUIHelper2(iface);
    FIXME("(%p)->(%x %x %s)\n", This, fSQM, fPhishing, debugstr_w(bstrLocale));
    return E_NOTIMPL;
}

static HRESULT WINAPI ShellUIHelper2_SqmEnabled(IShellUIHelper2 *iface, VARIANT_BOOL *pfEnabled)
{
    ShellUIHelper *This = impl_from_IShellUIHelper2(iface);
    FIXME("(%p)->(%p)\n", This, pfEnabled);
    return E_NOTIMPL;
}

static HRESULT WINAPI ShellUIHelper2_PhishingEnabled(IShellUIHelper2 *iface, VARIANT_BOOL *pfEnabled)
{
    ShellUIHelper *This = impl_from_IShellUIHelper2(iface);
    FIXME("(%p)->(%p)\n", This, pfEnabled);
    return E_NOTIMPL;
}

static HRESULT WINAPI ShellUIHelper2_BrandImageUri(IShellUIHelper2 *iface, BSTR *pbstrUri)
{
    ShellUIHelper *This = impl_from_IShellUIHelper2(iface);
    FIXME("(%p)->(%p)\n", This, pbstrUri);
    return E_NOTIMPL;
}

static HRESULT WINAPI ShellUIHelper2_SkipTabsWelcome(IShellUIHelper2 *iface)
{
    ShellUIHelper *This = impl_from_IShellUIHelper2(iface);
    FIXME("(%p)->()\n", This);
    return E_NOTIMPL;
}

static HRESULT WINAPI ShellUIHelper2_DiagnoseConnection(IShellUIHelper2 *iface)
{
    ShellUIHelper *This = impl_from_IShellUIHelper2(iface);
    FIXME("(%p)->()\n", This);
    return E_NOTIMPL;
}

static HRESULT WINAPI ShellUIHelper2_CustomizeClearType(IShellUIHelper2 *iface, VARIANT_BOOL fSet)
{
    ShellUIHelper *This = impl_from_IShellUIHelper2(iface);
    FIXME("(%p)->(%x)\n", This, fSet);
    return E_NOTIMPL;
}

static HRESULT WINAPI ShellUIHelper2_IsSearchProviderInstalled(IShellUIHelper2 *iface, BSTR URL, DWORD *pdwResult)
{
    ShellUIHelper *This = impl_from_IShellUIHelper2(iface);
    FIXME("(%p)->(%s %p)\n", This, debugstr_w(URL), pdwResult);
    return E_NOTIMPL;
}

static HRESULT WINAPI ShellUIHelper2_IsSearchMigrated(IShellUIHelper2 *iface, VARIANT_BOOL *pfMigrated)
{
    ShellUIHelper *This = impl_from_IShellUIHelper2(iface);
    FIXME("(%p)->(%p)\n", This, pfMigrated);
    return E_NOTIMPL;
}

static HRESULT WINAPI ShellUIHelper2_DefaultSearchProvider(IShellUIHelper2 *iface, BSTR *pbstrName)
{
    ShellUIHelper *This = impl_from_IShellUIHelper2(iface);
    FIXME("(%p)->(%p)\n", This, pbstrName);
    return E_NOTIMPL;
}

static HRESULT WINAPI ShellUIHelper2_RunOnceRequiredSettingsComplete(IShellUIHelper2 *iface, VARIANT_BOOL fComplete)
{
    ShellUIHelper *This = impl_from_IShellUIHelper2(iface);
    FIXME("(%p)->(%x)\n", This, fComplete);
    return E_NOTIMPL;
}

static HRESULT WINAPI ShellUIHelper2_RunOnceHasShown(IShellUIHelper2 *iface, VARIANT_BOOL *pfShown)
{
    ShellUIHelper *This = impl_from_IShellUIHelper2(iface);
    FIXME("(%p)->(%p)\n", This, pfShown);
    return E_NOTIMPL;
}

static HRESULT WINAPI ShellUIHelper2_SearchGuideUrl(IShellUIHelper2 *iface, BSTR *pbstrUrl)
{
    ShellUIHelper *This = impl_from_IShellUIHelper2(iface);
    FIXME("(%p)->(%p)\n", This, pbstrUrl);
    return E_NOTIMPL;
}

static const IShellUIHelper2Vtbl ShellUIHelper2Vtbl = {
    ShellUIHelper2_QueryInterface,
    ShellUIHelper2_AddRef,
    ShellUIHelper2_Release,
    ShellUIHelper2_GetTypeInfoCount,
    ShellUIHelper2_GetTypeInfo,
    ShellUIHelper2_GetIDsOfNames,
    ShellUIHelper2_Invoke,
    ShellUIHelper2_ResetFirstBootMode,
    ShellUIHelper2_ResetSafeMode,
    ShellUIHelper2_RefreshOfflineDesktop,
    ShellUIHelper2_AddFavourite,
    ShellUIHelper2_AddChannel,
    ShellUIHelper2_AddDesktopComponent,
    ShellUIHelper2_IsSubscribed,
    ShellUIHelper2_NavigateAndFind,
    ShellUIHelper2_ImportExportFavourites,
    ShellUIHelper2_AutoCompleteSaveForm,
    ShellUIHelper2_AutoScan,
    ShellUIHelper2_AutoCompleteAttach,
    ShellUIHelper2_ShowBrowserUI,
    ShellUIHelper2_AddSearchProvider,
    ShellUIHelper2_RunOnceShown,
    ShellUIHelper2_SkipRunOnce,
    ShellUIHelper2_CustomizeSettings,
    ShellUIHelper2_SqmEnabled,
    ShellUIHelper2_PhishingEnabled,
    ShellUIHelper2_BrandImageUri,
    ShellUIHelper2_SkipTabsWelcome,
    ShellUIHelper2_DiagnoseConnection,
    ShellUIHelper2_CustomizeClearType,
    ShellUIHelper2_IsSearchProviderInstalled,
    ShellUIHelper2_IsSearchMigrated,
    ShellUIHelper2_DefaultSearchProvider,
    ShellUIHelper2_RunOnceRequiredSettingsComplete,
    ShellUIHelper2_RunOnceHasShown,
    ShellUIHelper2_SearchGuideUrl
};

#ifdef __REACTOS__
static inline ShellUIHelper *impl_from_IDispatchEx(IDispatchEx *iface)
{
    return CONTAINING_RECORD(iface, ShellUIHelper, IDispatchEx_iface);
}

static HRESULT WINAPI ShellUIHelperDispEx_QueryInterface(IDispatchEx *iface, REFIID riid, void **ppv)
{
    return IShellUIHelper2_QueryInterface(&impl_from_IDispatchEx(iface)->IShellUIHelper2_iface, riid, ppv);
}

static ULONG WINAPI ShellUIHelperDispEx_AddRef(IDispatchEx *iface)
{
    return IShellUIHelper2_AddRef(&impl_from_IDispatchEx(iface)->IShellUIHelper2_iface);
}

static ULONG WINAPI ShellUIHelperDispEx_Release(IDispatchEx *iface)
{
    return IShellUIHelper2_Release(&impl_from_IDispatchEx(iface)->IShellUIHelper2_iface);
}

static HRESULT WINAPI ShellUIHelperDispEx_GetTypeInfoCount(IDispatchEx *iface, UINT *pctinfo)
{
    return IShellUIHelper2_GetTypeInfoCount(&impl_from_IDispatchEx(iface)->IShellUIHelper2_iface, pctinfo);
}

static HRESULT WINAPI ShellUIHelperDispEx_GetTypeInfo(IDispatchEx *iface, UINT iTInfo, LCID lcid, ITypeInfo **ppTInfo)
{
    return IShellUIHelper2_GetTypeInfo(&impl_from_IDispatchEx(iface)->IShellUIHelper2_iface, iTInfo, lcid, ppTInfo);
}

static HRESULT WINAPI ShellUIHelperDispEx_GetIDsOfNames(IDispatchEx *iface, REFIID riid, LPOLESTR *rgszNames,
        UINT cNames, LCID lcid, DISPID *rgDispId)
{
    return IShellUIHelper2_GetIDsOfNames(&impl_from_IDispatchEx(iface)->IShellUIHelper2_iface, riid, rgszNames,
            cNames, lcid, rgDispId);
}

static HRESULT WINAPI ShellUIHelperDispEx_Invoke(IDispatchEx *iface, DISPID dispIdMember, REFIID riid, LCID lcid,
        WORD wFlags, DISPPARAMS *pDispParams, VARIANT *pVarResult, EXCEPINFO *pExcepInfo, UINT *puArgErr)
{
    return IShellUIHelper2_Invoke(&impl_from_IDispatchEx(iface)->IShellUIHelper2_iface, dispIdMember, riid, lcid,
            wFlags, pDispParams, pVarResult, pExcepInfo, puArgErr);
}

static HRESULT WINAPI ShellUIHelperDispEx_GetDispID(IDispatchEx *iface, BSTR bstrName, DWORD grfdex, DISPID *pid)
{
    ITypeInfo *typeinfo;
    HRESULT hres;

    TRACE("(%p)->(%s %lx %p)\n", iface, debugstr_w(bstrName), grfdex, pid);

    hres = get_typeinfo(IShellUIHelper2_tid, &typeinfo);
    if(FAILED(hres))
        return hres;
    return ITypeInfo_GetIDsOfNames(typeinfo, &bstrName, 1, pid);
}

static HRESULT WINAPI ShellUIHelperDispEx_InvokeEx(IDispatchEx *iface, DISPID id, LCID lcid, WORD wFlags,
        DISPPARAMS *pdp, VARIANT *pvarRes, EXCEPINFO *pei, IServiceProvider *pspCaller)
{
    TRACE("(%p)->(%ld %ld %x %p %p %p %p)\n", iface, id, lcid, wFlags, pdp, pvarRes, pei, pspCaller);

    return IShellUIHelper2_Invoke(&impl_from_IDispatchEx(iface)->IShellUIHelper2_iface, id, &IID_NULL, lcid,
            wFlags, pdp, pvarRes, pei, NULL);
}

static HRESULT WINAPI ShellUIHelperDispEx_DeleteMemberByName(IDispatchEx *iface, BSTR bstrName, DWORD grfdex)
{
    return S_FALSE;
}

static HRESULT WINAPI ShellUIHelperDispEx_DeleteMemberByDispID(IDispatchEx *iface, DISPID id)
{
    return S_FALSE;
}

static HRESULT WINAPI ShellUIHelperDispEx_GetMemberProperties(IDispatchEx *iface, DISPID id, DWORD grfdexFetch,
        DWORD *pgrfdex)
{
    return E_NOTIMPL;
}

static HRESULT WINAPI ShellUIHelperDispEx_GetMemberName(IDispatchEx *iface, DISPID id, BSTR *pbstrName)
{
    ITypeInfo *typeinfo;
    UINT count;
    HRESULT hres;

    TRACE("(%p)->(%ld %p)\n", iface, id, pbstrName);

    hres = get_typeinfo(IShellUIHelper2_tid, &typeinfo);
    if(FAILED(hres))
        return hres;
    hres = ITypeInfo_GetNames(typeinfo, id, pbstrName, 1, &count);
    if(SUCCEEDED(hres) && !count)
        hres = DISP_E_UNKNOWNNAME;
    return hres;
}

static HRESULT WINAPI ShellUIHelperDispEx_GetNextDispID(IDispatchEx *iface, DWORD grfdex, DISPID id, DISPID *pid)
{
    return E_NOTIMPL;
}

static HRESULT WINAPI ShellUIHelperDispEx_GetNameSpaceParent(IDispatchEx *iface, IUnknown **ppunk)
{
    if(!ppunk)
        return E_POINTER;
    *ppunk = NULL;
    return E_NOTIMPL;
}

static const IDispatchExVtbl ShellUIHelperDispExVtbl = {
    ShellUIHelperDispEx_QueryInterface,
    ShellUIHelperDispEx_AddRef,
    ShellUIHelperDispEx_Release,
    ShellUIHelperDispEx_GetTypeInfoCount,
    ShellUIHelperDispEx_GetTypeInfo,
    ShellUIHelperDispEx_GetIDsOfNames,
    ShellUIHelperDispEx_Invoke,
    ShellUIHelperDispEx_GetDispID,
    ShellUIHelperDispEx_InvokeEx,
    ShellUIHelperDispEx_DeleteMemberByName,
    ShellUIHelperDispEx_DeleteMemberByDispID,
    ShellUIHelperDispEx_GetMemberProperties,
    ShellUIHelperDispEx_GetMemberName,
    ShellUIHelperDispEx_GetNextDispID,
    ShellUIHelperDispEx_GetNameSpaceParent
};

static inline ShellUIHelper *impl_from_IObjectWithSite(IObjectWithSite *iface)
{
    return CONTAINING_RECORD(iface, ShellUIHelper, IObjectWithSite_iface);
}

static HRESULT WINAPI ShellUIHelperSite_QueryInterface(IObjectWithSite *iface, REFIID riid, void **ppv)
{
    return IShellUIHelper2_QueryInterface(&impl_from_IObjectWithSite(iface)->IShellUIHelper2_iface, riid, ppv);
}

static ULONG WINAPI ShellUIHelperSite_AddRef(IObjectWithSite *iface)
{
    return IShellUIHelper2_AddRef(&impl_from_IObjectWithSite(iface)->IShellUIHelper2_iface);
}

static ULONG WINAPI ShellUIHelperSite_Release(IObjectWithSite *iface)
{
    return IShellUIHelper2_Release(&impl_from_IObjectWithSite(iface)->IShellUIHelper2_iface);
}

static HRESULT WINAPI ShellUIHelperSite_SetSite(IObjectWithSite *iface, IUnknown *site)
{
    ShellUIHelper *This = impl_from_IObjectWithSite(iface);

    TRACE("(%p)->(%p)\n", This, site);

    if(site)
        IUnknown_AddRef(site);
    if(This->site)
        IUnknown_Release(This->site);
    This->site = site;
    return S_OK;
}

static HRESULT WINAPI ShellUIHelperSite_GetSite(IObjectWithSite *iface, REFIID riid, void **ppv)
{
    ShellUIHelper *This = impl_from_IObjectWithSite(iface);

    TRACE("(%p)->(%s %p)\n", This, debugstr_guid(riid), ppv);

    if(!ppv)
        return E_POINTER;
    *ppv = NULL;
    if(!This->site)
        return E_FAIL;
    return IUnknown_QueryInterface(This->site, riid, ppv);
}

static const IObjectWithSiteVtbl ShellUIHelperSiteVtbl = {
    ShellUIHelperSite_QueryInterface,
    ShellUIHelperSite_AddRef,
    ShellUIHelperSite_Release,
    ShellUIHelperSite_SetSite,
    ShellUIHelperSite_GetSite
};

static inline ShellUIHelper *impl_from_IObjectSafety(IObjectSafety *iface)
{
    return CONTAINING_RECORD(iface, ShellUIHelper, IObjectSafety_iface);
}

static HRESULT WINAPI ShellUIHelperSafety_QueryInterface(IObjectSafety *iface, REFIID riid, void **ppv)
{
    return IShellUIHelper2_QueryInterface(&impl_from_IObjectSafety(iface)->IShellUIHelper2_iface, riid, ppv);
}

static ULONG WINAPI ShellUIHelperSafety_AddRef(IObjectSafety *iface)
{
    return IShellUIHelper2_AddRef(&impl_from_IObjectSafety(iface)->IShellUIHelper2_iface);
}

static ULONG WINAPI ShellUIHelperSafety_Release(IObjectSafety *iface)
{
    return IShellUIHelper2_Release(&impl_from_IObjectSafety(iface)->IShellUIHelper2_iface);
}

#define SHELLUIHELPER_SAFETY_OPTIONS (INTERFACESAFE_FOR_UNTRUSTED_CALLER | INTERFACESAFE_FOR_UNTRUSTED_DATA)

static HRESULT WINAPI ShellUIHelperSafety_GetInterfaceSafetyOptions(IObjectSafety *iface, REFIID riid,
        DWORD *pdwSupportedOptions, DWORD *pdwEnabledOptions)
{
    ShellUIHelper *This = impl_from_IObjectSafety(iface);
    IUnknown *unk;

    TRACE("(%p)->(%s %p %p)\n", This, debugstr_guid(riid), pdwSupportedOptions, pdwEnabledOptions);

    if(!pdwSupportedOptions || !pdwEnabledOptions)
        return E_POINTER;
    if(FAILED(IShellUIHelper2_QueryInterface(&This->IShellUIHelper2_iface, riid, (void **)&unk)))
        return E_NOINTERFACE;
    IUnknown_Release(unk);

    *pdwSupportedOptions = SHELLUIHELPER_SAFETY_OPTIONS;
    *pdwEnabledOptions = This->safety_options;
    return S_OK;
}

static HRESULT WINAPI ShellUIHelperSafety_SetInterfaceSafetyOptions(IObjectSafety *iface, REFIID riid,
        DWORD dwOptionSetMask, DWORD dwEnabledOptions)
{
    ShellUIHelper *This = impl_from_IObjectSafety(iface);
    IUnknown *unk;

    TRACE("(%p)->(%s %lx %lx)\n", This, debugstr_guid(riid), dwOptionSetMask, dwEnabledOptions);

    if(FAILED(IShellUIHelper2_QueryInterface(&This->IShellUIHelper2_iface, riid, (void **)&unk)))
        return E_NOINTERFACE;
    IUnknown_Release(unk);
    if(dwOptionSetMask & ~SHELLUIHELPER_SAFETY_OPTIONS)
        return E_FAIL;

    This->safety_options = (This->safety_options & ~dwOptionSetMask) | (dwEnabledOptions & dwOptionSetMask);
    return S_OK;
}

static const IObjectSafetyVtbl ShellUIHelperSafetyVtbl = {
    ShellUIHelperSafety_QueryInterface,
    ShellUIHelperSafety_AddRef,
    ShellUIHelperSafety_Release,
    ShellUIHelperSafety_GetInterfaceSafetyOptions,
    ShellUIHelperSafety_SetInterfaceSafetyOptions
};

HRESULT WINAPI ShellUIHelper_Create(IClassFactory *iface, IUnknown *outer, REFIID riid, void **ppv)
{
    IShellUIHelper2 *helper;
    HRESULT hres;

    TRACE("(%p %s %p)\n", outer, debugstr_guid(riid), ppv);

    *ppv = NULL;
    if(outer)
        return CLASS_E_NOAGGREGATION;

    hres = create_shell_ui_helper(&helper);
    if(FAILED(hres))
        return hres;

    hres = IShellUIHelper2_QueryInterface(helper, riid, ppv);
    IShellUIHelper2_Release(helper);
    return hres;
}
#endif

HRESULT create_shell_ui_helper(IShellUIHelper2 **_ret)
{
    ShellUIHelper *ret;

    ret = malloc(sizeof(*ret));
    if(!ret)
        return E_OUTOFMEMORY;

    ret->IShellUIHelper2_iface.lpVtbl = &ShellUIHelper2Vtbl;
#ifdef __REACTOS__
    ret->IDispatchEx_iface.lpVtbl = &ShellUIHelperDispExVtbl;
    ret->IObjectWithSite_iface.lpVtbl = &ShellUIHelperSiteVtbl;
    ret->IObjectSafety_iface.lpVtbl = &ShellUIHelperSafetyVtbl;
    ret->site = NULL;
    ret->safety_options = 0;
#endif
    ret->ref = 1;

    *_ret = &ret->IShellUIHelper2_iface;
    return S_OK;
}
