/*
 * PROJECT:     LiberNT Network Connections Shell
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Marshalling shims for the network connection call_as methods
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#define COBJMACROS

#include <stdarg.h>

#include "windef.h"
#include "winbase.h"
#include "objbase.h"
#include "netshell_netcon_p.h"

HRESULT CALLBACK INetConnectionCommonUi_ChooseConnection_Proxy(INetConnectionCommonUi *This, void *pChooseConn,
                                                               INetConnection **ppCon)
{
    return INetConnectionCommonUi_RemoteChooseConnection_Proxy(This, ppCon);
}

HRESULT __RPC_STUB INetConnectionCommonUi_ChooseConnection_Stub(INetConnectionCommonUi *This, INetConnection **ppCon)
{
    return INetConnectionCommonUi_ChooseConnection(This, NULL, ppCon);
}
