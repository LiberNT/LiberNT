/*
 * Winscard definitions
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

#ifndef __WINE_WINSCARD_H
#define __WINE_WINSCARD_H

#include <wtypes.h>
#include <winioctl.h>
#include <winsmcrd.h>
#include <scarderr.h>

#ifndef WINSCARDAPI
#define WINSCARDAPI DECLSPEC_IMPORT
#endif

#ifdef __REACTOS__
#ifndef WINSCARDDATA
#define WINSCARDDATA DECLSPEC_IMPORT
#endif

WINSCARDDATA extern const SCARD_IO_REQUEST g_rgSCardT0Pci, g_rgSCardT1Pci, g_rgSCardRawPci;
#define SCARD_PCI_T0  (&g_rgSCardT0Pci)
#define SCARD_PCI_T1  (&g_rgSCardT1Pci)
#define SCARD_PCI_RAW (&g_rgSCardRawPci)
#endif

/* Valid scopes for contexts */
#define SCARD_SCOPE_USER     0
#define SCARD_SCOPE_TERMINAL 1
#define SCARD_SCOPE_SYSTEM   2

#ifndef _LPCBYTE_DEFINED
#define _LPCBYTE_DEFINED
typedef const BYTE *LPCBYTE;
#endif

typedef ULONG_PTR SCARDCONTEXT, *PSCARDCONTEXT, *LPSCARDCONTEXT;
typedef ULONG_PTR SCARDHANDLE,  *PSCARDHANDLE,  *LPSCARDHANDLE;

typedef struct _SCARD_ATRMASK
{
    DWORD cbAtr;
    BYTE  rgbAtr[36];
    BYTE  rgbMask[36];
} SCARD_ATRMASK, *PSCARD_ATRMASK, *LPSCARD_ATRMASK;

typedef struct
{
    LPCSTR szReader;
    LPVOID pvUserData;
    DWORD  dwCurrentState;
    DWORD  dwEventState;
    DWORD  cbAtr;
    BYTE   rgbAtr[36];
} SCARD_READERSTATEA, *PSCARD_READERSTATEA, *LPSCARD_READERSTATEA;
typedef struct
{
    LPCWSTR szReader;
    LPVOID  pvUserData;
    DWORD   dwCurrentState;
    DWORD   dwEventState;
    DWORD   cbAtr;
    BYTE    rgbAtr[36];
} SCARD_READERSTATEW, *PSCARD_READERSTATEW, *LPSCARD_READERSTATEW;
DECL_WINELIB_TYPE_AW(SCARD_READERSTATE)
DECL_WINELIB_TYPE_AW(PSCARD_READERSTATE)
DECL_WINELIB_TYPE_AW(LPSCARD_READERSTATE)

#define SCARD_AUTOALLOCATE (DWORD)(-1)

#define SCARD_SCOPE_USER     0
#define SCARD_SCOPE_TERMINAL 1
#define SCARD_SCOPE_SYSTEM   2

#define SCARD_STATE_UNAWARE     0x00000000
#define SCARD_STATE_IGNORE      0x00000001
#define SCARD_STATE_CHANGED     0x00000002
#define SCARD_STATE_UNKNOWN     0x00000004
#define SCARD_STATE_UNAVAILABLE 0x00000008
#define SCARD_STATE_EMPTY       0x00000010
#define SCARD_STATE_PRESENT     0x00000020
#define SCARD_STATE_ATRMATCH    0x00000040
#define SCARD_STATE_EXCLUSIVE   0x00000080
#define SCARD_STATE_INUSE       0x00000100
#define SCARD_STATE_MUTE        0x00000200
#define SCARD_STATE_UNPOWERED   0x00000400

#define SCARD_SHARE_EXCLUSIVE 1
#define SCARD_SHARE_SHARED    2
#define SCARD_SHARE_DIRECT    3

#define SCARD_LEAVE_CARD   0
#define SCARD_RESET_CARD   1
#define SCARD_UNPOWER_CARD 2
#define SCARD_EJECT_CARD   3

#ifdef __cplusplus
extern "C" {
#endif

WINSCARDAPI HANDLE      WINAPI SCardAccessStartedEvent(void);
WINSCARDAPI LONG        WINAPI SCardAddReaderToGroupA(SCARDCONTEXT,LPCSTR,LPCSTR);
WINSCARDAPI LONG        WINAPI SCardAddReaderToGroupW(SCARDCONTEXT,LPCWSTR,LPCWSTR);
#define                        SCardAddReaderToGroup WINELIB_NAME_AW(SCardAddReaderToGroup)
WINSCARDAPI LONG        WINAPI SCardBeginTransaction(SCARDHANDLE);
WINSCARDAPI LONG        WINAPI SCardCancel(SCARDCONTEXT);
WINSCARDAPI LONG        WINAPI SCardConnectA(SCARDCONTEXT,LPCSTR,DWORD,DWORD,LPSCARDHANDLE,LPDWORD);
WINSCARDAPI LONG        WINAPI SCardConnectW(SCARDCONTEXT,LPCWSTR,DWORD,DWORD,LPSCARDHANDLE,LPDWORD);
#define                        SCardConnect WINELIB_NAME_AW(SCardConnect)
WINSCARDAPI LONG        WINAPI SCardControl(SCARDHANDLE,DWORD,LPCVOID,DWORD,LPVOID,DWORD,LPDWORD);
WINSCARDAPI LONG        WINAPI SCardDisconnect(SCARDHANDLE,DWORD);
WINSCARDAPI LONG        WINAPI SCardEndTransaction(SCARDHANDLE,DWORD);
WINSCARDAPI LONG        WINAPI SCardEstablishContext(DWORD,LPCVOID,LPCVOID,LPSCARDCONTEXT);
WINSCARDAPI LONG        WINAPI SCardForgetCardTypeA(SCARDCONTEXT,LPCSTR);
WINSCARDAPI LONG        WINAPI SCardForgetCardTypeW(SCARDCONTEXT,LPCWSTR);
#define                        SCardForgetCardType WINELIB_NAME_AW(SCardForgetCardType)
WINSCARDAPI LONG        WINAPI SCardForgetReaderA(SCARDCONTEXT,LPCSTR);
WINSCARDAPI LONG        WINAPI SCardForgetReaderW(SCARDCONTEXT,LPCWSTR);
#define                        SCardForgetReader WINELIB_NAME_AW(SCardForgetReader)
WINSCARDAPI LONG        WINAPI SCardForgetReaderGroupA(SCARDCONTEXT,LPCSTR);
WINSCARDAPI LONG        WINAPI SCardForgetReaderGroupW(SCARDCONTEXT,LPCWSTR);
#define                        SCardForgetReaderGroup WINELIB_NAME_AW(SCardForgetReaderGroup)
WINSCARDAPI LONG        WINAPI SCardFreeMemory(SCARDCONTEXT,LPCVOID);
WINSCARDAPI LONG        WINAPI SCardGetAttrib(SCARDHANDLE,DWORD,LPBYTE,LPDWORD);
WINSCARDAPI LONG        WINAPI SCardGetCardTypeProviderNameA(SCARDCONTEXT,LPCSTR,DWORD,LPSTR,LPDWORD);
WINSCARDAPI LONG        WINAPI SCardGetCardTypeProviderNameW(SCARDCONTEXT,LPCWSTR,DWORD,LPWSTR,LPDWORD);
#define                        SCardGetCardTypeProviderName WINELIB_NAME_AW(SCardGetCardTypeProviderName)
WINSCARDAPI LONG        WINAPI SCardGetProviderIdA(SCARDCONTEXT,LPCSTR,LPGUID);
WINSCARDAPI LONG        WINAPI SCardGetProviderIdW(SCARDCONTEXT,LPCWSTR,LPGUID);
#define                        SCardGetProviderId WINELIB_NAME_AW(SCardGetProviderId)
WINSCARDAPI LONG        WINAPI SCardGetStatusChangeA(SCARDCONTEXT,DWORD,LPSCARD_READERSTATEA,DWORD);
WINSCARDAPI LONG        WINAPI SCardGetStatusChangeW(SCARDCONTEXT,DWORD,LPSCARD_READERSTATEW,DWORD);
#define                        SCardGetStatusChange WINELIB_NAME_AW(SCardGetStatusChange)
WINSCARDAPI LONG        WINAPI SCardIntroduceCardTypeA(SCARDCONTEXT,LPCSTR,LPCGUID,LPCGUID,DWORD,LPCBYTE,LPCBYTE,DWORD);
WINSCARDAPI LONG        WINAPI SCardIntroduceCardTypeW(SCARDCONTEXT,LPCWSTR,LPCGUID,LPCGUID,DWORD,LPCBYTE,LPCBYTE,DWORD);
#define                        SCardIntroduceCardType WINELIB_NAME_AW(SCardIntroduceCardType)
WINSCARDAPI LONG        WINAPI SCardIntroduceReaderA(SCARDCONTEXT,LPCSTR,LPCSTR);
WINSCARDAPI LONG        WINAPI SCardIntroduceReaderW(SCARDCONTEXT,LPCWSTR,LPCWSTR);
#define                        SCardIntroduceReader WINELIB_NAME_AW(SCardIntroduceReader)
WINSCARDAPI LONG        WINAPI SCardIntroduceReaderGroupA(SCARDCONTEXT,LPCSTR);
WINSCARDAPI LONG        WINAPI SCardIntroduceReaderGroupW(SCARDCONTEXT,LPCWSTR);
#define                        SCardIntroduceReaderGroup WINELIB_NAME_AW(SCardIntroduceReaderGroup)
WINSCARDAPI LONG        WINAPI SCardIsValidContext(SCARDCONTEXT);
WINSCARDAPI LONG        WINAPI SCardListCardsA(SCARDCONTEXT,LPCBYTE,LPCGUID,DWORD,LPSTR,LPDWORD);
WINSCARDAPI LONG        WINAPI SCardListCardsW(SCARDCONTEXT,LPCBYTE,LPCGUID,DWORD,LPWSTR,LPDWORD);
#define                        SCardListCards WINELIB_NAME_AW(SCardListCards)
WINSCARDAPI LONG        WINAPI SCardListInterfacesA(SCARDCONTEXT,LPCSTR,LPGUID,LPDWORD);
WINSCARDAPI LONG        WINAPI SCardListInterfacesW(SCARDCONTEXT,LPCWSTR,LPGUID,LPDWORD);
#define                        SCardListInterfaces WINELIB_NAME_AW(SCardListInterfaces)
WINSCARDAPI LONG        WINAPI SCardListReadersA(SCARDCONTEXT,const CHAR *,CHAR *,DWORD *);
WINSCARDAPI LONG        WINAPI SCardListReadersW(SCARDCONTEXT,const WCHAR *,WCHAR *,DWORD *);
#define                        SCardListReaders WINELIB_NAME_AW(SCardListReaders)
WINSCARDAPI LONG        WINAPI SCardListReaderGroupsA(SCARDCONTEXT,LPSTR,LPDWORD);
WINSCARDAPI LONG        WINAPI SCardListReaderGroupsW(SCARDCONTEXT,LPWSTR,LPDWORD);
#define                        SCardListReaderGroups WINELIB_NAME_AW(SCardListReaderGroups)
WINSCARDAPI LONG        WINAPI SCardLocateCardsA(SCARDCONTEXT,LPCSTR,LPSCARD_READERSTATEA,DWORD);
WINSCARDAPI LONG        WINAPI SCardLocateCardsW(SCARDCONTEXT,LPCWSTR,LPSCARD_READERSTATEW,DWORD);
#define                        SCardLocateCards WINELIB_NAME_AW(SCardLocateCards)
WINSCARDAPI LONG        WINAPI SCardLocateCardsByATRA(SCARDCONTEXT,LPSCARD_ATRMASK,DWORD,LPSCARD_READERSTATEA,DWORD);
WINSCARDAPI LONG        WINAPI SCardLocateCardsByATRW(SCARDCONTEXT,LPSCARD_ATRMASK,DWORD,LPSCARD_READERSTATEW,DWORD);
#define                        SCardLocateCardsByATR WINELIB_NAME_AW(SCardLocateCardsByATR)
WINSCARDAPI LONG        WINAPI SCardReconnect(SCARDHANDLE,DWORD,DWORD,DWORD,LPDWORD);
WINSCARDAPI LONG        WINAPI SCardReleaseContext(SCARDCONTEXT);
WINSCARDAPI void        WINAPI SCardReleaseStartedEvent(void);
WINSCARDAPI LONG        WINAPI SCardRemoveReaderFromGroupA(SCARDCONTEXT,LPCSTR,LPCSTR);
WINSCARDAPI LONG        WINAPI SCardRemoveReaderFromGroupW(SCARDCONTEXT,LPCWSTR,LPCWSTR);
#define                        SCardRemoveReaderFromGroup WINELIB_NAME_AW(SCardRemoveReaderFromGroup)
WINSCARDAPI LONG        WINAPI SCardSetAttrib(SCARDHANDLE,DWORD,LPCBYTE,DWORD);
WINSCARDAPI LONG        WINAPI SCardSetCardTypeProviderNameA(SCARDCONTEXT,LPCSTR,DWORD,LPCSTR);
WINSCARDAPI LONG        WINAPI SCardSetCardTypeProviderNameW(SCARDCONTEXT,LPCWSTR,DWORD,LPCWSTR);
#define                        SCardSetCardTypeProviderName WINELIB_NAME_AW(SCardSetCardTypeProviderName)
WINSCARDAPI LONG        WINAPI SCardState(SCARDHANDLE,LPDWORD,LPDWORD,LPBYTE,LPDWORD);
WINSCARDAPI LONG        WINAPI SCardStatusA(SCARDHANDLE,LPSTR,LPDWORD,LPDWORD,LPDWORD,LPBYTE,LPDWORD);
WINSCARDAPI LONG        WINAPI SCardStatusW(SCARDHANDLE,LPWSTR,LPDWORD,LPDWORD,LPDWORD,LPBYTE,LPDWORD);
#define                        SCardStatus WINELIB_NAME_AW(SCardStatus)
WINSCARDAPI LONG        WINAPI SCardTransmit(SCARDHANDLE,LPCSCARD_IO_REQUEST,LPCBYTE,DWORD,LPSCARD_IO_REQUEST,LPBYTE,LPDWORD);

#ifdef __cplusplus
}
#endif

#ifndef SCARD_AUDIT_CHV_SUCCESS
#define SCARD_AUDIT_CHV_SUCCESS 0x1
#endif
#ifndef SCARD_PROVIDER_CSP
#define SCARD_PROVIDER_CSP 2
#endif
#ifndef SCARD_PROVIDER_KSP
#define SCARD_PROVIDER_KSP 3
#endif
#ifndef SCARD_PROVIDER_PRIMARY
#define SCARD_PROVIDER_PRIMARY 1
#endif
#ifndef SCERR_NOCARDNAME
#define SCERR_NOCARDNAME 0x4000
#endif
#ifndef SCERR_NOGUIDS
#define SCERR_NOGUIDS 0x8000
#endif
#ifndef SC_DLG_FORCE_UI
#define SC_DLG_FORCE_UI 0x04
#endif
#ifndef SC_DLG_NO_UI
#define SC_DLG_NO_UI 0x02
#endif

typedef enum READER_SEL_REQUEST_MATCH_TYPE {
    RSR_MATCH_TYPE_READER_AND_CONTAINER = 1,
    RSR_MATCH_TYPE_SERIAL_NUMBER = 2,
    RSR_MATCH_TYPE_ALL_CARDS = 3
} READER_SEL_REQUEST_MATCH_TYPE;

typedef struct READER_SEL_RESPONSE {
  DWORD cbReaderNameOffset;
  DWORD cchReaderNameLength;
  DWORD cbCardNameOffset;
  DWORD cchCardNameLength;
} READER_SEL_RESPONSE, *PREADER_SEL_RESPONSE;

#ifdef UNICODE
#define GetOpenCardName GetOpenCardNameW
#else
#ifdef UNICODE
#define GetOpenCardName GetOpenCardNameW
#else
#define GetOpenCardName GetOpenCardNameA
#endif
#endif
#define LPOCNCONNPROC 0
#define LPOPENCARDNAME_A LPOPENCARDNAMEA
#define LPOPENCARDNAME_W LPOPENCARDNAMEW
#ifdef UNICODE
#define LPOPENCARDNAMEA_EX LPOPENCARDNAME_EXW
#else
#define LPOPENCARDNAMEA_EX LPOPENCARDNAME_EXA
#endif
#define LPOPENCARDNAMEW_EX LPOPENCARDNAME_EXW
#define LPSCARD_READERSTATE_A LPSCARD_READERSTATEA
#define LPSCARD_READERSTATE_W LPSCARD_READERSTATEW
#define OPENCARDNAME_A OPENCARDNAMEA
#define OPENCARDNAME_W OPENCARDNAMEW
#ifdef UNICODE
#define OPENCARDNAMEA_EX OPENCARDNAME_EXW
#else
#define OPENCARDNAMEA_EX OPENCARDNAME_EXA
#endif
#define OPENCARDNAMEW_EX OPENCARDNAME_EXW
#define POPENCARDNAME_A POPENCARDNAMEA
#define POPENCARDNAME_W POPENCARDNAMEW
#ifdef UNICODE
#define POPENCARDNAMEA_EX POPENCARDNAME_EXW
#else
#define POPENCARDNAMEA_EX POPENCARDNAME_EXA
#endif
#define POPENCARDNAMEW_EX POPENCARDNAME_EXW
#define PSCARD_READERSTATE_A PSCARD_READERSTATEA
#define PSCARD_READERSTATE_W PSCARD_READERSTATEW
#define SC_DLG_MINIMAL_UI 0x01
#define SCARD_ALL_READERS 0
#define SCARD_AUDIT_CHV_FAILURE 0x0
#define SCARD_DEFAULT_READERS TEXT("SCard$DefaultReaders\000")
#define SCARD_LOCAL_READERS TEXT("SCard$LocalReaders\000")
#define SCARD_READER_SEL_AUTH_PACKAGE ((DWORD) - 629)
#define SCARD_READERSTATE_A SCARD_READERSTATEA
#define SCARD_READERSTATE_W SCARD_READERSTATEW
#define SCARD_SYSTEM_READERS TEXT("SCard$SystemReaders\000")
#ifdef UNICODE
#define SCardGetDeviceTypeId SCardGetDeviceTypeIdW
#else
#ifdef UNICODE
#define SCardGetDeviceTypeId SCardGetDeviceTypeIdW
#else
#define SCardGetDeviceTypeId SCardGetDeviceTypeIdA
#endif
#endif
#define SCardGetReaderCapabilities SCardGetAttrib
#ifdef UNICODE
#define SCardGetReaderDeviceInstanceId SCardGetReaderDeviceInstanceIdW
#else
#ifdef UNICODE
#define SCardGetReaderDeviceInstanceId SCardGetReaderDeviceInstanceIdW
#else
#define SCardGetReaderDeviceInstanceId SCardGetReaderDeviceInstanceIdA
#endif
#endif
#ifdef UNICODE
#define SCardGetReaderIcon SCardGetReaderIconW
#else
#ifdef UNICODE
#define SCardGetReaderIcon SCardGetReaderIconW
#else
#define SCardGetReaderIcon SCardGetReaderIconA
#endif
#endif
#define SCardListCardTypes SCardListCards
#ifdef UNICODE
#define SCardListReadersWithDeviceInstanceId SCardListReadersWithDeviceInstanceIdW
#else
#ifdef UNICODE
#define SCardListReadersWithDeviceInstanceId SCardListReadersWithDeviceInstanceIdW
#else
#define SCardListReadersWithDeviceInstanceId SCardListReadersWithDeviceInstanceIdA
#endif
#endif
#ifdef UNICODE
#define SCardReadCache SCardReadCacheW
#else
#ifdef UNICODE
#define SCardReadCache SCardReadCacheW
#else
#define SCardReadCache SCardReadCacheA
#endif
#endif
#define SCardSetReaderCapabilities SCardSetAttrib
#define SCardUIDlgSelectCard 0
#ifdef UNICODE
#define SCardWriteCache SCardWriteCacheW
#else
#ifdef UNICODE
#define SCardWriteCache SCardWriteCacheW
#else
#define SCardWriteCache SCardWriteCacheA
#endif
#endif

typedef BOOL (WINAPI *LPOCNCHKPROC)(SCARDCONTEXT, SCARDHANDLE, PVOID);

typedef SCARDHANDLE (WINAPI *LPOCNCONNPROCA)(SCARDCONTEXT, LPSTR, LPSTR, PVOID);

typedef SCARDHANDLE (WINAPI *LPOCNCONNPROCW)(SCARDCONTEXT, LPWSTR, LPWSTR, PVOID);

typedef void (WINAPI *LPOCNDSCPROC)(SCARDCONTEXT, SCARDHANDLE, PVOID);

struct OPENCARD_SEARCH_CRITERIAA
{
    DWORD dwStructSize;
    LPSTR lpstrGroupNames;
    DWORD nMaxGroupNames;
    LPCGUID rgguidInterfaces;
    DWORD cguidInterfaces;
    LPSTR lpstrCardNames;
    DWORD nMaxCardNames;
    LPOCNCHKPROC lpfnCheck;
    LPOCNCONNPROCA lpfnConnect;
    LPOCNDSCPROC lpfnDisconnect;
    LPVOID pvUserData;
    DWORD dwShareMode;
    DWORD dwPreferredProtocols;
};

typedef struct OPENCARD_SEARCH_CRITERIAA *LPOPENCARD_SEARCH_CRITERIAA;

typedef LPOPENCARD_SEARCH_CRITERIAA LPOPENCARD_SEARCH_CRITERIA;

typedef OPENCARD_SEARCH_CRITERIAA OPENCARD_SEARCH_CRITERIA;

struct OPENCARD_SEARCH_CRITERIAW
{
    DWORD dwStructSize;
    LPWSTR lpstrGroupNames;
    DWORD nMaxGroupNames;
    LPCGUID rgguidInterfaces;
    DWORD cguidInterfaces;
    LPWSTR lpstrCardNames;
    DWORD nMaxCardNames;
    LPOCNCHKPROC lpfnCheck;
    LPOCNCONNPROCW lpfnConnect;
    LPOCNDSCPROC lpfnDisconnect;
    LPVOID pvUserData;
    DWORD dwShareMode;
    DWORD dwPreferredProtocols;
};

typedef struct OPENCARD_SEARCH_CRITERIAW *LPOPENCARD_SEARCH_CRITERIAW;

struct OPENCARDNAMEA
{
    DWORD dwStructSize;
    HWND hwndOwner;
    SCARDCONTEXT hSCardContext;
    LPSTR lpstrGroupNames;
    DWORD nMaxGroupNames;
    LPSTR lpstrCardNames;
    DWORD nMaxCardNames;
    LPCGUID rgguidInterfaces;
    DWORD cguidInterfaces;
    LPSTR lpstrRdr;
    DWORD nMaxRdr;
    LPSTR lpstrCard;
    DWORD nMaxCard;
    LPCSTR lpstrTitle;
    DWORD dwFlags;
    LPVOID pvUserData;
    DWORD dwShareMode;
    DWORD dwPreferredProtocols;
    DWORD dwActiveProtocol;
    LPOCNCONNPROCA lpfnConnect;
    LPOCNCHKPROC lpfnCheck;
    LPOCNDSCPROC lpfnDisconnect;
    SCARDHANDLE hCardHandle;
};

typedef struct OPENCARDNAMEA *LPOPENCARDNAMEA;

typedef LPOPENCARDNAMEA LPOPENCARDNAME;

typedef OPENCARDNAMEA OPENCARDNAME;

struct OPENCARDNAMEW
{
    DWORD dwStructSize;
    HWND hwndOwner;
    SCARDCONTEXT hSCardContext;
    LPWSTR lpstrGroupNames;
    DWORD nMaxGroupNames;
    LPWSTR lpstrCardNames;
    DWORD nMaxCardNames;
    LPCGUID rgguidInterfaces;
    DWORD cguidInterfaces;
    LPWSTR lpstrRdr;
    DWORD nMaxRdr;
    LPWSTR lpstrCard;
    DWORD nMaxCard;
    LPCWSTR lpstrTitle;
    DWORD dwFlags;
    LPVOID pvUserData;
    DWORD dwShareMode;
    DWORD dwPreferredProtocols;
    DWORD dwActiveProtocol;
    LPOCNCONNPROCW lpfnConnect;
    LPOCNCHKPROC lpfnCheck;
    LPOCNDSCPROC lpfnDisconnect;
    SCARDHANDLE hCardHandle;
};

typedef struct OPENCARDNAMEW *LPOPENCARDNAMEW;

typedef struct OPENCARD_SEARCH_CRITERIAA *POPENCARD_SEARCH_CRITERIAA;

struct OPENCARDNAME_EXA
{
    DWORD dwStructSize;
    SCARDCONTEXT hSCardContext;
    HWND hwndOwner;
    DWORD dwFlags;
    LPCSTR lpstrTitle;
    LPCSTR lpstrSearchDesc;
    HICON hIcon;
    POPENCARD_SEARCH_CRITERIAA pOpenCardSearchCriteria;
    LPOCNCONNPROCA lpfnConnect;
    LPVOID pvUserData;
    DWORD dwShareMode;
    DWORD dwPreferredProtocols;
    LPSTR lpstrRdr;
    DWORD nMaxRdr;
    LPSTR lpstrCard;
    DWORD nMaxCard;
    DWORD dwActiveProtocol;
    SCARDHANDLE hCardHandle;
};

typedef struct OPENCARDNAME_EXA *LPOPENCARDNAME_EXA;

typedef LPOPENCARDNAME_EXA LPOPENCARDNAME_EX;

typedef OPENCARDNAME_EXA OPENCARDNAME_EX;

typedef POPENCARD_SEARCH_CRITERIAA POPENCARD_SEARCH_CRITERIA;

typedef struct OPENCARD_SEARCH_CRITERIAW *POPENCARD_SEARCH_CRITERIAW;

struct OPENCARDNAME_EXW
{
    DWORD dwStructSize;
    SCARDCONTEXT hSCardContext;
    HWND hwndOwner;
    DWORD dwFlags;
    LPCWSTR lpstrTitle;
    LPCWSTR lpstrSearchDesc;
    HICON hIcon;
    POPENCARD_SEARCH_CRITERIAW pOpenCardSearchCriteria;
    LPOCNCONNPROCW lpfnConnect;
    LPVOID pvUserData;
    DWORD dwShareMode;
    DWORD dwPreferredProtocols;
    LPWSTR lpstrRdr;
    DWORD nMaxRdr;
    LPWSTR lpstrCard;
    DWORD nMaxCard;
    DWORD dwActiveProtocol;
    SCARDHANDLE hCardHandle;
};

typedef struct OPENCARDNAME_EXW *LPOPENCARDNAME_EXW;

typedef struct OPENCARDNAME_EXA *POPENCARDNAME_EXA;

typedef POPENCARDNAME_EXA POPENCARDNAME_EX;

typedef struct OPENCARDNAME_EXW *POPENCARDNAME_EXW;

typedef struct OPENCARDNAMEA *POPENCARDNAMEA;

typedef POPENCARDNAMEA POPENCARDNAME;

typedef struct OPENCARDNAMEW *POPENCARDNAMEW;

struct READER_SEL_REQUEST
{
    DWORD dwShareMode;
    DWORD dwPreferredProtocols;
    READER_SEL_REQUEST_MATCH_TYPE MatchType;
    union
    {
        struct
        {
            DWORD cbReaderNameOffset;
            DWORD cchReaderNameLength;
            DWORD cbContainerNameOffset;
            DWORD cchContainerNameLength;
            DWORD dwDesiredCardModuleVersion;
            DWORD dwCspFlags;
        } ReaderAndContainerParameter;
        struct
        {
            DWORD cbSerialNumberOffset;
            DWORD cbSerialNumberLength;
            DWORD dwDesiredCardModuleVersion;
        } SerialNumberParameter;
    };
};

typedef struct READER_SEL_REQUEST *PREADER_SEL_REQUEST;

#ifdef __cplusplus
extern "C" {
#endif

/* TODO: LiberNT does not implement the functions below yet */
LONG WINAPI GetOpenCardNameA(LPOPENCARDNAMEA);
LONG WINAPI GetOpenCardNameW(LPOPENCARDNAMEW);
LONG WINAPI SCardAudit(SCARDCONTEXT hContext, DWORD dwEvent);
LONG WINAPI SCardCancelTransaction(SCARDHANDLE hCard);
LONG WINAPI SCardDlgExtendedError(void);
LONG WINAPI SCardGetDeviceTypeIdA(SCARDCONTEXT hContext, LPCSTR szReaderName, LPDWORD pdwDeviceTypeId);
LONG WINAPI SCardGetDeviceTypeIdW(SCARDCONTEXT hContext, LPCWSTR szReaderName, LPDWORD pdwDeviceTypeId);
LONG WINAPI SCardGetReaderDeviceInstanceIdA(SCARDCONTEXT hContext, LPCSTR szReaderName, LPSTR szDeviceInstanceId, LPDWORD pcchDeviceInstanceId);
LONG WINAPI SCardGetReaderDeviceInstanceIdW(SCARDCONTEXT hContext, LPCWSTR szReaderName, LPWSTR szDeviceInstanceId, LPDWORD pcchDeviceInstanceId);
LONG WINAPI SCardGetReaderIconA(SCARDCONTEXT hContext, LPCSTR szReaderName, LPBYTE pbIcon, LPDWORD pcbIcon);
LONG WINAPI SCardGetReaderIconW(SCARDCONTEXT hContext, LPCWSTR szReaderName, LPBYTE pbIcon, LPDWORD pcbIcon);
LONG WINAPI SCardGetTransmitCount(SCARDHANDLE hCard, LPDWORD pcTransmitCount);
LONG WINAPI SCardListReadersWithDeviceInstanceIdA(SCARDCONTEXT hContext, LPCSTR szDeviceInstanceId, LPSTR mszReaders, LPDWORD pcchReaders);
LONG WINAPI SCardListReadersWithDeviceInstanceIdW(SCARDCONTEXT hContext, LPCWSTR szDeviceInstanceId, LPWSTR mszReaders, LPDWORD pcchReaders);
LONG WINAPI SCardReadCacheA(SCARDCONTEXT hContext, UUID *CardIdentifier, DWORD FreshnessCounter, LPSTR LookupName, PBYTE Data, DWORD *DataLen);
LONG WINAPI SCardReadCacheW(SCARDCONTEXT hContext, UUID *CardIdentifier, DWORD FreshnessCounter, LPWSTR LookupName, PBYTE Data, DWORD *DataLen);
LONG WINAPI SCardUIDlgSelectCardA(LPOPENCARDNAME_EXA);
LONG WINAPI SCardUIDlgSelectCardW(LPOPENCARDNAME_EXW);
LONG WINAPI SCardWriteCacheA(SCARDCONTEXT hContext, UUID *CardIdentifier, DWORD FreshnessCounter, LPSTR LookupName, PBYTE Data, DWORD DataLen);
LONG WINAPI SCardWriteCacheW(SCARDCONTEXT hContext, UUID *CardIdentifier, DWORD FreshnessCounter, LPWSTR LookupName, PBYTE Data, DWORD DataLen);

#ifdef __cplusplus
}
#endif

#define PCSCardIntroduceCardType(hContext, szCardName, pbAtr, pbAtrMask, cbAtrLen, pguidPrimaryProvider, rgguidInterfaces, dwInterfaceCount) SCardIntroduceCardType(hContext, szCardName, pguidPrimaryProvider, rgguidInterfaces, dwInterfaceCount, pbAtr, pbAtrMask, cbAtrLen)

#endif  /* __WINE_WINSCARD_H */
