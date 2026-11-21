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

#define TC_INVALID_HANDLE 0
#define tcDstPort S_un . S_un_ports . s_dstport
#ifdef UNICODE
#define TcGetFlowName TcGetFlowNameW
#else
#define TcGetFlowName TcGetFlowNameA
#endif
#define tcIcmpCode S_un . S_un_icmp . s_code
#define tcIcmpType S_un . S_un_icmp . s_type
#ifdef UNICODE
#define TcOpenInterface TcOpenInterfaceW
#else
#define TcOpenInterface TcOpenInterfaceA
#endif
#ifdef UNICODE
#define TcQueryFlow TcQueryFlowW
#else
#define TcQueryFlow TcQueryFlowA
#endif
#ifdef UNICODE
#define TcSetFlow TcSetFlowW
#else
#define TcSetFlow TcSetFlowA
#endif
#define tcSpi S_un . S_Spi
#define tcSrcPort 0

struct _ADDRESS_LIST_DESCRIPTOR
{
    ULONG MediaType;
    int AddressList;
};

struct _IP_PATTERN
{
    ULONG Reserved1;
    ULONG Reserved2;
    ULONG SrcAddr;
    ULONG DstAddr;
    union
    {
        struct
        {
            USHORT s_srcport;
            USHORT s_dstport;
        } S_un_ports;
        struct
        {
            UCHAR s_type;
            UCHAR s_code;
            USHORT filler;
        } S_un_icmp;
        ULONG S_Spi;
    } S_un;
    UCHAR ProtocolId;
    UCHAR Reserved3[3];
};

struct _IPX_PATTERN
{
    struct
    {
        ULONG NetworkAddress;
        UCHAR NodeAddress[6];
        USHORT Socket;
    } Src;
    struct
    {
        ULONG NetworkAddress;
        UCHAR NodeAddress[6];
        USHORT Socket;
    } Dest;
};

struct _TC_GEN_FLOW
{
    int SendingFlowspec;
    int ReceivingFlowspec;
    ULONG TcObjectsLength;
    int TcObjects[1];
};

typedef struct _ADDRESS_LIST_DESCRIPTOR ADDRESS_LIST_DESCRIPTOR;

struct _TC_IFC_DESCRIPTOR
{
    ULONG Length;
    LPWSTR pInterfaceName;
    LPWSTR pInterfaceID;
    ADDRESS_LIST_DESCRIPTOR AddressListDesc;
};

struct _TC_SUPPORTED_INFO_BUFFER
{
    USHORT InstanceIDLength;
    WCHAR InstanceID[256];
    ULONG64 InterfaceLuid;
    ADDRESS_LIST_DESCRIPTOR AddrListDesc;
};

typedef struct _IP_PATTERN IP_PATTERN;

typedef struct _IPX_PATTERN IPX_PATTERN;

typedef struct _ADDRESS_LIST_DESCRIPTOR *PADDRESS_LIST_DESCRIPTOR;

typedef struct _IP_PATTERN *PIP_PATTERN;

typedef struct _IPX_PATTERN *PIPX_PATTERN;

typedef struct _TC_GEN_FLOW *PTC_GEN_FLOW;

struct _ENUMERATION_BUFFER
{
    ULONG Length;
    ULONG OwnerProcessId;
    USHORT FlowNameLength;
    WCHAR FlowName[256];
    PTC_GEN_FLOW pFlow;
    ULONG NumberOfFilters;
    TC_GEN_FILTER GenericFilter[1];
};

typedef struct _ENUMERATION_BUFFER ENUMERATION_BUFFER;

typedef struct _ENUMERATION_BUFFER *PENUMERATION_BUFFER;

typedef struct _TC_IFC_DESCRIPTOR *PTC_IFC_DESCRIPTOR;

typedef struct _TC_SUPPORTED_INFO_BUFFER *PTC_SUPPORTED_INFO_BUFFER;

typedef struct _TC_GEN_FLOW TC_GEN_FLOW;

typedef struct _TC_IFC_DESCRIPTOR TC_IFC_DESCRIPTOR;

typedef struct _TC_SUPPORTED_INFO_BUFFER TC_SUPPORTED_INFO_BUFFER;

#ifdef __cplusplus
extern "C" {
#endif

ULONG WINAPI TcDeregisterClient(HANDLE ClientHandle);

/* TODO: LiberNT does not implement the functions below yet */
ULONG WINAPI TcAddFilter(HANDLE FlowHandle, PTC_GEN_FILTER pGenericFilter, PHANDLE pFilterHandle);
ULONG WINAPI TcAddFlow(HANDLE IfcHandle, HANDLE ClFlowCtx, ULONG Flags, PTC_GEN_FLOW pGenericFlow, PHANDLE pFlowHandle);
ULONG WINAPI TcCloseInterface(HANDLE IfcHandle);
ULONG WINAPI TcDeleteFilter(HANDLE FilterHandle);
ULONG WINAPI TcDeleteFlow(HANDLE FlowHandle);
ULONG WINAPI TcEnumerateFlows(HANDLE IfcHandle, PHANDLE pEnumHandle, PULONG pFlowCount, PULONG pBufSize, PENUMERATION_BUFFER Buffer);
ULONG WINAPI TcEnumerateInterfaces(HANDLE ClientHandle, PULONG pBufferSize, PTC_IFC_DESCRIPTOR InterfaceBuffer);
ULONG WINAPI TcGetFlowNameA(HANDLE FlowHandle, ULONG StrSize, LPSTR pFlowName);
ULONG WINAPI TcGetFlowNameW(HANDLE FlowHandle, ULONG StrSize, LPWSTR pFlowName);
ULONG WINAPI TcModifyFlow(HANDLE FlowHandle, PTC_GEN_FLOW pGenericFlow);
ULONG WINAPI TcOpenInterfaceA(LPSTR pInterfaceName, HANDLE ClientHandle, HANDLE ClIfcCtx, PHANDLE pIfcHandle);
ULONG WINAPI TcOpenInterfaceW(LPWSTR pInterfaceName, HANDLE ClientHandle, HANDLE ClIfcCtx, PHANDLE pIfcHandle);
ULONG WINAPI TcQueryFlowA(LPSTR pFlowName, LPGUID pGuidParam, PULONG pBufferSize, PVOID Buffer);
ULONG WINAPI TcQueryFlowW(LPWSTR pFlowName, LPGUID pGuidParam, PULONG pBufferSize, PVOID Buffer);
ULONG WINAPI TcQueryInterface(HANDLE IfcHandle, LPGUID pGuidParam, BOOLEAN NotifyChange, PULONG pBufferSize, PVOID Buffer);
ULONG WINAPI TcSetFlowA(LPSTR pFlowName, LPGUID pGuidParam, ULONG BufferSize, PVOID Buffer);
ULONG WINAPI TcSetFlowW(LPWSTR pFlowName, LPGUID pGuidParam, ULONG BufferSize, PVOID Buffer);
ULONG WINAPI TcSetInterface(HANDLE IfcHandle, LPGUID pGuidParam, ULONG BufferSize, PVOID Buffer);

#ifdef __cplusplus
}
#endif

#endif /* __WINE_TRAFFIC_H */
