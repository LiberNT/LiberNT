/*
 * TRAFFIC definitions
 *
 * Copyright (c) 2009 Stefan Leichter
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

#ifndef __WINE_TRAFFIC_H
#define __WINE_TRAFFIC_H

#ifdef __cplusplus
extern "C" {
#endif /* defined(__cplusplus) */

typedef VOID (CALLBACK * TCI_ADD_FLOW_COMPLETE_HANDLER)(HANDLE, ULONG);
typedef VOID (CALLBACK * TCI_DEL_FLOW_COMPLETE_HANDLER)(HANDLE, ULONG);
typedef VOID (CALLBACK * TCI_MOD_FLOW_COMPLETE_HANDLER)(HANDLE, ULONG);
typedef VOID (CALLBACK * TCI_NOTIFY_HANDLER)
                              (HANDLE,HANDLE,ULONG,HANDLE,ULONG,PVOID);

typedef struct _TCI_CLIENT_FUNC_LIST
{
    TCI_NOTIFY_HANDLER ClNotifyHandler;
    TCI_ADD_FLOW_COMPLETE_HANDLER ClAddFlowCompleteHandler;
    TCI_MOD_FLOW_COMPLETE_HANDLER ClModifyFlowCompleteHandler;
    TCI_DEL_FLOW_COMPLETE_HANDLER ClDeleteFlowCompleteHandler;
} TCI_CLIENT_FUNC_LIST, *PTCI_CLIENT_FUNC_LIST;

ULONG WINAPI TcRegisterClient(ULONG,HANDLE,PTCI_CLIENT_FUNC_LIST,PHANDLE);

#ifdef __cplusplus
} /* extern "C" */
#endif /* defined(__cplusplus) */

#ifndef CURRENT_TCI_VERSION
#define CURRENT_TCI_VERSION 0x0002
#endif
#ifndef MAX_STRING_LENGTH
#define MAX_STRING_LENGTH 256
#endif
#ifndef TC_NOTIFY_FLOW_CLOSE
#define TC_NOTIFY_FLOW_CLOSE 5
#endif
#ifndef TC_NOTIFY_IFC_CHANGE
#define TC_NOTIFY_IFC_CHANGE 3
#endif
#ifndef TC_NOTIFY_IFC_CLOSE
#define TC_NOTIFY_IFC_CLOSE 2
#endif
#ifndef TC_NOTIFY_IFC_UP
#define TC_NOTIFY_IFC_UP 1
#endif
#ifndef TC_NOTIFY_PARAM_CHANGED
#define TC_NOTIFY_PARAM_CHANGED 4
#endif

typedef struct _TC_GEN_FILTER {
  USHORT AddressType;
  ULONG PatternSize;
  PVOID Pattern;
  PVOID Mask;
} TC_GEN_FILTER, *PTC_GEN_FILTER;

#endif /* __WINE_TRAFFIC_H */
