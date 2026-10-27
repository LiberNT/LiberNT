#pragma once

#define WIN32_NO_STATUS
#define _INC_WINDOWS
#define COM_NO_WINDOWS_H
#include <stdarg.h>
#include <windef.h>
#include <winbase.h>
#include <winreg.h>
#include <wingdi.h>
#include <winuser.h>
#include <commdlg.h>
#include <rpc.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <newdev.h>
#include <reactos/dll/devmgr/devmgr.h>
#include <cpl.h>

#define NDEBUG
#include <debug.h>

#define MAX_STR_SIZE 255

#define WM_DETECT_START    (WM_APP + 1)
#define WM_DETECT_CLASS    (WM_APP + 2)
#define WM_DETECT_PROGRESS (WM_APP + 3)
#define WM_DETECT_DONE     (WM_APP + 4)

typedef struct _HWTYPE_ENTRY
{
    GUID ClassGuid;
    WCHAR Description[MAX_STR_SIZE];
} HWTYPE_ENTRY, *PHWTYPE_ENTRY;

extern HINSTANCE hApplet;

/* EOF */
