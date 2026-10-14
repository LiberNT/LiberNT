/*
 * PROJECT:     LiberNT MSVCRT
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     PowerPC floating-point control and status words
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <stdarg.h>
#include "windef.h"
#include "winbase.h"
#include "msvcrt.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(msvcrt);

void msvcrt_arch_setfp(unsigned int *cw, unsigned int cw_mask, unsigned int *sw, unsigned int sw_mask)
{
    FIXME("not implemented\n");
    if (cw) *cw = 0;
    if (sw) *sw = 0;
}
