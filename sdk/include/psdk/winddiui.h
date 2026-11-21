/*
 *  Header for the Device Driver Interface - User Interface library
 *
 *  Copyright 2007 Marcel Partap
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

#ifndef __WINE_WINDDIUI_H
#define __WINE_WINDDIUI_H

#include <compstui.h>

#ifdef __cplusplus
extern "C" {
#endif

#if (NTDDI_VERSION >= NTDDI_WINXP)
typedef struct _DOCEVENT_FILTER {
    UINT    cbSize;
    UINT    cElementsAllocated;
    UINT    cElementsNeeded;
    UINT    cElementsReturned;
    DWORD   aDocEventCall[ANYSIZE_ARRAY];
} DOCEVENT_FILTER, *PDOCEVENT_FILTER;
typedef struct _DOCEVENT_CREATEDCPRE {
    PWSTR       pszDriver;
    PWSTR       pszDevice;
    PDEVMODEW   pdm;
    BOOL        bIC;
} DOCEVENT_CREATEDCPRE, *PDCEVENT_CREATEDCPRE;
typedef struct _DOCEVENT_ESCAPE {
    int    iEscape;
    int    cjInput;
    PVOID  pvInData;
} DOCEVENT_ESCAPE, *PDOCEVENT_ESCAPE;
#endif
#define DOCUMENTEVENT_FIRST         1
#define DOCUMENTEVENT_CREATEDCPRE   1
#define DOCUMENTEVENT_CREATEDCPOST  2
#define DOCUMENTEVENT_RESETDCPRE    3
#define DOCUMENTEVENT_RESETDCPOST   4
#define DOCUMENTEVENT_STARTDOC      5
#define DOCUMENTEVENT_STARTDOCPRE   5
#define DOCUMENTEVENT_STARTPAGE     6
#define DOCUMENTEVENT_ENDPAGE       7
#define DOCUMENTEVENT_ENDDOC        8
#define DOCUMENTEVENT_ENDDOCPRE     8
#define DOCUMENTEVENT_ABORTDOC      9
#define DOCUMENTEVENT_DELETEDC     10
#define DOCUMENTEVENT_ESCAPE       11
#define DOCUMENTEVENT_ENDDOCPOST   12
#define DOCUMENTEVENT_STARTDOCPOST 13
#if (NTDDI_VERSION >= NTDDI_VISTA)
#define DOCUMENTEVENT_QUERYFILTER 14
#define DOCUMENTEVENT_XPS_ADDFIXEDDOCUMENTSEQUENCEPRE              1
#define DOCUMENTEVENT_XPS_ADDFIXEDDOCUMENTPRE                      2
#define DOCUMENTEVENT_XPS_ADDFIXEDPAGEEPRE                         3
#define DOCUMENTEVENT_XPS_ADDFIXEDPAGEPOST                         4
#define DOCUMENTEVENT_XPS_ADDFIXEDDOCUMENTPOST                     5
#define DOCUMENTEVENT_XPS_CANCELJOB                                6
#define DOCUMENTEVENT_XPS_ADDFIXEDDOCUMENTSEQUENCEPRINTTICKETPRE   7
#define DOCUMENTEVENT_XPS_ADDFIXEDDOCUMENTPRINTTICKETPRE           8
#define DOCUMENTEVENT_XPS_ADDFIXEDPAGEPRINTTICKETPRE               9
#define DOCUMENTEVENT_XPS_ADDFIXEDPAGEPRINTTICKETPOST             10
#define DOCUMENTEVENT_XPS_ADDFIXEDDOCUMENTPRINTTICKETPOST         11
#define DOCUMENTEVENT_XPS_ADDFIXEDDOCUMENTSEQUENCEPRINTTICKETPOST 12
#define DOCUMENTEVENT_XPS_ADDFIXEDDOCUMENTSEQUENCEPOST            13
#define DOCUMENTEVENT_LAST 15
#elif (NTDDI_VERSION >= NTDDI_WINXP)
#define DOCUMENTEVENT_QUERYFILTER  14
#define DOCUMENTEVENT_LAST         15
#else
#define DOCUMENTEVENT_LAST 14
#endif
#define DOCUMENTEVENT_SPOOLED 0x10000
#define DOCUMENTEVENT_SUCCESS     1
#define DOCUMENTEVENT_UNSUPPORTED 0
#define DOCUMENTEVENT_FAILURE    -1
#define DOCUMENTEVENT_EVENT(iX) (LOWORD(iX))
#define DOCUMENTEVENT_FLAGS(iX) (HIWORD(iX))

int WINAPI DrvDocumentEvent(HANDLE,HDC,int,ULONG,PVOID,ULONG,PVOID);

#define DRIVER_EVENT_INITIALIZE 1
#define DRIVER_EVENT_DELETE 2

#define PRINTER_EVENT_ADD_CONNECTION 1
#define PRINTER_EVENT_DELETE_CONNECTION 2
#define PRINTER_EVENT_INITIALIZE 3
#define PRINTER_EVENT_DELETE 4
#define PRINTER_EVENT_CACHE_REFRESH 5
#define PRINTER_EVENT_CACHE_DELETE 6
#define PRINTER_EVENT_ATTRIBUTES_CHANGED 7

#define PRINTER_EVENT_FLAG_NO_UI 1

BOOL WINAPI DrvDriverEvent(DWORD, DWORD, LPBYTE, LPARAM);
BOOL WINAPI DrvPrinterEvent(LPWSTR, INT, DWORD, LPARAM);
DWORD WINAPI DrvDeviceCapabilities(HANDLE,PWSTR,WORD,PVOID,PDEVMODE);

LONG WINAPI DrvDevicePropertySheets(PPROPSHEETUI_INFO,LPARAM);

typedef struct _DEVICEPROPERTYHEADER {
    WORD    cbSize;
    WORD    Flags;
    HANDLE  hPrinter;
    LPTSTR  pszPrinterName;
} DEVICEPROPERTYHEADER, *PDEVICEPROPERTYHEADER;

#define DPS_NOPERMISSION    0x0001

LONG WINAPI DrvDocumentPropertySheets(PPROPSHEETUI_INFO,LPARAM);

typedef struct _DOCUMENTPROPERTYHEADER {
    WORD     cbSize;
    WORD     Reserved;
    HANDLE   hPrinter;
    LPTSTR   pszPrinterName;
    PDEVMODE pdmIn;
    PDEVMODE pdmOut;
    DWORD    cbOut;
    DWORD    fMode;
} DOCUMENTPROPERTYHEADER, *PDOCUMENTPROPERTYHEADER;

#define DM_ADVANCED         0x10
#define DM_NOPERMISSION     0x20
#define DM_USER_DEFAULT     0x40

typedef struct _DEVQUERYPRINT_INFO {
    WORD    cbSize;
    WORD    Level;
    HANDLE  hPrinter;
    DEVMODE *pDevMode;
    LPWSTR  pszErrorStr;
    DWORD   cchErrorStr;
    DWORD   cchNeeded;
} DEVQUERYPRINT_INFO, *PDEVQUERYPRINT_INFO;

BOOL WINAPI DevQueryPrintEx(PDEVQUERYPRINT_INFO);

#ifdef __cplusplus
} /* extern "C" */
#endif

#ifndef BOOKLET_PRINT
#define BOOKLET_PRINT 0x00000002
#endif
#ifndef BORDER_PRINT
#define BORDER_PRINT 0x00000000
#endif
#ifndef CDM_CONVERT
#define CDM_CONVERT 0x01
#endif
#ifndef CDM_CONVERT351
#define CDM_CONVERT351 0x02
#endif
#ifndef CDM_DRIVER_DEFAULT
#define CDM_DRIVER_DEFAULT 0x04
#endif
#ifndef COLOR_OPTIMIZATION
#define COLOR_OPTIMIZATION 0x00000001
#endif
#ifndef DM_INVALIDATE_DRIVER_CACHE
#define DM_INVALIDATE_DRIVER_CACHE 0x20000000
#endif
#ifndef DM_PROMPT_NON_MODAL
#define DM_PROMPT_NON_MODAL 0x40000000
#endif
#ifndef DM_RESERVED
#define DM_RESERVED 0x80000000
#endif
#ifndef NO_BORDER_PRINT
#define NO_BORDER_PRINT 0x00000001
#endif
#ifndef NO_COLOR_OPTIMIZATION
#define NO_COLOR_OPTIMIZATION 0x00000000
#endif
#ifndef PRINTER_EVENT_ADD_CONNECTION_NO_UI
#define PRINTER_EVENT_ADD_CONNECTION_NO_UI 9
#endif
#ifndef PRINTER_EVENT_CONFIGURATION_CHANGE
#define PRINTER_EVENT_CONFIGURATION_CHANGE 0
#endif
#ifndef PRINTER_EVENT_CONFIGURATION_UPDATE
#define PRINTER_EVENT_CONFIGURATION_UPDATE 8
#endif
#ifndef PRINTER_EVENT_DELETE_CONNECTION_NO_UI
#define PRINTER_EVENT_DELETE_CONNECTION_NO_UI 10
#endif
#ifndef QCP_DEVICEPROFILE
#define QCP_DEVICEPROFILE 0x0000
#endif
#ifndef QCP_PROFILEDISK
#define QCP_PROFILEDISK 0x0002
#endif
#ifndef QCP_PROFILEMEMORY
#define QCP_PROFILEMEMORY 0x0001
#endif
#ifndef QCP_SOURCEPROFILE
#define QCP_SOURCEPROFILE 0x0001
#endif

typedef struct _DRIVER_UPGRADE_INFO_1 {
  LPTSTR pPrinterName;
  LPTSTR pOldDriverDirectory;
} DRIVER_UPGRADE_INFO_1, *PDRIVER_UPGRADE_INFO_1;

typedef struct _DRIVER_UPGRADE_INFO_2 {
  LPTSTR pPrinterName;
  LPTSTR pOldDriverDirectory;
  DWORD cVersion;
  LPTSTR pName;
  LPTSTR pEnvironment;
  LPTSTR pDriverPath;
  LPTSTR pDataFile;
  LPTSTR pConfigFile;
  LPTSTR pHelpFile;
  LPTSTR pDependentFiles;
  LPTSTR pMonitorName;
  LPTSTR pDefaultDataType;
  LPTSTR pszzPreviousNames;
} DRIVER_UPGRADE_INFO_2, *PDRIVER_UPGRADE_INFO_2;

typedef struct _PRINTER_EVENT_ATTRIBUTES_INFO {
  DWORD cbSize;
  DWORD dwOldAttributes;
  DWORD dwNewAttributes;
} PRINTER_EVENT_ATTRIBUTES_INFO, *PPRINTER_EVENT_ATTRIBUTES_INFO;

typedef struct _ATTRIBUTE_INFO_1 {
  DWORD dwJobNumberOfPagesPerSide;
  DWORD dwDrvNumberOfPagesPerSide;
  DWORD dwNupBorderFlags;
  DWORD dwJobPageOrderFlags;
  DWORD dwDrvPageOrderFlags;
  DWORD dwJobNumberOfCopies;
  DWORD dwDrvNumberOfCopies;
} ATTRIBUTE_INFO_1, *PATTRIBUTE_INFO_1;

typedef struct _ATTRIBUTE_INFO_2 {
  DWORD dwJobNumberOfPagesPerSide;
  DWORD dwDrvNumberOfPagesPerSide;
  DWORD dwNupBorderFlags;
  DWORD dwJobPageOrderFlags;
  DWORD dwDrvPageOrderFlags;
  DWORD dwJobNumberOfCopies;
  DWORD dwDrvNumberOfCopies;
  DWORD dwColorOptimization;
} ATTRIBUTE_INFO_2, *PATTRIBUTE_INFO_2;

#endif /* __WINE_WINDDIUI_H */
