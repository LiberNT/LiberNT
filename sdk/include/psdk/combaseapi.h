/*
 * Copyright 2021 Jacek Caban for CodeWeavers
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

#include <rpc.h>
#include <rpcndr.h>

#ifndef _COMBASEAPI_H_
#define _COMBASEAPI_H_

#ifndef RC_INVOKED
#include <stdlib.h>
#endif

#include <objidlbase.h>
#include <guiddef.h>

#ifndef INITGUID
#include <cguid.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tagServerInformation
{
    DWORD   dwServerPid;
    DWORD   dwServerTid;
    UINT64  ui64ServerAddress;
} ServerInformation, *PServerInformation;

enum AgileReferenceOptions
{
    AGILEREFERENCE_DEFAULT,
    AGILEREFERENCE_DELAYEDMARSHAL
};

HRESULT WINAPI CoDecodeProxy(_In_ DWORD client_pid, _In_ UINT64 proxy_addr, _Out_ ServerInformation *server_info);
HRESULT WINAPI CoRegisterActivationFilter(_In_opt_ IActivationFilter *filter);
HRESULT WINAPI RoGetAgileReference(_In_ enum AgileReferenceOptions options, _In_ REFIID riid, _In_ IUnknown *obj, _Out_ IAgileReference **agile_reference);

#ifdef __cplusplus
extern "C++" template<typename T> void **IID_PPV_ARGS_Helper(T **obj)
{
    (void)static_cast<IUnknown *>(*obj);
    return reinterpret_cast<void **>(obj);
}
#define IID_PPV_ARGS(obj) __uuidof(**(obj)), IID_PPV_ARGS_Helper(obj)
#endif /* __cplusplus */

#ifdef __cplusplus
}
#endif

typedef enum tagCOINITBASE {
    COINITBASE_MULTITHREADED = 0
} COINITBASE;

typedef enum CWMO_FLAGS {
    CWMO_DEFAULT = 0,
    CWMO_DISPATCH_CALLS = 1,
    CWMO_DISPATCH_WINDOW_MESSAGES = 2
} CWMO_FLAGS;

#ifdef __cplusplus
extern "C" {
#endif
HRESULT WINAPI CoGetDefaultContext(APTTYPE aptType, const IID *const riid, void ** ppv);
HRESULT WINAPI CoDisconnectContext(DWORD dwTimeout);
HRESULT WINAPI CoCreateInstanceFromApp(const IID *const Clsid, IUnknown * punkOuter, DWORD dwClsCtx, PVOID reserved, DWORD dwCount, MULTI_QI * pResults);
HRESULT WINAPI CoGetCancelObject(DWORD dwThreadId, const IID *const iid, void ** ppUnk);
#ifdef __cplusplus
}
#endif

typedef struct CO_DEVICE_CATALOG_COOKIE__ {
  int unused;
} *CO_DEVICE_CATALOG_COOKIE;

#endif /* _COMBASEAPI_H_ */

#ifndef COM_RIGHTS_ACTIVATE_LOCAL
#define COM_RIGHTS_ACTIVATE_LOCAL 8
#endif
#ifndef COM_RIGHTS_ACTIVATE_REMOTE
#define COM_RIGHTS_ACTIVATE_REMOTE 16
#endif
#ifndef COM_RIGHTS_EXECUTE
#define COM_RIGHTS_EXECUTE 1
#endif
#ifndef COM_RIGHTS_EXECUTE_LOCAL
#define COM_RIGHTS_EXECUTE_LOCAL 2
#endif
#ifndef COM_RIGHTS_EXECUTE_REMOTE
#define COM_RIGHTS_EXECUTE_REMOTE 4
#endif
#ifndef COM_RIGHTS_RESERVED1
#define COM_RIGHTS_RESERVED1 32
#endif
#ifndef COM_RIGHTS_RESERVED2
#define COM_RIGHTS_RESERVED2 64
#endif
#ifndef CWMO_MAX_HANDLES
#define CWMO_MAX_HANDLES 56
#endif
