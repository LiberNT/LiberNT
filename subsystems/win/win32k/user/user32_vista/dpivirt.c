/*
 * PROJECT:     LiberNT Vista+ user32 APIs
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     DPI virtualization state for the standalone user32_vista module
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#define WIN32_NO_STATUS
#define _INC_WINDOWS
#define COM_NO_WINDOWS_H
#include <windef.h>
#include <winbase.h>
#include <wingdi.h>
#include <winuser.h>
#include <imm.h>
#include <immdev.h>
#include <imm32_undoc.h>

#define NTOS_MODE_USER
#include <ndk/umtypes.h>
#include <ndk/pstypes.h>
#include <ndk/rtlfuncs.h>

#include <ntuser.h>

ULONG WINAPI IntGetThreadDpiContext(VOID);

BOOL
WINAPI
User32IsDpiVirtualizationActive(VOID)
{
    return !!(NtUserCallOneParam(ROS_SERVERINFO_SRVIFLAGS, ONEPARAM_ROUTINE_ROS_GETSERVERINFO) &
              SRVINFO_DPIVIRTUALIZATION);
}

BOOL
WINAPI
User32IsDpiVirtualThread(VOID)
{
    return User32IsDpiVirtualizationActive() &&
           (IntGetThreadDpiContext() & 0x0f) == DPI_AWARENESS_UNAWARE;
}

UINT
WINAPI
User32GetSystemDpi(VOID)
{
    return (UINT)NtUserCallOneParam(ROS_SERVERINFO_LOGPIXELS, ONEPARAM_ROUTINE_ROS_GETSERVERINFO);
}

INT
WINAPI
User32GetFoldedPaddedBorder(VOID)
{
    INT Padded;
    UINT Dpi;

    if (NtCurrentPeb()->ImageSubsystemMajorVersion >= 6)
        return 0;
    Padded = (INT)NtUserCallOneParam(ROS_SERVERINFO_PADDEDBORDER, ONEPARAM_ROUTINE_ROS_GETSERVERINFO);
    if (!User32IsDpiVirtualThread())
        return Padded;
    Dpi = User32GetSystemDpi();
    return Dpi ? (INT)(((LONGLONG)Padded * USER_DEFAULT_SCREEN_DPI + Dpi / 2) / Dpi) : Padded;
}
