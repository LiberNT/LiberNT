/*
 */

#ifndef __USERINIT_H__
#define __USERINIT_H__

#include <stdio.h>
#include <stdlib.h>

/* PSDK/NDK Headers */
#define WIN32_NO_STATUS
#define _INC_WINDOWS
#define COM_NO_WINDOWS_H
#include <windef.h>
#include <winbase.h>
#include <wingdi.h>
#include <winreg.h>
#include <regstr.h>
#include <winnls.h>
#include <winuser.h>
#include <undocuser.h>
#include <powrprof.h>

#include <strsafe.h>

#include <ndk/exfuncs.h>

#include <reactos/desktopready.h>

#include <wine/debug.h>
WINE_DEFAULT_DEBUG_CHANNEL(userinit);

#include "resource.h"


extern HINSTANCE hInstance;

LONG
ReadRegSzKey(
    IN HKEY hKey,
    IN LPCWSTR pszKey,
    OUT LPWSTR *pValue);

BOOL
IsLiveCD(VOID);

#endif /* __USERINIT_H__ */
