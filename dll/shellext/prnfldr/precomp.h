/*
 * PROJECT:     LiberNT Printers Folder
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Precompiled header
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#define WIN32_NO_STATUS
#define _INC_WINDOWS
#define COM_NO_WINDOWS_H

#include <windef.h>
#include <winbase.h>
#include <winreg.h>
#include <wingdi.h>
#include <winuser.h>
#include <wincon.h>
#include <winspool.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <shellapi.h>
#include <atlbase.h>
#include <atlcom.h>
#include <atlsimpcoll.h>
#include <shlguid_undoc.h>
#include <shlobj_undoc.h>
#include <undocshell.h>
#include <shellutils.h>
#include <strsafe.h>

#include <wine/debug.h>

#include "resource.h"

extern HINSTANCE prnfldr_hInstance;
