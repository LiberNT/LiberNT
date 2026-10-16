#ifndef _PRECOMP_H__
#define _PRECOMP_H__

#include <stdio.h>

#define WIN32_NO_STATUS
#define _INC_WINDOWS
#define COM_NO_WINDOWS_H

#include <windef.h>
#include <winbase.h>
#include <wingdi.h>
#include <wincon.h>
#include <winreg.h>
#include <winnls.h>
#include <shlwapi.h>
#include <shlobj.h>
#include <shellapi.h>
#include <reactos/trayicons.h>
#include <atlbase.h>
#include <atlcom.h>
#include <atlcoll.h>
#include <atlstr.h>
#include <winsock2.h>
#include <ws2ipdef.h>
#include <iphlpapi.h>
#include <setupapi.h>
#include <devguid.h>
#include <netcon.h>
#include <shlguid_undoc.h>
#include <prsht.h>
#include <undocshell.h>
#include <shellutils.h>

#include <netcfgx.h>
#include <netcfgn.h>
#include <strsafe.h>

#include <wine/debug.h>
WINE_DEFAULT_DEBUG_CHANNEL(shell);

#include "resource.h"


#define USE_CUSTOM_CONMGR 1

/* globals */
extern HINSTANCE netshell_hInstance;

/* enumlist.c */
#pragma pack(push, 1)
#define NETCONIDSTRUCT_SIG 0x4EFF
typedef struct tagNETCONIDSTRUCT
{
    WORD             Signature;
    BYTE             Unknown[8];
    CLSID            clsidThisObject;
    GUID             guidId;
    DWORD            dwCharacter;
    NETCON_MEDIATYPE MediaType;
    NETCON_STATUS    Status;
    ULONG            uNameOffset;
    ULONG            uDeviceNameOffset;
} NETCONIDSTRUCT, *PNETCONIDSTRUCT;
#pragma pack(pop)

C_ASSERT(2 + FIELD_OFFSET(NETCONIDSTRUCT, clsidThisObject) == 2 + (2 + 8));
C_ASSERT(2 + FIELD_OFFSET(NETCONIDSTRUCT, guidId) == 2 + (2 + 8 + 16));
C_ASSERT(2 + FIELD_OFFSET(NETCONIDSTRUCT, dwCharacter) == 2 + (2 + 8 + 32));
C_ASSERT(2 + FIELD_OFFSET(NETCONIDSTRUCT, MediaType) == 2 + (2 + 8 + 36));

PNETCONIDSTRUCT ILGetConnData(PCITEMID_CHILD pidl);
PWCHAR ILGetConnName(PCITEMID_CHILD pidl);
PWCHAR ILGetDeviceName(PCITEMID_CHILD pidl);
PITEMID_CHILD ILCreateNetConnectItem(INetConnection * pItem);
HRESULT ILGetConnection(PCITEMID_CHILD pidl, INetConnection ** pItem);
HRESULT CEnumIDList_CreateInstance(HWND hwndOwner, DWORD dwFlags, REFIID riid, LPVOID * ppv);

#define NCCF_NOTIFY_DISCONNECTED 0x100000

HPROPSHEETPAGE InitializePropertySheetPage(LPWSTR resname, DLGPROC dlgproc, LPARAM lParam, LPWSTR szTitle);

#include "connectmanager.h"
#include "lanconnectui.h"
#include "lanstatusui.h"
#include "shfldr_netconnect.h"


#endif /* _PRECOMP_H__ */
