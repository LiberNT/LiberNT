/*
 * PROJECT:     LiberNT unlodctr
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Removes performance counters
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <stdarg.h>
#include <windef.h>
#include <winbase.h>
#include <loadperf.h>
#include "../lodctr/manifest.h"

int wmain(int argc, WCHAR *argv[])
{
    if (argc > 1 && !_wcsnicmp(argv[1], L"/m:", 3))
        return LodctrUninstallManifest(argv[1] + 3);
    return UnloadPerfCounterTextStringsW(GetCommandLineW(), FALSE);
}
