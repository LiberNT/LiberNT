/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Child process of the layerrun test, without a static user32 import
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <windows.h>

int main(void)
{
    OSVERSIONINFOA Info = { sizeof(Info) };

    GetVersionExA(&Info);
    return (int)((Info.dwMajorVersion << 8) | Info.dwMinorVersion);
}
