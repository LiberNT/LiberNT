/*
 * Copyright (C) 2001 Ove Kaaven
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

#ifndef __RPCPROXY_H_VERSION__
#define __RPCPROXY_H_VERSION__ (477)

#endif

#ifndef __WINE_RPCPROXY_H
#define __WINE_RPCPROXY_H

#define __midl_proxy

#include <basetsd.h>
#ifndef GUID_DEFINED
#include <guiddef.h>
#endif
#include <rpc.h>
#include <rpcndr.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tagCInterfaceStubVtbl *PCInterfaceStubVtblList;
typedef struct tagCInterfaceProxyVtbl *PCInterfaceProxyVtblList;
typedef const char *PCInterfaceName;
typedef int __stdcall IIDLookupRtn( const IID *pIID, int *pIndex );
typedef IIDLookupRtn *PIIDLookup;

typedef struct tagProxyFileInfo
{
  const PCInterfaceProxyVtblList *pProxyVtblList;
  const PCInterfaceStubVtblList *pStubVtblList;
  const PCInterfaceName *pNamesArray;
  const IID **pDelegatedIIDs;
  const PIIDLookup pIIDLookupRtn;
  unsigned short TableSize;
  unsigned short TableVersion;
  const IID **pAsyncIIDLookup;
  LONG_PTR Filler2;
  LONG_PTR Filler3;
  LONG_PTR Filler4;
} ProxyFileInfo;

typedef ProxyFileInfo ExtendedProxyFileInfo;

typedef struct tagCInterfaceProxyHeader
{
#ifdef USE_STUBLESS_PROXY
  const void *pStublessProxyInfo;
#endif
  const IID *piid;
} CInterfaceProxyHeader;

#define CINTERFACE_PROXY_VTABLE(n) \
  struct \
  { \
    CInterfaceProxyHeader header; \
    void *Vtbl[n]; \
  }

typedef struct tagCInterfaceProxyVtbl
{
  CInterfaceProxyHeader header;
  void *Vtbl[];
} CInterfaceProxyVtbl;

typedef void (__RPC_STUB *PRPC_STUB_FUNCTION)(
  IRpcStubBuffer *This,
  IRpcChannelBuffer *_pRpcChannelBuffer,
  PRPC_MESSAGE _pRpcMessage,
  DWORD *pdwStubPhase);

typedef struct tagCInterfaceStubHeader
{
  const IID *piid;
  const MIDL_SERVER_INFO *pServerInfo;
  ULONG DispatchTableCount;
  const PRPC_STUB_FUNCTION *pDispatchTable;
} CInterfaceStubHeader;

typedef struct tagCInterfaceStubVtbl
{
  CInterfaceStubHeader header;
  IRpcStubBufferVtbl Vtbl;
} CInterfaceStubVtbl;

typedef struct tagCStdStubBuffer
{
  const IRpcStubBufferVtbl *lpVtbl;
  LONG RefCount;
  struct IUnknown *pvServerObject;
  const struct ICallFactoryVtbl *pCallFactoryVtbl;
  const IID *pAsyncIID;
  struct IPSFactoryBuffer *pPSFactory;
} CStdStubBuffer;

typedef struct tagCStdPSFactoryBuffer
{
  const IPSFactoryBufferVtbl *lpVtbl;
  LONG RefCount;
  const ProxyFileInfo **pProxyFileList;
  LONG Filler1;
} CStdPSFactoryBuffer;

#define STUB_FORWARDING_FUNCTION NdrStubForwardingFunction

#define CStdStubBuffer_DELEGATING_METHODS 0, 0, CStdStubBuffer2_Release, 0, 0, 0, 0, 0, 0, 0

RPCRTAPI HRESULT   WINAPI CStdStubBuffer_QueryInterface( IRpcStubBuffer *This, REFIID riid, void **ppvObject );
RPCRTAPI ULONG     WINAPI CStdStubBuffer_AddRef( IRpcStubBuffer *This );
RPCRTAPI HRESULT   WINAPI CStdStubBuffer_Connect( IRpcStubBuffer *This, IUnknown *pUnkServer );
RPCRTAPI void      WINAPI CStdStubBuffer_Disconnect( IRpcStubBuffer *This );
RPCRTAPI HRESULT   WINAPI CStdStubBuffer_Invoke( IRpcStubBuffer *This, RPCOLEMESSAGE *pRpcMsg, IRpcChannelBuffer *pRpcChannelBuffer );
RPCRTAPI IRpcStubBuffer * WINAPI CStdStubBuffer_IsIIDSupported( IRpcStubBuffer *This, REFIID riid );
RPCRTAPI ULONG     WINAPI CStdStubBuffer_CountRefs( IRpcStubBuffer *This );
RPCRTAPI HRESULT   WINAPI CStdStubBuffer_DebugServerQueryInterface( IRpcStubBuffer *This, void **ppv );
RPCRTAPI void      WINAPI CStdStubBuffer_DebugServerRelease( IRpcStubBuffer *This, void *pv );
RPCRTAPI ULONG     WINAPI NdrCStdStubBuffer_Release( IRpcStubBuffer *This, IPSFactoryBuffer *pPSF );
RPCRTAPI ULONG     WINAPI NdrCStdStubBuffer2_Release(IRpcStubBuffer *This, IPSFactoryBuffer *pPSF);

ULONG STDMETHODCALLTYPE CStdStubBuffer_Release( IRpcStubBuffer *This );
ULONG STDMETHODCALLTYPE CStdStubBuffer2_Release(IRpcStubBuffer *This);

#define CStdStubBuffer_METHODS \
  CStdStubBuffer_QueryInterface, \
  CStdStubBuffer_AddRef, \
  CStdStubBuffer_Release, \
  CStdStubBuffer_Connect, \
  CStdStubBuffer_Disconnect, \
  CStdStubBuffer_Invoke, \
  CStdStubBuffer_IsIIDSupported, \
  CStdStubBuffer_CountRefs, \
  CStdStubBuffer_DebugServerQueryInterface, \
  CStdStubBuffer_DebugServerRelease

RPCRTAPI void RPC_ENTRY
  NdrProxyInitialize( void *This, PRPC_MESSAGE pRpcMsg, PMIDL_STUB_MESSAGE pStubMsg,
                      PMIDL_STUB_DESC pStubDescriptor, unsigned int ProcNum );
RPCRTAPI void RPC_ENTRY
  NdrProxyGetBuffer( void *This, PMIDL_STUB_MESSAGE pStubMsg );
RPCRTAPI void RPC_ENTRY
  NdrProxySendReceive( void *This, PMIDL_STUB_MESSAGE pStubMsg );
RPCRTAPI void RPC_ENTRY
  NdrProxyFreeBuffer( void *This, PMIDL_STUB_MESSAGE pStubMsg );
RPCRTAPI HRESULT RPC_ENTRY
  NdrProxyErrorHandler( DWORD dwExceptionCode );

RPCRTAPI void RPC_ENTRY
  NdrStubInitialize( PRPC_MESSAGE pRpcMsg, PMIDL_STUB_MESSAGE pStubMsg,
                     PMIDL_STUB_DESC pStubDescriptor, IRpcChannelBuffer *pRpcChannelBuffer );
RPCRTAPI void RPC_ENTRY
  NdrStubInitializePartial( PRPC_MESSAGE pRpcMsg, PMIDL_STUB_MESSAGE pStubMsg,
                            PMIDL_STUB_DESC pStubDescriptor, IRpcChannelBuffer *pRpcChannelBuffer,
                            ULONG RequestedBufferSize );
void __RPC_STUB NdrStubForwardingFunction( IRpcStubBuffer *This, IRpcChannelBuffer *pChannel,
                                           PRPC_MESSAGE pMsg, DWORD *pdwStubPhase );
RPCRTAPI void RPC_ENTRY
  NdrStubGetBuffer( IRpcStubBuffer *This, IRpcChannelBuffer *pRpcChannelBuffer, PMIDL_STUB_MESSAGE pStubMsg );
RPCRTAPI HRESULT RPC_ENTRY
  NdrStubErrorHandler( DWORD dwExceptionCode );

RPCRTAPI HRESULT RPC_ENTRY
  NdrDllGetClassObject( REFCLSID rclsid, REFIID riid, void **ppv, const ProxyFileInfo **pProxyFileList,
                        const CLSID *pclsid, CStdPSFactoryBuffer *pPSFactoryBuffer );
RPCRTAPI HRESULT RPC_ENTRY
  NdrDllCanUnloadNow( CStdPSFactoryBuffer *pPSFactoryBuffer );

RPCRTAPI HRESULT RPC_ENTRY
  NdrDllRegisterProxy( HMODULE hDll, const ProxyFileInfo **pProxyFileList, const CLSID *pclsid );
RPCRTAPI HRESULT RPC_ENTRY
  NdrDllUnregisterProxy( HMODULE hDll, const ProxyFileInfo **pProxyFileList, const CLSID *pclsid );

HRESULT __cdecl __wine_register_resources(void);
HRESULT __cdecl __wine_unregister_resources(void);
#ifdef __REACTOS__
HRESULT __cdecl __wine_register_resources_module(HMODULE module);
#endif

#define CSTDSTUBBUFFERRELEASE(pFactory) \
ULONG WINAPI CStdStubBuffer_Release(IRpcStubBuffer *This) \
  { return NdrCStdStubBuffer_Release(This, (IPSFactoryBuffer *)pFactory); }

#ifdef PROXY_DELEGATION
#define CSTDSTUBBUFFER2RELEASE(pFactory) \
ULONG WINAPI CStdStubBuffer2_Release(IRpcStubBuffer *This) \
  { return NdrCStdStubBuffer2_Release(This, (IPSFactoryBuffer *)pFactory); }
#else
#define CSTDSTUBBUFFER2RELEASE(pFactory)
#endif

#define IID_GENERIC_CHECK_IID(name,pIID,index) memcmp(pIID, name##_ProxyVtblList[index]->header.piid, sizeof(IID))

/*
 * In these macros, BS stands for Binary Search. MIDL uses these to
 * "unroll" a binary search into the module's IID_Lookup function.
 * However, I haven't bothered to reimplement that stuff yet;
 * I've just implemented a linear search for now.
 */
#define IID_BS_LOOKUP_SETUP \
  int c;
#define IID_BS_LOOKUP_INITIAL_TEST(name, sz, split)
#define IID_BS_LOOKUP_NEXT_TEST(name, split)
#define IID_BS_LOOKUP_RETURN_RESULT(name, sz, index) \
  for (c=0; c<sz; c++) if (!name##_CHECK_IID(c)) { (index)=c; return 1; } \
  return 0;

/* macros used in dlldata.c files */
#define EXTERN_PROXY_FILE(proxy) \
    EXTERN_C const ProxyFileInfo proxy##_ProxyFileInfo;

#define PROXYFILE_LIST_START \
    const ProxyFileInfo * aProxyFileList[] = \
    {

#define REFERENCE_PROXY_FILE(proxy) \
        & proxy##_ProxyFileInfo

#define PROXYFILE_LIST_END \
        NULL \
    };


/* define PROXY_CLSID to use an existing CLSID */
/* define PROXY_CLSID_IS to specify the CLSID data of the PSFactoryBuffer */
/* define neither to use the GUID of the first interface */
#ifdef PROXY_CLSID
# define CLSID_PSFACTORYBUFFER extern CLSID PROXY_CLSID;
#else
# ifdef PROXY_CLSID_IS
#  define CLSID_PSFACTORYBUFFER const CLSID CLSID_PSFactoryBuffer = PROXY_CLSID_IS;
#  define PROXY_CLSID CLSID_PSFactoryBuffer
# else
#  define CLSID_PSFACTORYBUFFER
# endif
#endif

#ifndef PROXY_CLSID
# define GET_DLL_CLSID (aProxyFileList[0]->pStubVtblList[0] ? \
    aProxyFileList[0]->pStubVtblList[0]->header.piid : NULL)
#else
# define GET_DLL_CLSID &PROXY_CLSID
#endif

#ifdef ENTRY_PREFIX
# define __rpc_macro_expand2(a, b) a##b
# define __rpc_macro_expand(a, b) __rpc_macro_expand2(a, b)
# define DLLREGISTERSERVER_ENTRY __rpc_macro_expand(ENTRY_PREFIX, DllRegisterServer)
# define DLLUNREGISTERSERVER_ENTRY __rpc_macro_expand(ENTRY_PREFIX, DllUnregisterServer)
# define DLLMAIN_ENTRY __rpc_macro_expand(ENTRY_PREFIX, DllMain)
# define DLLGETCLASSOBJECT_ENTRY __rpc_macro_expand(ENTRY_PREFIX, DllGetClassObject)
# define DLLCANUNLOADNOW_ENTRY __rpc_macro_expand(ENTRY_PREFIX, DllCanUnloadNow)
#else
# define DLLREGISTERSERVER_ENTRY DllRegisterServer
# define DLLUNREGISTERSERVER_ENTRY DllUnregisterServer
# define DLLMAIN_ENTRY DllMain
# define DLLGETCLASSOBJECT_ENTRY DllGetClassObject
# define DLLCANUNLOADNOW_ENTRY DllCanUnloadNow
#endif

#ifdef WINE_REGISTER_DLL
# define WINE_DO_REGISTER_DLL(pfl, clsid) return __wine_register_resources()
# define WINE_DO_UNREGISTER_DLL(pfl, clsid) return __wine_unregister_resources()
#else
# define WINE_DO_REGISTER_DLL(pfl, clsid)   return NdrDllRegisterProxy( hProxyDll, (pfl), (clsid) )
# define WINE_DO_UNREGISTER_DLL(pfl, clsid) return NdrDllUnregisterProxy( hProxyDll, (pfl), (clsid) )
#endif


#define DLLDATA_GETPROXYDLLINFO(pfl, rclsid) \
    void RPC_ENTRY GetProxyDllInfo(const ProxyFileInfo ***ppProxyFileInfo, \
                                   const CLSID **ppClsid); \
    void RPC_ENTRY GetProxyDllInfo(const ProxyFileInfo ***ppProxyFileInfo, \
                                   const CLSID **ppClsid) \
    { \
        *ppProxyFileInfo = (pfl); \
        *ppClsid = (rclsid); \
    }

#define DLLGETCLASSOBJECTROUTINE(pfl, factory_clsid, factory) \
    HRESULT WINAPI DLLGETCLASSOBJECT_ENTRY(REFCLSID rclsid, REFIID riid, void **ppv); \
    HRESULT WINAPI DLLGETCLASSOBJECT_ENTRY(REFCLSID rclsid, REFIID riid, \
                                           void **ppv) \
    { \
        return NdrDllGetClassObject(rclsid, riid, ppv, (pfl), \
                                    (factory_clsid), factory); \
    }

#define DLLCANUNLOADNOW(factory) \
    HRESULT WINAPI DLLCANUNLOADNOW_ENTRY(void); \
    HRESULT WINAPI DLLCANUNLOADNOW_ENTRY(void) \
    { \
        return NdrDllCanUnloadNow((factory)); \
    }

#define REGISTER_PROXY_DLL_ROUTINES(pfl, factory_clsid) \
    HINSTANCE hProxyDll = NULL; \
    \
    BOOL WINAPI DLLMAIN_ENTRY(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved); \
    BOOL WINAPI DLLMAIN_ENTRY(HINSTANCE hinstDLL, DWORD fdwReason, \
                              LPVOID lpvReserved) \
    { \
        if (fdwReason == DLL_PROCESS_ATTACH) \
        { \
            DisableThreadLibraryCalls(hinstDLL); \
            hProxyDll = hinstDLL; \
        } \
        return TRUE; \
    } \
    \
    HRESULT WINAPI DLLREGISTERSERVER_ENTRY(void); \
    HRESULT WINAPI DLLREGISTERSERVER_ENTRY(void) \
    { \
        WINE_DO_REGISTER_DLL( (pfl), (factory_clsid) ); \
    } \
    \
    HRESULT WINAPI DLLUNREGISTERSERVER_ENTRY(void); \
    HRESULT WINAPI DLLUNREGISTERSERVER_ENTRY(void) \
    { \
        WINE_DO_UNREGISTER_DLL( (pfl), (factory_clsid) ); \
    }

#if defined(REGISTER_PROXY_DLL) || defined(WINE_REGISTER_DLL)
# define DLLREGISTRY_ROUTINES(pfl, factory_clsid) \
    REGISTER_PROXY_DLL_ROUTINES(pfl, factory_clsid)
#else
# define DLLREGISTRY_ROUTINES(pfl, factory_clsid)
#endif

#define DLLDATA_ROUTINES(pfl, factory_clsid) \
    CLSID_PSFACTORYBUFFER \
    CStdPSFactoryBuffer gPFactory = { NULL, 0, NULL, 0 }; \
    DLLDATA_GETPROXYDLLINFO(pfl, factory_clsid) \
    DLLGETCLASSOBJECTROUTINE(pfl, factory_clsid, &gPFactory) \
    DLLCANUNLOADNOW(&gPFactory) \
    CSTDSTUBBUFFERRELEASE(&gPFactory) \
    CSTDSTUBBUFFER2RELEASE(&gPFactory) \
    DLLREGISTRY_ROUTINES(pfl, factory_clsid)

#if 0

RPCRTAPI HRESULT RPC_ENTRY
  CreateProxyFromTypeInfo( LPTYPEINFO pTypeInfo, LPUNKNOWN pUnkOuter, REFIID riid,
                           LPRPCPROXYBUFFER *ppProxy, LPVOID *ppv );
RPCRTAPI HRESULT RPC_ENTRY
  CreateStubFromTypeInfo( LPTYPEINFO pTypeInfo, REFIID riid, LPUNKNOWN pUnkServer,
                          LPRPCSTUBBUFFER *ppStub );

#endif

#ifdef __cplusplus
}
#endif

#if (defined(WIN32) || defined(_M_AMD64))
#define CStdAsyncStubBuffer_DELEGATING_METHODS 0
#define CStdAsyncStubBuffer_DELEGATING_METHODS_OPT CStdAsyncStubBuffer_QueryInterface, CStdAsyncStubBuffer_AddRef, CStdAsyncStubBuffer2_Release, CStdAsyncStubBuffer2_Connect, CStdAsyncStubBuffer2_Disconnect, CStdAsyncStubBuffer_Invoke, CStdStubBuffer_IsIIDSupported, CStdStubBuffer2_CountRefs, CStdStubBuffer_DebugServerQueryInterface, CStdStubBuffer_DebugServerRelease
#ifdef _WIN64
#define CStdAsyncStubBuffer_DELEGATING_METHODS_TAG (-4)
#else
#define CStdAsyncStubBuffer_DELEGATING_METHODS_TAG 0xFFFFFFFC
#endif
#define CStdAsyncStubBuffer_METHODS 0
#define CStdAsyncStubBuffer_METHODS_OPT CStdAsyncStubBuffer_QueryInterface, CStdAsyncStubBuffer_AddRef, CStdAsyncStubBuffer_Release, CStdAsyncStubBuffer_Connect, CStdAsyncStubBuffer_Disconnect, CStdAsyncStubBuffer_Invoke, CStdStubBuffer_IsIIDSupported, CStdStubBuffer_CountRefs, CStdStubBuffer_DebugServerQueryInterface, CStdStubBuffer_DebugServerRelease
#ifdef _WIN64
#define CStdAsyncStubBuffer_METHODS_TAG (-3)
#else
#define CStdAsyncStubBuffer_METHODS_TAG 0xFFFFFFFD
#endif
#define CStdStubBuffer_DELEGATING_METHODS_OPT CStdStubBuffer2_QueryInterface, CStdStubBuffer_AddRef, CStdStubBuffer2_Release, CStdStubBuffer2_Connect, CStdStubBuffer2_Disconnect, CStdStubBuffer_Invoke, CStdStubBuffer_IsIIDSupported, CStdStubBuffer2_CountRefs, CStdStubBuffer_DebugServerQueryInterface, CStdStubBuffer_DebugServerRelease
#ifdef _WIN64
#define CStdStubBuffer_DELEGATING_METHODS_TAG (-2)
#else
#define CStdStubBuffer_DELEGATING_METHODS_TAG 0xFFFFFFFE
#endif
#define CStdStubBuffer_METHODS_OPT CStdStubBuffer_QueryInterface, CStdStubBuffer_AddRef, CStdStubBuffer_Release, CStdStubBuffer_Connect, CStdStubBuffer_Disconnect, CStdStubBuffer_Invoke, CStdStubBuffer_IsIIDSupported, CStdStubBuffer_CountRefs, CStdStubBuffer_DebugServerQueryInterface, CStdStubBuffer_DebugServerRelease
#define CStdStubBuffer_METHODS_TAG ((void *) - 1)
#define DLLDATA_STANDARD_ROUTINES DLLDATA_ROUTINES((const ProxyFileInfo * *) pProxyFileList, & CLSID_PSFactoryBuffer)
#ifdef _WIN64
#define IInspectableInterfaceProxyTag (-1)
#else
#define IInspectableInterfaceProxyTag 0xFFFFFFFF
#endif
#ifdef _WIN64
#define IInspectableNdrStubCall2CommonStubListTag (-1)
#else
#define IInspectableNdrStubCall2CommonStubListTag 0xFFFFFFFF
#endif
#ifdef _WIN64
#define IInspectableNdrStubCall3CommonStubListTag (-2)
#else
#define IInspectableNdrStubCall3CommonStubListTag 0xFFFFFFFE
#endif
#endif
#define INC_OLE2
#if (defined(WIN32) || defined(_M_AMD64))
#ifdef _WIN64
#define IUnknownInterfaceProxyTag (-2)
#else
#define IUnknownInterfaceProxyTag 0xFFFFFFFE
#endif
#define NdrDefaultIIDLookup ((PIIDLookup) - 1)
#endif
#define RPCPROXY_IID_DECLSPEC_SECTION __declspec(allocate("rpcproxy$_iid"))

#ifdef __cplusplus
extern "C" {
#endif

/* TODO: LiberNT does not implement the functions below yet */
#if (defined(WIN32) || defined(_M_AMD64))
HRESULT WINAPI CStdAsyncStubBuffer2_Connect(IRpcStubBuffer *pthis, IUnknown *pUnkServer);
void WINAPI CStdAsyncStubBuffer2_Disconnect(IRpcStubBuffer *pthis);
ULONG WINAPI CStdAsyncStubBuffer2_Release(IRpcStubBuffer *pthis);
ULONG WINAPI CStdAsyncStubBuffer_AddRef(IRpcStubBuffer *pthis);
HRESULT WINAPI CStdAsyncStubBuffer_Connect(IRpcStubBuffer *pthis, IUnknown *pUnkServer);
void WINAPI CStdAsyncStubBuffer_Disconnect(IRpcStubBuffer *pthis);
HRESULT WINAPI CStdAsyncStubBuffer_Invoke(IRpcStubBuffer *pthis, RPCOLEMESSAGE *_prpcmsg, IRpcChannelBuffer *_pRpcChannelBuffer);
HRESULT WINAPI CStdAsyncStubBuffer_QueryInterface(IRpcStubBuffer *pthis, const IID *const riid, void **ppvObject);
ULONG WINAPI CStdAsyncStubBuffer_Release(IRpcStubBuffer *pthis);
HRESULT WINAPI CStdStubBuffer2_Connect(IRpcStubBuffer *pthis, IUnknown *pUnkServer);
ULONG WINAPI CStdStubBuffer2_CountRefs(IRpcStubBuffer *pthis);
void WINAPI CStdStubBuffer2_Disconnect(IRpcStubBuffer *pthis);
HRESULT WINAPI CStdStubBuffer2_QueryInterface(IRpcStubBuffer *This, const IID *const riid, void **ppvObject);
void WINAPI NdrProxyForwardingFunction10(void);
void WINAPI NdrProxyForwardingFunction11(void);
void WINAPI NdrProxyForwardingFunction12(void);
void WINAPI NdrProxyForwardingFunction13(void);
void WINAPI NdrProxyForwardingFunction14(void);
void WINAPI NdrProxyForwardingFunction15(void);
void WINAPI NdrProxyForwardingFunction16(void);
void WINAPI NdrProxyForwardingFunction17(void);
void WINAPI NdrProxyForwardingFunction18(void);
void WINAPI NdrProxyForwardingFunction19(void);
void WINAPI NdrProxyForwardingFunction20(void);
void WINAPI NdrProxyForwardingFunction21(void);
void WINAPI NdrProxyForwardingFunction22(void);
void WINAPI NdrProxyForwardingFunction23(void);
void WINAPI NdrProxyForwardingFunction24(void);
void WINAPI NdrProxyForwardingFunction25(void);
void WINAPI NdrProxyForwardingFunction26(void);
void WINAPI NdrProxyForwardingFunction27(void);
void WINAPI NdrProxyForwardingFunction28(void);
void WINAPI NdrProxyForwardingFunction29(void);
void WINAPI NdrProxyForwardingFunction3(void);
void WINAPI NdrProxyForwardingFunction30(void);
void WINAPI NdrProxyForwardingFunction31(void);
void WINAPI NdrProxyForwardingFunction32(void);
void WINAPI NdrProxyForwardingFunction4(void);
void WINAPI NdrProxyForwardingFunction5(void);
void WINAPI NdrProxyForwardingFunction6(void);
void WINAPI NdrProxyForwardingFunction7(void);
void WINAPI NdrProxyForwardingFunction8(void);
void WINAPI NdrProxyForwardingFunction9(void);
void WINAPI ObjectStublessClient10(void);
void WINAPI ObjectStublessClient11(void);
void WINAPI ObjectStublessClient12(void);
void WINAPI ObjectStublessClient13(void);
void WINAPI ObjectStublessClient14(void);
void WINAPI ObjectStublessClient15(void);
void WINAPI ObjectStublessClient16(void);
void WINAPI ObjectStublessClient17(void);
void WINAPI ObjectStublessClient18(void);
void WINAPI ObjectStublessClient19(void);
void WINAPI ObjectStublessClient20(void);
void WINAPI ObjectStublessClient21(void);
void WINAPI ObjectStublessClient22(void);
void WINAPI ObjectStublessClient23(void);
void WINAPI ObjectStublessClient24(void);
void WINAPI ObjectStublessClient25(void);
void WINAPI ObjectStublessClient26(void);
void WINAPI ObjectStublessClient27(void);
void WINAPI ObjectStublessClient28(void);
void WINAPI ObjectStublessClient29(void);
void WINAPI ObjectStublessClient3(void);
void WINAPI ObjectStublessClient30(void);
void WINAPI ObjectStublessClient31(void);
void WINAPI ObjectStublessClient32(void);
void WINAPI ObjectStublessClient4(void);
void WINAPI ObjectStublessClient5(void);
void WINAPI ObjectStublessClient6(void);
void WINAPI ObjectStublessClient7(void);
void WINAPI ObjectStublessClient8(void);
void WINAPI ObjectStublessClient9(void);
#endif

#ifdef __cplusplus
}
#endif

#if (defined(WIN32) || defined(_M_AMD64))
#define DLLRPCDUMMYCALL void __cdecl DLLRPCDUMMYCALL_ENTRY(void) { /* Don't define __purecall method anymore */ }
#endif
#define DLLRPCDUMMYCALL_ENTRY DllRpcDummyCall


#if (defined(WIN32) || defined(_M_AMD64))
typedef struct tagCInterfaceStubVtblTag CInterfaceStubVtblTag;

struct tagCInterfaceStubVtblTag
{
    CInterfaceStubHeader header;
    void *tag;
};

#define DISABLE_THREAD_LIBRARY_CALLS(x) DisableThreadLibraryCalls(x)
#define RPCPROXY_GET_STUB_HEADER(StubVtblListEntry) (& (StubVtblListEntry) -> header)
#endif

#endif /*__WINE_RPCPROXY_H */
