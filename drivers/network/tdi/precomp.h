#ifndef _TDI_PCH_
#define _TDI_PCH_

#include <wdm.h>
#include <tdikrnl.h>

#define TAG_TDI_PNP 'pIDT'

typedef struct _TDI_PNP_CLIENT {
    LIST_ENTRY ListEntry;
    TDI_CLIENT_INTERFACE_INFO Info;
} TDI_PNP_CLIENT, *PTDI_PNP_CLIENT;

typedef struct _TDI_NET_ADDRESS {
    LIST_ENTRY ListEntry;
    UNICODE_STRING DeviceName;
    PTDI_PNP_CONTEXT Context;
    TA_ADDRESS Address;
} TDI_NET_ADDRESS, *PTDI_NET_ADDRESS;

extern LIST_ENTRY TdiPnpClientList;
extern LIST_ENTRY TdiNetAddressList;
extern KMUTEX TdiPnpMutex;

VOID
TdiPnpNotifyAddress(
    _In_ PTDI_PNP_CLIENT Client,
    _In_ PTDI_NET_ADDRESS NetAddress,
    _In_ BOOLEAN Add);

#endif /* _TDI_PCH_ */
