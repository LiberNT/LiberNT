/*
 * Copyright 2005 Eric Kohl
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

#ifndef __RPC_PRIVATE_H
#define __RPC_PRIVATE_H

#undef __WINESRC__

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <wchar.h>

#ifndef WIN32_NO_STATUS
#define WIN32_NO_STATUS
#endif

#include <windows.h>
#include <winternl.h>
#include <cfgmgr32.h>
#include <regstr.h>
#include <sddl.h>
#include <setupapi.h>

#include <wine/debug.h>

#define CMP_MAGIC 0x01234567

RPC_STATUS PnpBindRpc(LPCWSTR pszMachine,
                      RPC_BINDING_HANDLE* BindingHandle);

BOOL
PnpGetLocalHandles(RPC_BINDING_HANDLE *BindingHandle);

DEVINST CfgmgrDevInstFromId(const WCHAR *id);
BOOL CfgmgrIdFromDevInst(DEVINST node, WCHAR *id, ULONG len);
BOOL CfgmgrIsKernelDevNodeProperty(ULONG ulProperty);
BOOL CfgmgrIsValidMachine(HMACHINE hMachine);
CONFIGRET CfgmgrGetDevNodeRegistryPropertyA(DEVINST dnDevInst, ULONG ulProperty, PULONG pulRegDataType, PVOID Buffer, PULONG pulLength, ULONG ulFlags, HMACHINE hMachine);
CONFIGRET CfgmgrGetDevNodeRegistryPropertyW(DEVINST dnDevInst, ULONG ulProperty, PULONG pulRegDataType, PVOID Buffer, PULONG pulLength, ULONG ulFlags, HMACHINE hMachine);

#endif /* __RPC_PRIVATE_H */
