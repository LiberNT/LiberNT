/*
 * PROJECT:     LiberNT CRT library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Unicode GUI executable entry point
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "commonCRTStartup.hpp"
#include <windows.h>

template<>
int call_main<decltype(wWinMain)>()
{
    STARTUPINFOW StartupInfo;

    _configure_wide_argv(_crt_argv_unexpanded_arguments);

    GetStartupInfoW(&StartupInfo);

    return wWinMain(GetModuleHandleW(NULL),
               NULL,
               _get_wide_winmain_command_line(),
               (StartupInfo.dwFlags & STARTF_USESHOWWINDOW) ? StartupInfo.wShowWindow : SW_SHOWDEFAULT);
}

extern "C" unsigned long wWinMainCRTStartup(void*)
{
    __security_init_cookie();

    return __commonCRTStartup<decltype(wWinMain)>();
}
