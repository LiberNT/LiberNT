/*
 * PROJECT:     LiberNT C Runtime
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     VCRUNTIME140 DLL entry point. The DLL is a pure forwarder
 *              onto ucrtbase (see vcruntime140.spec); it keeps no state
 *              of its own, so the entry point only succeeds.
 * COPYRIGHT:   Copyright 2026 Cairn contributors
 */

#include <windef.h>
#include <winbase.h>

BOOL WINAPI
DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    (void)hinstDLL;
    (void)fdwReason;
    (void)lpvReserved;
    return TRUE;
}
