/*
 * PROJECT:     LiberNT Certificates Snap-in
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Precompiled header
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <stdio.h>

#define WIN32_NO_STATUS
#define _INC_WINDOWS
#define COM_NO_WINDOWS_H

#include <windef.h>
#include <winbase.h>
#include <winreg.h>
#include <wingdi.h>
#include <winuser.h>
#include <wincon.h>
#include <winver.h>
#include <wincrypt.h>
#include <cryptuiapi.h>
#include <ole2.h>
#include <commctrl.h>
#include <mmc.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <atlbase.h>
#include <atlcom.h>
#include <atlsimpcoll.h>
#include <shellutils.h>
#include <strsafe.h>

#include <wine/debug.h>

#include "resource.h"

extern HINSTANCE certmgr_hInstance;

#include "guids.h"

HRESULT CertMgr_LoadString(UINT uId, LPOLESTR *ppsz);
